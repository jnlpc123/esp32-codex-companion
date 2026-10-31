#include "es8311.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "core/config.h"

static const char *TAG = "es8311";

// The SDK's default codec scale is 0..100 = -50..0 dB. The UI uses
// 0..80: give short notifications a useful range while retaining headroom.
static int codec_volume(int volume)
{
    if(volume<=1)return 0;
    if(volume<20)return (volume-1)*56/19;
    if(volume<40)return 56+(volume-20)*28/20;
    if(volume<60)return 84+(volume-40)*8/20;
    if(volume<80)return 92+(volume-60)*4/20;
    return 96;
}

struct es8311_context {
    i2c_master_bus_handle_t i2c_bus_handle;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_data_if_t *data_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    i2s_chan_handle_t tx_handle;
    esp_codec_dev_handle_t codec_handle;
};

static esp_err_t es8311_bus_init(es8311_handle_t handle)
{
    /* Initialize I2C peripheral */
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = (i2c_port_t)ES8311_I2C_NUM,
        .sda_io_num = ES8311_I2C_SDA_IO,
        .scl_io_num = ES8311_I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags ={
            .enable_internal_pullup = true,
            .allow_pd = false,
        }
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_cfg, &handle->i2c_bus_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    /* Codec addresses use 8-bit format; the bus probe takes a 7-bit address.
     * Probe before allocating I2S resources or touching codec registers. */
    ret = i2c_master_probe(handle->i2c_bus_handle,
                           ES8311_CODEC_DEFAULT_ADDR >> 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 unavailable at 0x%02x (%s); skipping audio initialization",
                 ES8311_CODEC_DEFAULT_ADDR >> 1, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t es8311_codec_init(es8311_handle_t handle, int volume)
{

    /* Create control interface with I2C bus handle */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)ES8311_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = handle->i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    handle->ctrl_if = ctrl_if;
    if (!ctrl_if) return ESP_ERR_NO_MEM;

    /* Create data interface with I2S bus handle */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = ES8311_I2S_NUM,
        .rx_handle = NULL,
        .tx_handle = handle->tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    handle->data_if = data_if;
    if (!data_if) return ESP_ERR_NO_MEM;

    /* Create ES8311 interface handle */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    handle->gpio_if = gpio_if;
    if (!gpio_if) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = ES8311_PA_CTRL_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = ES8311_I2S_MCLK_IO >= 0,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = ES8311_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    handle->codec_if = es8311_if;
    if (!es8311_if) return ESP_FAIL;

    /* Create the top codec handle */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    handle->codec_handle = esp_codec_dev_new(&dev_cfg);
    if (!handle->codec_handle) return ESP_ERR_NO_MEM;

    /* Specify the sample configurations and open the device */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = ES8311_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(handle->codec_handle, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Open codec device failed");
        return ESP_FAIL;
    }

    /* Set the initial volume */
    if (esp_codec_dev_set_out_vol(handle->codec_handle, codec_volume(volume)) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "set output volume failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t i2s_driver_init(es8311_handle_t handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ES8311_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &handle->tx_handle, NULL);
    if (ret != ESP_OK) return ret;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ES8311_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ES8311_I2S_MCLK_IO,
            .bclk = ES8311_I2S_BCLK_IO,
            .ws = ES8311_I2S_WS_IO,
            .dout = ES8311_I2S_DOUT_IO,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = ES8311_MCLK_MULTIPLE;

    ret = i2s_channel_init_std_mode(handle->tx_handle, &std_cfg);
    if (ret != ESP_OK) return ret;
    ret = i2s_channel_enable(handle->tx_handle);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

es8311_handle_t es8311_init(int volume)
{
    es8311_handle_t handle = (es8311_handle_t)calloc(1, sizeof(struct es8311_context));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for ES8311 context");
        return NULL;
    }

    if (es8311_bus_init(handle) != ESP_OK) {
        es8311_deinit(handle);
        return NULL;
    }

    /* Initialize I2S only after confirming the codec is present. */
    if (i2s_driver_init(handle) != ESP_OK) {
        ESP_LOGE(TAG, "i2s driver init failed");
        es8311_deinit(handle);
        return NULL;
    }
    ESP_LOGI(TAG, "i2s driver init success");

    /* Initialize i2c peripheral and config es8311 codec by i2c */
    if (es8311_codec_init(handle, volume) != ESP_OK) {
        ESP_LOGE(TAG, "es8311 codec init failed");
        es8311_deinit(handle);
        return NULL;
    }
    ESP_LOGI(TAG, "es8311 codec init success");

    return handle;
}

void es8311_deinit(es8311_handle_t handle)
{
    if (handle) {
        if (handle->codec_handle) {
            esp_codec_dev_close(handle->codec_handle);
            esp_codec_dev_delete(handle->codec_handle);
        }
        if (handle->codec_if) audio_codec_delete_codec_if(handle->codec_if);
        if (handle->data_if) audio_codec_delete_data_if(handle->data_if);
        if (handle->gpio_if) audio_codec_delete_gpio_if(handle->gpio_if);
        if (handle->ctrl_if) audio_codec_delete_ctrl_if(handle->ctrl_if);
        if (handle->tx_handle) {
            i2s_channel_disable(handle->tx_handle);
            i2s_del_channel(handle->tx_handle);
        }
        if (handle->i2c_bus_handle) {
            i2c_del_master_bus(handle->i2c_bus_handle);
        }
        free(handle);
    }
}

esp_err_t es8311_set_volume(es8311_handle_t handle, int volume)
{
    if (!handle || !handle->codec_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_codec_dev_set_out_vol(handle->codec_handle, codec_volume(volume));
}

i2s_chan_handle_t es8311_get_tx_handle(es8311_handle_t handle)
{
    return handle ? handle->tx_handle : NULL;
}
