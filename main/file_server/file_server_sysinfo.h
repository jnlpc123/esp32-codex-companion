/*
 * System info API handler
 */

#ifndef FILE_SERVER_SYSINFO_H
#define FILE_SERVER_SYSINFO_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void register_sysinfo_handler(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* FILE_SERVER_SYSINFO_H */
