#include "animation_cache.h"
#include "display_init.h"
#include "animation_crc.h"
#include "storage_service.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CACHE_MAGIC 0x35434e41u
#define CACHE_MAX_PIXELS (160 * 160)
#define CACHE_MAX_BYTES (CACHE_MAX_PIXELS * 2 + 4)
static const char *TAG = "anim_cache";
typedef struct {
    uint32_t magic, source_crc, pixel_crc, encoded_crc, bytes;
    uint16_t width, height;
} cache_header_t;

static esp_err_t decode_cache(const char *path, uint16_t **pixels,
                              uint16_t *width, uint16_t *height, bool verify_pixels);

static bool valid_header(const cache_header_t *h)
{
    return h->magic == CACHE_MAGIC && h->width && h->height &&
           h->width <= 160 && h->height <= 160 && h->bytes && h->bytes <= CACHE_MAX_BYTES;
}

static bool cache_valid(const char *path, uint32_t source_crc)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    cache_header_t h;
    bool valid = fread(&h, 1, sizeof(h), f) == sizeof(h) && valid_header(&h) && h.source_crc == source_crc;
    uint32_t crc = 0;
    size_t remaining = valid ? h.bytes : 0;
    uint8_t chunk[512];
    while (remaining) {
        size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (fread(chunk, 1, n, f) != n) { valid = false; break; }
        crc = animation_crc32_le(crc, chunk, n);
        remaining -= n;
    }
    valid = valid && crc == h.encoded_crc && fgetc(f) == EOF;
    fclose(f);
    return valid;
}

static bool write_encoded(FILE *f, const void *data, size_t size, cache_header_t *h)
{
    if (fwrite(data, 1, size, f) != size) return false;
    h->encoded_crc = animation_crc32_le(h->encoded_crc, data, size);
    h->bytes += size;
    return true;
}

bool animation_cache_prepare(const char *jpeg_path, char *cache_path, size_t path_size)
{
    FILE *source = fopen(jpeg_path, "rb");
    if (!source) return false;
    uint32_t source_crc = 0;
    uint8_t chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), source))) source_crc = animation_crc32_le(source_crc, chunk, n);
    bool source_ok = !ferror(source);
    fclose(source);
    if (!source_ok) return false;
    int len = snprintf(cache_path, path_size, "/data/.anim_%08lx.r5", (unsigned long)source_crc);
    if (len < 0 || (size_t)len >= path_size) return false;
    if (cache_valid(cache_path, source_crc)) return true;
    size_t total, used;
    if (esp_spiffs_info(NULL, &total, &used) != ESP_OK || total - used < CACHE_MAX_BYTES + 131072) {
        ESP_LOGW(TAG, "Insufficient cache space; retaining JPEG: %s", jpeg_path);
        return false;
    }
    uint16_t *pixels = NULL, width = 0, height = 0;
    if (display_jpeg_decode_file(jpeg_path, &pixels, &width, &height) != ESP_OK) return false;
    if (!width || !height || width > 160 || height > 160) { free(pixels); return false; }
    cache_header_t h = {.magic=CACHE_MAGIC, .source_crc=source_crc, .width=width, .height=height};
    size_t count = width * height;
    h.pixel_crc = animation_crc32_le(0, (const uint8_t *)pixels, count * 2);
    char temp_path[64];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", cache_path);
    FILE *f = fopen(temp_path, "wb");
    if (!f) { free(pixels); return false; }
    setvbuf(f, NULL, _IOFBF, 4096);
    bool ok = fwrite(&h, 1, sizeof(h), f) == sizeof(h);
    size_t i = 0;
    while (ok && i < count) {
        size_t run = 1;
        while (i + run < count && pixels[i + run] == pixels[i] && run < 32767) run++;
        if (run >= 3) {
            uint16_t token = 0x8000u | run;
            ok = write_encoded(f, &token, 2, &h) && write_encoded(f, pixels + i, 2, &h);
            i += run;
        } else {
            size_t start = i;
            i += run;
            while (i < count && i - start < 32767) {
                if (i + 2 < count && pixels[i] == pixels[i+1] && pixels[i] == pixels[i+2]) break;
                i++;
            }
            uint16_t token = i - start;
            ok = write_encoded(f, &token, 2, &h) && write_encoded(f, pixels + start, token * 2, &h);
        }
    }
    if (ok) ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(&h, 1, sizeof(h), f) == sizeof(h);
    if (fclose(f) != 0) ok = false;
    // Verify exact RGB565 output before publishing the cache.
    uint16_t *check = NULL, check_w = 0, check_h = 0;
    free(pixels);
    if (ok) {
        ok = decode_cache(temp_path, &check, &check_w, &check_h, true) == ESP_OK;
        free(check);
    }
    if (ok) {
        unlink(cache_path);
        ok = rename(temp_path, cache_path) == 0;
    }
    if (!ok) unlink(temp_path);
    storage_note_change(); // Includes failed temporary writes and replacement.
    if (ok) ESP_LOGI(TAG, "Cached %s: %lu bytes, pixel CRC verified", jpeg_path, (unsigned long)h.bytes);
    return ok;
}

typedef struct {
    FILE *file;
    size_t remaining, position, filled;
    uint32_t crc;
    uint8_t buffer[2048];
} cache_stream_t;

static bool stream_read(cache_stream_t *stream, void *target, size_t size)
{
    uint8_t *out = target;
    while (size) {
        if (stream->position == stream->filled) {
            size_t count = stream->remaining < sizeof(stream->buffer) ? stream->remaining : sizeof(stream->buffer);
            if (!count || fread(stream->buffer, 1, count, stream->file) != count) return false;
            stream->crc = animation_crc32_le(stream->crc, stream->buffer, count);
            stream->remaining -= count;
            stream->position = 0;
            stream->filled = count;
        }
        size_t count = stream->filled - stream->position;
        if (count > size) count = size;
        memcpy(out, stream->buffer + stream->position, count);
        stream->position += count; out += count; size -= count;
    }
    return true;
}

static esp_err_t decode_cache(const char *path, uint16_t **pixels,
                              uint16_t *width, uint16_t *height, bool verify_pixels)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    cache_header_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || !valid_header(&h)) { fclose(f); return ESP_FAIL; }
    size_t count = h.width * h.height;
    // Reserve the large contiguous pixel allocation first. Never hold an entire
    // encoded frame alongside it: that starves the signal-composition canvas.
    uint16_t *decoded = malloc(count * 2);
    cache_stream_t *stream = calloc(1, sizeof(*stream));
    if (!decoded || !stream) { free(decoded); free(stream); fclose(f); return ESP_ERR_NO_MEM; }
    stream->file = f; stream->remaining = h.bytes;
    size_t pos = 0, out = 0;
    bool ok = true;
    while (ok && pos + 2 <= h.bytes && out < count) {
        uint16_t token;
        if (!stream_read(stream, &token, 2)) { ok = false; break; }
        pos += 2;
        size_t run = token & 0x7fff;
        size_t needed = token & 0x8000 ? 2 : run * 2;
        if (!run || run > count - out || needed > h.bytes - pos) { ok = false; break; }
        if (token & 0x8000) {
            uint16_t pixel;
            ok = stream_read(stream, &pixel, 2);
            if (ok) for (size_t j = 0; j < run; j++) decoded[out + j] = pixel;
        } else ok = stream_read(stream, decoded + out, needed);
        pos += needed; out += run;
    }
    // All encoded bytes are still verified, before any pixel reaches the LCD.
    ok = ok && pos == h.bytes && out == count && stream->crc == h.encoded_crc && fgetc(f) == EOF;
    if (ok && verify_pixels) ok = animation_crc32_le(0, (const uint8_t *)decoded, count * 2) == h.pixel_crc;
    free(stream); fclose(f);
    if (!ok) { free(decoded); return ESP_FAIL; }
    *pixels = decoded; *width = h.width; *height = h.height;
    return ESP_OK;
}

esp_err_t animation_cache_decode(const char *path, uint16_t **pixels, uint16_t *width, uint16_t *height)
{
    return decode_cache(path, pixels, width, height, false);
}

esp_err_t animation_cache_decode_into(const char *path, uint16_t *pixels, size_t capacity,
                                      uint16_t *width, uint16_t *height)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    cache_header_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || !valid_header(&h) ||
        !pixels || (size_t)h.width * h.height > capacity) { fclose(f); return ESP_FAIL; }
    uint8_t *encoded = malloc(h.bytes);
    if (!encoded) { fclose(f); return ESP_ERR_NO_MEM; }
    bool ok = fread(encoded, 1, h.bytes, f) == h.bytes && fgetc(f) == EOF;
    fclose(f);
    ok = ok && animation_crc32_le(0, encoded, h.bytes) == h.encoded_crc;
    size_t count = h.width * h.height, pos = 0, out = 0;
    while (ok && pos + 2 <= h.bytes && out < count) {
        uint16_t token; memcpy(&token, encoded + pos, 2); pos += 2;
        size_t run = token & 0x7fff;
        size_t needed = token & 0x8000 ? 2 : run * 2;
        if (!run || run > count - out || needed > h.bytes - pos) { ok = false; break; }
        if (token & 0x8000) {
            uint16_t pixel; memcpy(&pixel, encoded + pos, 2);
            for (size_t i = 0; i < run; i++) pixels[out + i] = pixel;
        } else memcpy(pixels + out, encoded + pos, needed);
        pos += needed; out += run;
    }
    ok = ok && pos == h.bytes && out == count;
    free(encoded);
    if (!ok) return ESP_FAIL;
    *width = h.width; *height = h.height;
    return ESP_OK;
}
