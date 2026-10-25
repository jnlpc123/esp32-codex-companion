#pragma once
#include <stdint.h>
#include <stddef.h>

void animation_crc_init(void);
uint32_t animation_crc32_le(uint32_t crc, const uint8_t *data, size_t size);
