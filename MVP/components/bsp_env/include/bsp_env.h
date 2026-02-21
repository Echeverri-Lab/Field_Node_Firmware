#pragma once

#include "driver/i2c.h"
#include "esp_err.h"

// I2C Pin Definitions (Matching breadboardtest.ino)
#define BSP_I2C_SDA_IO                                                         \
  (GPIO_NUM_42) // Example, adjust based on breadboardtest.ino
#define BSP_I2C_SCL_IO (GPIO_NUM_41)
#define BSP_I2C_PORT_NUM I2C_NUM_0

// PIR Sensor Pin
#define BSP_PIR_IO (GPIO_NUM_1) // Example

/**
 * @brief Initialize I2C bus and sensors (AHT20)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_env_init(void);

/**
 * @brief Read Temperature and Humidity
 *
 * @param[out] temp Pointer to float for temperature (C)
 * @param[out] hum Pointer to float for humidity (%)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_env_read(float *temp, float *hum);

/**
 * @brief Check PIR status
 *
 * @return true if motion detected
 * @return false otherwise
 */
bool bsp_pir_check(void);
