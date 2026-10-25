/*
 * Captive Portal for WiFi provisioning.
 *
 * Provides AP mode + DNS redirect for captive portal detection.
 * The HTTP server is now handled by the file server (file_server.c),
 * which always runs. This module only manages AP netif + DNS redirect.
 */
#include "captive_portal.h"
#include "dns_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <string.h>
#include "lwip/inet.h"

#define TAG "captive_portal"

// SoftAP configuration
#define AP_MAX_CONN 4

static dns_server_handle_t s_dns_server = NULL;
static esp_netif_t *s_ap_netif = NULL;

/* ---------------------------------------------------------------------------
 * AP initialization
 * -------------------------------------------------------------------------*/

static void wifi_init_softap(void) {
  // Read MAC address for SSID
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);
  ESP_LOGI(TAG, "SoftAP SSID: %s (no password)", ssid);

  // Configure SoftAP
  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
  wifi_config.ap.ssid_len = strlen(ssid);
  wifi_config.ap.password[0] = '\0'; // no password
  wifi_config.ap.max_connection = AP_MAX_CONN;
  wifi_config.ap.authmode = WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Log IP address
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(s_ap_netif, &ip_info);
  char ip_addr[16];
  inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
  ESP_LOGI(TAG, "SoftAP IP: %s", ip_addr);
}

/* ---------------------------------------------------------------------------
 * DHCP Option 114 (RFC 8910) for captive portal detection
 * -------------------------------------------------------------------------*/

static void dhcp_set_captiveportal_uri(void) {
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(s_ap_netif, &ip_info);

  char ip_addr[16];
  inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);

  char uri[32];
  snprintf(uri, sizeof(uri), "http://%s", ip_addr);

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(s_ap_netif));
  ESP_ERROR_CHECK(esp_netif_dhcps_option(
      s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri,
      strlen(uri)));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));

  ESP_LOGI(TAG, "DHCP Option 114 set to: %s", uri);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void start_ap_dns_only(void) {
  ESP_LOGI(TAG, "Starting AP + DNS (no HTTP server)...");

  // Suppress noisy HTTP logs (still useful since file server may log)
  esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
  esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
  esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

  // Create AP netif
  s_ap_netif = esp_netif_create_default_wifi_ap();
  assert(s_ap_netif);

  // Ensure STA netif exists (needed for APSTA mode: scan + re-connect)
  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta_netif == NULL) {
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
  }

  // Initialize SoftAP
  wifi_init_softap();

  // DHCP Option 114
  dhcp_set_captiveportal_uri();

  // Start DNS server (redirects all queries to AP IP)
  dns_server_config_t dns_config =
      DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
  s_dns_server = start_dns_server(&dns_config);
  if (!s_dns_server) {
    ESP_LOGE(TAG, "Failed to start DNS server");
  } else {
    ESP_LOGI(TAG, "DNS server started (all queries → AP IP)");
  }

  ESP_LOGI(TAG, "AP+DNS running. Connect to ESP32_<MAC> and open http://192.168.4.1");

}

void stop_captive_portal(void) {
  ESP_LOGI(TAG, "Stopping Captive Portal...");
  if (s_dns_server) {
    stop_dns_server(s_dns_server);
    s_dns_server = NULL;
  }
  if (s_ap_netif) {
    esp_netif_destroy(s_ap_netif);
    s_ap_netif = NULL;
  }
  ESP_LOGI(TAG, "Captive Portal stopped");
}
