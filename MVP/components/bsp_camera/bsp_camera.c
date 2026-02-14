#include "bsp_camera.h"
#include "esp_log.h"

static const char *TAG = "BSP_CAMERA";

esp_err_t bsp_camera_init(void) {
  ESP_LOGI(TAG, "Initializing OV2640...");

  /*
   * TODO: Configure camera_config_t
   * - Set pins based on header defines
   * - Set XCLK frequency (20MHz typical)
   * - Set Pixel Format (PIXFORMAT_JPEG)
   * - Set Frame Size (FRAMESIZE_UXGA for photos, QVGA for streaming)
   * - Call esp_camera_init(&config)
   */

  return ESP_OK;
}

camera_fb_t *bsp_camera_capture(void) {
  /*
   * TODO: Wrapper for esp_camera_fb_get()
   * - Check for valid framebuffer
   * - Log errors if capture fails
   */
  return NULL;
}
