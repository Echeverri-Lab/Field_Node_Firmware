#include "bsp_storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "driver/sdmmc_default_configs.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SD_MOSI_PIN GPIO_NUM_9
#define SD_MISO_PIN GPIO_NUM_8
#define SD_SCLK_PIN GPIO_NUM_7
#define SD_CS_PIN   GPIO_NUM_21

static const char *TAG = "BSP_STORAGE";
static bool s_ready = false;

static bool is_retention_candidate(const char *name) {
  if (!name) {
    return false;
  }

  size_t len = strlen(name);
  if (len == 0) {
    return false;
  }

  static const char *k_extensions[] = {
      ".jpg",
      ".jpeg",
      ".wav",
      ".jpg.upl",
      ".jpeg.upl",
      ".wav.upl",
  };

  for (size_t i = 0; i < sizeof(k_extensions) / sizeof(k_extensions[0]); i++) {
    size_t ext_len = strlen(k_extensions[i]);
    if (len >= ext_len && strcasecmp(name + (len - ext_len), k_extensions[i]) == 0) {
      return true;
    }
  }

  return false;
}

static esp_err_t scan_dir_for_oldest(const char *dir_path, char *oldest_path,
                                     size_t oldest_path_len,
                                     time_t *oldest_mtime, bool *found_any) {
  DIR *dir = opendir(dir_path);
  if (!dir) {
    return ESP_FAIL;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        !is_retention_candidate(entry->d_name)) {
      continue;
    }

    char path[160] = {0};
    int written = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
      continue;
    }

    struct stat st = {0};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }

    if (!*found_any || st.st_mtime < *oldest_mtime) {
      *oldest_mtime = st.st_mtime;
      *found_any = true;
      strncpy(oldest_path, path, oldest_path_len - 1);
      oldest_path[oldest_path_len - 1] = '\0';
    }
  }

  closedir(dir);
  return ESP_OK;
}

esp_err_t bsp_storage_init(void) {
  if (s_ready) {
    return ESP_OK;
  }

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = SD_MOSI_PIN,
      .miso_io_num = SD_MISO_PIN,
      .sclk_io_num = SD_SCLK_PIN,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  esp_err_t bus_err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(bus_err));
    return bus_err;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = 10000;

  sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_cfg.gpio_cs = SD_CS_PIN;
  slot_cfg.host_id = (spi_host_device_t)host.slot;

  esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
      .format_if_mount_failed = false,
      .max_files = 8,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_card_t *card = NULL;
  esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_cfg, &mount_cfg, &card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
    return err;
  }

  mkdir("/sdcard/timelapse", 0775);
  mkdir("/sdcard/pir", 0775);
  mkdir("/sdcard/audio", 0775);
  mkdir("/sdcard/logs", 0775);

  s_ready = true;
  ESP_LOGI(TAG, "SD card mounted");
  return ESP_OK;
}

bool bsp_storage_is_ready(void) {
  return s_ready;
}

int64_t bsp_storage_now_ms(void) {
  return esp_timer_get_time() / 1000;
}

esp_err_t bsp_storage_make_path(char *out, size_t out_len,
                                const char *subdir, const char *prefix,
                                const char *extension) {
  if (!s_ready || !out || !subdir || !prefix || !extension) {
    return ESP_ERR_INVALID_ARG;
  }

  int written = snprintf(out, out_len, "/sdcard/%s/%s_%lld.%s",
                         subdir, prefix, (long long)bsp_storage_now_ms(), extension);
  if (written < 0 || (size_t)written >= out_len) {
    return ESP_ERR_INVALID_SIZE;
  }
  return ESP_OK;
}

esp_err_t bsp_storage_write_blob(const char *path, const void *data, size_t len) {
  if (!s_ready || !path || !data || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    return ESP_FAIL;
  }

  size_t written = fwrite(data, 1, len, f);
  fclose(f);
  return (written == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_storage_disk_usage(uint64_t *total_bytes, uint64_t *free_bytes) {
  if (!s_ready || !total_bytes || !free_bytes) {
    return ESP_ERR_INVALID_ARG;
  }

  struct statvfs fs = {0};
  if (statvfs("/sdcard", &fs) != 0) {
    return ESP_FAIL;
  }

  *total_bytes = (uint64_t)fs.f_blocks * (uint64_t)fs.f_frsize;
  *free_bytes = (uint64_t)fs.f_bavail * (uint64_t)fs.f_frsize;
  return ESP_OK;
}

esp_err_t bsp_storage_find_oldest_file(char *out_path, size_t out_len) {
  if (!s_ready || !out_path || out_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  static const char *k_data_dirs[] = {
      "/sdcard/timelapse",
      "/sdcard/pir",
      "/sdcard/audio",
  };

  bool found_any = false;
  time_t oldest_mtime = 0;
  out_path[0] = '\0';

  for (size_t i = 0; i < sizeof(k_data_dirs) / sizeof(k_data_dirs[0]); i++) {
    if (scan_dir_for_oldest(k_data_dirs[i], out_path, out_len, &oldest_mtime,
                            &found_any) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to scan %s for retention", k_data_dirs[i]);
    }
  }

  return found_any ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t bsp_storage_delete_file(const char *path) {
  if (!s_ready || !path || path[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_storage_mark_uploaded(const char *path, char *out_path, size_t out_path_len) {
  if (!s_ready || !path || path[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  struct stat st = {0};
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    return ESP_ERR_NOT_FOUND;
  }

  const char *suffix = ".upl";
  size_t path_len = strlen(path);
  size_t suffix_len = strlen(suffix);

  if (path_len > suffix_len &&
      strcasecmp(path + (path_len - suffix_len), suffix) == 0) {
    if (out_path && out_path_len > 0) {
      if (path_len >= out_path_len) {
        return ESP_ERR_INVALID_SIZE;
      }
      memcpy(out_path, path, path_len + 1U);
    }
    return ESP_OK;
  }

  char renamed[192] = {0};
  int written = snprintf(renamed, sizeof(renamed), "%s%s", path, suffix);
  if (written < 0 || (size_t)written >= sizeof(renamed)) {
    return ESP_ERR_INVALID_SIZE;
  }

  if (rename(path, renamed) != 0) {
    return ESP_FAIL;
  }

  if (out_path && out_path_len > 0) {
    size_t renamed_len = strlen(renamed);
    if (renamed_len >= out_path_len) {
      return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_path, renamed, renamed_len + 1U);
  }

  return ESP_OK;
}

esp_err_t bsp_storage_append_env_log(float latitude, float longitude,
                                     float temperature_c, float humidity_pct,
                                     bool has_fix) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  FILE *logf = fopen("/sdcard/timelapse/env_log.csv", "a");
  if (!logf) {
    return ESP_FAIL;
  }

  fprintf(logf, "%lld,", (long long)bsp_storage_now_ms());
  if (has_fix) {
    fprintf(logf, "%.6f,%.6f,", latitude, longitude);
  } else {
    fprintf(logf, "NaN,NaN,");
  }
  fprintf(logf, "%f,%f\n", temperature_c, humidity_pct);
  fclose(logf);

  return ESP_OK;
}

esp_err_t bsp_storage_append_health_log(uint32_t uptime_s, uint32_t free_heap_bytes,
                                        uint32_t min_free_heap_bytes,
                                        uint64_t storage_free_bytes,
                                        uint64_t storage_total_bytes) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  FILE *logf = fopen("/sdcard/logs/system_health.csv", "a+");
  if (!logf) {
    return ESP_FAIL;
  }

  if (fseek(logf, 0, SEEK_END) != 0) {
    fclose(logf);
    return ESP_FAIL;
  }

  long size = ftell(logf);
  if (size == 0) {
    fprintf(logf, "timestamp_ms,uptime_s,free_heap_bytes,min_free_heap_bytes,storage_free_bytes,storage_total_bytes\n");
  }

  fprintf(logf, "%lld,%u,%u,%u,%llu,%llu\n",
          (long long)bsp_storage_now_ms(),
          (unsigned)uptime_s,
          (unsigned)free_heap_bytes,
          (unsigned)min_free_heap_bytes,
          (unsigned long long)storage_free_bytes,
          (unsigned long long)storage_total_bytes);
  fclose(logf);

  return ESP_OK;
}
