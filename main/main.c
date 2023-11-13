#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "drv2605.h"
#include "epaper.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hextools.h"
#include "managed_i2c.h"
#include "mma8452q.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"
#include "sid.h"

#include <inttypes.h>
#include <stdio.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <sdmmc_cmd.h>
#include <string.h>
#include <time.h>

#define AUTOMATIC_SLEEP 0

#ifndef AUTOMATIC_SLEEP
#define AUTOMATIC_SLEEP 1
#endif

#define I2C_BUS     0
#define I2C_SPEED   400000  // 400 kHz
#define I2C_TIMEOUT 250     // us

#define GPIO_I2C_SCL 7
#define GPIO_I2C_SDA 6

// extern const uint8_t renze_png_start[] asm("_binary_renze_png_start");
// extern const uint8_t renze_png_end[] asm("_binary_renze_png_end");

static char const *TAG = "main";

i2s_chan_handle_t         i2s_handle  = NULL;
adc_oneshot_unit_handle_t adc1_handle = NULL;

pax_buf_t gfx;
pax_col_t palette[] = {0xffffffff, 0xff000000, 0xffff0000};  // white, black, red

hink_t epaper = {
    .spi_bus               = SPI2_HOST,
    .pin_cs                = 8,
    .pin_dcx               = 5,
    .pin_reset             = 16,
    .pin_busy              = 10,
    .spi_speed             = 10000000,
    .spi_max_transfer_size = SOC_SPI_MAXIMUM_BUFFER_SIZE,
};

drv2605_t drv2605_device = {
    .i2c_bus     = I2C_BUS,
    .i2c_address = DRV2605_ADDR,
};

mma8452q_t mma8452q_device = {
    .i2c_bus     = I2C_BUS,
    .i2c_address = MMA8452Q_ADDR,
};

sdmmc_card_t *card = NULL;

static esp_err_t initialize_nvs(void) {
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        res = nvs_flash_erase();
        if (res != ESP_OK)
            return res;
        res = nvs_flash_init();
    }
    return res;
}

static esp_err_t initialize_adc() {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t res = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing ADC failed");
        return res;
    }
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_11,
    };
    res = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing ADC channel 2 failed");
        return res;
    }
    return res;
}

float get_battery_voltage() {
    int       raw;
    esp_err_t res = adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &raw);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Reading battery voltage failed");
        return 0;
    }

    return (raw * 3.3 * 2) / 4096;
}

static esp_err_t initialize_system() {
    esp_err_t res;

    // Non-volatile storage
    res = initialize_nvs();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing NVS failed");
        return res;
    }

    // Buttons
    // gpio_reset_pin(9);
    // gpio_reset_pin(4);
    // gpio_reset_pin(15);
    // gpio_set_direction(9, GPIO_MODE_INPUT);
    // gpio_set_direction(4, GPIO_MODE_INPUT);
    // gpio_set_direction(15, GPIO_MODE_INPUT);
    // gpio_set_pull_mode(9, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);

    // if (!gpio_get_level(9)) {
    //     hink_read_lut(19, 21, epaper.pin_cs, epaper.pin_dcx, epaper.pin_reset, epaper.pin_busy);
    // }

    // I2C bus
    // i2c_config_t i2c_config = {
    //     .mode             = I2C_MODE_MASTER,
    //     .sda_io_num       = GPIO_I2C_SDA,
    //     .scl_io_num       = GPIO_I2C_SCL,
    //     .master.clk_speed = I2C_SPEED,
    //     .sda_pullup_en    = false,
    //     .scl_pullup_en    = false,
    //     .clk_flags        = 0};

    // res = i2c_param_config(I2C_BUS, &i2c_config);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Configuring I2C bus parameters failed");
    //     return res;
    // }

    // res = i2c_set_timeout(I2C_BUS, I2C_TIMEOUT * 80);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Configuring I2C bus timeout failed");
    //     // return res;
    // }

    // res = i2c_driver_install(I2C_BUS, i2c_config.mode, 0, 0, 0);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing I2C bus failed");
    //     return res;
    // }

    // I2S audio
    // i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

    // res = i2s_new_channel(&chan_cfg, &i2s_handle, NULL);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing I2S channel failed");
    //     return res;
    // }

    // i2s_std_config_t i2s_config = {
    //     .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    //     .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    //     .gpio_cfg =
    //         {
    //             .mclk = I2S_GPIO_UNUSED,
    //             .bclk = GPIO_NUM_23,
    //             .ws   = GPIO_NUM_17,
    //             .dout = GPIO_NUM_22,
    //             .din  = I2S_GPIO_UNUSED,
    //             .invert_flags =
    //                 {
    //                     .mclk_inv = false,
    //                     .bclk_inv = false,
    //                     .ws_inv   = false,
    //                 },
    //         },
    // };

    // res = i2s_channel_init_std_mode(i2s_handle, &i2s_config);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Configuring I2S channel failed");
    //     return res;
    // }

    // res = i2s_channel_enable(i2s_handle);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Enabling I2S channel failed");
    //     return res;
    // }

    // GPIO for controlling power to the audio amplifier
    // gpio_config_t pin_amp_enable_cfg = {
    //     .pin_bit_mask = 1 << 1,
    //     .mode         = GPIO_MODE_OUTPUT,
    //     .pull_up_en   = false,
    //     .pull_down_en = false,
    //     .intr_type    = GPIO_INTR_DISABLE};
    // gpio_set_level(1, false);
    // gpio_config(&pin_amp_enable_cfg);

    // // SPI bus
    // spi_bus_config_t busConfiguration = {0};
    // busConfiguration.mosi_io_num      = 19;
    // busConfiguration.miso_io_num      = 20;
    // busConfiguration.sclk_io_num      = 21;
    // busConfiguration.quadwp_io_num    = -1;
    // busConfiguration.quadhd_io_num    = -1;
    // busConfiguration.max_transfer_sz  = SOC_SPI_MAXIMUM_BUFFER_SIZE;

    // res = spi_bus_initialize(SPI2_HOST, &busConfiguration, SPI_DMA_CH_AUTO);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing SPI bus failed");
    //     return res;
    // }

    // Epaper display
    res = hink_init(&epaper);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Initializing epaper display failed");
        return res;
    }

    // DRV2605 vibration motor driver
    // res = drv2605_init(&drv2605_device);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing DRV2605 failed");
    //     return res;
    // }

    // res = mma8452q_init(&mma8452q_device);
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing MMA8452Q failed");
    //     return res;
    // }

    // Graphics stack
    ESP_LOGI(TAG, "Creating graphics...");
    pax_buf_init(&gfx, NULL, 152, 152, PAX_BUF_2_PAL);
    gfx.palette      = palette;
    gfx.palette_size = sizeof(palette) / sizeof(pax_col_t);

    // SD card
    sdmmc_host_t          host        = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = 18;
    slot_config.host_id               = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024};

    res = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "Initializing SD card failed");
        card = NULL;
    }

    // SID emulator
    // if (!gpio_get_level(15)) {
    //     res = sid_init(i2s_handle);
    //     if (res != ESP_OK) {
    //         ESP_LOGE(TAG, "Initializing SID emulator failed");
    //         return res;
    //     }
    //     gpio_set_level(1, true);  // Enable amplifier
    // } else {
    //     res = ESP_OK;
    // }

    // res = initialize_adc();
    // if (res != ESP_OK) {
    //     ESP_LOGE(TAG, "Initializing ADC failed");
    //     return res;
    // }

    // return res;
}

void test_time() {
    time_t    now;
    char      strftime_buf[64];
    struct tm timeinfo;

    time(&now);
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
}

uint8_t lut_normal_20deg[] = {
    // 31,5 seconds
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x7a, 0x75, 0x0c, 0x00, 0x02, 0x03,
    0x01, 0x02, 0x0e, 0x12, 0x01, 0x12, 0x01, 0x04, 0x04, 0x0a, 0x06, 0x08, 0x02, 0x06, 0x04, 0x02, 0x2e, 0x04, 0x14,
    0x06, 0x02, 0x2a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_short[] = {
    // 6.5 seconds
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03,
    0x01, 0x02, 0x00, 0x12, 0x01, 0x12, 0x01, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x2e, 0x00, 0x14,
    0x06, 0x02, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_shorter_old[] = {
    // 6.3 seconds
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x12, 0x01, 0x12, 0x01, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x2e, 0x00, 0x14,
    0x06, 0x02, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_shorter_only_black[] = {
    // 2.8 seconds
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x09, 0x01, 0x09, 0x01, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x01, 0x00, 0x01,
    0x06, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_shorter_only_black_2[] = {
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x09, 0x01, 0x09, 0x01, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x01, 0x00, 0x01,
    0x06, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_fast[] = {
    // 2.2 seconds
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x01, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

uint8_t lut_experiment21[] = {
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

uint8_t lut_experiment[] = {
    0x82, 0x66, 0x96, 0x51, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x11, 0x66, 0x96, 0xa8, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x66, 0x96, 0x91, 0x2b, 0x2f, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x5a, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0a, 0x06, 0x08, 0x00, 0x06, 0x04, 0x02, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x3c, 0xc1, 0x2e, 0x50, 0x11, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

void app_main(void) {
    esp_err_t res = initialize_system();
    if (res != ESP_OK) {
        // Device init failed, stop.
        return;
    }

    if (card) {
        sdmmc_card_print_info(stdout, card);
    }

    // Clear screen
    pax_background(&gfx, 0);
    pax_draw_text(&gfx, 1, pax_font_marker, 18, 1, 0, "Tanoshi");
    pax_draw_text(&gfx, 1, pax_font_sky, 12, 1, 50, "1. Sleep");
    pax_draw_text(&gfx, 1, pax_font_sky, 12, 1, 65, "2. Quick");
    pax_draw_text(&gfx, 1, pax_font_sky, 12, 1, 80, "3. Slow");
    // hink_set_lut_ext(&epaper, lut_fast);
    hink_write(&epaper, gfx.buf, false);
};

// char *strings[] = {"Quick", "updates", "are", "nice"};

// uint32_t counter       = 0;
// uint32_t time          = 0;
// uint32_t sleep_counter = 0;
// while (1) {
//     uint8_t btn_a   = !gpio_get_level(9);
//     uint8_t btn_b   = !gpio_get_level(4);
//     uint8_t btn_c   = !gpio_get_level(15);
//     float   battery = get_battery_voltage();
//     if (btn_a) {
//         ESP_LOGI(TAG, "A");
//         pax_background(&gfx, 0);
//         char counter_string[64];
//         sprintf(counter_string, "vbatt: %.2fv", battery);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 1, 71, counter_string);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 1, 91, counter_string);
//         pax_set_pixel(&gfx, 1, 5, 5);
//         pax_set_pixel(&gfx, 2, 5, 10);

//         pax_draw_rect(&gfx, 1, 0, 0, 50, 20);
//         pax_draw_text(&gfx, 0, pax_font_sky, 18, 1, 1, "Test");
//         pax_draw_rect(&gfx, 2, 50, 0, 50, 20);
//         pax_draw_text(&gfx, 0, pax_font_sky, 18, 51, 1, "Test");

//         pax_draw_rect(&gfx, 0, 0, 20, 50, 20);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 1, 21, "Test");
//         pax_draw_rect(&gfx, 2, 50, 20, 50, 20);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 51, 21, "Test");

//         pax_draw_rect(&gfx, 0, 0, 40, 50, 20);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 1, 41, "Test");
//         pax_draw_rect(&gfx, 1, 50, 40, 50, 20);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 51, 41, "Test");
//         uint32_t a = esp_timer_get_time() / 1000;
//         hink_set_lut_ext(&epaper, lut_normal_20deg);
//         hink_write(&epaper, gfx.buf, false);
//         uint32_t b = esp_timer_get_time() / 1000;
//         time       = b - a;
//         counter++;
//         sleep_counter = 0;
//     } else if (btn_b) {
//         ESP_LOGI(TAG, "B");
//         pax_background(&gfx, 0);
//         char counter_string[64];
//         sprintf(counter_string, "vbatt: %.2fv", battery);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 1, 21, counter_string);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 1, 41, counter_string);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 1, 61, strings[counter % 4]);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 1, 81, strings[counter % 4]);
//         sprintf(counter_string, "%" PRIu32 "  %u %u %u", time, btn_a, btn_b, btn_c);
//         pax_draw_text(&gfx, 1, pax_font_sky, 18, 1, 101, counter_string);
//         pax_draw_text(&gfx, 2, pax_font_sky, 18, 1, 121, counter_string);
//         uint32_t a = esp_timer_get_time() / 1000;
//         hink_set_lut_ext(&epaper, lut_fast);
//         hink_write(&epaper, gfx.buf, false);
//         uint32_t b = esp_timer_get_time() / 1000;
//         time       = b - a;
//         counter++;
//         sleep_counter = 0;
// #if AUTOMATIC_SLEEP == 1
//     } else if (btn_c || sleep_counter > 1000) {
// #else
//     } else if (btn_c) {
// #endif
//         ESP_LOGI(TAG, "C");
//         esp_err_t res = esp_deep_sleep_enable_gpio_wakeup(1 << 4, ESP_GPIO_WAKEUP_GPIO_LOW);

//         if (res != ESP_OK) {
//             pax_background(&gfx, 0);
//             pax_draw_text(&gfx, 1, pax_font_sky, 18, 0, 0, "GPIO failed!");
//             hink_write(&epaper, gfx.buf, false);
//             continue;
//         }

//         pax_background(&gfx, 0);
//         /*pax_draw_text(&gfx, 1, pax_font_marker, 18, 0, 0, "Sleeping...");
//         pax_draw_text(&gfx, 1, pax_font_sky, 12, 0, 30, "Press button 2");
//         hink_set_lut_ext(&epaper, lut_short);*/

//         pax_insert_png_buf(&gfx, renze_png_start, renze_png_end - renze_png_start, 0, 0, 0);
//         ESP_LOGI(
//             "lolol",
//             "%ld %ld %ld",
//             pax_get_pixel_raw(&gfx, 0, 0),
//             pax_get_pixel_raw(&gfx, 1, 0),
//             pax_get_pixel_raw(&gfx, 2, 0)
//         );
//         ESP_LOGI(
//             "lolol",
//             "%ld %ld %ld",
//             pax_get_pixel(&gfx, 0, 0),
//             pax_get_pixel(&gfx, 1, 0),
//             pax_get_pixel(&gfx, 2, 0)
//         );
//         hink_set_lut_ext(&epaper, lut_normal_20deg);

//         hink_write(&epaper, gfx.buf, false);
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//         hink_sleep(&epaper);
//         drv2605_sleep(&drv2605_device);
//         gpio_set_level(1, false);
//         esp_deep_sleep_start();
//     } else {
//         ESP_LOGI(TAG, "Waiting for button... %" PRIu32 " (vbatt = %02.fv)", sleep_counter, battery);
//         sleep_counter++;
//     }
//     vTaskDelay(20 / portTICK_PERIOD_MS);
// }
// }
