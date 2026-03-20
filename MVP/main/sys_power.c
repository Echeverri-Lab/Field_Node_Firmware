#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_audio.h"
#include "bsp_camera.h"
#include "bsp_env.h"
#include "system_app.h"

static const char *TAG = "SYS_POWER";

#define SYS_POWER_BATTERY_LOW_V        3.3f
#define SYS_POWER_BATTERY_DIVIDER      2.0f
#define SYS_POWER_BATTERY_CHECK_MS     30000LL
#define SYS_POWER_CAPTURE_HOLD_MS      10000LL
#define SYS_POWER_SLEEP_WAKE_TIMER_US  (30ULL * 60ULL * 1000000ULL)
#define SYS_POWER_BUTTON_WAKE_GPIO     GPIO_NUM_0
#define SYS_POWER_BATTERY_ADC_UNIT     ADC_UNIT_1
#define SYS_POWER_BATTERY_ADC_CHANNEL  ADC_CHANNEL_8
#define SYS_POWER_BATTERY_ADC_ATTEN    ADC_ATTEN_DB_12

static power_state_t s_state = POWER_STATE_IDLE;
static int64_t s_last_capture_ms = 0;
static int64_t s_last_battery_check_ms = 0;
static bool s_low_battery_posted = false;
static adc_oneshot_unit_handle_t s_battery_adc = NULL;

static void configure_wakeup_sources(void) {
  (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  (void)esp_sleep_enable_ext0_wakeup(BSP_PIR_IO, 1);
  (void)esp_sleep_enable_timer_wakeup(SYS_POWER_SLEEP_WAKE_TIMER_US);
  (void)esp_sleep_enable_ext1_wakeup(1ULL << SYS_POWER_BUTTON_WAKE_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH);
}

static esp_err_t battery_adc_init(void) {
  if (s_battery_adc) {
    return ESP_OK;
  }

  adc_oneshot_unit_init_cfg_t unit_cfg = {
      .unit_id = SYS_POWER_BATTERY_ADC_UNIT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_battery_adc);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Battery ADC unit init failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = SYS_POWER_BATTERY_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_oneshot_config_channel(s_battery_adc, SYS_POWER_BATTERY_ADC_CHANNEL, &chan_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Battery ADC channel init failed: %s", esp_err_to_name(err));
  }
  return err;
}

static float read_battery_voltage(void) {
  if (battery_adc_init() != ESP_OK) {
    return -1.0f;
  }

  int raw = 0;
  esp_err_t err = adc_oneshot_read(s_battery_adc, SYS_POWER_BATTERY_ADC_CHANNEL, &raw);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Battery ADC read failed: %s", esp_err_to_name(err));
    return -1.0f;
  }

  float sense_voltage = ((float)raw / 4095.0f) * 3.3f;
  return sense_voltage * SYS_POWER_BATTERY_DIVIDER;
}

static void post_battery_low_event(void) {
  if (s_low_battery_posted || !g_main_event_queue) {
    return;
  }

  app_event_t event = {
      .type = APP_EVENT_BATTERY_LOW,
      .timestamp_ms = esp_timer_get_time() / 1000,
  };
  if (xQueueSend(g_main_event_queue, &event, 0) == pdTRUE) {
    s_low_battery_posted = true;
  }
}

static void check_battery(void) {
  int64_t now_ms = esp_timer_get_time() / 1000;
  if ((now_ms - s_last_battery_check_ms) < SYS_POWER_BATTERY_CHECK_MS) {
    return;
  }
  s_last_battery_check_ms = now_ms;

  float battery_v = read_battery_voltage();
  if (battery_v < 0.0f) {
    return;
  }

  ESP_LOGI(TAG, "Battery voltage %.2fV", battery_v);
  if (battery_v < SYS_POWER_BATTERY_LOW_V) {
    ESP_LOGW(TAG, "Battery below threshold, requesting deep sleep");
    post_battery_low_event();
  } else if (battery_v > (SYS_POWER_BATTERY_LOW_V + 0.1f)) {
    s_low_battery_posted = false;
  }
}

static void handle_power_command(const power_msg_t *msg) {
  switch (msg->type) {
    case POWER_CMD_SET_IDLE:
      s_state = POWER_STATE_IDLE;
      ESP_LOGI(TAG, "State -> IDLE");
      break;

    case POWER_CMD_SET_CAPTURE:
      s_state = POWER_STATE_CAPTURE;
      s_last_capture_ms = esp_timer_get_time() / 1000;
      ESP_LOGI(TAG, "State -> CAPTURE");
      break;

    case POWER_CMD_ENTER_DEEP_SLEEP:
      s_state = POWER_STATE_SLEEP;
      ESP_LOGW(TAG, "State -> SLEEP");
      configure_wakeup_sources();
      bsp_audio_deinit();
      (void)bsp_camera_deinit();
      esp_deep_sleep_start();
      break;
  }
}

void sys_power_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

  configure_wakeup_sources();
  (void)battery_adc_init();

  while (1) {
    power_msg_t msg = {0};
    if (g_power_req_queue &&
        xQueueReceive(g_power_req_queue, &msg, pdMS_TO_TICKS(250)) == pdTRUE) {
      handle_power_command(&msg);
    }

    check_battery();

    if (s_state == POWER_STATE_CAPTURE) {
      int64_t now_ms = esp_timer_get_time() / 1000;
      if ((now_ms - s_last_capture_ms) >= SYS_POWER_CAPTURE_HOLD_MS) {
        s_state = POWER_STATE_IDLE;
        ESP_LOGI(TAG, "Capture window complete, returning to IDLE");
      }
    }
  }
}
