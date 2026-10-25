#include "animation_crc.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "esp_attr.h"
#include <stdbool.h>
#include <string.h>
#include "animation_crc_tables.h"

static bool s_verified;

static uint32_t crc_slicing4(uint32_t crc, const uint8_t *data, size_t size)
{
    crc = ~crc;
    while (size >= 4) {
        uint32_t word;
        memcpy(&word, data, sizeof(word));
        word ^= crc;
        crc = crc_tables[3][word & 255] ^ crc_tables[2][(word >> 8) & 255] ^
              crc_tables[1][(word >> 16) & 255] ^ crc_tables[0][word >> 24];
        data += 4;
        size -= 4;
    }
    while (size--) crc = crc_tables[0][(crc ^ *data++) & 255] ^ (crc >> 8);
    return ~crc;
}

void animation_crc_init(void)
{
    uint8_t probe[73];
    for (unsigned i = 0; i < sizeof(probe); i++) probe[i] = i * 37 + 11;
    const uint32_t seeds[] = {0, 0xffffffffu, 0x12345678u};
    for (unsigned seed = 0; seed < 3; seed++) {
        for (unsigned offset = 0; offset < 4; offset++) {
            for (unsigned size = 0; size <= 67; size++) {
                if (crc_slicing4(seeds[seed], probe + offset, size) !=
                    esp_rom_crc32_le(seeds[seed], probe + offset, size)) {
                    ESP_LOGE("anim_crc", "ROM comparison failed; retaining ROM CRC");
                    return;
                }
            }
        }
        uint32_t chained = crc_slicing4(seeds[seed], probe, 39);
        chained = crc_slicing4(chained, probe + 39, sizeof(probe) - 39);
        if (chained != esp_rom_crc32_le(seeds[seed], probe, sizeof(probe))) return;
    }
    s_verified = true;
    ESP_LOGI("anim_crc", "Fast CRC enabled: 816 alignment/length/seed cases and chaining match ROM");
}

uint32_t animation_crc32_le(uint32_t crc, const uint8_t *data, size_t size)
{
    return s_verified ? crc_slicing4(crc, data, size) : esp_rom_crc32_le(crc, data, size);
}
