#include "driver/adc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "SYS_POWER";

void sys_power_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task Started (Critical Priority)");

  while (1) {
    /*
     * TODO: Power Management State Machine
     * 1. Check Battery Voltage (ADC)
     *    - If V < 3.3V: CRITICAL_LOW -> Flush logs -> Deep Sleep immediately
     * 2. Manage Idle Time
     *    - If System Idle for > X seconds:
     *      a. Notify Orchestrator "Requesting Sleep"
     *      b. Wait for "OK" (Tasks stopped, files closed)
     *      c. Configure Wakeup Sources (PIR, Timer)
     *      d. esp_deep_sleep_start()
     */

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
