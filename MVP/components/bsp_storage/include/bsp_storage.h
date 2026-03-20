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
esp_err_t bsp_storage_disk_usage(uint64_t *total_bytes, uint64_t *free_bytes);
esp_err_t bsp_storage_find_oldest_file(char *out_path, size_t out_len);
esp_err_t bsp_storage_delete_file(const char *path);
esp_err_t bsp_storage_mark_uploaded(const char *path, char *out_path, size_t out_path_len);
esp_err_t bsp_storage_append_env_log(float latitude, float longitude,
                                     float temperature_c, float humidity_pct,
                                     bool has_fix);
esp_err_t bsp_storage_append_health_log(uint32_t uptime_s, uint32_t free_heap_bytes,
                                        uint32_t min_free_heap_bytes,
                                        uint64_t storage_free_bytes,
                                        uint64_t storage_total_bytes);
