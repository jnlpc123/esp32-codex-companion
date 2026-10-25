#include "display_init.h"
#include "backlight.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_lcd_gc9a01.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <stdlib.h>
#include <jpeg_decoder.h>
#include "esp_timer.h"
#include "animation_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "display"

static display_rotation_t s_rotation = DISPLAY_ROTATION_0;
static esp_lcd_panel_io_handle_t io1_handle, io2_handle, io3_handle;
static SemaphoreHandle_t s_draw_mutex;
static backlight_handle_t s_backlight;

display_rotation_t display_get_rotation(void) { return s_rotation; }
void display_set_brightness(int brightness)
{
  if (s_backlight) backlight_set_brightness_immediate(s_backlight, brightness);
}

void display_set_rotation(display_rotation_t rotation)
{
  s_rotation = rotation;
  ESP_LOGI(TAG, "Software rotation set to %d", (int)rotation);
}

static const gc9a01_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xFE, (uint8_t[]){0x00}, 0, 0},
    {0xEF, (uint8_t[]){0x00}, 0, 0},
    {0x80, (uint8_t[]){0xFF}, 1, 0},
    {0x81, (uint8_t[]){0xFF}, 1, 0},
    {0x82, (uint8_t[]){0xFF}, 1, 0},
    {0x83, (uint8_t[]){0xFF}, 1, 0},
    {0x84, (uint8_t[]){0xFF}, 1, 0},
    {0x85, (uint8_t[]){0xFF}, 1, 0},
    {0x86, (uint8_t[]){0xFF}, 1, 0},
    {0x87, (uint8_t[]){0xFF}, 1, 0},
    {0x88, (uint8_t[]){0xFF}, 1, 0},
    {0x89, (uint8_t[]){0xFF}, 1, 0},
    {0x8A, (uint8_t[]){0xFF}, 1, 0},
    {0x8B, (uint8_t[]){0xFF}, 1, 0},
    {0x8C, (uint8_t[]){0xFF}, 1, 0},
    {0x8D, (uint8_t[]){0xFF}, 1, 0},
    {0x8E, (uint8_t[]){0xFF}, 1, 0},
    {0x8F, (uint8_t[]){0xFF}, 1, 0},
    {0x3A, (uint8_t[]){0x05}, 1, 0},
    {0xEC, (uint8_t[]){0x01}, 1, 0},
    {0x74, (uint8_t[]){0x02, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x98, (uint8_t[]){0x3E}, 1, 0},
    {0x99, (uint8_t[]){0x3E}, 1, 0},
    {0xB5, (uint8_t[]){0x0D, 0x0D}, 2, 0},
    {0x60, (uint8_t[]){0x38, 0x0F, 0x79, 0x67}, 4, 0},
    {0x61, (uint8_t[]){0x38, 0x11, 0x79, 0x67}, 4, 0},
    {0x64, (uint8_t[]){0x38, 0x17, 0x71, 0x5F, 0x79, 0x67}, 6, 0},
    {0x65, (uint8_t[]){0x38, 0x13, 0x71, 0x5B, 0x79, 0x67}, 6, 0},
    {0x6A, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0x6C, (uint8_t[]){0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50}, 7, 0},
    {0x6E, (uint8_t[]){0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0x0F, 0x0F,
                       0x0D, 0x0D, 0x0B, 0x0B, 0x09, 0x09, 0x00, 0x00,
                       0x00, 0x00, 0x0A, 0x0A, 0x0C, 0x0C, 0x0E, 0x0E,
                       0x10, 0x10, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04},
     32, 0},
    {0xBF, (uint8_t[]){0x01}, 1, 0},
    {0xF9, (uint8_t[]){0x40}, 1, 0},
    {0x9B, (uint8_t[]){0x3B}, 1, 0},
    {0x93, (uint8_t[]){0x33, 0x7F, 0x00}, 3, 0},
    {0x7E, (uint8_t[]){0x30}, 1, 0},
    {0x70, (uint8_t[]){0x0D, 0x02, 0x08, 0x0D, 0x02, 0x08}, 6, 0},
    {0x71, (uint8_t[]){0x0D, 0x02, 0x08}, 3, 0},
    {0x91, (uint8_t[]){0x0E, 0x09}, 2, 0},
    {0xC3, (uint8_t[]){0x18}, 1, 0},
    {0xC4, (uint8_t[]){0x18}, 1, 0},
    {0xC9, (uint8_t[]){0x3C}, 1, 0},
    {0xF0, (uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x38}, 6, 0},
    {0xF2, (uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x34}, 6, 0},
    {0xF1, (uint8_t[]){0x4B, 0xB8, 0x7B, 0x34, 0x35, 0xEF}, 6, 0},
    {0xF3, (uint8_t[]){0x47, 0xB4, 0x72, 0x34, 0x35, 0xDA}, 6, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 200},
    {0x29, (uint8_t[]){0x00}, 0, 0},
    {0x2C, (uint8_t[]){0x00}, 0, 20},
};

static esp_lcd_panel_handle_t panel1_handle = NULL;
static esp_lcd_panel_handle_t panel2_handle = NULL;
static esp_lcd_panel_handle_t panel3_handle = NULL;

static void InitializeSpi(void) {
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
  buscfg.miso_io_num = GPIO_NUM_NC;
  buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
  buscfg.quadwp_io_num = GPIO_NUM_NC;
  buscfg.quadhd_io_num = GPIO_NUM_NC;
  buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
}

static void InitializeBacklight(void) {
  s_backlight = backlight_create(DISPLAY_BACKLIGHT_PIN,
                                                  DISPLAY_BACKLIGHT_OUTPUT_INVERT,
                                                  25000);
  if (s_backlight) {
    backlight_set_brightness_immediate(s_backlight, 60);
  }
}

esp_lcd_panel_handle_t get_display_panel(int channel)
{
  switch (channel)
  {
  case 3:
    return panel1_handle;
  case 2:
    return panel2_handle;
  case 1:
    return panel3_handle;
  default:
    return NULL;
  }
}

esp_err_t display_draw_bitmap_sync(esp_lcd_panel_handle_t panel,
                                  int x_start, int y_start, int x_end, int y_end,
                                  const void *pixels)
{
  esp_lcd_panel_io_handle_t io = panel == panel1_handle ? io1_handle :
      panel == panel2_handle ? io2_handle : panel == panel3_handle ? io3_handle : NULL;
  if (!io || !s_draw_mutex || !pixels) return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_draw_mutex, portMAX_DELAY);
  esp_err_t ret = esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, pixels);
  /* A parameter transaction drains queued color transactions. With command -1
   * and no parameters it only waits, without issuing any LCD command. */
  esp_err_t flush_ret = esp_lcd_panel_io_tx_param(io, -1, NULL, 0);
  xSemaphoreGive(s_draw_mutex);
  return ret != ESP_OK ? ret : flush_ret;
}

void InitializeDisplay(bool mirror_x, bool mirror_y, bool swap_xy) {
  s_draw_mutex = xSemaphoreCreateMutex();
  assert(s_draw_mutex);
  InitializeSpi();

  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask =
      ((1ULL << DISPLAY_SPI_CS1_PIN) | (1ULL << DISPLAY_SPI_CS2_PIN) |
       (1ULL << DISPLAY_SPI_CS3_PIN));
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);
  gpio_set_level(DISPLAY_SPI_CS1_PIN, 1);
  gpio_set_level(DISPLAY_SPI_CS2_PIN, 1);
  gpio_set_level(DISPLAY_SPI_CS2_PIN, 1);

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = DISPLAY_SPI_CS1_PIN;
  io_config.dc_gpio_num = DISPLAY_DC_PIN;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 80 * 1000 * 1000;
  io_config.trans_queue_depth = 6;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io1_handle));
  io_config.cs_gpio_num = DISPLAY_SPI_CS2_PIN;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io2_handle));
  io_config.cs_gpio_num = DISPLAY_SPI_CS3_PIN;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io3_handle));

  gc9a01_vendor_config_t gc9a01_vendor_config = {
      .init_cmds = vendor_specific_init_default,
      .init_cmds_size =
          sizeof(vendor_specific_init_default) / sizeof(gc9a01_lcd_init_cmd_t),
  };

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = GPIO_NUM_NC;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &gc9a01_vendor_config;

  ESP_ERROR_CHECK(
      esp_lcd_new_panel_gc9a01(io1_handle, &panel_config, &panel1_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel1_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel1_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel1_handle, swap_xy));
  ESP_ERROR_CHECK(
      esp_lcd_panel_mirror(panel1_handle, mirror_x, mirror_y));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel1_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel1_handle, false));

  ESP_ERROR_CHECK(
      esp_lcd_new_panel_gc9a01(io2_handle, &panel_config, &panel2_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel2_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel2_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel2_handle, swap_xy));
  ESP_ERROR_CHECK(
      esp_lcd_panel_mirror(panel2_handle, mirror_x, mirror_y));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel2_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel2_handle, false));

  ESP_ERROR_CHECK(
      esp_lcd_new_panel_gc9a01(io3_handle, &panel_config, &panel3_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel3_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel3_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel3_handle, swap_xy));
  ESP_ERROR_CHECK(
      esp_lcd_panel_mirror(panel3_handle, mirror_x, mirror_y));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel3_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel3_handle, false));

  /* Fill all panels with black before turning on backlight */
  {
    const size_t frame_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    uint16_t *black_frame = (uint16_t *)malloc(frame_bytes);
    if (black_frame) {
      memset(black_frame, 0, frame_bytes);
      esp_lcd_panel_handle_t panels[] = {panel1_handle, panel2_handle,
                                         panel3_handle};
      int i;
      for (i = 0; i < 3; i++) {
        display_draw_bitmap_sync(panels[i], 0, 0, DISPLAY_WIDTH,
                                  DISPLAY_HEIGHT, black_frame);
      }
      free(black_frame);
    }
  }

  InitializeBacklight();
}

/* ---------------------------------------------------------------------------
 * JPEG decoding and rendering
 * -------------------------------------------------------------------------*/

esp_err_t display_jpeg_decode_file(const char *filepath,
                                    uint16_t **out_buf,
                                    uint16_t *out_width,
                                    uint16_t *out_height) {
  const char *extension = strrchr(filepath, '.');
  if (extension && strcmp(extension, ".r5") == 0) {
    return animation_cache_decode(filepath, out_buf, out_width, out_height);
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
  uint16_t *img_buf = (uint16_t *)malloc(out_buf_size);
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
    free(img_buf);
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
