#include "frame_transform.h"
#include "animation_cache.h"
#include "display_init.h"
#include "jpeg_decoder.h"
#include "companion.h"
#include "companion_render.h"
#include "alpha_cache.h"
#include "lamp_effects.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#undef malloc
#undef calloc
#undef free
typedef union {size_t size;max_align_t alignment;} allocation_t;
static size_t used,peak;
static bool fail_allocation,fail_codec;
void *fixture_malloc(size_t size)
{
    if(fail_allocation)return NULL;
    allocation_t *a=malloc(sizeof(*a)+size);if(!a)return NULL;
    a->size=size;used+=size;if(used>peak)peak=used;return a+1;
}
void *fixture_calloc(size_t count,size_t size)
{void *p=fixture_malloc(count*size);if(p)memset(p,0,count*size);return p;}
void fixture_free(void *p)
{if(p){allocation_t *a=(allocation_t *)p-1;assert(used>=a->size);used-=a->size;free(a);}}
void fixture_log(const char *tag,const char *format,...){(void)tag;(void)format;}
void storage_note_change(void){}
int esp_spiffs_info(const char *label,size_t *total,size_t *occupied)
{(void)label;*total=4000000;*occupied=0;return ESP_OK;}
int64_t esp_timer_get_time(void){return 0;}
unsigned esp_get_free_heap_size(void){return 60000;}
uint32_t animation_crc32_le(uint32_t crc,const uint8_t *p,size_t n)
{
    crc=~crc;
    while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
    return ~crc;
}
esp_err_t esp_jpeg_decode(const esp_jpeg_image_cfg_t *c,esp_jpeg_image_output_t *out)
{
    if(fail_codec)return ESP_FAIL;
    out->width=((unsigned)c->indata[9]<<8)|c->indata[10];
    out->height=((unsigned)c->indata[7]<<8)|c->indata[8];
    assert(c->outbuf_size==(size_t)out->width*out->height*2);
    for(size_t i=0;i<(size_t)out->width*out->height;i++)((uint16_t *)c->outbuf)[i]=(uint16_t)(i*13);
    return ESP_OK;
}
static uint16_t pixels[160*160+4],original[160*160],expected[160*160];
static void transform(unsigned w,unsigned h,unsigned dw,unsigned dh)
{
    for(unsigned i=0;i<w*h;i++)original[i]=(uint16_t)(i*713+17);
    for(unsigned r=0;r<360;r+=90){
        memcpy(pixels,original,w*h*2);pixels[160*160]=0x1234;
        for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
            size_t index=r==0 ? y*dw+x : r==180 ? (dh-1-y)*dw+dw-1-x : frame_rotated_index(y*dw+x,dw,dh,r);
            expected[index]=original[y*h/dh*w+x*w/dw];
        }
        assert(frame_resize(pixels,160*160,w,h,dw,dh));
        assert(frame_rotate(pixels,160*160,dw,dh,r));
        assert(!memcmp(expected,pixels,dw*dh*2));assert(pixels[160*160]==0x1234);
    }
}
static void transforms(void)
{
    // Exhaust small rectangles, including single rows/columns and equal axes.
    for(unsigned w=1;w<=12;w++)for(unsigned h=1;h<=12;h++)
        for(unsigned dw=1;dw<=16;dw++)for(unsigned dh=1;dh<=16;dh++)
            if((dw>=w&&dh>=h)||(dw<=w&&dh<=h))transform(w,h,dw,dh);
    const unsigned sizes[]={1,2,79,80,111,112,131,132,159,160};
    for(unsigned wi=0;wi<10;wi++)for(unsigned hi=0;hi<10;hi++)for(unsigned size=1;size<=132;size++){
        unsigned w=sizes[wi],h=sizes[hi],dw=size,dh=size;
        if(w>h)dh=h*dw/w;else if(h>w)dw=w*dh/h;
        transform(w,h,dw?dw:1,dh?dh:1);
    }
    assert(!frame_resize(pixels,10,4,4,2,2));
    assert(!frame_resize(pixels,25600,8,4,4,8));
    assert(!frame_rotate(pixels,15,3,5,90)); // no scratch space
    assert(!frame_rotate(pixels,25600,3,5,45));
}
typedef struct {uint32_t magic,source_crc,pixel_crc,encoded_crc,bytes;uint16_t width,height;} header_t;
static void cache_file(const uint8_t *payload,size_t size,uint32_t crc)
{
    header_t h={.magic=0x35434e41u,.encoded_crc=crc,.bytes=(uint32_t)size,.width=160,.height=160};
    FILE *f=fopen("frame.r5","wb");assert(f);assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));
    assert(fwrite(payload,1,size,f)==size);assert(!fclose(f));
}
static void cache_tests(void)
{
    static uint8_t payload[51204];uint16_t literal=25600;
    memcpy(payload,&literal,2);
    for(unsigned i=0;i<25600;i++)original[i]=(uint16_t)(i*713);
    memcpy(payload+2,original,51200);
    uint32_t crc=animation_crc32_le(0,payload,51202);cache_file(payload,51202,crc);
    uint16_t w=0,h=0;peak=0;
    assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)==ESP_OK);
    assert(w==160&&h==160&&!memcmp(pixels,original,51200)&&!used&&peak<=2200);
    printf("R5 literal decode: %zu bytes peak application allocation (was 51202)\n",peak);
    assert(animation_cache_decode_into("frame.r5",pixels,25599,&w,&h)==ESP_ERR_INVALID_SIZE);
    fail_allocation=true;assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)==ESP_ERR_NO_MEM);fail_allocation=false;
    uint16_t *allocated=NULL;assert(animation_cache_decode("frame.r5",&allocated,&w,&h)==ESP_OK);
    assert(!memcmp(allocated,original,51200));fixture_free(allocated);assert(!used);
    cache_file(payload,51202,crc^1);assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)!=ESP_OK);
    cache_file(payload,51201,animation_crc32_le(0,payload,51201));assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)!=ESP_OK);
    uint16_t token=0x8000|25600,value=0x3715;memcpy(payload,&token,2);memcpy(payload+2,&value,2);
    cache_file(payload,4,animation_crc32_le(0,payload,4));assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)==ESP_OK);
    for(unsigned i=0;i<25600;i++)assert(pixels[i]==value);
    FILE *f=fopen("frame.r5","ab");assert(f);fputc(1,f);fclose(f);
    assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)!=ESP_OK);
    payload[0]=payload[1]=0;cache_file(payload,4,animation_crc32_le(0,payload,4));
    assert(animation_cache_decode_into("frame.r5",pixels,25600,&w,&h)!=ESP_OK);assert(!used);
    remove("frame.r5");
}
static void jpeg_tests(void)
{
    uint8_t bytes[32]={0xff,0xd8,0xff,0xc0,0,17,8,0,160,0,160};
    FILE *f=fopen("frame.jpg","wb");assert(f);fwrite(bytes,1,sizeof(bytes),f);fclose(f);
    uint16_t w=0,h=0;peak=0;
    assert(display_jpeg_decode_into("frame.jpg",pixels,25600,&w,&h)==ESP_OK);
    assert(w==160&&h==160&&peak==32&&!used);
    for(unsigned i=0;i<25600;i++)assert(pixels[i]==(uint16_t)(i*13));
    assert(display_jpeg_decode_into("frame.jpg",pixels,25599,&w,&h)==ESP_ERR_INVALID_SIZE);assert(!used);
    fail_codec=true;assert(display_jpeg_decode_into("frame.jpg",pixels,25600,&w,&h)==ESP_FAIL);fail_codec=false;assert(!used);
    fail_allocation=true;assert(display_jpeg_decode_into("frame.jpg",pixels,25600,&w,&h)==ESP_ERR_NO_MEM);fail_allocation=false;
    uint16_t *allocated=NULL;assert(display_jpeg_decode_file("frame.jpg",&allocated,&w,&h)==ESP_OK);
    assert(used==51200);fixture_free(allocated);assert(!used);
    bytes[3]=0xc2;f=fopen("frame.jpg","wb");fwrite(bytes,1,sizeof(bytes),f);fclose(f);
    assert(display_jpeg_decode_into("frame.jpg",pixels,25600,&w,&h)==ESP_FAIL);assert(!used);remove("frame.jpg");
}
static uint16_t lcd[160*160];
static companion_visual_t visual={.background=0x07e0,.base_background=0x07e0,.foreground_size=132,.revision=1,.count=5,.active=true};
static unsigned source_w=80,source_h=80;
static display_rotation_t rotation;
static void *canvas;
void companion_get_visual(int channel,companion_visual_t *out){assert(channel==1);*out=visual;}
display_rotation_t display_get_rotation(void){return rotation;}
esp_lcd_panel_handle_t get_display_panel(int channel){assert(channel==1);return (void *)1;}
esp_err_t display_draw_bitmap_sync(esp_lcd_panel_handle_t panel,int x,int y,int end_x,int end_y,const void *buffer)
{
    assert(panel==(void *)1&&x>=0&&y>=0&&end_x<=160&&end_y<=160&&end_x>x&&end_y>y);
    unsigned w=end_x-x;const uint16_t *p=buffer;
    for(int row=y;row<end_y;row++)memcpy(lcd+row*160+x,p+(row-y)*w,w*2);
    return ESP_OK;
}
uint16_t alpha_cache_gray(uint16_t v){return v;}
esp_err_t alpha_cache_decode_file_mode(const char *path,uint16_t *out,size_t capacity,uint16_t bg,alpha_cache_info_t *info,bool gray)
{
    (void)path;(void)bg;(void)gray;assert(source_w*source_h<=capacity);canvas=out;
    for(unsigned i=0;i<source_w*source_h;i++)out[i]=(uint16_t)(i*713+17);
    info->width=source_w;info->height=source_h;return ESP_OK;
}
static void composition_tests(void)
{
    assert(!used);peak=0;assert(companion_render_init()==ESP_OK);assert(used==51200);
    const unsigned sizes[][2]={{80,80},{160,50},{40,160},{1,1},{132,131},{131,132},{160,160}};
    for(unsigned r=0;r<360;r+=90)for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);s++){
        rotation=(display_rotation_t)r;source_w=sizes[s][0];source_h=sizes[s][1];
        unsigned dw=132,dh=132;if(source_w>source_h)dh=source_h*dw/source_w;else if(source_h>source_w)dw=source_w*dh/source_h;
        if(!dw)dw=1;if(!dh)dh=1;
        unsigned rw=(r==90||r==270)?dh:dw,rh=(r==90||r==270)?dw:dh;
        transform(source_w,source_h,dw,dh); // regenerates independent golden
        for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
            size_t index=r==0?y*dw+x:r==180?(dh-1-y)*dw+dw-1-x:frame_rotated_index(y*dw+x,dw,dh,r);
            expected[index]=original[y*source_h/dh*source_w+x*source_w/dw];
        }
        assert(companion_render_frame(1,"frame.a5")==ESP_OK);assert(used==51200&&peak==51200);
        unsigned left=(160-rw)/2,top=(144-rh)/2;if(top+rh>132)top=132-rh;
        for(unsigned y=0;y<132;y++)for(unsigned x=0;x<160;x++){
            uint16_t want=x>=left&&x<left+rw&&y>=top&&y<top+rh?expected[(y-top)*rw+x-left]:__builtin_bswap16(visual.background);
            assert(lcd[y*160+x]==want);
        }
        assert(lcd[144*160+80]==__builtin_bswap16((uint16_t)~visual.background));
    }
    visual.background=0;visual.revision++;
    assert(companion_render_frame(1,NULL)==ESP_OK);
    visual.base_background=0xffe0;visual.revision++;
    assert(companion_render_frame(1,NULL)==ESP_OK);
    assert(lcd[144*160+80]==__builtin_bswap16((uint16_t)~visual.base_background));
    fixture_free(canvas);assert(!used);
}
static void effect_tests(void)
{
    for(unsigned t=0;t<20000;t+=7){
        unsigned on=0;
        for(unsigned ch=0;ch<3;ch++){
            on+=light_effect_level(LIGHT_CHASE,t,1600,ch,1)==8;
            for(unsigned mode=0;mode<4;mode++)assert(light_effect_level((light_effect_t)mode,t,1600,ch,0)<=8);
            assert(light_effect_level(LIGHT_BLINK,t,1600,ch,1)==light_effect_level(LIGHT_BLINK,t,1600,0,1));
        }
        assert(on==1);
    }
    assert(light_effect_level(LIGHT_BREATHE,0,1600,0,1)==0);
    assert(light_effect_level(LIGHT_BREATHE,800,1600,0,1)==8);
    for(unsigned p=0;p<8;p++)assert(light_effect_level(LIGHT_DOUBLE,p*200,1600,0,1)==(p==0||p==2?8:0));
    assert(light_effect_color(0xf800,8)==0xf800&&light_effect_color(0xffff,0)==0);
    const uint16_t delays[]={100,200,300};
    assert(animation_link_frame(delays,3,600000,600000,99999,ANIMATION_SYNC,0)==0);
    assert(animation_link_frame(delays,3,600000,600000,100000,ANIMATION_SYNC,0)==1);
    assert(animation_link_frame(delays,3,600000,600000,300000,ANIMATION_SYNC,0)==2);
    assert(animation_link_frame(delays,3,600000,600000,600000,ANIMATION_SYNC,0)==0);
    assert(animation_link_frame(delays,3,600000,600000,0,ANIMATION_STAGGER,1)==1);
    assert(animation_link_frame(delays,3,600000,600000,0,ANIMATION_STAGGER,2)==2);
    assert(animation_link_frame(delays,3,600000,1200000,200000,ANIMATION_SYNC,2)==1);
    for(unsigned ch=0;ch<3;ch++)assert(animation_link_frame(delays,3,600000,600000,UINT64_C(600000000000)+100000,ANIMATION_SYNC,ch)==1);
    assert(animation_link_frame(NULL,0,0,0,0,ANIMATION_SYNC,0)==0);
}
int main(void)
{
    transforms();cache_tests();jpeg_tests();composition_tests();effect_tests();
    puts("In-place scale/rotation, R5 bounds/CRC/streaming, JPEG ownership and mixed-aspect backgrounds/indicators: PASS");return 0;
}
