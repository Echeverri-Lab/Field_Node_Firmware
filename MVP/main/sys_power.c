#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_POWER";

void sys_power_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started (placeholder)");

  while (1) {
    // TODO: Implement battery ADC read and deep-sleep transition.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
