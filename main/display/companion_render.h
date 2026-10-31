#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t companion_render_init(void);
// Owned by the animation task. NULL path draws a black empty-media plus sign.
esp_err_t companion_render_frame(int channel, const char *path);
// Animation task only: borrows the existing canvas while no frame is drawing.
bool companion_render_prepare_cache(const char *jpeg_path,char *cache_path,size_t path_size);
esp_err_t companion_render_prompt(int channel,const uint8_t *jpeg,size_t size,int rotation);
