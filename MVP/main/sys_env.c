#include "bsp_env.h"
#include "bsp_gps.h"
#include "bsp_storage.h"

#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_ENV";
static const int64_t ENV_INTERVAL_MS = 5LL * 60LL * 1000LL;

void sys_env_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started");

  int64_t last_sample_ms = esp_timer_get_time() / 1000 - ENV_INTERVAL_MS;

  while (1) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    if ((now_ms - last_sample_ms) >= ENV_INTERVAL_MS) {
      last_sample_ms = now_ms;

      float temp_c = NAN;
      float humidity = NAN;
      bsp_gps_fix_t fix = {0};

      esp_err_t env_err = bsp_env_read(&temp_c, &humidity);
      (void)bsp_gps_get_latest_fix(1500, &fix);

      if (env_err == ESP_OK) {
        ESP_LOGI(TAG, "Env %.2fC %.2f%%", temp_c, humidity);
      } else {
        ESP_LOGW(TAG, "Env read failed: %s", esp_err_to_name(env_err));
      }

      if (fix.valid) {
        ESP_LOGI(TAG, "GPS %.6f, %.6f", fix.latitude, fix.longitude);
      } else if (fix.raw[0] != '\0') {
        ESP_LOGW(TAG, "GPS no fix. Raw: %s", fix.raw);
      }

      if (bsp_storage_is_ready()) {
        if (bsp_storage_append_env_log(fix.latitude, fix.longitude, temp_c, humidity, fix.valid) != ESP_OK) {
          ESP_LOGW(TAG, "Failed to append env log");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
