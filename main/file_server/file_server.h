#pragma once
#include "esp_http_server.h"
#include "esp_err.h"

esp_err_t example_start_file_server(const char *base_path);
httpd_handle_t get_file_server_handle(void);
esp_err_t example_register_wildcard_handler(void);
