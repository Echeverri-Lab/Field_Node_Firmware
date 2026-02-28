#include "bsp_audio.h"
#include "bsp_storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_AUDIO";

// Keep Sean's lifecycle safety: init/deinit audio per monitoring cycle.
static const int64_t AUDIO_MONITOR_INTERVAL_MS = 2LL * 60LL * 60LL * 1000LL;
static const int64_t AUDIO_MONITOR_WINDOW_MS = 60LL * 1000LL;
static const int64_t AUDIO_TRIGGER_COOLDOWN_MS = 2000;

// Rachel-inspired event capture settings.
#define AUDIO_PRE_TRIGGER_SECONDS  5U
#define AUDIO_POST_TRIGGER_SECONDS 3U
#define AUDIO_READ_CHUNK_SAMPLES   512U
#define AUDIO_EVENT_THRESHOLD      2500
#define AUDIO_EVENT_HIT_COUNT      10U

typedef struct {
  int32_t *samples;
  size_t size_samples;
  size_t write_idx;
  bool full;
} audio_ring_buffer_t;

static audio_ring_buffer_t s_ring = {0};

static inline int16_t pcm32_to_pcm16(int32_t sample) {
  int32_t shifted = sample >> BSP_AUDIO_PCM_SHIFT;
  if (shifted > INT16_MAX) {
    shifted = INT16_MAX;
  } else if (shifted < INT16_MIN) {
    shifted = INT16_MIN;
  }
  return (int16_t)shifted;
}

static esp_err_t ring_buffer_init(audio_ring_buffer_t *rb, size_t size_samples) {
  rb->samples = (int32_t *)heap_caps_malloc(size_samples * sizeof(int32_t), MALLOC_CAP_SPIRAM);
  if (!rb->samples) {
    rb->samples = (int32_t *)malloc(size_samples * sizeof(int32_t));
  }
  if (!rb->samples) {
    ESP_LOGE(TAG, "Ring buffer allocation failed for %zu samples", size_samples);
    return ESP_ERR_NO_MEM;
  }

  rb->size_samples = size_samples;
  rb->write_idx = 0;
  rb->full = false;
  memset(rb->samples, 0, size_samples * sizeof(int32_t));
  return ESP_OK;
}

static void ring_buffer_reset(audio_ring_buffer_t *rb) {
  rb->write_idx = 0;
  rb->full = false;
  if (rb->samples && rb->size_samples > 0) {
    memset(rb->samples, 0, rb->size_samples * sizeof(int32_t));
  }
}

static void ring_buffer_write(audio_ring_buffer_t *rb, const int32_t *samples, size_t count) {
  if (!rb->samples || rb->size_samples == 0 || !samples || count == 0) {
    return;
  }

  for (size_t i = 0; i < count; i++) {
    rb->samples[rb->write_idx] = samples[i];
    rb->write_idx++;
    if (rb->write_idx >= rb->size_samples) {
      rb->write_idx = 0;
      rb->full = true;
    }
  }
}

static size_t ring_buffer_copy_chronological(const audio_ring_buffer_t *rb, int32_t *dest, size_t max_samples) {
  if (!rb->samples || !dest || max_samples == 0) {
    return 0;
  }

  size_t available = rb->full ? rb->size_samples : rb->write_idx;
  if (available > max_samples) {
    available = max_samples;
  }

  size_t read_idx = rb->full ? rb->write_idx : 0;
  for (size_t i = 0; i < available; i++) {
    dest[i] = rb->samples[read_idx];
    read_idx++;
    if (read_idx >= rb->size_samples) {
      read_idx = 0;
    }
  }
  return available;
}

static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample,
                             uint32_t data_size) {
  uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8U;
  uint16_t block_align = (uint16_t)(channels * bits_per_sample / 8U);
  uint32_t riff_chunk_size = 36U + data_size;
  uint32_t fmt_chunk_size = 16U;
  uint16_t audio_format = 1U;

  fwrite("RIFF", 1, 4, f);
  fwrite(&riff_chunk_size, sizeof(riff_chunk_size), 1, f);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  fwrite(&fmt_chunk_size, sizeof(fmt_chunk_size), 1, f);
  fwrite(&audio_format, sizeof(audio_format), 1, f);
  fwrite(&channels, sizeof(channels), 1, f);
  fwrite(&sample_rate, sizeof(sample_rate), 1, f);
  fwrite(&byte_rate, sizeof(byte_rate), 1, f);
  fwrite(&block_align, sizeof(block_align), 1, f);
  fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&data_size, sizeof(data_size), 1, f);
}

static esp_err_t save_clip_to_wav(const int32_t *samples, size_t sample_count) {
  if (!bsp_storage_is_ready()) {
    return ESP_ERR_INVALID_STATE;
  }

  char path[128] = {0};
  if (bsp_storage_make_path(path, sizeof(path), "audio", "audio", "wav") != ESP_OK) {
    return ESP_ERR_INVALID_SIZE;
  }

  FILE *f = fopen(path, "wb+");
  if (!f) {
    ESP_LOGW(TAG, "Failed to open %s", path);
    return ESP_FAIL;
  }

  uint32_t data_size = (uint32_t)(sample_count * sizeof(int16_t));
  write_wav_header(f, BSP_AUDIO_RATE_HZ, 1, 16, data_size);

  int16_t pcm16_chunk[256];
  size_t offset = 0;
  while (offset < sample_count) {
    size_t chunk_count = sample_count - offset;
    if (chunk_count > 256) {
      chunk_count = 256;
    }

    for (size_t i = 0; i < chunk_count; i++) {
      pcm16_chunk[i] = pcm32_to_pcm16(samples[offset + i]);
    }

    size_t written = fwrite(pcm16_chunk, sizeof(int16_t), chunk_count, f);
    if (written != chunk_count) {
      fclose(f);
      ESP_LOGE(TAG, "Partial WAV write to %s", path);
      return ESP_FAIL;
    }
    offset += chunk_count;
  }

  fclose(f);
  ESP_LOGI(TAG, "Saved %s (%u bytes)", path, data_size);
  return ESP_OK;
}

static bool detect_audio_event(const int32_t *samples, size_t sample_count) {
  size_t hits = 0;

  for (size_t i = 0; i < sample_count; i++) {
    int32_t shifted = samples[i] >> BSP_AUDIO_PCM_SHIFT;
    int32_t magnitude = shifted < 0 ? -shifted : shifted;
    if (magnitude >= AUDIO_EVENT_THRESHOLD) {
      hits++;
      if (hits >= AUDIO_EVENT_HIT_COUNT) {
        return true;
      }
    }
  }
  return false;
}

static esp_err_t capture_post_trigger(int32_t *dest, size_t requested_samples, size_t *captured_samples) {
  size_t total = 0;
  int32_t chunk[AUDIO_READ_CHUNK_SAMPLES] = {0};
  int empty_loops = 0;

  while (total < requested_samples) {
    size_t bytes_read = 0;
    esp_err_t err = bsp_audio_read(chunk, sizeof(chunk), &bytes_read, 200);
    if (err != ESP_OK) {
      if (err == ESP_ERR_TIMEOUT) {
        if (++empty_loops > 20) {
          break;
        }
        continue;
      }
      return err;
    }

    size_t read_samples = bytes_read / sizeof(int32_t);
    if (read_samples == 0) {
      if (++empty_loops > 20) {
        break;
      }
      continue;
    }
    empty_loops = 0;

    ring_buffer_write(&s_ring, chunk, read_samples);

    size_t remaining = requested_samples - total;
    size_t to_copy = read_samples < remaining ? read_samples : remaining;
    memcpy(dest + total, chunk, to_copy * sizeof(int32_t));
    total += to_copy;
  }

  if (captured_samples) {
    *captured_samples = total;
  }
  return total > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t record_triggered_clip(void) {
  size_t pre_trigger_samples = s_ring.size_samples;
  size_t post_trigger_samples = BSP_AUDIO_RATE_HZ * AUDIO_POST_TRIGGER_SECONDS;
  size_t max_total_samples = pre_trigger_samples + post_trigger_samples;

  int32_t *clip = (int32_t *)heap_caps_malloc(max_total_samples * sizeof(int32_t), MALLOC_CAP_SPIRAM);
  if (!clip) {
    clip = (int32_t *)malloc(max_total_samples * sizeof(int32_t));
  }
  if (!clip) {
    ESP_LOGE(TAG, "Clip allocation failed (%zu samples)", max_total_samples);
    return ESP_ERR_NO_MEM;
  }

  size_t copied_pre = ring_buffer_copy_chronological(&s_ring, clip, pre_trigger_samples);
  size_t captured_post = 0;
  esp_err_t err = capture_post_trigger(clip + copied_pre, post_trigger_samples, &captured_post);
  if (err != ESP_OK) {
    heap_caps_free(clip);
    ESP_LOGW(TAG, "Post-trigger capture failed: %s", esp_err_to_name(err));
    return err;
  }

  size_t total_samples = copied_pre + captured_post;
  err = save_clip_to_wav(clip, total_samples);
  heap_caps_free(clip);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Audio clip saved (%.2fs)", (float)total_samples / (float)BSP_AUDIO_RATE_HZ);
  }
  return err;
}

static void run_monitor_cycle(void) {
  int64_t start_ms = bsp_storage_now_ms();
  int32_t chunk[AUDIO_READ_CHUNK_SAMPLES] = {0};

  ring_buffer_reset(&s_ring);
  ESP_LOGI(TAG, "Audio monitor cycle started (%lld ms window)", (long long)AUDIO_MONITOR_WINDOW_MS);

  while ((bsp_storage_now_ms() - start_ms) < AUDIO_MONITOR_WINDOW_MS) {
    size_t bytes_read = 0;
    esp_err_t err = bsp_audio_read(chunk, sizeof(chunk), &bytes_read, 100);
    if (err != ESP_OK) {
      if (err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Audio read failed: %s", esp_err_to_name(err));
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    size_t read_samples = bytes_read / sizeof(int32_t);
    if (read_samples == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    ring_buffer_write(&s_ring, chunk, read_samples);

    if (detect_audio_event(chunk, read_samples)) {
      ESP_LOGI(TAG, "Audio event detected");
      if (record_triggered_clip() != ESP_OK) {
        ESP_LOGW(TAG, "Triggered clip capture failed");
      }
      vTaskDelay(pdMS_TO_TICKS(AUDIO_TRIGGER_COOLDOWN_MS));
    }
  }
}

void sys_audio_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

  size_t pre_trigger_samples = BSP_AUDIO_RATE_HZ * AUDIO_PRE_TRIGGER_SECONDS;
  if (ring_buffer_init(&s_ring, pre_trigger_samples) != ESP_OK) {
    ESP_LOGE(TAG, "Audio ring buffer init failed; task exiting");
    vTaskDelete(NULL);
    return;
  }

  // Start first monitoring cycle right after boot for easier field verification.
  int64_t last_cycle_ms = bsp_storage_now_ms() - AUDIO_MONITOR_INTERVAL_MS;

  while (1) {
    int64_t now_ms = bsp_storage_now_ms();
    if ((now_ms - last_cycle_ms) >= AUDIO_MONITOR_INTERVAL_MS) {
      last_cycle_ms = now_ms;

      if (!bsp_storage_is_ready()) {
        ESP_LOGW(TAG, "Storage not ready, skipping audio cycle");
      } else if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed, skipping audio cycle");
      } else {
        run_monitor_cycle();
        bsp_audio_deinit();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
