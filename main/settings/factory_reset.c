#include "factory_reset.h"
#include "nvs.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "storage_service.h"
#include "wifi_provision.h"
#include <stddef.h>

// Protected by the storage-service mutex, also used by the poller below.
static int64_t s_restart_at;

esp_err_t factory_reset_resume(void)
{
    nvs_handle_t marker;
    esp_err_t ret=nvs_open("factory_reset",NVS_READONLY,&marker);
    if(ret==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(ret!=ESP_OK)return ret;
    uint8_t pending=0;ret=nvs_get_u8(marker,"pending",&pending);nvs_close(marker);
    if(ret==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(ret!=ESP_OK)return ret;
    if(!pending)return ESP_OK;
    if(pending!=1)return ESP_ERR_INVALID_STATE;

    // All runtime owners are absent. The confirmed marker survives every
    // destructive step and is committed away only after the last one succeeds.
    ret=esp_spiffs_format(NULL);
    if(ret!=ESP_OK)return ret;
    const char *names[]={"wifi","companion","display","nvs.net80211"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
        nvs_handle_t h;ret=nvs_open(names[i],NVS_READONLY,&h);
        if(ret==ESP_ERR_NVS_NOT_FOUND)continue;
        if(ret!=ESP_OK)return ret;
        nvs_close(h);ret=nvs_open(names[i],NVS_READWRITE,&h);
        if(ret!=ESP_OK)return ret;
        ret=nvs_erase_all(h);if(ret==ESP_OK)ret=nvs_commit(h);nvs_close(h);
        if(ret!=ESP_OK)return ret;
    }
    ret=nvs_open("factory_reset",NVS_READWRITE,&marker);
    if(ret!=ESP_OK)return ret;
    ret=nvs_erase_key(marker,"pending");if(ret==ESP_OK)ret=nvs_commit(marker);nvs_close(marker);
    return ret;
}

esp_err_t factory_reset_begin(void)
{
    if(!storage_service_lock())return ESP_ERR_INVALID_STATE;
    esp_err_t ret=ESP_OK;
    if(s_restart_at)goto finish; // Lost ACK/repeated click cannot delay restart.
    wifi_provision_status_t wifi;wifi_provision_status(&wifi);
    if(storage_import_busy()||storage_async_busy()||storage_maintenance_busy()||wifi.busy){ret=ESP_ERR_INVALID_STATE;goto finish;}
    nvs_handle_t marker;ret=nvs_open("factory_reset",NVS_READWRITE,&marker);
    if(ret!=ESP_OK)goto finish;
    ret=nvs_set_u8(marker,"pending",1);if(ret==ESP_OK)ret=nvs_commit(marker);nvs_close(marker);
    if(ret==ESP_OK){storage_maintenance_set(true);s_restart_at=esp_timer_get_time()+750000;}
finish:
    storage_service_unlock();return ret;
}
void factory_reset_tick(void)
{
    if(!storage_service_lock())return;
    bool due=s_restart_at&&esp_timer_get_time()>=s_restart_at;storage_service_unlock();
    if(due)esp_restart();
}
