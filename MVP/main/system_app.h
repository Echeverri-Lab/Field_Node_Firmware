#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
  APP_EVENT_PIR_INTERRUPT = 0,
  APP_EVENT_SCHEDULE_ENV,
  APP_EVENT_SCHEDULE_AUDIO,
  APP_EVENT_UPLOAD_TIMER,
  APP_EVENT_SYNC_TIMER,
  APP_EVENT_BATTERY_LOW,
  APP_EVENT_BUTTON_WAKE,
} app_event_type_t;

typedef struct {
  app_event_type_t type;
  uint32_t value;
  int64_t timestamp_ms;
} app_event_t;

typedef enum {
  VISION_CMD_CAPTURE_PHOTO = 0,
} vision_cmd_type_t;

typedef struct {
  vision_cmd_type_t type;
  bool send_over_usb;
} vision_msg_t;

typedef enum {
  AUDIO_CMD_START_RECORDING = 0,
  AUDIO_CMD_STREAM_TEST,
} audio_cmd_type_t;

typedef struct {
  audio_cmd_type_t type;
} audio_msg_t;

typedef enum {
  ENV_CMD_SAMPLE_ENV = 0,
} env_cmd_type_t;

typedef struct {
  env_cmd_type_t type;
} env_msg_t;

typedef enum {
  POWER_CMD_SET_IDLE = 0,
  POWER_CMD_SET_CAPTURE,
  POWER_CMD_ENTER_DEEP_SLEEP,
} power_cmd_type_t;

typedef struct {
  power_cmd_type_t type;
} power_msg_t;

typedef enum {
  POWER_STATE_IDLE = 0,
  POWER_STATE_CAPTURE,
  POWER_STATE_SLEEP,
} power_state_t;

typedef enum {
  COMMS_CMD_START_UPLOAD = 0,
} comms_cmd_type_t;

typedef struct {
  comms_cmd_type_t type;
} comms_msg_t;

typedef struct {
  bool restrict_active_hours;
  uint8_t active_start_hour;
  uint8_t active_end_hour;
  uint32_t env_interval_ms;
  uint32_t audio_interval_ms;
  uint32_t upload_interval_ms;
  uint32_t sync_interval_ms;
} system_app_config_t;

extern QueueHandle_t g_main_event_queue;
extern QueueHandle_t g_vision_req_queue;
extern QueueHandle_t g_audio_req_queue;
extern QueueHandle_t g_env_req_queue;
extern QueueHandle_t g_power_req_queue;
extern QueueHandle_t g_comms_req_queue;

extern system_app_config_t g_system_config;

void sys_vision_task(void *pvParameters);
void sys_audio_task(void *pvParameters);
void sys_env_task(void *pvParameters);
void sys_power_task(void *pvParameters);
void sys_audio_enable_streaming(void);

