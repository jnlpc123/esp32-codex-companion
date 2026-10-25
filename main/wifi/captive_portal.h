/*
 * Captive Portal for WiFi provisioning.
 *
 * Based on captive_portal_ai_programming_guide.md technical reference.
 * Provides:
 *   - WiFi SoftAP (ESP32_<MAC>, open)
 *   - DNS server (redirect all to AP IP)
 *   - HTTP server with config form
 *   - DHCP Option 114 (RFC 8910)
 *   - NVS save + esp_restart() on submit
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start AP + DNS only (no HTTP server).
 * The HTTP server is handled by the file server which always runs.
 *
 * 1. Creates default WiFi AP netif
 * 2. Initializes SoftAP with SSID "ESP32_<MAC>" (no password)
 * 3. Configures DHCP Option 114 with captive portal URL
 * 4. Starts DNS server redirecting all queries to AP IP
 * 5. Returns immediately (non-blocking)
 *
 * Call this when STA connection fails and AP provisioning mode is needed.
 */
void start_ap_dns_only(void);

/**
 * Stop the captive portal and release resources.
 * Stops DNS server and destroys AP netif.
 */
void stop_captive_portal(void);

#ifdef __cplusplus
}
#endif
