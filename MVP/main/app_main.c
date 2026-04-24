#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/gpio.h"

#include "bsp_camera.h"
#include "bsp_env.h"
#include "bsp_gps.h"
#include "bsp_halow.h"
#include "bsp_storage.h"
#include "system_app.h"

static const char *TAG = "APP_MAIN";

QueueHandle_t g_main_event_queue = NULL;
QueueHandle_t g_vision_req_queue = NULL;
QueueHandle_t g_audio_req_queue = NULL;
QueueHandle_t g_env_req_queue = NULL;
QueueHandle_t g_power_req_queue = NULL;
QueueHandle_t g_comms_req_queue = NULL;

system_app_config_t g_system_config = {
    .restrict_active_hours = true,
    .active_start_hour = 6,
    .active_end_hour = 20,
    .env_interval_ms = 5U * 60U * 1000U,
    .audio_interval_ms = 30U * 60U * 1000U,
    .upload_interval_ms = 60U * 60U * 1000U,
    .sync_interval_ms = 6U * 60U * 60U * 1000U,
};

static bool queue_send_event(app_event_type_t type, uint32_t value, int64_t timestamp_ms) {
  if (!g_main_event_queue) {
    return false;
  }

  app_event_t event = {
      .type = type,
      .value = value,
      .timestamp_ms = timestamp_ms,
  };
  return xQueueSend(g_main_event_queue, &event, 0) == pdTRUE;
}

static void IRAM_ATTR pir_isr_handler(void *arg) {
  (void)arg;

  if (!g_main_event_queue) {
    return;
  }

  BaseType_t task_woken = pdFALSE;
  app_event_t event = {
      .type = APP_EVENT_PIR_INTERRUPT,
      .timestamp_ms = 0,
  };
  xQueueSendFromISR(g_main_event_queue, &event, &task_woken);
  if (task_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static void configure_pir_interrupt(void) {
  ESP_ERROR_CHECK(gpio_set_intr_type(BSP_PIR_IO, GPIO_INTR_POSEDGE));
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_ERROR_CHECK(gpio_isr_handler_add(BSP_PIR_IO, pir_isr_handler, NULL));
}

static bool is_within_active_hours(void) {
  if (!g_system_config.restrict_active_hours) {
    return true;
  }

  time_t now = time(NULL);
  if (now <= 0) {
    return true;
  }

  struct tm now_tm = {0};
  if (!localtime_r(&now, &now_tm)) {
    return true;
  }

  int hour = now_tm.tm_hour;
  int start = g_system_config.active_start_hour;
  int end = g_system_config.active_end_hour;
  if (start <= end) {
    return hour >= start && hour < end;
  }
  return hour >= start || hour < end;
}

static bool sync_rtc_from_gps(void) {
  bsp_gps_fix_t fix = {0};
  if (bsp_gps_get_latest_fix(2500, &fix) != ESP_OK || fix.raw[0] == '\0') {
    ESP_LOGW(TAG, "GPS sync skipped: no NMEA line available");
    return false;
  }

  char line[sizeof(fix.raw)] = {0};
  strncpy(line, fix.raw, sizeof(line) - 1);

  char *save = NULL;
  char *token = strtok_r(line, ",", &save);
  int idx = 0;
  const char *time_s = NULL;
  const char *status = NULL;
  const char *date_s = NULL;

  while (token) {
    if (idx == 1) time_s = token;
    if (idx == 2) status = token;
    if (idx == 9) date_s = token;
    token = strtok_r(NULL, ",", &save);
    idx++;
  }

  if (!time_s || !date_s || !status || status[0] != 'A' ||
      strlen(time_s) < 6 || strlen(date_s) < 6) {
    ESP_LOGW(TAG, "GPS sync skipped: invalid RMC frame");
    return false;
  }

  struct tm gps_tm = {0};
  gps_tm.tm_hour = (time_s[0] - '0') * 10 + (time_s[1] - '0');
  gps_tm.tm_min = (time_s[2] - '0') * 10 + (time_s[3] - '0');
  gps_tm.tm_sec = (time_s[4] - '0') * 10 + (time_s[5] - '0');
  gps_tm.tm_mday = (date_s[0] - '0') * 10 + (date_s[1] - '0');
  gps_tm.tm_mon = ((date_s[2] - '0') * 10 + (date_s[3] - '0')) - 1;
  gps_tm.tm_year = 100 + ((date_s[4] - '0') * 10 + (date_s[5] - '0'));
  gps_tm.tm_isdst = 0;

  time_t epoch = mktime(&gps_tm);
  if (epoch <= 0) {
    ESP_LOGW(TAG, "GPS sync skipped: failed to convert GPS time");
    return false;
  }

  struct timeval tv = {
      .tv_sec = epoch,
      .tv_usec = 0,
  };
  if (settimeofday(&tv, NULL) != 0) {
    ESP_LOGW(TAG, "GPS sync failed while setting RTC");
    return false;
  }

  ESP_LOGI(TAG, "RTC updated from GPS RMC frame");
  return true;
}

static void route_event(const app_event_t *event) {
  switch (event->type) {
    case APP_EVENT_PIR_INTERRUPT: {
      if (!is_within_active_hours()) {
        ESP_LOGI(TAG, "PIR ignored outside active hours");
        return;
      }

      vision_msg_t vision = {
          .type = VISION_CMD_CAPTURE_PHOTO,
          .send_over_usb = true,
      };
      power_msg_t power = {.type = POWER_CMD_SET_CAPTURE};
      (void)xQueueSend(g_vision_req_queue, &vision, pdMS_TO_TICKS(50));
      (void)xQueueSend(g_power_req_queue, &power, 0);
      break;
    }

    case APP_EVENT_SCHEDULE_ENV: {
      env_msg_t env = {.type = ENV_CMD_SAMPLE_ENV};
      (void)xQueueSend(g_env_req_queue, &env, pdMS_TO_TICKS(50));
      break;
    }

    case APP_EVENT_SCHEDULE_AUDIO: {
      if (g_system_config.audio_interval_ms == 0) {
        return;
      }
      audio_msg_t audio = {.type = AUDIO_CMD_START_RECORDING};
      power_msg_t power = {.type = POWER_CMD_SET_CAPTURE};
      (void)xQueueSend(g_audio_req_queue, &audio, pdMS_TO_TICKS(50));
      (void)xQueueSend(g_power_req_queue, &power, 0);
      break;
    }

    case APP_EVENT_UPLOAD_TIMER: {
      comms_msg_t comms = {.type = COMMS_CMD_START_UPLOAD};
      if (g_comms_req_queue) {
        (void)xQueueOverwrite(g_comms_req_queue, &comms);
      }
      ESP_LOGI(TAG, "Upload requested");
      break;
    }

    case APP_EVENT_SYNC_TIMER:
      (void)sync_rtc_from_gps();
      break;

    case APP_EVENT_BATTERY_LOW: {
      power_msg_t power = {.type = POWER_CMD_ENTER_DEEP_SLEEP};
      (void)xQueueSend(g_power_req_queue, &power, pdMS_TO_TICKS(50));
      break;
    }

    case APP_EVENT_BUTTON_WAKE: {
      power_msg_t power = {.type = POWER_CMD_SET_IDLE};
      (void)xQueueSend(g_power_req_queue, &power, 0);
      break;
    }
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Field Node MVP starting");

  (void)bsp_storage_init();
  (void)bsp_env_init();
  (void)bsp_gps_init();
  g_main_event_queue = xQueueCreate(16, sizeof(app_event_t));
  g_vision_req_queue = xQueueCreate(4, sizeof(vision_msg_t));
  g_audio_req_queue = xQueueCreate(4, sizeof(audio_msg_t));
  g_env_req_queue = xQueueCreate(4, sizeof(env_msg_t));
  g_power_req_queue = xQueueCreate(4, sizeof(power_msg_t));
  g_comms_req_queue = xQueueCreate(1, sizeof(comms_msg_t));

  if (!g_main_event_queue || !g_vision_req_queue || !g_audio_req_queue ||
      !g_env_req_queue || !g_power_req_queue || !g_comms_req_queue) {
    ESP_LOGE(TAG, "Failed to create system queues");
    return;
  }

  configure_pir_interrupt();

  xTaskCreatePinnedToCore(sys_vision_task, "VisionTask", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(sys_audio_task, "AudioTask", 8192, NULL, 6, NULL, 0);
  xTaskCreatePinnedToCore(sys_env_task, "EnvTask", 4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(sys_power_task, "PowerTask", 4096, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(sys_comms_task, "CommsTask", 8192, NULL, 3, NULL, 1);

  ESP_LOGI(TAG, "All tasks started");

  int64_t now_ms = esp_timer_get_time() / 1000;
  int64_t last_env_ms = now_ms - g_system_config.env_interval_ms + 5000;   /* first at ~5 s */
  int64_t last_audio_ms = now_ms - g_system_config.audio_interval_ms + 10000; /* first at ~10 s */
  int64_t last_upload_ms = now_ms;  /* first upload waits a full interval */
  int64_t last_sync_ms = now_ms - g_system_config.sync_interval_ms;

  while (1) {
    now_ms = esp_timer_get_time() / 1000;

    if ((now_ms - last_env_ms) >= g_system_config.env_interval_ms) {
      last_env_ms = now_ms;
      (void)queue_send_event(APP_EVENT_SCHEDULE_ENV, 0, now_ms);
    }

    if (g_system_config.audio_interval_ms > 0 &&
        (now_ms - last_audio_ms) >= g_system_config.audio_interval_ms) {
      last_audio_ms = now_ms;
      (void)queue_send_event(APP_EVENT_SCHEDULE_AUDIO, 0, now_ms);
    }

    if ((now_ms - last_upload_ms) >= g_system_config.upload_interval_ms) {
      last_upload_ms = now_ms;
      (void)queue_send_event(APP_EVENT_UPLOAD_TIMER, 0, now_ms);
    }

    if ((now_ms - last_sync_ms) >= g_system_config.sync_interval_ms) {
      last_sync_ms = now_ms;
      (void)queue_send_event(APP_EVENT_SYNC_TIMER, 0, now_ms);
    }

    app_event_t event = {0};
    if (xQueueReceive(g_main_event_queue, &event, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (event.timestamp_ms == 0) {
        event.timestamp_ms = esp_timer_get_time() / 1000;
      }
      route_event(&event);
    }
  }
}
