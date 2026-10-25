#pragma once
#include "driver/i2s_types.h"
#include "esp_err.h"

typedef struct es8311_context *es8311_handle_t;
// Output-only codec; no microphone or I2S RX channel is created.
es8311_handle_t es8311_init(int volume);
void es8311_deinit(es8311_handle_t handle);
esp_err_t es8311_set_volume(es8311_handle_t handle, int volume);
i2s_chan_handle_t es8311_get_tx_handle(es8311_handle_t handle);
