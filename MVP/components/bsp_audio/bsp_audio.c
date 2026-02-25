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
        .communication_format = I2S_COMM_FORMAT_STAND_MBS, //changed from I2S_COMM_FORMAT_STAND_I2S
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
        ESP_LOGE(TAG, "Failed to install i2s driver: %s", esp_err_to_name(ret));
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
    //check input params
    if (dest == NULL || bytes_read == NULL) {
        ESP_LOGE(TAG, "Invalid parameters: dest or bytes_read is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    //convert timeout to tick
    TickType_t ticks_to_wait = timeout_ms / portTICK_PERIOD_MS;

    //call read func
    //reads 32bit samples from DMA buffer into dest
    esp_err_t ret = i2s_read(I2S_NUM_0, dest, len, bytes_read, ticks_to_wait);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // SPH0645 outputs 24-bit audio data in 32-bit frames (left-aligned)
    // effective resolution is 18 bits, so shift right by 14 bits
    // to normalize data to a usable range
    
    // calc num samples (each sample is 4 bytes = 32 bits)
    size_t num_samples = (*bytes_read) / sizeof(int32_t);
    int32_t *samples = (int32_t *)dest;
    
    for (size_t i = 0; i < num_samples; i++) {
        // shift right by 14 bits to normalize SPH0645's 18-bit effective resolution
        samples[i] >>= 14;
    }
    return ESP_OK;
}

/**
 * @brief Blocking call to fetch raw PCM data from DMA.
 * * @param buffer Pointer to the destination buffer
 * @param buffer_size Size of the buffer in bytes
 * @param timeout_ms Total timeout for the entire read operation
 * @return esp_err_t ESP_OK on success, or error code
 */
esp_err_t read_i2s_buffer(int32_t *buffer, size_t buffer_size, uint32_t timeout_ms) {
    size_t total_bytes_read = 0;
    size_t bytes_to_read = buffer_size;
    uint32_t start_tick = xTaskGetTickCount();
    
    while (total_bytes_read < buffer_size) {
        size_t current_read_len = 0;
        
        // Calculate remaining timeout to prevent infinite blocking
        uint32_t elapsed = (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
        if (elapsed >= timeout_ms) {
            ESP_LOGW(TAG, "Read timeout reached after %u ms", elapsed);
            return ESP_ERR_TIMEOUT;
        }

        // Call your existing read function
        esp_err_t ret = bsp_audio_read(
            (uint8_t *)buffer + total_bytes_read, 
            bytes_to_read - total_bytes_read, 
            &current_read_len, 
            timeout_ms - elapsed
        );

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error during buffer fill: %s", esp_err_to_name(ret));
            return ret;
        }

        total_bytes_read += current_read_len;

        // If no data was read, yield to prevent a tight loop
        if (current_read_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return ESP_OK;
}
