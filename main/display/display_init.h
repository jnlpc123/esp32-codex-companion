#ifndef DISPLAY_INIT_H
#define DISPLAY_INIT_H

#include <esp_lcd_panel_ops.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Display rotation --- */
typedef enum {
    DISPLAY_ROTATION_0   = 0,
    DISPLAY_ROTATION_90  = 90,
    DISPLAY_ROTATION_180 = 180,
    DISPLAY_ROTATION_270 = 270,
} display_rotation_t;

// Set the software rotation to apply to JPEG-rendered images.
void display_set_rotation(display_rotation_t rotation);
display_rotation_t display_get_rotation(void);
void display_set_brightness(int brightness);

// Initialize SPI bus, GC9A01 panels, and backlight.
// mirror_x / mirror_y / swap_xy: runtime display orientation overrides.
void InitializeDisplay(bool mirror_x, bool mirror_y, bool swap_xy);

// Get display panel handle
esp_lcd_panel_handle_t get_display_panel(int channel);

// Keep the pixel buffer alive until the SPI DMA transfer has completed.
esp_err_t display_draw_bitmap_sync(esp_lcd_panel_handle_t panel,
                                  int x_start, int y_start, int x_end, int y_end,
                                  const void *pixels);

/* --- JPEG decoding and rendering --- */

// Decode a JPEG file from SPIFFS into an RGB565 pixel buffer.
// Caller must free *out_buf when done.
esp_err_t display_jpeg_decode_file(const char *filepath,
                                    uint16_t **out_buf,
                                    uint16_t *out_width,
                                    uint16_t *out_height);

// Decode directly into a reusable RGB565 canvas; capacity is in pixels.
esp_err_t display_jpeg_decode_into(const char *filepath, uint16_t *pixels, size_t capacity,
                                  uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_INIT_H
