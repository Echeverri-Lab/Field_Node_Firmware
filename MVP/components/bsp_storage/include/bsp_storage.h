#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t bsp_storage_init(void);
esp_err_t bsp_storage_remount(void);
bool bsp_storage_is_ready(void);
int64_t bsp_storage_now_ms(void);

esp_err_t bsp_storage_make_path(char *out, size_t out_len,
                                const char *subdir, const char *prefix,
                                const char *extension);

esp_err_t bsp_storage_write_blob(const char *path, const void *data, size_t len);
esp_err_t bsp_storage_append_env_log(float latitude, float longitude,
                                     float temperature_c, float humidity_pct,
                                     bool has_fix);

/**
 * Callback invoked once per pending (not-yet-uploaded) file.
 * Return true to keep iterating, false to stop early.
 */
typedef bool (*bsp_storage_file_cb_t)(const char *path, void *ctx);

/**
 * Scan the SD card data directories and invoke @p cb for every file
 * that has not been marked as uploaded (i.e. no ".uploaded" suffix).
 */
esp_err_t bsp_storage_list_pending(bsp_storage_file_cb_t cb, void *ctx);

/**
 * Mark a file as successfully uploaded by renaming it with a
 * ".uploaded" suffix so it is skipped on future scans.
 */
esp_err_t bsp_storage_mark_uploaded(const char *path);
