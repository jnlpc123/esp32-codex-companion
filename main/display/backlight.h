#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct backlight_context *backlight_handle_t;

/**
 * Create a PWM backlight instance.
 * @param pin           GPIO pin for PWM output
 * @param output_invert true to invert PWM output
 * @param freq_hz       PWM frequency in Hz
 * @return handle on success, NULL on failure
 */
backlight_handle_t backlight_create(gpio_num_t pin, bool output_invert, uint32_t freq_hz);

/**
 * Destroy the backlight instance and release resources.
 */
void backlight_delete(backlight_handle_t handle);

/**
 * Get current brightness level.
 * @return brightness 0-100
 */
uint8_t backlight_get_brightness(backlight_handle_t handle);

/**
 * Set brightness with optional permanent save to NVS.
 * @param brightness 0-100
 * @param permanent  if true, save to NVS ("display" namespace)
 */
void backlight_set_brightness(backlight_handle_t handle, uint8_t brightness, bool permanent);

/**
 * Restore brightness from NVS settings ("display" namespace, key "brightness").
 */
void backlight_restore_brightness(backlight_handle_t handle);

/**
 * Set brightness immediately without transition animation.
 * @param brightness 0-100
 */
void backlight_set_brightness_immediate(backlight_handle_t handle, uint8_t brightness);

#ifdef __cplusplus
}
#endif
