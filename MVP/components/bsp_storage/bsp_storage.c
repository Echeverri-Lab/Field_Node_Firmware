#include "bsp_storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
static sdmmc_card_t *s_card = NULL;
static bool s_spi_bus_inited = false;

static esp_err_t sd_mount(void)
{
    if (!s_spi_bus_inited) {
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
        s_spi_bus_inited = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 4000;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS_PIN;
    slot_cfg.host_id = (spi_host_device_t)host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_cfg,
                                             &mount_cfg, &s_card);
    if (err == ESP_OK) {
        mkdir("/sdcard/timelapse", 0775);
        mkdir("/sdcard/pir", 0775);
        mkdir("/sdcard/audio", 0775);
    }
    return err;
}

static void sd_unmount(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
        s_card = NULL;
    }
}

static esp_err_t sd_remount(void)
{
    ESP_LOGW(TAG, "Remounting SD card...");
    sd_unmount();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t err = sd_mount();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD card remount succeeded");
    } else {
        ESP_LOGE(TAG, "SD card remount failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t bsp_storage_init(void) {
  if (s_ready) {
    return ESP_OK;
  }

  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 5; attempt++) {
    err = sd_mount();
    if (err == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "SD mount attempt %d/5 failed: %s", attempt, esp_err_to_name(err));
    sd_unmount();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SD mount failed after 5 attempts");
    return err;
  }

  FILE *test = fopen("/sdcard/audio/_sdtest.tmp", "wb");
  if (test) {
    fwrite("ok", 1, 2, test);
    fclose(test);
    remove("/sdcard/audio/_sdtest.tmp");
    ESP_LOGI(TAG, "SD card write test passed");
  } else {
    ESP_LOGE(TAG, "SD card write test FAILED (errno %d)", errno);
  }

  s_ready = true;
  ESP_LOGI(TAG, "SD card mounted");
  return ESP_OK;
}

esp_err_t bsp_storage_remount(void) {
    s_ready = false;
    esp_err_t err = sd_remount();
    if (err == ESP_OK) {
        s_ready = true;
    }
    return err;
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

static FILE *sd_fopen_retry(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) {
        return f;
    }
    ESP_LOGW(TAG, "fopen(%s) failed (errno %d: %s), remounting SD...",
             path, errno, strerror(errno));

    if (sd_remount() != ESP_OK) {
        return NULL;
    }

    f = fopen(path, mode);
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) still fails after remount (errno %d: %s)",
                 path, errno, strerror(errno));
    }
    return f;
}

esp_err_t bsp_storage_write_blob(const char *path, const void *data, size_t len) {
  if (!s_ready || !path || !data || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = sd_fopen_retry(path, "wb");
  if (!f) {
    return ESP_FAIL;
  }

  size_t written = fwrite(data, 1, len, f);
  fclose(f);
  return (written == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_storage_append_env_log(float latitude, float longitude,
                                     float temperature_c, float humidity_pct,
                                     bool has_fix) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  FILE *logf = sd_fopen_retry("/sdcard/timelapse/env_log.csv", "a");
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

/* ----- Store-and-forward helpers ----------------------------------- */

static const char *s_data_dirs[] = {
    "/sdcard/audio",
    "/sdcard/pir",
    "/sdcard/timelapse",
};
#define NUM_DATA_DIRS (sizeof(s_data_dirs) / sizeof(s_data_dirs[0]))

static bool has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (nlen < slen) {
        return false;
    }
    return strcmp(name + nlen - slen, suffix) == 0;
}

esp_err_t bsp_storage_list_pending(bsp_storage_file_cb_t cb, void *ctx)
{
    if (!s_ready || !cb) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t d = 0; d < NUM_DATA_DIRS; d++) {
        DIR *dir = opendir(s_data_dirs[d]);
        if (!dir) {
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_REG) {
                continue;
            }
            if (has_suffix(entry->d_name, ".uploaded")) {
                continue;
            }

            char path[300];
            int plen = snprintf(path, sizeof(path), "%s/%s",
                                s_data_dirs[d], entry->d_name);
            if (plen < 0 || (size_t)plen >= sizeof(path)) {
                continue;
            }

            if (!cb(path, ctx)) {
                closedir(dir);
                return ESP_OK;
            }
        }
        closedir(dir);
    }
    return ESP_OK;
}

esp_err_t bsp_storage_mark_uploaded(const char *path)
{
    if (!s_ready || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    char new_path[172];
    int n = snprintf(new_path, sizeof(new_path), "%s.uploaded", path);
    if (n < 0 || (size_t)n >= sizeof(new_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (rename(path, new_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}
