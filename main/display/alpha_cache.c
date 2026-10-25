#include "alpha_cache.h"
#include "animation_crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 24
#define MAX_FILE 65536
#define CHUNK 2048
typedef struct {
    const uint8_t *memory;
    FILE *file;
    uint8_t *chunk;
    size_t size, pos, available, offset;
    uint32_t crc;
} reader_t;
static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static bool header(const uint8_t *h, size_t length, alpha_cache_info_t *info)
{
    if (length < HEADER+2 || length > MAX_FILE || le32(h)!=0x31464156 || le32(h+16) ||
        le32(h+8)!=length-HEADER || animation_crc32_le(0,h,20)!=le32(h+20)) return false;
    info->width=le16(h+4); info->height=le16(h+6);
    info->payload_bytes=le32(h+8);info->payload_crc=le32(h+12);info->pixel_hash=0;
    return info->width && info->height && info->width<=160 && info->height<=160;
}
static bool take(reader_t *r, void *target, size_t n)
{
    if (n > r->size-r->pos) return false;
    if (r->memory) {
        if (target) memcpy(target,r->memory+r->pos,n);
        r->pos+=n;return true;
    }
    uint8_t *out=target;
    while(n) {
        if(r->offset==r->available) {
            size_t need=r->size-r->pos;if(need>CHUNK)need=CHUNK;
            r->available=fread(r->chunk,1,need,r->file);r->offset=0;
            if(r->available!=need)return false;
            r->crc=animation_crc32_le(r->crc,r->chunk,need);
        }
        size_t count=r->available-r->offset;if(count>n)count=n;
        if(out){memcpy(out,r->chunk+r->offset,count);out+=count;}
        r->offset+=count;r->pos+=count;n-=count;
    }
    return true;
}
uint16_t alpha_cache_gray(uint16_t color)
{
    unsigned r=(color>>11)*255/31,g=((color>>5)&63)*255/63,b=(color&31)*255/31;
    unsigned y=(r*77+g*150+b*29+128)>>8;
    return ((y>>3)<<11)|((y>>2)<<5)|(y>>3);
}
static inline uint16_t blend(uint16_t color, uint16_t bg, unsigned a)
{
    unsigned inverse=255-a;
    unsigned red=((color>>11)*a+(bg>>11)*inverse+127)/255;
    unsigned green=(((color>>5)&63)*a+((bg>>5)&63)*inverse+127)/255;
    unsigned blue=((color&31)*a+(bg&31)*inverse+127)/255;
    return (red<<11)|(green<<5)|blue;
}
static inline uint32_t hash_pixel(uint32_t hash,uint16_t color)
{
    return ((hash^(color>>8))*16777619u^(color&255))*16777619u;
}
static esp_err_t decode(reader_t *r, uint16_t *pixels, size_t capacity, uint16_t bg,
                        alpha_cache_info_t *info, bool inspect, bool gray)
{
    size_t used=0,total=(size_t)info->width*info->height;
    if(pixels && capacity<total)return ESP_ERR_INVALID_SIZE;
    uint32_t hash=2166136261u;
    while(r->pos<r->size) {
        uint8_t word[3];if(!take(r,word,2))return ESP_ERR_INVALID_ARG;
        unsigned token=le16(word),kind=token>>14,count=token&16383;
        if(!count || count>total-used)return ESP_ERR_INVALID_ARG;
        if(kind<2) {
            uint16_t color=bg;
            if(kind==1){if(!take(r,word,2))return ESP_ERR_INVALID_ARG;color=(word[0]<<8)|word[1];if(gray)color=alpha_cache_gray(color);}
            uint16_t wire=__builtin_bswap16(color);
            if(pixels)for(unsigned i=0;i<count;i++)pixels[used+i]=wire;
            if(inspect)for(unsigned i=0;i<count;i++)hash=hash_pixel(hash,color);
            used+=count;
        } else if(kind==2 && !inspect && !gray) {
            if(!take(r,pixels?pixels+used:NULL,count*2))return ESP_ERR_INVALID_ARG;
            used+=count;
        } else {
            for(unsigned i=0;i<count;i++) {
                if(!take(r,word,kind==3?3:2))return ESP_ERR_INVALID_ARG;
                uint16_t color=(word[0]<<8)|word[1];if(gray)color=alpha_cache_gray(color);
                if(kind==3){if(!word[2] || word[2]==255)return ESP_ERR_INVALID_ARG;color=blend(color,bg,word[2]);}
                if(pixels)pixels[used]=__builtin_bswap16(color);
                if(inspect)hash=hash_pixel(hash,color);
                used++;
            }
        }
    }
    if(used!=total || (!r->memory && r->crc!=info->payload_crc))return ESP_ERR_INVALID_ARG;
    info->pixel_hash=inspect?hash:0;return ESP_OK;
}
esp_err_t alpha_cache_decode_buffer(const uint8_t *data,size_t length,uint16_t *pixels,
                                    size_t capacity,uint16_t bg,alpha_cache_info_t *info)
{
    alpha_cache_info_t local;
    if(!info)info=&local;
    if(!data || !header(data,length,info) || animation_crc32_le(0,data+HEADER,length-HEADER)!=info->payload_crc)return ESP_ERR_INVALID_ARG;
    reader_t r={.memory=data+HEADER,.size=length-HEADER};
    return decode(&r,pixels,capacity,bg,info,false,false);
}
static esp_err_t load(const char *path,uint16_t *pixels,size_t capacity,uint16_t bg,
                      alpha_cache_info_t *info,bool inspect,bool gray)
{
    FILE *f=fopen(path,"rb");if(!f)return ESP_ERR_NOT_FOUND;
    uint8_t h[HEADER];alpha_cache_info_t local;if(!info)info=&local;
    esp_err_t result=ESP_ERR_INVALID_ARG;
    if(fseek(f,0,SEEK_END))goto done;
    long size=ftell(f);if(size<HEADER+2 || size>MAX_FILE || fseek(f,0,SEEK_SET) || fread(h,1,HEADER,f)!=HEADER || !header(h,size,info))goto done;
    // Typical sprites use a fast contiguous read; large/fragmented allocations
    // fall back to a bounded reader instead of competing with the LCD canvas.
    if(pixels && !gray && info->payload_bytes<=24576) {
        uint8_t *data=malloc(size);
        if(data) {
            memcpy(data,h,HEADER);
            if(fread(data+HEADER,1,size-HEADER,f)==size-HEADER)result=alpha_cache_decode_buffer(data,size,pixels,capacity,bg,info);
            free(data);goto done;
        }
    }
    uint8_t *chunk=malloc(CHUNK);if(!chunk){result=ESP_ERR_NO_MEM;goto done;}
    reader_t r={.file=f,.chunk=chunk,.size=info->payload_bytes};
    result=decode(&r,pixels,capacity,bg,info,inspect,gray);free(chunk);
done:
    fclose(f);return result;
}
esp_err_t alpha_cache_decode_file(const char *path,uint16_t *pixels,size_t capacity,uint16_t bg,alpha_cache_info_t *info)
{ return load(path,pixels,capacity,bg,info,false,false); }
bool alpha_cache_validate(const char *path) { return load(path,NULL,0,0,NULL,false,false)==ESP_OK; }
esp_err_t alpha_cache_inspect(const char *path,uint16_t bg,alpha_cache_info_t *info)
{ return load(path,NULL,0,bg,info,true,false); }

esp_err_t alpha_cache_decode_file_mode(const char *path,uint16_t *pixels,size_t capacity,uint16_t bg,alpha_cache_info_t *info,bool gray)
{return load(path,pixels,capacity,bg,info,false,gray);}
esp_err_t alpha_cache_inspect_mode(const char *path,uint16_t bg,alpha_cache_info_t *info,bool gray)
{return load(path,NULL,0,bg,info,true,gray);}
