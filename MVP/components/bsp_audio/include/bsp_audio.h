#pragma once

#include "driver/i2s.h"
#include "esp_err.h"

// Pin Definitions (Matching breadboardtest.ino)
#define BSP_I2S_BCK_IO      (GPIO_NUM_42) // Customize per schematic
#define BSP_I2S_WS_IO       (GPIO_NUM_41)
#define BSP_I2S_DO_IO       (-1)          // Not used (Mic only)
#define BSP_I2S_DI_IO       (GPIO_NUM_2) // Customize per schematic

/**
 * @brief Initialize the I2S peripheral for the SPH0645 Microphone
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_audio_init(void);

/**
 * @brief Read audio samples from the I2S DMA buffer
 * 
 * @param[out] dest Buffer to write samples to
 * @param[in] len Length of buffer in bytes
 * @param[out] bytes_read Number of bytes actually read
 * @param[in] timeout_ms Wait timeout
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_audio_read(void *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms);
