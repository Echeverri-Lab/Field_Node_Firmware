#include "bsp_env.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "SYS_ENV";

void sys_env_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task Started");

  while (1) {
    /*
     * TODO: Periodic Sampling
     * 1. Wait for Timer Request or Schedule
     * 2. Read Sensors -> bsp_env_read(&t, &h);
     * 3. Log to CSV on SD Card ("timestamp, temp, humidity")
     * 4. Check for PIR wakeup source (why did we wake up?)
     */

    float t, h;
    // Mock read
    bsp_env_read(&t, &h);
    ESP_LOGD(TAG, "Environment: %.1fC, %.1f%%", t, h);

    vTaskDelay(pdMS_TO_TICKS(60000)); // Sample every minute
  }
}
