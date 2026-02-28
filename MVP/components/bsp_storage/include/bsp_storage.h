#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t bsp_storage_init(void);
bool bsp_storage_is_ready(void);
int64_t bsp_storage_now_ms(void);

esp_err_t bsp_storage_make_path(char *out, size_t out_len,
                                const char *subdir, const char *prefix,
                                const char *extension);

esp_err_t bsp_storage_write_blob(const char *path, const void *data, size_t len);
esp_err_t bsp_storage_append_env_log(float latitude, float longitude,
                                     float temperature_c, float humidity_pct,
                                     bool has_fix);
