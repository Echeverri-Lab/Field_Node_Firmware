#include "bsp_camera.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_CAMERA";
static bool s_camera_ready = false;
static const int s_capture_retries = 5;

static bool is_valid_jpeg(const camera_fb_t *fb) {
  if (!fb || !fb->buf || fb->len < 4) {
    return false;
  }
  return fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 &&
         fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9;
}

esp_err_t bsp_camera_init(void) {
  if (s_camera_ready) {
    return ESP_OK;
  }

  camera_config_t cfg = {
      .pin_pwdn = CAM_PIN_PWDN,
      .pin_reset = CAM_PIN_RESET,
      .pin_xclk = CAM_PIN_XCLK,
      .pin_sccb_sda = CAM_PIN_SIOD,
      .pin_sccb_scl = CAM_PIN_SIOC,
      .pin_d7 = CAM_PIN_D7,
      .pin_d6 = CAM_PIN_D6,
      .pin_d5 = CAM_PIN_D5,
      .pin_d4 = CAM_PIN_D4,
      .pin_d3 = CAM_PIN_D3,
      .pin_d2 = CAM_PIN_D2,
      .pin_d1 = CAM_PIN_D1,
      .pin_d0 = CAM_PIN_D0,
      .pin_vsync = CAM_PIN_VSYNC,
      .pin_href = CAM_PIN_HREF,
      .pin_pclk = CAM_PIN_PCLK,
      .xclk_freq_hz = 20000000,
      .ledc_timer = LEDC_TIMER_0,
      .ledc_channel = LEDC_CHANNEL_0,
      .pixel_format = PIXFORMAT_JPEG,
      .frame_size = FRAMESIZE_QVGA,
      .jpeg_quality = 12,
      .fb_count = 2,
      .fb_location = CAMERA_FB_IN_PSRAM,
      .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
  };

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Drain first stale frame after init.
  camera_fb_t *warmup = esp_camera_fb_get();
  if (warmup) {
    esp_camera_fb_return(warmup);
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    sensor->set_quality(sensor, 12);
  }

  s_camera_ready = true;
  ESP_LOGI(TAG, "Camera initialized");
  return ESP_OK;
}

camera_fb_t *bsp_camera_capture(void) {
  if (!s_camera_ready) {
    if (bsp_camera_init() != ESP_OK) {
      return NULL;
    }
  }

  camera_fb_t *fb = NULL;
  for (int i = 0; i < s_capture_retries; i++) {
    fb = esp_camera_fb_get();
    if (fb && is_valid_jpeg(fb)) {
      return fb;
    }
    if (fb) {
      ESP_LOGW(TAG, "Invalid JPEG frame discarded (len=%u)", (unsigned)fb->len);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }

  ESP_LOGW(TAG, "Capture failed, reinitializing camera");
  bsp_camera_deinit();
  if (bsp_camera_init() != ESP_OK) {
    return NULL;
  }
  for (int i = 0; i < s_capture_retries; i++) {
    fb = esp_camera_fb_get();
    if (fb && is_valid_jpeg(fb)) {
      return fb;
    }
    if (fb) {
      ESP_LOGW(TAG, "Invalid JPEG frame discarded after reinit (len=%u)", (unsigned)fb->len);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  return NULL;
}

esp_err_t bsp_camera_set_framesize(framesize_t frame_size) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (!sensor) {
    return ESP_ERR_INVALID_STATE;
  }
  return sensor->set_framesize(sensor, frame_size);
}

esp_err_t bsp_camera_deinit(void) {
  if (!s_camera_ready) {
    return ESP_OK;
  }
  esp_err_t err = esp_camera_deinit();
  if (err == ESP_OK) {
    s_camera_ready = false;
  }
  return err;
}
