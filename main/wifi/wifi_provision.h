#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {WIFI_SETUP_IDLE,WIFI_SETUP_CONNECTING,WIFI_SETUP_CONNECTED,WIFI_SETUP_FAILED} wifi_setup_state_t;
typedef struct {
    uint32_t job_id;
    wifi_setup_state_t state;
    esp_err_t error;
    bool busy,saved;
    char ssid[33];
} wifi_provision_status_t;
bool wifi_provision_valid(const char *ssid,const char *password);
esp_err_t wifi_provision_begin(const char *ssid,const char *password,uint32_t *job_id);
void wifi_provision_status(wifi_provision_status_t *out);
