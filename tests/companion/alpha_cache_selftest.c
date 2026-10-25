#include "alpha_cache.h"
#include "animation_crc.h"
#include <string.h>
static const uint8_t fixture[]={86,65,70,49,2,0,2,0,14,0,0,0,247,220,48,97,0,0,0,0,216,210,157,120,1,0,1,128,0,0,2,192,248,0,128,255,255,64};
static void put32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(i*8);}
static void crc(uint8_t *p,size_t n){put32(p+8,n-24);put32(p+12,animation_crc32_le(0,p+24,n-24));put32(p+20,animation_crc32_le(0,p,20));}
bool alpha_cache_selftest(void) {
 if(alpha_cache_gray(0xf800)!=0x4a69 || alpha_cache_gray(0x07e0)!=0x94b2 || alpha_cache_gray(0x001f)!=0x18e3 || alpha_cache_gray(0xffff)!=0xffff)return false;
 uint16_t pixels[4];alpha_cache_info_t info;
 if(alpha_cache_decode_buffer(fixture,sizeof(fixture),pixels,4,0x07e0,&info)!=ESP_OK || info.width!=2 || info.height!=2 || pixels[0]!=0xe007 || pixels[1]!=0 || pixels[2]!=0xe083 || pixels[3]!=0xe847)return false;
 uint8_t bad[sizeof(fixture)+2];const unsigned offsets[]={0,4,8,12,16,20,24,sizeof(fixture)-1};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){memcpy(bad,fixture,sizeof(fixture));bad[offsets[i]]^=1;if(alpha_cache_decode_buffer(bad,sizeof(fixture),pixels,4,0,NULL)==ESP_OK)return false;}
 if(alpha_cache_decode_buffer(fixture,sizeof(fixture)-1,pixels,4,0,NULL)==ESP_OK || alpha_cache_decode_buffer(fixture,sizeof(fixture),pixels,3,0,NULL)==ESP_OK)return false;
 const unsigned invalid_counts[]={0,5};
 for(unsigned i=0;i<2;i++){memcpy(bad,fixture,sizeof(fixture));bad[24]=invalid_counts[i];bad[25]=0;crc(bad,sizeof(fixture));if(alpha_cache_decode_buffer(bad,sizeof(fixture),pixels,4,0,NULL)==ESP_OK)return false;}
 for(unsigned a=0;a<=255;a+=255){memcpy(bad,fixture,sizeof(fixture));bad[sizeof(fixture)-1]=a;crc(bad,sizeof(fixture));if(alpha_cache_decode_buffer(bad,sizeof(fixture),pixels,4,0,NULL)==ESP_OK)return false;}
 memcpy(bad,fixture,sizeof(fixture));bad[sizeof(fixture)]=0;bad[sizeof(fixture)+1]=0;crc(bad,sizeof(bad));if(alpha_cache_decode_buffer(bad,sizeof(bad),pixels,4,0,NULL)==ESP_OK)return false;
 return true;
}
