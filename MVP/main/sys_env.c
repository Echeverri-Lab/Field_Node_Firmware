#include "bsp_env.h"
#include "bsp_gps.h"
#include "bsp_storage.h"
#include "system_app.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "SYS_ENV";

void sys_env_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started");

  while (1) {
    if (!g_env_req_queue) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    env_msg_t msg = {0};
    if (xQueueReceive(g_env_req_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (msg.type != ENV_CMD_SAMPLE_ENV) {
      continue;
    }

    float temp_c = NAN;
    float humidity = NAN;
    bsp_gps_fix_t fix = {0};

    esp_err_t env_err = bsp_env_read(&temp_c, &humidity);
    (void)bsp_gps_get_latest_fix(500, &fix);

    if (env_err == ESP_OK) {
      ESP_LOGI(TAG, "Env %.2fC %.2f%%", temp_c, humidity);
    } else {
      ESP_LOGW(TAG, "Env read failed: %s", esp_err_to_name(env_err));
    }

    if (fix.valid) {
      ESP_LOGI(TAG, "GPS %.6f, %.6f", fix.latitude, fix.longitude);
    }

    if (bsp_storage_is_ready()) {
      if (bsp_storage_append_env_log(fix.latitude, fix.longitude, temp_c, humidity, fix.valid) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to append env log");
      }
    }
  }
}
