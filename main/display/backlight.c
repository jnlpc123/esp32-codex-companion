#include "backlight.h"
#include "settings.h"

#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/ledc.h>

#define TAG "Backlight"

struct backlight_context {
    /* Backlight base fields */
    esp_timer_handle_t transition_timer;
    uint8_t brightness;
    uint8_t target_brightness;
    int8_t step;
    /* PWM-specific fields */
    gpio_num_t pin;
    bool output_invert;
    uint32_t freq_hz;
};

static void backlight_set_brightness_impl(backlight_handle_t handle, uint8_t brightness);

static void backlight_timer_callback(void *arg) {
    backlight_handle_t self = (backlight_handle_t)arg;
    if (self->brightness == self->target_brightness) {
        esp_timer_stop(self->transition_timer);
        return;
    }

    self->brightness += self->step;
    backlight_set_brightness_impl(self, self->brightness);

    if (self->brightness == self->target_brightness) {
        esp_timer_stop(self->transition_timer);
    }
}

static void backlight_set_brightness_impl(backlight_handle_t handle, uint8_t brightness) {
    /* LEDC resolution set to 10bits: 100% = 1023 */
    uint32_t duty_cycle = (1023 * (uint32_t)brightness) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_cycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

backlight_handle_t backlight_create(gpio_num_t pin, bool output_invert, uint32_t freq_hz) {
    backlight_handle_t handle = (backlight_handle_t)calloc(1, sizeof(struct backlight_context));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate backlight handle");
        return NULL;
    }

    handle->pin = pin;
    handle->output_invert = output_invert;
    handle->freq_hz = freq_hz;

    /* Configure LEDC timer for PWM backlight */
    const ledc_timer_config_t backlight_timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };
    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer_cfg));

    /* Configure LEDC channel for PWM backlight control */
    const ledc_channel_config_t backlight_channel_cfg = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = output_invert,
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel_cfg));

    /* Create brightness transition timer */
    const esp_timer_create_args_t timer_args = {
        .callback = backlight_timer_callback,
        .arg = handle,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "backlight_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &handle->transition_timer));

    return handle;
}

void backlight_delete(backlight_handle_t handle) {
    if (!handle) return;
    if (handle->transition_timer != NULL) {
        esp_timer_stop(handle->transition_timer);
        esp_timer_delete(handle->transition_timer);
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    free(handle);
}

uint8_t backlight_get_brightness(backlight_handle_t handle) {
    if (!handle) return 0;
    return handle->brightness;
}

void backlight_set_brightness(backlight_handle_t handle, uint8_t brightness, bool permanent) {
    if (!handle) return;
    if (brightness > 100) {
        brightness = 100;
    }

    if (handle->brightness == brightness) {
        return;
    }

    if (permanent) {
        settings_handle_t s = settings_open("display", true);
        if (s) {
            settings_set_int(s, "brightness", brightness);
            settings_close(s);
        }
    }

    handle->target_brightness = brightness;
    handle->step = (handle->target_brightness > handle->brightness) ? 1 : -1;

    if (handle->transition_timer != NULL) {
        /* Start periodic timer at 5ms intervals */
        esp_timer_start_periodic(handle->transition_timer, 5 * 1000);
    }
    ESP_LOGI(TAG, "Set brightness to %d", brightness);
}

void backlight_restore_brightness(backlight_handle_t handle) {
    if (!handle) return;

    settings_handle_t s = settings_open("display", false);
    int saved_brightness = settings_get_int(s, "brightness", 75);
    settings_close(s);

    /* If brightness value is 0 or too small, set a safe default */
    if (saved_brightness <= 0) {
        ESP_LOGW(TAG, "Brightness value (%d) is too small, setting to default (10)", saved_brightness);
        saved_brightness = 10;
    }

    backlight_set_brightness(handle, (uint8_t)saved_brightness, false);
}

void backlight_set_brightness_immediate(backlight_handle_t handle, uint8_t brightness) {
    if (!handle) return;
    if (brightness > 100) {
        brightness = 100;
    }
    handle->brightness = brightness;
    handle->target_brightness = brightness;
    backlight_set_brightness_impl(handle, brightness);
}
