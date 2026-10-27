#pragma once
#include "esp_err.h"
typedef struct {char ssid[33],password[65];} wifi_credential_t;
esp_err_t wifi_credentials_load(wifi_credential_t *out,unsigned capacity,unsigned *count);
esp_err_t wifi_credentials_save(const char *ssid,const char *password);
