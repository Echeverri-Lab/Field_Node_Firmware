#include "bsp_audio.h"
#include "bsp_storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_AUDIO";

static const int64_t AUDIO_INTERVAL_MS = 2LL * 60LL * 60LL * 1000LL;
static const int64_t AUDIO_DURATION_MS = 1LL * 60LL * 60LL * 1000LL;

static bool s_recording = false;
static FILE *s_audio_file = NULL;
static uint32_t s_audio_data_bytes = 0;
static int64_t s_record_start_ms = 0;
static bool s_audio_ready = false;

static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t bits_per_sample,
                             uint16_t channels, uint32_t data_size) {
  uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;
  uint32_t riff_chunk_size = 36 + data_size;

  fwrite("RIFF", 1, 4, f);
  fwrite(&riff_chunk_size, 4, 1, f);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);

  uint32_t fmt_chunk_size = 16;
  uint16_t audio_format = 1;
  fwrite(&fmt_chunk_size, 4, 1, f);
  fwrite(&audio_format, 2, 1, f);
  fwrite(&channels, 2, 1, f);
  fwrite(&sample_rate, 4, 1, f);
  fwrite(&byte_rate, 4, 1, f);
  fwrite(&block_align, 2, 1, f);
  fwrite(&bits_per_sample, 2, 1, f);

  fwrite("data", 1, 4, f);
  fwrite(&data_size, 4, 1, f);
}

static void stop_recording(void) {
  if (!s_recording || !s_audio_file) {
    s_recording = false;
    if (s_audio_ready) {
      bsp_audio_deinit();
      s_audio_ready = false;
    }
    return;
  }

  s_recording = false;
  fseek(s_audio_file, 0, SEEK_SET);
  write_wav_header(s_audio_file, BSP_AUDIO_RATE_HZ, 16, 1, s_audio_data_bytes);
  fclose(s_audio_file);
  s_audio_file = NULL;
  if (s_audio_ready) {
    bsp_audio_deinit();
    s_audio_ready = false;
  }

  ESP_LOGI(TAG, "Recording complete. Bytes: %u", s_audio_data_bytes);
}

static bool start_recording(void) {
  if (!bsp_storage_is_ready()) {
    ESP_LOGW(TAG, "SD not ready, skipping audio cycle");
    return false;
  }

  char path[128] = {0};
  if (bsp_storage_make_path(path, sizeof(path), "audio", "audio", "wav") != ESP_OK) {
    ESP_LOGW(TAG, "Failed to make audio filename");
    return false;
  }

  s_audio_file = fopen(path, "wb+");
  if (!s_audio_file) {
    ESP_LOGW(TAG, "Failed to open %s", path);
    return false;
  }

  write_wav_header(s_audio_file, BSP_AUDIO_RATE_HZ, 16, 1, 0);
  s_audio_data_bytes = 0;
  s_record_start_ms = esp_timer_get_time() / 1000;
  s_recording = true;

  ESP_LOGI(TAG, "Recording started: %s", path);
  return true;
}

void sys_audio_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

  int64_t last_cycle_ms = esp_timer_get_time() / 1000;

  while (1) {
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (!s_recording && (now_ms - last_cycle_ms) >= AUDIO_INTERVAL_MS) {
      last_cycle_ms = now_ms;
      if (!s_audio_ready) {
        if (bsp_audio_init() != ESP_OK) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          continue;
        }
        s_audio_ready = true;
      }
      if (!start_recording()) {
        bsp_audio_deinit();
        s_audio_ready = false;
      }
    }

    if (s_recording && s_audio_file) {
      int32_t raw[256] = {0};
      size_t bytes_read = 0;
      if (bsp_audio_read(raw, sizeof(raw), &bytes_read, 20) == ESP_OK && bytes_read > 0) {
        size_t samples = bytes_read / sizeof(int32_t);
        int16_t pcm16[256] = {0};

        for (size_t i = 0; i < samples; i++) {
          int32_t s = raw[i] >> BSP_AUDIO_PCM_SHIFT;
          if (s > 32767) s = 32767;
          if (s < -32768) s = -32768;
          pcm16[i] = (int16_t)s;
        }

        size_t written = fwrite(pcm16, sizeof(int16_t), samples, s_audio_file);
        s_audio_data_bytes += (uint32_t)(written * sizeof(int16_t));
      }

      if ((now_ms - s_record_start_ms) >= AUDIO_DURATION_MS) {
        stop_recording();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
