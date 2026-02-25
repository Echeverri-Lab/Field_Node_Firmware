#pragma once

#include "esp_camera.h"
#include "esp_err.h"

// XIAO ESP32S3 Sense (OV2640)
#define CAM_PIN_PWDN  (-1)
#define CAM_PIN_RESET (-1)
#define CAM_PIN_XCLK  (10)
#define CAM_PIN_SIOD  (40)
#define CAM_PIN_SIOC  (39)
#define CAM_PIN_D7    (48)
#define CAM_PIN_D6    (11)
#define CAM_PIN_D5    (12)
#define CAM_PIN_D4    (14)
#define CAM_PIN_D3    (16)
#define CAM_PIN_D2    (18)
#define CAM_PIN_D1    (17)
#define CAM_PIN_D0    (15)
#define CAM_PIN_VSYNC (38)
#define CAM_PIN_HREF  (47)
#define CAM_PIN_PCLK  (13)

esp_err_t bsp_camera_init(void);
camera_fb_t *bsp_camera_capture(void);
esp_err_t bsp_camera_set_framesize(framesize_t frame_size);
esp_err_t bsp_camera_deinit(void);
