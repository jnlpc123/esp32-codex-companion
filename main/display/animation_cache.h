#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include <stdint.h>

bool animation_cache_prepare(const char *jpeg_path, char *cache_path, size_t path_size);
esp_err_t animation_cache_decode(const char *path, uint16_t **pixels, uint16_t *width, uint16_t *height);
esp_err_t animation_cache_decode_into(const char *path, uint16_t *pixels, size_t capacity,
                                      uint16_t *width, uint16_t *height);
