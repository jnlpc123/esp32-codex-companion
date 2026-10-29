#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Only called before user storage, playback and network initialization.
esp_err_t factory_reset_resume(void);
// Records a confirmed reset; live files are never formatted here.
esp_err_t factory_reset_begin(void);
// Existing USB owner calls this even when no computer is connected.
void factory_reset_tick(void);
