#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// Include our BSP headers
#include "bsp_audio.h"
#include "bsp_camera.h"
#include "bsp_env.h"

// Forward declarations of task functions (implemented in separate files or
// below) In a real project, these would be in their own headers (sys_vision.h,
// etc.)
void sys_vision_task(void *pvParameters);
void sys_audio_task(void *pvParameters);
void sys_env_task(void *pvParameters);
void sys_power_task(void *pvParameters);

static const char *TAG = "MAIN";

void app_main(void) {
  ESP_LOGI(TAG, "BioMesh Field Node - MVP Firmware Starting...");

  // 1. Initialize Hardware (BSP Layer)
  // We init hardware *before* starting tasks to ensure drivers are ready.
  bsp_camera_init();
  bsp_audio_init();
  bsp_env_init();

  // 2. Create Synchronization Primitives
  // TODO: Create Queues (xQueueCreate) for inter-task communication
  // e.g., QueueHandle_t vision_req_queue = xQueueCreate(5,
  // sizeof(vision_msg_t));

  // 3. Start Subsystem Tasks
  // Tasks are pinned to cores for determinism.
  // Core 1 (App Core): Vision, Comms (High Level)
  // Core 0 (Pro Core): Wi-Fi, Audio (Real-time)

  xTaskCreatePinnedToCore(sys_vision_task, "VisionTask", 4096, NULL, 5, NULL,
                          1);
  xTaskCreatePinnedToCore(sys_audio_task, "AudioTask", 4096, NULL, 6, NULL,
                          0); // High priority
  xTaskCreatePinnedToCore(sys_env_task, "EnvTask", 2048, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(sys_power_task, "PowerTask", 2048, NULL, 10, NULL,
                          1); // Highest priority

  ESP_LOGI(TAG, "All tasks started. Orchestrator entering idle loop.");

  // 4. Main Loop (Orchestrator Logic)
  while (1) {
    // In a real implementation, this loop might handle system status reporting
    // or process events from a central event queue.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
