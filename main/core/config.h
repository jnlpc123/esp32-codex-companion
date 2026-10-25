#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define BOARD_VER_MAJOR  1
#define BOARD_VER_MINOR  1

#if (BOARD_VER_MAJOR == 1 && BOARD_VER_MINOR == 0)
#define BOOT_BUTTON_GPIO        GPIO_NUM_9
#define DISPLAY_SPI_SCK_PIN     GPIO_NUM_1
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_0
#define DISPLAY_DC_PIN          GPIO_NUM_2
#define DISPLAY_SPI_CS1_PIN     GPIO_NUM_13
#define DISPLAY_SPI_CS2_PIN     GPIO_NUM_12
#define DISPLAY_SPI_CS3_PIN     GPIO_NUM_5

#elif (BOARD_VER_MAJOR == 1 && BOARD_VER_MINOR == 1)
#define BOOT_BUTTON_GPIO        GPIO_NUM_9
#define DISPLAY_SPI_SCK_PIN     GPIO_NUM_1
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_0
#define DISPLAY_DC_PIN          GPIO_NUM_2
#define DISPLAY_SPI_CS1_PIN     GPIO_NUM_13
#define DISPLAY_SPI_CS2_PIN     GPIO_NUM_5
#define DISPLAY_SPI_CS3_PIN     GPIO_NUM_4
/* ES8311 Audio Codec GPIO Configuration */
#define ES8311_I2C_NUM         0
#define ES8311_I2C_SDA_IO      GPIO_NUM_21      // TODO: Add real IO
#define ES8311_I2C_SCL_IO      GPIO_NUM_20      // TODO: Add real IO

#define ES8311_I2S_NUM         0
#define ES8311_I2S_MCLK_IO     GPIO_NUM_12      // TODO: Add real IO
#define ES8311_I2S_BCLK_IO    GPIO_NUM_10      // TODO: Add real IO
#define ES8311_I2S_WS_IO       GPIO_NUM_7      // TODO: Add real IO
#define ES8311_I2S_DOUT_IO     GPIO_NUM_6      // TODO: Add real IO
#define ES8311_I2S_DIN_IO      GPIO_NUM_8      // TODO: Add real IO

#define ES8311_PA_CTRL_IO     GPIO_NUM_NC      // TODO: Add real IO (Power Amplifier control)
#define ES8311_MCLK_MULTIPLE  I2S_MCLK_MULTIPLE_256

#define ES8311_SAMPLE_RATE     16000
#define ES8311_VOICE_VOLUME   80
#endif

#define DISPLAY_WIDTH   160
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_ROTATION  0
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_3
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false



#endif // _BOARD_CONFIG_H_
