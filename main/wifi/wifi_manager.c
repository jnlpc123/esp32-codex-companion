/*
 * WiFi Manager
 *
 * Manages WiFi STA mode with credentials stored in NVS (max 3 groups).
 * Flow:
 *   1. Load cached credentials from NVS
 *   2. Scan + try to connect (best RSSI match)
 *   3. On failure, retry up to 3 cycles
 *   4. After 3 failures → enter Captive Portal AP mode for provisioning
 */
#include "wifi_manager.h"

#include "captive_portal.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#define TAG "wifi"

#define WIFI_CRED_NAMESPACE "wifi"
#define MAX_CREDENTIALS 3
#define MAX_RETRY_CYCLES 3

typedef struct {
  char ssid[64];
  char password[64];
} wifi_cred_t;

static EventGroupHandle_t s_wifi_events;
static volatile bool s_wifi_reconnect_enabled;
#define WIFI_IP_READY BIT0

static void wifi_connection_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifi_events, WIFI_IP_READY);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_wifi_events, WIFI_IP_READY);
    wifi_event_sta_disconnected_t *event = data;
    ESP_LOGW(TAG, "Station disconnected, reason=%u", event->reason);
    if (s_wifi_reconnect_enabled) {
      esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) ESP_LOGW(TAG, "Reconnect failed to start: %s", esp_err_to_name(err));
    }
  }
}

/* ---------------------------------------------------------------------------
 * NVS helpers
 * -------------------------------------------------------------------------*/

/**
 * Load WiFi credentials from NVS namespace "wifi".
 * Returns the number of credentials loaded (0..MAX_CREDENTIALS).
 */
static int wifi_load_credentials(wifi_cred_t *creds, int max_count) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(WIFI_CRED_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "No WiFi credentials found in NVS");
    return 0;
  }

  uint8_t count = 0;
  err = nvs_get_u8(nvs, "count", &count);
  if (err != ESP_OK || count == 0) {
    nvs_close(nvs);
    return 0;
  }

  if (count > max_count) count = max_count;

  for (uint8_t i = 0; i < count; i++) {
    char ssid_key[16], pwd_key[16];
    snprintf(ssid_key, sizeof(ssid_key), "ssid_%d", i);
    snprintf(pwd_key, sizeof(pwd_key), "pwd_%d", i);

    size_t len = sizeof(creds[i].ssid);
    if (nvs_get_str(nvs, ssid_key, creds[i].ssid, &len) != ESP_OK) {
      creds[i].ssid[0] = '\0';
    }
    len = sizeof(creds[i].password);
    if (nvs_get_str(nvs, pwd_key, creds[i].password, &len) != ESP_OK) {
      creds[i].password[0] = '\0';
    }
    ESP_LOGI(TAG, "Loaded credential #%d: SSID=%s", i, creds[i].ssid);
  }

  nvs_close(nvs);
  return count;
}

/* ---------------------------------------------------------------------------
 * WiFi STA operations
 * -------------------------------------------------------------------------*/

/**
 * Scan for APs and try to connect to the best matching network from the
 * provided credential list. Returns true on successful connection.
 */
static bool scan_and_connect_best(wifi_cred_t *creds, int cred_count) {
  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
  };

  ESP_LOGI(TAG, "Starting WiFi scan...");
  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
    return false;
  }

  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
  if (ap_count == 0) {
    ESP_LOGW(TAG, "No APs found");
    return false;
  }

  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (ap_records == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for AP records");
    return false;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  // Scan results are ordered by RSSI. Try each matching BSSID: a stronger
  // mesh node may associate successfully but fail DHCP or immediately drop us.
  for (int c = 0; c < cred_count; c++) {
    for (int i = 0; i < ap_count; i++) {
      if (strcmp((char *)ap_records[i].ssid, creds[c].ssid) != 0) continue;
      ESP_LOGI(TAG, "Connecting to %s BSSID=" MACSTR " RSSI=%d", creds[c].ssid,
               MAC2STR(ap_records[i].bssid), ap_records[i].rssi);
      wifi_config_t config = {0};
      strlcpy((char *)config.sta.ssid, creds[c].ssid, sizeof(config.sta.ssid));
      strlcpy((char *)config.sta.password, creds[c].password, sizeof(config.sta.password));
      config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
      config.sta.bssid_set = true;
      memcpy(config.sta.bssid, ap_records[i].bssid, sizeof(config.sta.bssid));
      xEventGroupClearBits(s_wifi_events, WIFI_IP_READY);
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
      esp_err_t err = esp_wifi_connect();
      EventBits_t bits = err == ESP_OK ? xEventGroupWaitBits(s_wifi_events, WIFI_IP_READY,
                               pdFALSE, pdTRUE, pdMS_TO_TICKS(12000)) : 0;
      wifi_ap_record_t connected;
      if ((bits & WIFI_IP_READY) && esp_wifi_sta_get_ap_info(&connected) == ESP_OK) {
        ESP_LOGI(TAG, "Connected with IP: %s BSSID=" MACSTR " Channel=%d RSSI=%d",
                 creds[c].ssid, MAC2STR(connected.bssid), connected.primary, connected.rssi);
        free(ap_records);
        return true;
      }
      ESP_LOGW(TAG, "No usable IP on this BSSID; trying next access point");
      esp_wifi_disconnect();
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }

  free(ap_records);
  return false;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void wifi_init(void) {
  s_wifi_events = xEventGroupCreate();
  assert(s_wifi_events);
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_connection_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_connection_event, NULL));
  esp_netif_t *netif = esp_netif_create_default_wifi_sta();
  assert(netif);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_LOGI(TAG, "WiFi init done");
}

bool wifi_try_connect_or_enter_ap(void) {
  s_wifi_reconnect_enabled = false;
  wifi_cred_t creds[MAX_CREDENTIALS];
  int cred_count = wifi_load_credentials(creds, MAX_CREDENTIALS);

  if (cred_count == 0) {
    ESP_LOGW(TAG, "No cached WiFi credentials found, entering AP mode");
    start_ap_dns_only();
    return false;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  /* Real-time display frames need low latency rather than modem sleep. */
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));


  for (int cycle = 0; cycle < MAX_RETRY_CYCLES; cycle++) {
    ESP_LOGI(TAG, "Connection attempt cycle %d/%d", cycle + 1,
             MAX_RETRY_CYCLES);

    if (scan_and_connect_best(creds, cred_count)) {
      s_wifi_reconnect_enabled = true;
      ESP_LOGI(TAG, "Successfully connected to WiFi");
      return true;
    }

    ESP_LOGW(TAG, "Connection attempt %d failed", cycle + 1);

    if (cycle < MAX_RETRY_CYCLES - 1) {
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }

  // All retries exhausted — enter AP mode for provisioning
  ESP_LOGW(TAG, "All %d connection attempts failed, starting AP mode",
           MAX_RETRY_CYCLES);
  start_ap_dns_only();
  return false;
}

void wifi_save_credential(const char *ssid, const char *password) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(WIFI_CRED_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return;
  }

  uint8_t count = 0;
  nvs_get_u8(nvs, "count", &count);
  if (count >= MAX_CREDENTIALS) {
    // Shift entries (FIFO eviction)
    for (uint8_t i = 1; i < MAX_CREDENTIALS; i++) {
      char src_ssid_key[16], dst_ssid_key[16];
      char src_pwd_key[16], dst_pwd_key[16];
      snprintf(src_ssid_key, sizeof(src_ssid_key), "ssid_%u", (unsigned)i);
      snprintf(dst_ssid_key, sizeof(dst_ssid_key), "ssid_%u", (unsigned)(i - 1));
      snprintf(src_pwd_key, sizeof(src_pwd_key), "pwd_%u", (unsigned)i);
      snprintf(dst_pwd_key, sizeof(dst_pwd_key), "pwd_%u", (unsigned)(i - 1));

      size_t len = 0;
      nvs_get_str(nvs, src_ssid_key, NULL, &len);
      if (len > 0) {
        char *buf = (char *)malloc(len);
        if (buf) {
          nvs_get_str(nvs, src_ssid_key, buf, &len);
          nvs_set_str(nvs, dst_ssid_key, buf);
          free(buf);
        }
      }
      len = 0;
      nvs_get_str(nvs, src_pwd_key, NULL, &len);
      if (len > 0) {
        char *buf = (char *)malloc(len);
        if (buf) {
          nvs_get_str(nvs, src_pwd_key, buf, &len);
          nvs_set_str(nvs, dst_pwd_key, buf);
          free(buf);
        }
      }
    }
    count = MAX_CREDENTIALS - 1;
  }

  char ssid_key[16], pwd_key[16];
  snprintf(ssid_key, sizeof(ssid_key), "ssid_%d", count);
  snprintf(pwd_key, sizeof(pwd_key), "pwd_%d", count);

  nvs_set_str(nvs, ssid_key, ssid);
  nvs_set_str(nvs, pwd_key, password);
  count++;
  nvs_set_u8(nvs, "count", count);
  nvs_commit(nvs);
  nvs_close(nvs);

  ESP_LOGI(TAG, "Saved credential #%d: SSID=%s", count - 1, ssid);
}
