#include "wifi_credentials.h"
#include "nvs.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CREDENTIAL_MAGIC 0x32574656u
typedef struct {uint32_t magic,count;wifi_credential_t entries[3];} credential_record_t;
static void clear_record(credential_record_t *record)
{volatile unsigned char *p=(volatile unsigned char*)record;for(size_t i=0;i<sizeof(*record);i++)p[i]=0;}
static esp_err_t load_open(nvs_handle_t nvs,wifi_credential_t *out,unsigned capacity,unsigned *count)
{
    *count=0;credential_record_t record={0};size_t size=sizeof(record);
    esp_err_t err=nvs_get_blob(nvs,"credentials_v2",&record,&size);
    if(err==ESP_OK){
        if(size!=sizeof(record)||record.magic!=CREDENTIAL_MAGIC||record.count>3||record.count>capacity)err=ESP_ERR_INVALID_STATE;
        for(unsigned i=0;err==ESP_OK&&i<record.count;i++)
            if(!record.entries[i].ssid[0]||!memchr(record.entries[i].ssid,0,33)||!memchr(record.entries[i].password,0,65))err=ESP_ERR_INVALID_STATE;
        if(err==ESP_OK){*count=record.count;memcpy(out,record.entries,*count*sizeof(*out));}
        clear_record(&record);return err;
    }
    clear_record(&record);if(err!=ESP_ERR_NVS_NOT_FOUND)return err;
    uint8_t legacy_count=0;err=nvs_get_u8(nvs,"count",&legacy_count);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    if(legacy_count>3||legacy_count>capacity)return ESP_ERR_INVALID_STATE;
    for(unsigned i=0;i<legacy_count;i++){
        char key[16];size_t n=sizeof(out[i].ssid);snprintf(key,sizeof(key),"ssid_%u",i);
        err=nvs_get_str(nvs,key,out[i].ssid,&n);if(err!=ESP_OK)return err;
        n=sizeof(out[i].password);snprintf(key,sizeof(key),"pwd_%u",i);
        err=nvs_get_str(nvs,key,out[i].password,&n);if(err!=ESP_OK)return err;
    }
    *count=legacy_count;return ESP_OK;
}
esp_err_t wifi_credentials_load(wifi_credential_t *out,unsigned capacity,unsigned *count)
{
    *count=0;nvs_handle_t nvs;esp_err_t err=nvs_open("wifi",NVS_READONLY,&nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    err=load_open(nvs,out,capacity,count);nvs_close(nvs);return err;
}
esp_err_t wifi_credentials_save(const char *ssid,const char *password)
{
    if(!ssid||!password||!*ssid||strlen(ssid)>32||strlen(password)>64)return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;esp_err_t err=nvs_open("wifi",NVS_READWRITE,&nvs);if(err!=ESP_OK)return err;
    credential_record_t record={.magic=CREDENTIAL_MAGIC};unsigned count=0;
    err=load_open(nvs,record.entries,3,&count);if(err!=ESP_OK)goto finish;
    unsigned index=0;while(index<count&&strcmp(record.entries[index].ssid,ssid))index++;
    if(index==count){
        if(count==3){memmove(record.entries,record.entries+1,2*sizeof(record.entries[0]));count--;}
        index=count++;
    }
    memset(&record.entries[index],0,sizeof(record.entries[index]));
    memcpy(record.entries[index].ssid,ssid,strlen(ssid));memcpy(record.entries[index].password,password,strlen(password));record.count=count;
    // NVS publishes a whole blob atomically. Never shift/overwrite legacy keys.
    err=nvs_set_blob(nvs,"credentials_v2",&record,sizeof(record));if(err==ESP_OK)err=nvs_commit(nvs);
finish:
    clear_record(&record);nvs_close(nvs);return err;
}
