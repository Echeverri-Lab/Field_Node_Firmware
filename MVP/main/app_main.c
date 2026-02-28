#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_audio.h"
#include "bsp_camera.h"
#include "bsp_env.h"
#include "bsp_gps.h"
#include "bsp_storage.h"

void sys_vision_task(void *pvParameters);
void sys_audio_task(void *pvParameters);
void sys_env_task(void *pvParameters);
void sys_power_task(void *pvParameters);

static const char *TAG = "APP_MAIN";

void app_main(void) {
  ESP_LOGI(TAG, "Field Node MVP starting");

  (void)bsp_storage_init();
  (void)bsp_env_init();
  (void)bsp_gps_init();
  (void)bsp_camera_init();
  // Audio init is intentionally deferred to sys_audio task.

  xTaskCreatePinnedToCore(sys_vision_task, "VisionTask", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(sys_audio_task, "AudioTask", 8192, NULL, 6, NULL, 0);
  xTaskCreatePinnedToCore(sys_env_task, "EnvTask", 4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(sys_power_task, "PowerTask", 3072, NULL, 10, NULL, 1);

  ESP_LOGI(TAG, "All tasks started");
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
