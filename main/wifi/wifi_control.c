#include "wifi_control.h"
#include "wifi_provision.h"
#include "wifi_manager.h"
#include "app_tasks.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>
#include <stdio.h>

void wifi_control_status_json(cJSON *out)
{
    wifi_ap_record_t ap={0};esp_netif_ip_info_t info={0};wifi_mode_t mode=WIFI_MODE_NULL;
    char ip[16]="";bool connected=false;
    esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(wifi_is_ready()&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&netif&&esp_netif_get_ip_info(netif,&info)==ESP_OK&&info.ip.addr){
        connected=true;snprintf(ip,sizeof(ip),IPSTR,IP2STR(&info.ip));
    }
    if(wifi_is_ready())esp_wifi_get_mode(&mode);
    const char *prompts[]={"none","ap","connecting","success"};
    cJSON_AddStringToObject(out,"prompt",prompts[wifi_prompt_current()]);
    cJSON_AddStringToObject(out,"displayed_prompt",prompts[anim_network_prompt()]);
    cJSON_AddBoolToObject(out,"ignored",wifi_setup_ignored());
    cJSON_AddBoolToObject(out,"connected",connected);cJSON_AddBoolToObject(out,"ready",wifi_is_ready());
    cJSON_AddStringToObject(out,"ssid",connected?(char*)ap.ssid:"");cJSON_AddStringToObject(out,"ip",ip);
    cJSON_AddBoolToObject(out,"ap_mode",mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA);
    wifi_provision_status_t status;wifi_provision_status(&status);
    const char *names[]={"idle","connecting","connected","failed"};
    cJSON *job=cJSON_AddObjectToObject(out,"setup");
    cJSON_AddNumberToObject(job,"job_id",status.job_id);cJSON_AddStringToObject(job,"state",names[status.state]);
    cJSON_AddStringToObject(job,"ssid",status.ssid);cJSON_AddBoolToObject(job,"saved",status.saved);
    if(status.error)cJSON_AddStringToObject(job,"error",status.error==ESP_ERR_TIMEOUT?"连接超时，请检查 2.4 GHz 网络和密码":"连接或保存失败，请重试");
}
bool wifi_control_command(const cJSON *request,cJSON *reply,const char **error)
{
    const cJSON *op=cJSON_GetObjectItemCaseSensitive(request,"op");
    if(!cJSON_IsString(op)||(strcmp(op->valuestring,"wifi_configure")&&strcmp(op->valuestring,"wifi_status")&&strcmp(op->valuestring,"wifi_ignore")))return false;
    if(!strcmp(op->valuestring,"wifi_ignore")){
        const cJSON *ignored=cJSON_GetObjectItemCaseSensitive(request,"ignored");
        if(!cJSON_IsBool(ignored)){*error="invalid Wi-Fi ignore flag";return true;}
        if(wifi_set_setup_ignored(cJSON_IsTrue(ignored))!=ESP_OK){*error="Wi-Fi ignore preference could not be saved";return true;}
    }
    if(!strcmp(op->valuestring,"wifi_configure")){
        const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(request,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(request,"password");
        if(!cJSON_IsString(ssid)||!cJSON_IsString(password)){*error="invalid Wi-Fi credentials";return true;}
        uint32_t id;esp_err_t ret=wifi_provision_begin(ssid->valuestring,password->valuestring,&id);
        if(ret!=ESP_OK){*error=ret==ESP_ERR_INVALID_ARG?"invalid Wi-Fi credentials":ret==ESP_ERR_INVALID_STATE?"Wi-Fi initializing or configuration busy; retry":"Wi-Fi setup could not start";return true;}
    }
    wifi_control_status_json(cJSON_AddObjectToObject(reply,"wifi"));return true;
}
