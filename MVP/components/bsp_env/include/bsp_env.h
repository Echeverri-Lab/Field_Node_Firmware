#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define BSP_I2C_SDA_IO   (GPIO_NUM_5)   // D4
#define BSP_I2C_SCL_IO   (GPIO_NUM_6)   // D5
#define BSP_I2C_PORT_NUM (I2C_NUM_0)
#define BSP_SHTC3_ADDR   (0x70)
#define BSP_PIR_IO       (GPIO_NUM_1)   // D0

esp_err_t bsp_env_init(void);
esp_err_t bsp_env_read(float *temp, float *hum);
bool bsp_pir_check(void);
