#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#define BSP_AUDIO_BCLK_IO   (GPIO_NUM_9)  // D10
#define BSP_AUDIO_WS_IO     (GPIO_NUM_7)  // D8
#define BSP_AUDIO_DIN_IO    (GPIO_NUM_8)  // D9
#define BSP_AUDIO_RATE_HZ   (16000)
#define BSP_AUDIO_PCM_SHIFT (14)

esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_read(void *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms);
void bsp_audio_deinit(void);
