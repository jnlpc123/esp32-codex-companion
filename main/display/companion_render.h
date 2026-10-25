#pragma once
#include "esp_err.h"

esp_err_t companion_render_init(void);
// Owned by the animation task. NULL path draws a background without media.
esp_err_t companion_render_frame(int channel, const char *path);
