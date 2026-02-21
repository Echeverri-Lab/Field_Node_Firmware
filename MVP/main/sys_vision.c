#include "bsp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// TODO: Include SD Card / Storage headers once created

static const char *TAG = "SYS_VISION";

void sys_vision_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task Started on Core %d", xPortGetCoreID());

  // Main Task Loop
  while (1) {
    /*
     * TODO: Wait for Command from Queue
     * e.g., xQueueReceive(vision_req_queue, &msg, portMAX_DELAY);
     *
     * Logic:
     * 1. If msg == CAPTURE_PHOTO:
     *    a. Turn on IR LEDs (if night) -> bsp_camera_ir_on()
     *    b. Wait for exposure
     *    c. Capture Frame -> fb = bsp_camera_capture()
     *    d. Turn off IR LEDs
     *    e. Save fb->buf to SD Card (timestamp.jpg)
     *    f. Return Framebuffer -> esp_camera_fb_return(fb)
     */

    // For MVP demo, just sleep
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
