#include "bsp_audio.h"
#include "esp_log.h"

static const char *TAG = "BSP_AUDIO";

esp_err_t bsp_audio_init(void) {
    ESP_LOGI(TAG, "Initializing I2S for SPH0645...");

    /* 
     * TODO: Implement I2S Driver Installation
     * 1. Define i2s_config_t with:
     *    - mode: I2S_MODE_MASTER | I2S_MODE_RX
     *    - sample_rate: 16000
     *    - format: I2S_CHANNEL_FMT_ONLY_LEFT
     * 2. Define i2s_pin_config_t with pins from header
     * 3. Call i2s_driver_install() and i2s_set_pin()
     */
    // I2S config for SPH0645 microphone
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0

    };

    // I2S pin config for SPH0645 microphone
    const i2s_pin_config_t pin_config = {
        .bck_io_num = BSP_I2S_BCK_IO,
        .ws_io_num = BSP_I2S_WS_IO,
        .data_out_num = BSP_I2S_DO_IO,
        .data_in_num = BSP_I2S_DI_IO
    };

    //install i2s driver
    esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install i2s driver: %s", esp_err_to_name(ret))
    }

    //configure i2s pins
    ret = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins: %s", esp_err_to_name(ret));
        i2s_driver_uninstall(I2S_NUM_0);
        return ret;
    }

    ESP_LOGI(TAG, "I2S driver installed and pins configured successfully");
    return ESP_OK;
}

esp_err_t bsp_audio_read(void *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms) {
    /*
     * TODO: Wrapper for i2s_read()
     * - Handle bit-shifting if raw data is 32-bit but mic is 24-bit (SPH0645 style)
     */
    return ESP_OK;
}
