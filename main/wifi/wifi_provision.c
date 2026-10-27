#include "wifi_provision.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

typedef struct {char ssid[33],password[65];} credentials_t;
static portMUX_TYPE s_mux=portMUX_INITIALIZER_UNLOCKED;
static wifi_provision_status_t s_status;
static uint32_t s_sequence;
static void erase_credentials(credentials_t *credentials)
{
    volatile unsigned char *p=(volatile unsigned char *)credentials;
    for(size_t i=0;i<sizeof(*credentials);i++)p[i]=0;
    free(credentials);
}
bool wifi_provision_valid(const char *ssid,const char *password)
{
    if(!ssid||!password)return false;
    size_t n=strlen(ssid),p=strlen(password);
    if(!n||n>32)return false;
    if(!p||(p>=8&&p<=63))return true;
    if(p!=64)return false;
    return strspn(password,"0123456789abcdefABCDEF")==64;
}
static void provision_task(void *context)
{
    credentials_t *credentials=context;
    esp_err_t result=wifi_connect_requested(credentials->ssid,credentials->password);
    if(result==ESP_OK)result=wifi_save_credential_checked(credentials->ssid,credentials->password);
    erase_credentials(credentials);
    if(result==ESP_OK)wifi_configuration_connected();else wifi_configuration_failed();
    portENTER_CRITICAL(&s_mux);
    s_status.error=result;s_status.saved=result==ESP_OK;
    s_status.state=result==ESP_OK?WIFI_SETUP_CONNECTED:WIFI_SETUP_FAILED;
    s_status.busy=result==ESP_OK;
    portEXIT_CRITICAL(&s_mux);
    if(result==ESP_OK){
        // Leave the success image visible, then resume animation without reboot.
        vTaskDelay(pdMS_TO_TICKS(2000));wifi_configuration_complete();
        portENTER_CRITICAL(&s_mux);s_status.busy=false;portEXIT_CRITICAL(&s_mux);
    }
    vTaskDelete(NULL);
}
esp_err_t wifi_provision_begin(const char *ssid,const char *password,uint32_t *job_id)
{
    if(!job_id||!wifi_provision_valid(ssid,password))return ESP_ERR_INVALID_ARG;
    if(!wifi_is_ready())return ESP_ERR_INVALID_STATE;
    credentials_t *credentials=malloc(sizeof(*credentials));
    if(!credentials)return ESP_ERR_NO_MEM;
    memset(credentials,0,sizeof(*credentials));
    memcpy(credentials->ssid,ssid,strlen(ssid));memcpy(credentials->password,password,strlen(password));
    portENTER_CRITICAL(&s_mux);
    if(s_status.busy){portEXIT_CRITICAL(&s_mux);erase_credentials(credentials);return ESP_ERR_INVALID_STATE;}
    s_sequence=(s_sequence+1)&0x7fffffff;if(!s_sequence)s_sequence=1;
    s_status=(wifi_provision_status_t){.job_id=s_sequence,.state=WIFI_SETUP_CONNECTING,.busy=true};
    memcpy(s_status.ssid,credentials->ssid,sizeof(s_status.ssid));*job_id=s_sequence;
    portEXIT_CRITICAL(&s_mux);
    if(xTaskCreate(provision_task,"wifi_setup",6144,credentials,3,NULL)!=pdPASS){
        portENTER_CRITICAL(&s_mux);s_status.busy=false;s_status.state=WIFI_SETUP_FAILED;s_status.error=ESP_ERR_NO_MEM;portEXIT_CRITICAL(&s_mux);
        erase_credentials(credentials);return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void wifi_provision_status(wifi_provision_status_t *out)
{portENTER_CRITICAL(&s_mux);*out=s_status;portEXIT_CRITICAL(&s_mux);}
