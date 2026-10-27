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
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "wifi_credentials.h"
#include <string.h>

#define TAG "wifi"

#define WIFI_CRED_NAMESPACE "wifi"
#define MAX_CREDENTIALS 3
#define MAX_RETRY_CYCLES 3

typedef wifi_credential_t wifi_cred_t;

static EventGroupHandle_t s_wifi_events;
static volatile bool s_wifi_reconnect_enabled;
static volatile bool s_wifi_ready;
static bool s_setup_ignored;
static volatile bool s_manual_provision;
static SemaphoreHandle_t s_network_lock;
static bool s_ap_cleanup_pending,s_ap_cleanup_running;
static void schedule_ap_cleanup(void);
static bool station_has_ip(void);
static wifi_config_t s_previous_network;
static bool s_previous_valid,s_previous_reconnect;
static void clear_previous_network(void)
{volatile unsigned char *p=(volatile unsigned char*)&s_previous_network;for(size_t i=0;i<sizeof(s_previous_network);i++)p[i]=0;s_previous_valid=false;}
static portMUX_TYPE s_prompt_mux=portMUX_INITIALIZER_UNLOCKED;
static wifi_prompt_t s_prompt;
static int64_t s_prompt_until;
static void set_prompt(wifi_prompt_t phase)
{
  portENTER_CRITICAL(&s_prompt_mux);s_prompt=phase;
  s_prompt_until=phase==WIFI_PROMPT_SUCCESS?esp_timer_get_time()+2000000:0;
  portEXIT_CRITICAL(&s_prompt_mux);
}
#define WIFI_IP_READY BIT0

static void wifi_connection_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifi_events, WIFI_IP_READY);
    if(!s_manual_provision){set_prompt(WIFI_PROMPT_SUCCESS);schedule_ap_cleanup();}
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_wifi_events, WIFI_IP_READY);
    wifi_event_sta_disconnected_t *event = data;
    ESP_LOGW(TAG, "Station disconnected, reason=%u", event->reason);
    if (s_wifi_reconnect_enabled) {
      set_prompt(WIFI_PROMPT_CONNECTING);
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
static int wifi_load_credentials(wifi_cred_t *creds,int max_count)
{
  unsigned count=0;esp_err_t err=wifi_credentials_load(creds,max_count,&count);
  if(err!=ESP_OK){ESP_LOGW(TAG,"Could not read Wi-Fi credentials: %s",esp_err_to_name(err));return 0;}
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
      memcpy(config.sta.ssid, creds[c].ssid, strlen(creds[c].ssid));
      memcpy(config.sta.password, creds[c].password, strlen(creds[c].password));
      config.sta.threshold.authmode = creds[c].password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
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
  nvs_handle_t preferences;uint8_t ignored=0;
  if(nvs_open(WIFI_CRED_NAMESPACE,NVS_READONLY,&preferences)==ESP_OK){nvs_get_u8(preferences,"ignore_setup",&ignored);nvs_close(preferences);}
  portENTER_CRITICAL(&s_prompt_mux);s_setup_ignored=ignored==1;portEXIT_CRITICAL(&s_prompt_mux);
  s_network_lock=xSemaphoreCreateMutex();assert(s_network_lock);
  s_wifi_events = xEventGroupCreate();
  assert(s_wifi_events);
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_connection_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_connection_event, NULL));
  esp_netif_t *netif = esp_netif_create_default_wifi_sta();
  assert(netif);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_LOGI(TAG, "WiFi init done");
}

bool wifi_try_connect_or_enter_ap(void) {
  set_prompt(WIFI_PROMPT_CONNECTING);
  s_wifi_reconnect_enabled = false;
  wifi_cred_t creds[MAX_CREDENTIALS];
  int cred_count = wifi_load_credentials(creds, MAX_CREDENTIALS);

  if (cred_count == 0) {
    ESP_LOGW(TAG, "No cached WiFi credentials found, entering AP mode");
    start_ap_dns_only();
    set_prompt(WIFI_PROMPT_AP);
    s_wifi_ready=true;
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
      s_wifi_ready=true;
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
  set_prompt(WIFI_PROMPT_AP);
  s_wifi_ready=true;
  return false;
}

bool wifi_is_ready(void){return s_wifi_ready;}
bool wifi_requires_setup(void)
{
  wifi_mode_t mode=WIFI_MODE_NULL;
  if(!s_wifi_ready||wifi_setup_ignored()||station_has_ip())return false;
  return esp_wifi_get_mode(&mode)==ESP_OK&&(mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA);
}

esp_err_t wifi_connect_requested(const char *ssid,const char *password)
{
  if(!s_wifi_ready)return ESP_ERR_INVALID_STATE;
  wifi_config_t next={0};
  xSemaphoreTake(s_network_lock,portMAX_DELAY);
  clear_previous_network();
  esp_err_t result=esp_wifi_get_config(WIFI_IF_STA,&s_previous_network);
  if(result!=ESP_OK){xSemaphoreGive(s_network_lock);return result;}
  s_previous_valid=true;s_previous_reconnect=s_wifi_reconnect_enabled;s_wifi_reconnect_enabled=false;
  s_manual_provision=true;set_prompt(WIFI_PROMPT_CONNECTING);
  esp_wifi_disconnect();vTaskDelay(pdMS_TO_TICKS(100));
  memcpy(next.sta.ssid,ssid,strlen(ssid));memcpy(next.sta.password,password,strlen(password));
  next.sta.threshold.authmode=*password?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
  xEventGroupClearBits(s_wifi_events,WIFI_IP_READY);
  result=esp_wifi_set_config(WIFI_IF_STA,&next);
  // The SDK keeps this configuration in RAM; only our successful job saves NVS.
  memset(&next,0,sizeof(next));
  if(result==ESP_OK)result=esp_wifi_connect();
  EventBits_t bits=result==ESP_OK?xEventGroupWaitBits(s_wifi_events,WIFI_IP_READY,pdFALSE,pdTRUE,pdMS_TO_TICKS(15000)):0;
  wifi_ap_record_t ap={0};esp_netif_ip_info_t ip={0};
  esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  bool connected=result==ESP_OK&&(bits&WIFI_IP_READY)&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&
      !strcmp((char*)ap.ssid,ssid)&&netif&&esp_netif_get_ip_info(netif,&ip)==ESP_OK&&ip.ip.addr;
  return connected?ESP_OK:result==ESP_OK?ESP_ERR_TIMEOUT:result;
}

esp_err_t wifi_save_credential_checked(const char *ssid,const char *password)
{return wifi_credentials_save(ssid,password);}

bool wifi_setup_ignored(void)
{portENTER_CRITICAL(&s_prompt_mux);bool ignored=s_setup_ignored;portEXIT_CRITICAL(&s_prompt_mux);return ignored;}
esp_err_t wifi_set_setup_ignored(bool ignored)
{
  nvs_handle_t nvs;esp_err_t ret=nvs_open(WIFI_CRED_NAMESPACE,NVS_READWRITE,&nvs);
  if(ret!=ESP_OK)return ret;
  ret=nvs_set_u8(nvs,"ignore_setup",ignored?1:0);if(ret==ESP_OK)ret=nvs_commit(nvs);nvs_close(nvs);
  if(ret==ESP_OK){portENTER_CRITICAL(&s_prompt_mux);s_setup_ignored=ignored;portEXIT_CRITICAL(&s_prompt_mux);}
  return ret;
}
wifi_prompt_t wifi_prompt_current(void)
{
  portENTER_CRITICAL(&s_prompt_mux);
  wifi_prompt_t prompt=wifi_prompt_select(s_setup_ignored,s_prompt,esp_timer_get_time(),s_prompt_until);
  portEXIT_CRITICAL(&s_prompt_mux);return prompt;
}
void wifi_configuration_connected(void)
{s_wifi_reconnect_enabled=true;set_prompt(WIFI_PROMPT_SUCCESS);}
static bool station_has_ip(void)
{
  wifi_ap_record_t ap;esp_netif_ip_info_t ip;
  esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  return (xEventGroupGetBits(s_wifi_events)&WIFI_IP_READY)&&netif&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&
      esp_netif_get_ip_info(netif,&ip)==ESP_OK&&ip.ip.addr;
}
void wifi_configuration_complete(void)
{
  s_manual_provision=false;s_wifi_reconnect_enabled=true;
  // A disconnect during the success image must not strand the station.
  if(!station_has_ip()){
    esp_wifi_connect();xEventGroupWaitBits(s_wifi_events,WIFI_IP_READY,pdFALSE,pdTRUE,pdMS_TO_TICKS(15000));
  }
  bool connected=station_has_ip();
  portENTER_CRITICAL(&s_prompt_mux);s_ap_cleanup_pending=!connected;portEXIT_CRITICAL(&s_prompt_mux);
  if(connected){stop_captive_portal();set_prompt(WIFI_PROMPT_NONE);}
  else set_prompt(WIFI_PROMPT_CONNECTING); // Keep SoftAP available during recovery.
  clear_previous_network();xSemaphoreGive(s_network_lock);
  if(!connected)schedule_ap_cleanup();
}
void wifi_configuration_failed(void)
{
  s_manual_provision=false;bool owned=s_previous_valid;
  if(s_previous_valid){
    s_wifi_reconnect_enabled=false;esp_wifi_disconnect();vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_set_config(WIFI_IF_STA,&s_previous_network);s_wifi_reconnect_enabled=s_previous_reconnect;
    if(s_previous_reconnect)esp_wifi_connect();
    clear_previous_network();
  }
  if(owned)xSemaphoreGive(s_network_lock);
  wifi_mode_t mode=WIFI_MODE_NULL;esp_wifi_get_mode(&mode);
  set_prompt(mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA?WIFI_PROMPT_AP:s_wifi_reconnect_enabled?WIFI_PROMPT_CONNECTING:WIFI_PROMPT_NONE);
}

static void ap_cleanup_task(void *unused)
{
  vTaskDelay(pdMS_TO_TICKS(2000));
  if(xSemaphoreTake(s_network_lock,0)==pdTRUE){
    portENTER_CRITICAL(&s_prompt_mux);bool pending=s_ap_cleanup_pending;portEXIT_CRITICAL(&s_prompt_mux);
    if(pending&&station_has_ip()){
      stop_captive_portal();set_prompt(WIFI_PROMPT_NONE);
      portENTER_CRITICAL(&s_prompt_mux);s_ap_cleanup_pending=false;portEXIT_CRITICAL(&s_prompt_mux);
    }
    xSemaphoreGive(s_network_lock);
  }
  portENTER_CRITICAL(&s_prompt_mux);s_ap_cleanup_running=false;portEXIT_CRITICAL(&s_prompt_mux);
  schedule_ap_cleanup(); // Compensate a GOT_IP delivered while this task exited.
  vTaskDelete(NULL);
}
static void schedule_ap_cleanup(void)
{
  if(!s_network_lock||s_manual_provision||!station_has_ip())return;
  portENTER_CRITICAL(&s_prompt_mux);
  bool start=s_ap_cleanup_pending&&!s_ap_cleanup_running;
  if(start)s_ap_cleanup_running=true;
  portEXIT_CRITICAL(&s_prompt_mux);
  if(start&&xTaskCreate(ap_cleanup_task,"wifi_finish",4096,NULL,3,NULL)!=pdPASS){
    portENTER_CRITICAL(&s_prompt_mux);s_ap_cleanup_running=false;portEXIT_CRITICAL(&s_prompt_mux);
    ESP_LOGW(TAG,"Deferred AP cleanup could not start");
  }
}
