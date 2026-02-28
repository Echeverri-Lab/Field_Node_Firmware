#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  bool valid;
  float latitude;
  float longitude;
  char raw[128];
} bsp_gps_fix_t;

esp_err_t bsp_gps_init(void);
esp_err_t bsp_gps_get_latest_fix(uint32_t timeout_ms, bsp_gps_fix_t *fix);
