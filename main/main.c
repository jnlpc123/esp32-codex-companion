#include "app_tasks.h"
#include "companion.h"
#include "companion_render.h"
#include "notify_tone.h"
#include "storage_service.h"
#include "config.h"
#include "display_init.h"
#include "file_serving_example_common.h"
#include "file_server.h"
#include "file_server_wifi.h"
#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mdns.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static void orientation(bool *mx, bool *my, bool *swap, int *rotation)
{
    *mx = DISPLAY_MIRROR_X; *my = DISPLAY_MIRROR_Y;
    *swap = DISPLAY_SWAP_XY; *rotation = DISPLAY_ROTATION;
    FILE *f = fopen("/data/config.json", "rb");
    if (!f) return;
    char text[2049]; size_t size = fread(text, 1, sizeof(text) - 1, f);
    fclose(f); text[size] = 0;
    cJSON *o = cJSON_Parse(text);
    if (!o) return;
    cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "display_mirror_x");
    if (cJSON_IsBool(p)) *mx = cJSON_IsTrue(p);
    p = cJSON_GetObjectItemCaseSensitive(o, "display_mirror_y");
    if (cJSON_IsBool(p)) *my = cJSON_IsTrue(p);
    p = cJSON_GetObjectItemCaseSensitive(o, "display_swap_xy");
    if (cJSON_IsBool(p)) *swap = cJSON_IsTrue(p);
    p = cJSON_GetObjectItemCaseSensitive(o, "display_rotation");
    if (cJSON_IsNumber(p) && (p->valueint == 90 || p->valueint == 180 || p->valueint == 270)) *rotation = p->valueint;
    cJSON_Delete(o);
}

void app_main(void)
{
    // Never erase user NVS or SPIFFS as an automatic upgrade recovery action.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_mount_storage("/data"));
    ESP_ERROR_CHECK(companion_init());
    ESP_ERROR_CHECK(storage_service_init());
    companion_config_t config; companion_get_config(&config);
    bool mx, my, swap; int rotation;
    orientation(&mx, &my, &swap, &rotation);
    InitializeDisplay(mx, my, swap);
    display_set_rotation((display_rotation_t)rotation);
    display_set_brightness(config.brightness);
    ESP_ERROR_CHECK(companion_render_init());
    // Only short generated notification melodies. No input/RX or media player.
    notify_tone_init(config.volume, config.muted);
    ESP_ERROR_CHECK(anim_playback_init() == pdPASS ? ESP_OK : ESP_FAIL);
    companion_restore_media();
    ESP_ERROR_CHECK(companion_usb_start());
    storage_reclaim_legacy_assets();
    // Queueing restore is not enough: Wi-Fi RF initialization can delay flash
    // reads. Let the first three composed frames reach the panels beforehand.
    int64_t first_frame_deadline = esp_timer_get_time() + 5000000;
    while ((!anim_rendered_frames(1) || !anim_rendered_frames(2) || !anim_rendered_frames(3)) &&
           esp_timer_get_time() < first_frame_deadline) vTaskDelay(pdMS_TO_TICKS(20));
    // Stored animation and USB are ready before any network connection attempt.
    wifi_init();
    ESP_ERROR_CHECK(example_start_file_server("/data"));
    register_wifi_handlers(get_file_server_handle());
    ESP_ERROR_CHECK(example_register_wildcard_handler());
    wifi_try_connect_or_enter_ap();
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("esp32");
        mdns_instance_name_set("Codex USB Companion");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
}
