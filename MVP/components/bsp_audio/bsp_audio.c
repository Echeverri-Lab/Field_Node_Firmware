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
    
    return ESP_OK;
}

esp_err_t bsp_audio_read(void *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms) {
    /*
     * TODO: Wrapper for i2s_read()
     * - Handle bit-shifting if raw data is 32-bit but mic is 24-bit (SPH0645 style)
     */
    return ESP_OK;
}
