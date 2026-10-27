#include "wifi_credentials.h"
#include "nvs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char blob[1024];static size_t bytes;static int fail_write;
static const char *ssids[]={"first","second","third"};
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *h){(void)n;(void)m;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){(void)h;assert(!strcmp(key,"credentials_v2"));if(!bytes)return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=bytes;return ESP_OK;}assert(*n>=bytes);memcpy(out,blob,bytes);*n=bytes;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t n){(void)h;assert(!strcmp(key,"credentials_v2"));if(fail_write)return ESP_FAIL;assert(n<=sizeof(blob));memcpy(blob,data,n);bytes=n;return ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){(void)h;assert(!strcmp(key,"count"));*out=3;return ESP_OK;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){(void)h;unsigned i=(unsigned)(key[strlen(key)-1]-'0');assert(i<3);const char *s=!strncmp(key,"ssid_",5)?ssids[i]:"12345678";assert(*n>strlen(s));strcpy(out,s);*n=strlen(s)+1;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
int main(void){
 wifi_credential_t credentials[3];unsigned count=0;
 assert(wifi_credentials_load(credentials,3,&count)==ESP_OK&&count==3&&!strcmp(credentials[0].ssid,"first"));
 fail_write=1;assert(wifi_credentials_save("new","12345678")!=ESP_OK);
 assert(wifi_credentials_load(credentials,3,&count)==ESP_OK&&count==3&&!strcmp(credentials[0].ssid,"first"));
 fail_write=0;assert(wifi_credentials_save("new","12345678")==ESP_OK);
 assert(wifi_credentials_load(credentials,3,&count)==ESP_OK&&count==3);
 assert(!strcmp(credentials[0].ssid,"second")&&!strcmp(credentials[1].ssid,"third")&&!strcmp(credentials[2].ssid,"new"));
 unsigned char original[1024];size_t length=bytes;memcpy(original,blob,bytes);
 fail_write=1;assert(wifi_credentials_save("next","12345678")!=ESP_OK);assert(bytes==length&&!memcmp(blob,original,bytes));
 fail_write=0;assert(wifi_credentials_save("third","87654321")==ESP_OK);
 assert(wifi_credentials_load(credentials,3,&count)==ESP_OK&&count==3&&!strcmp(credentials[1].password,"87654321"));
 blob[0]^=1;assert(wifi_credentials_load(credentials,3,&count)!=ESP_OK);
 puts("Atomic credential record: legacy fallback, full-list write failure, FIFO, update and corruption: PASS");
}
