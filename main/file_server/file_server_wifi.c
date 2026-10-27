/*
 * WiFi API handlers for the file server.
 *
 * Ported from captive_portal.c — provides scan, configure, and status
 * endpoints registered on the shared file server HTTP server.
 */
#include "file_server_wifi.h"
#include "wifi_manager.h"
#include "wifi_control.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"
#include <string.h>
#include <stdlib.h>

#define TAG "wifi_api"

/* ---------------------------------------------------------------------------
 * Simple JSON helpers
 * -------------------------------------------------------------------------*/

/* Simple JSON string escape for SSID names containing quotes */
static void json_escape_ssid(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j < dst_size - 3) dst[j++] = '\\';
            else break;
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

/* ---------------------------------------------------------------------------
 * GET /api/wifi/scan — perform WiFi scan and return JSON list of nearby SSIDs
 * -------------------------------------------------------------------------*/

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_FAIL;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    wifi_ap_record_t *ap_records =
        (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_records == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_FAIL;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    // Build JSON array: [{"ssid":"...","rssi":-xx}, ...]
    size_t buf_size = ap_count * 128 + 4;
    char *json = (char *)malloc(buf_size);
    if (json == NULL) {
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_FAIL;
    }

    size_t offset = 0;
    offset += snprintf(json + offset, buf_size - offset, "[");
    int first = 1;
    for (int i = 0; i < ap_count; i++) {
        // Skip empty SSID (hidden networks)
        if (ap_records[i].ssid[0] == '\0') continue;

        if (!first) {
            offset += snprintf(json + offset, buf_size - offset, ",");
        }

        char escaped_ssid[128];
        json_escape_ssid((const char *)ap_records[i].ssid, escaped_ssid, sizeof(escaped_ssid));

        offset += snprintf(json + offset, buf_size - offset,
                           "{\"ssid\":\"%s\",\"rssi\":%d}",
                           escaped_ssid, ap_records[i].rssi);
        first = 0;
    }
    offset += snprintf(json + offset, buf_size - offset, "]");

    free(ap_records);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, offset);
    free(json);
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * POST /api/wifi/configure — save credentials and connect to WiFi
 * -------------------------------------------------------------------------*/

static esp_err_t send_wifi_json(httpd_req_t *req,cJSON *reply,const char *error)
{
    if(error){cJSON_AddBoolToObject(reply,"ok",false);cJSON_AddStringToObject(reply,"error",error);httpd_resp_set_status(req,"400 Bad Request");}
    else cJSON_AddBoolToObject(reply,"ok",true);
    char *text=cJSON_PrintUnformatted(reply);cJSON_Delete(reply);
    if(!text)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Wi-Fi response unavailable");
    httpd_resp_set_type(req,"application/json");esp_err_t ret=httpd_resp_sendstr(req,text);free(text);return ret;
}
static esp_err_t wifi_configure_post_handler(httpd_req_t *req)
{
    char body[1025];size_t used=0;
    if(req->content_len<1||req->content_len>=sizeof(body))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid Wi-Fi request size");
    while(used<req->content_len){int n=httpd_req_recv(req,body+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    body[used]=0;bool nul=memchr(body,0,used)!=NULL;
    cJSON *request=nul?NULL:cJSON_ParseWithOpts(body,NULL,true),*reply=cJSON_CreateObject();
    volatile unsigned char *raw=(volatile unsigned char*)body;for(size_t i=0;i<sizeof(body);i++)raw[i]=0;
    const char *error=NULL;
    if(!cJSON_IsObject(request))error="Invalid Wi-Fi request";
    else{
        cJSON_DeleteItemFromObjectCaseSensitive(request,"op");cJSON_AddStringToObject(request,"op",!strcmp(req->uri,"/api/wifi/ignore")?"wifi_ignore":"wifi_configure");
        wifi_control_command(request,reply,&error);
        cJSON *password=cJSON_GetObjectItemCaseSensitive(request,"password");
        if(cJSON_IsString(password)){volatile char *p=password->valuestring;for(size_t i=0,n=strlen(password->valuestring);i<n;i++)p[i]=0;}
    }
    cJSON_Delete(request);return send_wifi_json(req,reply,error);
}
static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
    cJSON *reply=cJSON_CreateObject();wifi_control_status_json(reply);return send_wifi_json(req,reply,NULL);
}

/* ---------------------------------------------------------------------------
 * Register all WiFi handlers on the given HTTP server
 * -------------------------------------------------------------------------*/

void register_wifi_handlers(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register WiFi handlers: server is NULL");
        return;
    }

    httpd_uri_t scan_uri = {
        .uri       = "/api/wifi/scan",
        .method    = HTTP_GET,
        .handler   = wifi_scan_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &scan_uri);

    httpd_uri_t config_uri = {
        .uri       = "/api/wifi/configure",
        .method    = HTTP_POST,
        .handler   = wifi_configure_post_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &config_uri);

    httpd_uri_t status_uri = {
        .uri       = "/api/wifi/status",
        .method    = HTTP_GET,
        .handler   = wifi_status_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
    httpd_uri_t ignore_uri={.uri="/api/wifi/ignore",.method=HTTP_POST,.handler=wifi_configure_post_handler};
    httpd_register_uri_handler(server,&ignore_uri);

    ESP_LOGI(TAG, "WiFi API handlers registered");
}
