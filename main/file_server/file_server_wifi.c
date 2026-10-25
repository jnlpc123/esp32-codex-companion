/*
 * WiFi API handlers for the file server.
 *
 * Ported from captive_portal.c — provides scan, configure, and status
 * endpoints registered on the shared file server HTTP server.
 */
#include "file_server_wifi.h"
#include "wifi_manager.h"
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

/* Extract string value for a given key from a simple JSON object.
   e.g., {"ssid":"MyWiFi","password":"p@ss"} → json_get_string(json, "ssid", buf, size)
   Handles \" escaped quotes within values. */
static void json_get_string(const char *json, const char *key,
                            char *out, size_t out_size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    if (!start) {
        out[0] = '\0';
        return;
    }
    start += strlen(pattern);

    size_t i = 0;
    while (*start && i < out_size - 1) {
        if (*start == '\\' && start[1] == '"') {
            out[i++] = '"';
            start += 2;
        } else if (*start == '"') {
            break;
        } else {
            out[i++] = *start++;
        }
    }
    out[i] = '\0';
}

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

static esp_err_t wifi_configure_post_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Empty request\"}");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Configure request: %s", buf);

    // Parse JSON: {"ssid":"...", "password":"..."}
    char ssid[64] = {0};
    char password[64] = {0};
    json_get_string(buf, "ssid", ssid, sizeof(ssid));
    json_get_string(buf, "password", password, sizeof(password));

    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"SSID is required\"}");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuring: SSID=%s", ssid);

    // Save to NVS
    wifi_save_credential(ssid, password);

    // Configure STA with new credentials and connect
    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, password,
            sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    esp_err_t conn_ret = esp_wifi_connect();

    if (conn_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s",
                 esp_err_to_name(conn_ret));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to start connection\"}");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WiFi connection initiated for SSID=%s", ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");

    // Wait for STA connection (up to 15 seconds), then restart
    ESP_LOGI(TAG, "Waiting for WiFi connection before restart...");
    int wait_secs = 0;
    bool sta_connected = false;
    while (wait_secs < 15) {
        wifi_ap_record_t ap_info;
        memset(&ap_info, 0, sizeof(ap_info));
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.ssid[0] != '\0') {
            sta_connected = true;
            ESP_LOGI(TAG, "WiFi connected to %s, restarting", ap_info.ssid);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_secs++;
    }

    if (!sta_connected) {
        ESP_LOGW(TAG, "WiFi not connected within %d seconds, restarting anyway", wait_secs);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Restarting device...");
    esp_restart();
    return ESP_OK; // never reached
}

/* ---------------------------------------------------------------------------
 * GET /api/wifi/status — return current WiFi connection status
 * -------------------------------------------------------------------------*/

static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
    char json[256];

    // Check STA connection status
    wifi_ap_record_t ap_info;
    memset(&ap_info, 0, sizeof(ap_info));
    bool sta_connected = false;
    char ssid[64] = "";
    char ip_addr[16] = "";

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        sta_connected = true;
        strncpy(ssid, (const char *)ap_info.ssid, sizeof(ssid) - 1);
    }

    // Get STA IP address
    if (sta_connected) {
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif == NULL) {
            sta_netif = esp_netif_get_handle_from_ifkey("STA");
        }
        if (sta_netif != NULL) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK &&
                ip_info.ip.addr != 0) {
                snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }

    // Check if AP mode is active
    bool ap_mode = false;
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ap_mode = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    }

    snprintf(json, sizeof(json),
             "{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"ap_mode\":%s}",
             sta_connected ? "true" : "false",
             ssid,
             ip_addr,
             ap_mode ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
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

    ESP_LOGI(TAG, "WiFi API handlers registered");
}
