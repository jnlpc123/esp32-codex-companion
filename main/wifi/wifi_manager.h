/*
 * WiFi Manager
 *
 * Handles WiFi STA connection using NVS-stored credentials.
 * Falls back to Captive Portal AP mode if connection fails after retries.
 */
#pragma once

#include <stdbool.h>

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

/**
 * Save a WiFi credential to NVS.
 * Max 3 entries. When full, oldest is evicted (FIFO).
 */
void wifi_save_credential(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
