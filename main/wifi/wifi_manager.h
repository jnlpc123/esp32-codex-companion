/*
 * WiFi Manager
 *
 * Handles WiFi STA connection using NVS-stored credentials.
 * Falls back to Captive Portal AP mode if connection fails after retries.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "wifi_prompt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WiFi in STA mode.
 * Must be called before wifi_try_connect_or_enter_ap().
 */
void wifi_init(void);

/**
 * Try to connect to a cached WiFi network.
 * - Loads credentials from NVS (max 3)
 * - Scans + connects to the best matching AP
 * - Retries up to 3 cycles on failure
 * - On total failure, enters Captive Portal AP mode (blocks)
 *
 * @return true if connected successfully, false if AP mode was entered
 */
bool wifi_try_connect_or_enter_ap(void);

bool wifi_is_ready(void);
esp_err_t wifi_connect_requested(const char *ssid,const char *password);
esp_err_t wifi_save_credential_checked(const char *ssid,const char *password);
bool wifi_setup_ignored(void);
esp_err_t wifi_set_setup_ignored(bool ignored);
wifi_prompt_t wifi_prompt_current(void);
void wifi_configuration_connected(void);
void wifi_configuration_complete(void);
void wifi_configuration_failed(void);

#ifdef __cplusplus
}
#endif
