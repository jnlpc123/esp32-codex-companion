/*
 * System info API handler — GET /api/sysinfo
 *
 * Returns application and hardware information as JSON.
 */

#include "file_server_sysinfo.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_spiffs.h"
#include "esp_private/esp_clk.h"

#define TAG "sysinfo_api"

/* ---------------------------------------------------------------------------
 * GET /api/sysinfo — return device system info as JSON
 * -------------------------------------------------------------------------*/

static esp_err_t sysinfo_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();

    /* Collect chip info */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    /* Compose compile time string: "date time" */
    char compile_time[32];
    snprintf(compile_time, sizeof(compile_time), "%s %s",
             app_desc->date, app_desc->time);

    /* Convert ELF SHA256 to hex string */
    char elf_sha256_hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(elf_sha256_hex + i * 2, 3, "%02x",
                 app_desc->app_elf_sha256[i]);
    }
    elf_sha256_hex[64] = '\0';

    /* Get flash size */
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    uint32_t flash_size_mb = flash_size / (1024 * 1024);

    /* Get SPIFFS partition usage */
    size_t spiffs_total = 0, spiffs_used = 0;
    esp_spiffs_info(NULL, &spiffs_total, &spiffs_used);
    size_t spiffs_free = spiffs_total - spiffs_used;

    /* Build JSON response */
    char json[768];
    int n = snprintf(json, sizeof(json),
        "{"
        "\"project_name\":\"%s\","
        "\"app_version\":\"%s\","
        "\"compile_time\":\"%s\","
        "\"idf_version\":\"%s\","
        "\"elf_sha256\":\"%s\","
        "\"chip_revision\":%d,"
        "\"chip_cores\":%d,"
        "\"cpu_freq_hz\":%d,"
        "\"flash_size_mb\":%lu,"
        "\"free_heap\":%lu,"
        "\"spiffs_total\":%lu,"
        "\"spiffs_used\":%lu,"
        "\"spiffs_free\":%lu"
        "}",
        app_desc->project_name,
        app_desc->version,
        compile_time,
        app_desc->idf_ver,
        elf_sha256_hex,
        chip_info.revision,
        chip_info.cores,
        esp_clk_cpu_freq(),
        (unsigned long)flash_size_mb,
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)spiffs_total,
        (unsigned long)spiffs_used,
        (unsigned long)spiffs_free
    );

    if (n < 0 || (size_t)n >= sizeof(json)) {
        n = (int)sizeof(json) - 1;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, n);
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Register handler on the given HTTP server
 * -------------------------------------------------------------------------*/

void register_sysinfo_handler(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register sysinfo handler: server is NULL");
        return;
    }

    httpd_uri_t sysinfo_uri = {
        .uri       = "/api/sysinfo",
        .method    = HTTP_GET,
        .handler   = sysinfo_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &sysinfo_uri);

    ESP_LOGI(TAG, "Sysinfo API handler registered");
}
