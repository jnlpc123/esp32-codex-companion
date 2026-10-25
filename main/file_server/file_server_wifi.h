/*
 * WiFi API handlers for the file server.
 *
 * Provides HTTP endpoints for WiFi scanning, configuration, and status
 * checking. All handlers are registered on the shared file server HTTP server.
 */
#ifndef FILE_SERVER_WIFI_H
#define FILE_SERVER_WIFI_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register WiFi API handlers on the given HTTP server.
 * Registers:
 *   GET  /api/wifi/scan      — scan nearby WiFi networks (JSON)
 *   POST /api/wifi/configure  — save credentials and connect (JSON)
 *   GET  /api/wifi/status     — current WiFi status (JSON)
 */
void register_wifi_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* FILE_SERVER_WIFI_H */
