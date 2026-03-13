#include "bsp_storage.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_MAINT";

static const int64_t HEALTH_LOG_INTERVAL_MS = 60LL * 1000LL;
static const int64_t RETENTION_CHECK_INTERVAL_MS = 60LL * 1000LL;
static const uint64_t STORAGE_FREE_FLOOR_BYTES = 100ULL * 1024ULL * 1024ULL;

static uint32_t uptime_seconds(void) {
  return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

static void log_health_snapshot(void) {
  uint32_t free_heap = esp_get_free_heap_size();
  uint32_t min_free_heap = esp_get_minimum_free_heap_size();
  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;

  if (bsp_storage_is_ready() &&
      bsp_storage_disk_usage(&total_bytes, &free_bytes) == ESP_OK) {
    ESP_LOGI(TAG,
             "Health uptime=%" PRIu32 "s free_heap=%" PRIu32
             "B min_free_heap=%" PRIu32 "B storage_free=%" PRIu64
             "B storage_total=%" PRIu64 "B",
             uptime_seconds(), free_heap, min_free_heap, free_bytes, total_bytes);

    if (bsp_storage_append_health_log(uptime_seconds(), free_heap, min_free_heap,
                                      free_bytes, total_bytes) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to append health log");
    }
    return;
  }

  ESP_LOGI(TAG,
           "Health uptime=%" PRIu32 "s free_heap=%" PRIu32
           "B min_free_heap=%" PRIu32 "B storage=unavailable",
           uptime_seconds(), free_heap, min_free_heap);
}

static void enforce_storage_floor(void) {
  if (!bsp_storage_is_ready()) {
    return;
  }

  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;
  if (bsp_storage_disk_usage(&total_bytes, &free_bytes) != ESP_OK) {
    ESP_LOGW(TAG, "Storage usage check failed");
    return;
  }

  while (free_bytes < STORAGE_FREE_FLOOR_BYTES) {
    char oldest_path[160] = {0};
    esp_err_t find_err = bsp_storage_find_oldest_file(oldest_path, sizeof(oldest_path));
    if (find_err == ESP_ERR_NOT_FOUND) {
      ESP_LOGW(TAG, "Storage below floor but no retention candidate was found");
      return;
    }
    if (find_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to find oldest file for retention");
      return;
    }

    ESP_LOGW(TAG, "Deleting oldest file to recover space: %s", oldest_path);
    if (bsp_storage_delete_file(oldest_path) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to delete %s", oldest_path);
      return;
    }

    if (bsp_storage_disk_usage(&total_bytes, &free_bytes) != ESP_OK) {
      ESP_LOGW(TAG, "Storage usage check failed after deletion");
      return;
    }
  }
}

void sys_maint_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

  int64_t last_health_log_ms = bsp_storage_now_ms() - HEALTH_LOG_INTERVAL_MS;
  int64_t last_retention_check_ms = bsp_storage_now_ms() - RETENTION_CHECK_INTERVAL_MS;

  while (1) {
    int64_t now_ms = bsp_storage_now_ms();

    if ((now_ms - last_health_log_ms) >= HEALTH_LOG_INTERVAL_MS) {
      last_health_log_ms = now_ms;
      log_health_snapshot();
    }

    if ((now_ms - last_retention_check_ms) >= RETENTION_CHECK_INTERVAL_MS) {
      last_retention_check_ms = now_ms;
      enforce_storage_floor();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
