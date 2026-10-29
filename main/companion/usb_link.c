#include "companion.h"
#include "config.h"
#include "factory_reset.h"
#include "factory_reset_button.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static void usb_task(void *unused)
{
    char line[2048], reply[2048];
    uint8_t bytes[128];
    size_t used = 0;
    bool overflow = false;
    factory_button_t button={0};
    while (1) {
        int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(50));
        for (int i = 0; i < count; i++) {
            if (bytes[i] == 0) { used = 0; overflow = true; continue; }
            if (bytes[i] == '\r') continue;
            if (bytes[i] == '\n') {
                if (!overflow && used) {
                    line[used] = 0;
                    companion_handle_json(line, true, reply, sizeof(reply));
                    volatile char *clear=line;for(size_t j=0;j<used;j++)clear[j]=0;
                    usb_serial_jtag_write_bytes("VIBE ", 5, pdMS_TO_TICKS(100));
                    usb_serial_jtag_write_bytes(reply, strlen(reply), pdMS_TO_TICKS(100));
                    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
                }
                used = 0; overflow = false;
            } else if (!overflow) {
                if (used + 1 < sizeof(line)) line[used++] = bytes[i];
                else { used = 0; overflow = true; }
            }
        }
        if(factory_button_update(&button,gpio_get_level(BOOT_BUTTON_GPIO)==0,(uint32_t)(esp_timer_get_time()/1000))){
            esp_err_t result=factory_reset_begin();
            if(result!=ESP_OK)ESP_LOGW("factory_reset","Busy or reset confirmation could not be saved: %s",esp_err_to_name(result));
        }
        if(gpio_get_level(BOOT_BUTTON_GPIO)!=0)factory_reset_tick();
    }
}

esp_err_t companion_usb_start(void)
{
    gpio_config_t boot={.pin_bit_mask=1ULL<<BOOT_BUTTON_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    esp_err_t gpio_result=gpio_config(&boot);if(gpio_result!=ESP_OK)return gpio_result;
    // Replies are <=2048 bytes and writes drain concurrently to the USB FIFO.
    // Keep the RX burst margin for uploads without reserving a second TX reply.
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size=2048, .rx_buffer_size=4096};
    esp_err_t ret = usb_serial_jtag_driver_install(&config);
    if (ret != ESP_OK) return ret;
    if (xTaskCreate(usb_task, "vibe_usb", 12288, NULL, 6, NULL) != pdPASS) {
        usb_serial_jtag_driver_uninstall(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
