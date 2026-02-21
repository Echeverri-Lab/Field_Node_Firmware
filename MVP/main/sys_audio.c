#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "SYS_AUDIO";

void sys_audio_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task Started on Core %d (Real-time)", xPortGetCoreID());

  // Buffer Allocation
  // TODO: Allocate large RingBuffer in PSRAM
  // uint8_t *audio_buffer = heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM);

  while (1) {
    /*
     * TODO: Continuous Recording Loop
     * 1. Read chunk from I2S -> bsp_audio_read()
     * 2. Push chunk to RingBuffer
     * 3. Analyze chunk for Event Trigger (Amplitude > Threshold)
     *    - If Event:
     *      a. Signal "Event Detected" to Orchestrator
     *      b. Dump Pre-trigger + Post-trigger buffer to SD Card (.wav)
     */

    // Blocking read to keep the loop paced
    // size_t bytes_read = 0;
    // bsp_audio_read(temp_buff, chunk_size, &bytes_read, portMAX_DELAY);

    vTaskDelay(
        pdMS_TO_TICKS(100)); // Remove this when blocking read is implemented
  }
}
