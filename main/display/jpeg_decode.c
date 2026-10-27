#include "display_init.h"
#include "config.h"
#include "animation_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <jpeg_decoder.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TAG "display"

/* ---------------------------------------------------------------------------
 * JPEG decoding and rendering
 * -------------------------------------------------------------------------*/

static esp_err_t jpeg_decode_file(const char *filepath,
                                    uint16_t **out_buf,
                                    uint16_t *out_width,
                                    uint16_t *out_height,
                                    uint16_t *supplied, size_t capacity) {
  const char *extension = strrchr(filepath, '.');
  if (extension && strcmp(extension, ".r5") == 0) {
    return supplied ? animation_cache_decode_into(filepath,supplied,capacity,out_width,out_height) :
                      animation_cache_decode(filepath,out_buf,out_width,out_height);
  }
  int64_t read_start = esp_timer_get_time();
  FILE *f = fopen(filepath, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", filepath);
    return ESP_FAIL;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize < 32 || fsize > 65536) {
    ESP_LOGE(TAG, "Empty or invalid file: %s", filepath);
    fclose(f);
    return ESP_FAIL;
  }

  uint8_t *jpeg_buf = (uint8_t *)malloc((size_t)fsize);
  if (!jpeg_buf) {
    ESP_LOGE(TAG, "Failed to allocate JPEG read buffer (%ld bytes)", fsize);
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t read_bytes = fread(jpeg_buf, 1, (size_t)fsize, f);
  fclose(f);

  int64_t read_finished = esp_timer_get_time();

  if (read_bytes != (size_t)fsize) {
    ESP_LOGW(TAG, "Short read: %u/%ld bytes", (unsigned)read_bytes, fsize);
    free(jpeg_buf); return ESP_FAIL;
  }

  uint16_t jpeg_width = 0, jpeg_height = 0;
  /* --- Diagnostic: scan for SOF marker to identify JPEG type --- */
  /* TJpgDec only supports baseline JPEG (SOF0 = 0xFFC0).             */
  /* Progressive JPEG (SOF2 = 0xFFC2) and other types are rejected.    */
  {
    bool is_baseline = false;
    const char *sof_desc = "unknown";
    for (size_t i = 2; i < (size_t)fsize - 1; i++) {
      if (jpeg_buf[i] == 0xFF && jpeg_buf[i + 1] >= 0xC0 &&
          jpeg_buf[i + 1] <= 0xCF && jpeg_buf[i + 1] != 0xC4 &&
          jpeg_buf[i + 1] != 0xC8 && jpeg_buf[i + 1] != 0xCC) {
        switch (jpeg_buf[i + 1]) {
        case 0xC0:
          is_baseline = i + 8 < (size_t)fsize;
          if (is_baseline) {
            jpeg_height = ((uint16_t)jpeg_buf[i + 5] << 8) | jpeg_buf[i + 6];
            jpeg_width = ((uint16_t)jpeg_buf[i + 7] << 8) | jpeg_buf[i + 8];
          }
          sof_desc = "SOF0 (baseline)";
          break;
        case 0xC1:
          sof_desc = "SOF1 (extended sequential) - NOT SUPPORTED";
          break;
        case 0xC2:
          sof_desc = "SOF2 (progressive) - NOT SUPPORTED";
          break;
        case 0xC3:
          sof_desc = "SOF3 (lossless) - NOT SUPPORTED";
          break;
        default:
          sof_desc = "other - NOT SUPPORTED";
          break;
        }
        break;
      }
    }
    ESP_LOGD(TAG, "JPEG type for %s: %s, size=%ld", filepath, sof_desc,
             fsize);
    if (!is_baseline) {
      ESP_LOGE(TAG, "Unsupported JPEG type: %s. Convert images to baseline JPEG (SOF0).", sof_desc);
      free(jpeg_buf);
      return ESP_FAIL;
    }
  }

  if (!jpeg_width || !jpeg_height || jpeg_width > DISPLAY_WIDTH || jpeg_height > DISPLAY_HEIGHT) {
    free(jpeg_buf); return ESP_ERR_INVALID_SIZE;
  }
  size_t out_buf_size = (size_t)jpeg_width * jpeg_height * sizeof(uint16_t);
  if(supplied && (size_t)jpeg_width*jpeg_height>capacity){free(jpeg_buf);return ESP_ERR_INVALID_SIZE;}
  uint16_t *img_buf = supplied ? supplied : (uint16_t *)malloc(out_buf_size);
  if (!img_buf) {
    ESP_LOGE(TAG, "Failed to allocate output buffer");
    free(jpeg_buf);
    return ESP_ERR_NO_MEM;
  }

  esp_jpeg_image_cfg_t jpeg_cfg = {
      .indata = jpeg_buf,
      .indata_size = (uint32_t)fsize,
      .outbuf = (uint8_t *)img_buf,
      .outbuf_size = (uint32_t)out_buf_size,
      .out_format = JPEG_IMAGE_FORMAT_RGB565,
      .out_scale = JPEG_IMAGE_SCALE_0,
      .flags = {.swap_color_bytes = 1},
  };
  esp_jpeg_image_output_t outimg;

  esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
  int64_t decode_finished = esp_timer_get_time();
  free(jpeg_buf);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "JPEG decode failed for %s: %s", filepath,
             esp_err_to_name(ret));
    if(!supplied)free(img_buf);
    return ret;
  }

  *out_buf = img_buf;
  *out_width = outimg.width;
  *out_height = outimg.height;
  static unsigned timings;
  if (timings++ < 16) {
    ESP_LOGI(TAG, "[jpeg] %s read=%lldus decode=%lldus size=%ux%u heap=%u",
             filepath, (long long)(read_finished - read_start),
             (long long)(decode_finished - read_finished), outimg.width, outimg.height,
             (unsigned)esp_get_free_heap_size());
  }

  ESP_LOGD(TAG, "Decoded %s: %dx%d", filepath, outimg.width, outimg.height);
  return ESP_OK;
}

esp_err_t display_jpeg_decode_file(const char *filepath, uint16_t **out_buf,
                                  uint16_t *out_width, uint16_t *out_height)
{
  if(!filepath||!out_buf||!out_width||!out_height)return ESP_ERR_INVALID_ARG;
  return jpeg_decode_file(filepath,out_buf,out_width,out_height,NULL,0);
}

esp_err_t display_jpeg_decode_into(const char *filepath, uint16_t *pixels, size_t capacity,
                                  uint16_t *width, uint16_t *height)
{
  if(!filepath||!pixels||!width||!height)return ESP_ERR_INVALID_ARG;
  uint16_t *output=NULL;
  return jpeg_decode_file(filepath,&output,width,height,pixels,capacity);
}
