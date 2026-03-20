#include "bsp_camera.h"
#include "bsp_env.h"
#include "bsp_storage.h"
#include "system_app.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG = "SYS_VISION";

static bool send_image_over_usb_base64(const uint8_t *buf, size_t len) {
  size_t b64_cap = 4 * ((len + 2) / 3) + 1;
  unsigned char *b64 = malloc(b64_cap);
  if (!b64) {
    return false;
  }

  size_t out_len = 0;
  int rc = mbedtls_base64_encode(b64, b64_cap, &out_len, buf, len);
  if (rc != 0) {
    free(b64);
    return false;
  }

  printf("[USB_IMAGE_BEGIN] bytes=%u b64=%u\n", (unsigned)len, (unsigned)out_len);
  fwrite(b64, 1, out_len, stdout);
  printf("\n[USB_IMAGE_END]\n");
  fflush(stdout);

  free(b64);
  return true;
}

static bool capture_and_store(const char *subdir, const char *prefix, bool send_over_usb) {
  camera_fb_t *fb = bsp_camera_capture();
  if (!fb) {
    ESP_LOGW(TAG, "Camera capture failed");
    return false;
  }

  bool ok = false;
  if (bsp_storage_is_ready()) {
    char path[128] = {0};
    if (bsp_storage_make_path(path, sizeof(path), subdir, prefix, "jpg") == ESP_OK &&
        bsp_storage_write_blob(path, fb->buf, fb->len) == ESP_OK) {
      ESP_LOGI(TAG, "Saved %s (%u bytes)", path, (unsigned)fb->len);
      ok = true;
    }
  }

  if (send_over_usb) {
    if (send_image_over_usb_base64(fb->buf, fb->len)) {
      ESP_LOGI(TAG, "PIR image sent over USB serial");
      ok = true;
    } else {
      ESP_LOGW(TAG, "USB image transfer failed");
    }
  }

  esp_camera_fb_return(fb);
  return ok;
}

void sys_vision_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

  while (1) {
    if (!g_vision_req_queue) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    vision_msg_t msg = {0};
    if (xQueueReceive(g_vision_req_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (msg.type) {
      case VISION_CMD_CAPTURE_PHOTO:
        ESP_LOGI(TAG, "Capture request received");
        (void)capture_and_store("pir", "pir", msg.send_over_usb);
        break;
    }
  }
}
