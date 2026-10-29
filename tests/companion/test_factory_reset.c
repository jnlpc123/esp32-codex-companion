// Removing durable confirmation, dropping the retry marker early, or widening
// erasure to chip calibration must fail these tests of the actual reset engine.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "factory_reset.c"
#include "factory_reset_button.h"

static const char *spaces[]={"factory_reset","wifi","companion","display","nvs.net80211","phy"};
static int saved[6],format_count,files,commit_fail,format_fail,open_fail,set_fail,erase_fail,key_fail;
static bool maintenance,upload,async_job,wifi_busy,locked;
static int64_t clock_us;
static int restarts;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){
 (void)mode;for(int i=0;i<6;i++)if(!strcmp(name,spaces[i])){if(open_fail==i+1)return ESP_FAIL;*h=i+1;return ESP_OK;}return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v){assert(h==1&&!strcmp(key,"pending"));if(!saved[0])return ESP_ERR_NVS_NOT_FOUND;*v=saved[0];return ESP_OK;}
// IDF6.1 mutates immediately; commit/close do not roll back prior writes.
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v){assert(h==1&&!strcmp(key,"pending"));if(set_fail)return ESP_FAIL;saved[0]=v;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){assert(h==1&&!strcmp(key,"pending"));if(key_fail)return ESP_FAIL;saved[0]=0;return ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t h){assert(h>=2&&h<=5);if(erase_fail==h){saved[h-1]=1;return ESP_FAIL;}saved[h-1]=0;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){return commit_fail==h?ESP_FAIL:ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t esp_spiffs_format(const char *label){assert(label==NULL);format_count++;if(format_fail)return ESP_FAIL;files=0;return ESP_OK;}
bool storage_service_lock(void){if(locked)return false;locked=true;return true;}
void storage_service_unlock(void){assert(locked);locked=false;}
bool storage_import_busy(void){return upload;}
bool storage_async_busy(void){return async_job;}
bool storage_maintenance_busy(void){return maintenance;}
void storage_maintenance_set(bool v){maintenance=v;}
void wifi_provision_status(wifi_provision_status_t *s){s->busy=wifi_busy;}
int64_t esp_timer_get_time(void){return clock_us;}
void esp_restart(void){restarts++;}
static void fixture(void){memset(saved,0,sizeof(saved));for(int i=1;i<6;i++)saved[i]=3;files=50;format_count=0;commit_fail=format_fail=open_fail=set_fail=erase_fail=key_fail=0;maintenance=upload=async_job=wifi_busy=locked=false;clock_us=1000000;restarts=0;s_restart_at=0;}

static void button_tests(void){
 factory_button_t b={0};
 factory_button_t exact={0};
 assert(!factory_button_update(&exact,false,0));assert(!factory_button_update(&exact,false,100));
 assert(!factory_button_update(&exact,true,200));assert(!factory_button_update(&exact,true,250));
 assert(!factory_button_update(&exact,false,10200));assert(factory_button_update(&exact,false,10250));
 // Power-on held low, even forever, must not confirm a reset.
 for(uint32_t t=0;t<20000;t+=50)assert(!factory_button_update(&b,true,t));
 for(uint32_t t=20000;t<20200;t+=50)assert(!factory_button_update(&b,false,t));
 for(uint32_t t=20200;t<25000;t+=50)assert(!factory_button_update(&b,true,t));
 for(uint32_t t=25000;t<25200;t+=50)assert(!factory_button_update(&b,false,t));
 // Contact bounce cannot qualify; a long press fires only after stable release.
 assert(!factory_button_update(&b,true,25300));assert(!factory_button_update(&b,false,25310));
 for(uint32_t t=25400;t<36000;t+=50)assert(!factory_button_update(&b,true,t));
 assert(!factory_button_update(&b,false,36000));assert(factory_button_update(&b,false,36050));
 for(uint32_t t=36100;t<50000;t+=50)assert(!factory_button_update(&b,false,t));
}
int main(void){
 fixture();assert(factory_reset_resume()==ESP_OK);assert(files==50&&!format_count&&saved[1]);
 for(int i=0;i<4;i++){fixture();if(i==0)upload=true;if(i==1)async_job=true;if(i==2)maintenance=true;if(i==3)wifi_busy=true;assert(factory_reset_begin()!=ESP_OK);assert(!saved[0]&&files==50&&!s_restart_at);}
 fixture();set_fail=1;assert(factory_reset_begin()!=ESP_OK);assert(!saved[0]&&!maintenance&&!s_restart_at);
 fixture();commit_fail=1;assert(factory_reset_begin()!=ESP_OK);assert(saved[0]&&!maintenance&&!s_restart_at); // ACK unknown; confirmed marker can survive.
 fixture();assert(factory_reset_begin()==ESP_OK);assert(saved[0]==1&&files==50&&maintenance&&s_restart_at);
 clock_us+=500000;assert(factory_reset_begin()==ESP_OK);factory_reset_tick();assert(!restarts);clock_us+=250000;factory_reset_tick();assert(restarts==1);
 // A reboot/power cut reads the persisted marker. Stop at every failing step.
 for(int failure=0;failure<6;failure++){
  fixture();saved[0]=1;
  if(failure==0)format_fail=1;else commit_fail=failure;
  assert(factory_reset_resume()!=ESP_OK);assert(saved[0]==(failure==1?0:1)&&saved[5]==3);
  format_fail=commit_fail=0;assert(factory_reset_resume()==ESP_OK);
  assert(!files&&!saved[0]&&!saved[1]&&!saved[2]&&!saved[3]&&!saved[4]&&saved[5]==3);
  int count=format_count;assert(factory_reset_resume()==ESP_OK);assert(format_count==count);
 }
 for(int h=2;h<=5;h++){
  fixture();saved[0]=1;erase_fail=h;assert(factory_reset_resume()!=ESP_OK);assert(saved[0]&&saved[h-1]==1);
  erase_fail=0;assert(factory_reset_resume()==ESP_OK);assert(!saved[0]&&!saved[h-1]&&saved[5]==3);
 }
 fixture();saved[0]=1;key_fail=1;assert(factory_reset_resume()!=ESP_OK);assert(saved[0]&&!files&&!saved[1]&&!saved[2]&&!saved[3]&&!saved[4]);key_fail=0;assert(factory_reset_resume()==ESP_OK);
 fixture();saved[0]=1;open_fail=3;assert(factory_reset_resume()!=ESP_OK);assert(saved[0]);open_fail=0;assert(factory_reset_resume()==ESP_OK);
 button_tests();puts("Factory reset: durable confirmation, busy guards, interrupted retry, namespace scope, boot/short/long/released button: PASS");
}
