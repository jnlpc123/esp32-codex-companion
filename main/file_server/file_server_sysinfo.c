#include "file_server_sysinfo.h"
#include "storage_service.h"
#include <stdlib.h>
static esp_err_t sysinfo_get_handler(httpd_req_t *req)
{
    cJSON *reply=cJSON_CreateObject();if(!reply)return ESP_ERR_NO_MEM;
    if(storage_system_json(reply)!=ESP_OK){cJSON_Delete(reply);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage statistics unavailable");}
    char *text=cJSON_PrintUnformatted(reply);cJSON_Delete(reply);if(!text)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,text);free(text);return result;
}
void register_sysinfo_handler(httpd_handle_t server)
{
    httpd_uri_t uri={.uri="/api/sysinfo",.method=HTTP_GET,.handler=sysinfo_get_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server,&uri));
}
