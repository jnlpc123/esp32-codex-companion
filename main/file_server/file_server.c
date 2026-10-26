#include "file_server.h"
#include "file_server_sysinfo.h"
#include "companion.h"
#include "alpha_cache.h"
#include "esp_spiffs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "cJSON.h"
#include "import_transaction.h"
#include "storage_service.h"
#include "esp_timer.h"

extern const unsigned char _binary_vibe_index_start[], _binary_vibe_index_end[];
extern const unsigned char _binary_vibe_css_start[], _binary_vibe_css_end[];
extern const unsigned char _binary_vibe_js_start[], _binary_vibe_js_end[];
extern const unsigned char _binary_vibe_gif_start[], _binary_vibe_gif_end[];
extern const unsigned char _binary_vibe_alpha_start[], _binary_vibe_alpha_end[];
static httpd_handle_t s_server;

httpd_handle_t get_file_server_handle(void) { return s_server; }

static esp_err_t command(httpd_req_t *req)
{
    char *body = calloc(1, 2049), *reply = malloc(2048);
    if (!body || !reply) { free(body); free(reply); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); }
    if (req->method == HTTP_GET) strcpy(body, "{\"v\":1,\"op\":\"get\"}");
    else {
        if (!req->content_len || req->content_len > 2048) { free(body); free(reply); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Command must be 1-2048 bytes"); }
        int used = 0;
        while (used < req->content_len) {
            int n = httpd_req_recv(req, body + used, req->content_len - used);
            if (n <= 0) { free(body); free(reply); return ESP_FAIL; }
            used += n;
        }
    }
    esp_err_t result = companion_handle_json(body, false, reply, 2048);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (result != ESP_OK) httpd_resp_set_status(req, "400 Bad Request");
    result = httpd_resp_sendstr(req, reply);
    free(body); free(reply); return result;
}

static bool safe_media_name(const char *name)
{
    if (!name || !*name || strlen(name) > 27 || strstr(name, "..") || strstr(name, "//")) return false;
    for (const char *p = name; *p; p++) if (!isalnum((unsigned char)*p) && *p != '/' && *p != '_' && *p != '-' && *p != '.') return false;
    const char *ext = strrchr(name, '.');
    return ext && (!strcmp(ext, ".jpg") || !strcmp(ext, ".txt") || !strcmp(ext,".a5") || !strcmp(ext,".png"));
}

static esp_err_t upload(httpd_req_t *req) { return storage_http_upload(req); }

static esp_err_t serve(httpd_req_t *req)
{
    const unsigned char *start = NULL, *end = NULL; const char *type = NULL;
    if (!strcmp(req->uri, "/") || !strcmp(req->uri, "/index.html")) { start = _binary_vibe_index_start; end = _binary_vibe_index_end; type = "text/html; charset=utf-8"; }
    else if (!strcmp(req->uri, "/companion.css")) { start = _binary_vibe_css_start; end = _binary_vibe_css_end; type = "text/css"; }
    else if (!strcmp(req->uri, "/companion.js")) { start = _binary_vibe_js_start; end = _binary_vibe_js_end; type = "application/javascript"; }
    else if (!strcmp(req->uri, "/vendor/gifuct.js")) { start = _binary_vibe_gif_start; end = _binary_vibe_gif_end; type = "application/javascript"; }
    else if (!strcmp(req->uri, "/alpha-codec.js")) { start = _binary_vibe_alpha_start; end = _binary_vibe_alpha_end; type = "application/javascript"; }
    if (start) { httpd_resp_set_type(req, type); httpd_resp_set_hdr(req, "Cache-Control", "no-cache"); return httpd_resp_send(req, (const char *)start, end - start); }
    if (!strncmp(req->uri, "/api/", 5) || !strncmp(req->uri, "/audio", 6) || !strncmp(req->uri, "/display", 8))
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "This interface is not part of companion firmware");
    const char *name = req->uri + 1;
    if (!safe_media_name(name)) {
        if (strchr(name, '.')) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        httpd_resp_set_status(req, "302 Found"); httpd_resp_set_hdr(req, "Location", "/"); return httpd_resp_send(req, NULL, 0);
    }
    char path[48]; snprintf(path, sizeof(path), "/data/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    const char *ext=strrchr(name,'.');
    httpd_resp_set_type(req, !strcmp(ext,".jpg") ? "image/jpeg" : !strcmp(ext,".png") ? "image/png" : !strcmp(ext,".a5") ? "application/octet-stream" : "text/plain");
    char buffer[1024]; size_t n; esp_err_t ret = ESP_OK;
    while ((n = fread(buffer, 1, sizeof(buffer), f))) if ((ret = httpd_resp_send_chunk(req, buffer, n)) != ESP_OK) break;
    fclose(f);
    if (ret == ESP_OK) ret = httpd_resp_send_chunk(req, NULL, 0);
    return ret;
}

esp_err_t example_start_file_server(const char *base_path)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16; config.stack_size = 8192;
    // Browsers may retain several KiB of cookies for esp32.local. The SDK's
    // 1 KiB default rejects the entire page before its handler with HTTP 431.
    // Its parser grows the header scratch buffer on demand up to this bound.
    config.max_req_hdr_len = 8192;
    config.lru_purge_enable = true;
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) return ret;
    httpd_uri_t get = {.uri="/api/companion", .method=HTTP_GET, .handler=command};
    httpd_uri_t post = {.uri="/api/companion", .method=HTTP_POST, .handler=command};
    httpd_uri_t file = {.uri="/upload/*", .method=HTTP_POST, .handler=upload};
    httpd_uri_t transaction={.uri="/api/import",.method=HTTP_POST,.handler=import_transaction_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &file));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &transaction));
    register_sysinfo_handler(s_server);
    return ESP_OK;
}

esp_err_t example_register_wildcard_handler(void)
{
    httpd_uri_t wildcard = {.uri="/*", .method=HTTP_GET, .handler=serve};
    return httpd_register_uri_handler(s_server, &wildcard);
}
