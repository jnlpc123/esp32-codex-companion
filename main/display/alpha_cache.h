#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct {
    uint16_t width, height;
    uint32_t payload_bytes, payload_crc, pixel_hash;
} alpha_cache_info_t;
esp_err_t alpha_cache_decode_buffer(const uint8_t *data, size_t length, uint16_t *pixels,
                                    size_t capacity, uint16_t background, alpha_cache_info_t *info);
esp_err_t alpha_cache_decode_file(const char *path, uint16_t *pixels, size_t capacity,
                                  uint16_t background, alpha_cache_info_t *info);
bool alpha_cache_validate(const char *path);
esp_err_t alpha_cache_inspect(const char *path, uint16_t background, alpha_cache_info_t *info);
bool alpha_cache_selftest(void);
esp_err_t alpha_cache_inspect_mode(const char *path,uint16_t background,alpha_cache_info_t *info,bool gray);

uint16_t alpha_cache_gray(uint16_t color);
esp_err_t alpha_cache_decode_file_mode(const char *path,uint16_t *pixels,size_t capacity,uint16_t bg,alpha_cache_info_t *info,bool gray);
