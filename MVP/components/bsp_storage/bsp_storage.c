#include "bsp_storage.h"

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
