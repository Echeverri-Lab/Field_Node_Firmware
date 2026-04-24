#include "bsp_audio.h"
#include "bsp_storage.h"
#include "system_app.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG = "SYS_AUDIO";

// Keep Sean's lifecycle safety: init/deinit audio per monitoring cycle.
// Rachel-inspired event capture settings.
#define AUDIO_PRE_TRIGGER_SECONDS  5U
#define AUDIO_POST_TRIGGER_SECONDS 3U
#define AUDIO_READ_CHUNK_SAMPLES   512U
#define AUDIO_EVENT_THRESHOLD      2500
#define AUDIO_EVENT_HIT_COUNT      10U

// Audio streaming settings for testing
#define AUDIO_STREAM_DURATION_MS   3000    // 3 seconds of streaming
#define AUDIO_STREAM_CHUNK_SIZE    512     // Samples per chunk
static volatile bool s_streaming_enabled = false;

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

  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGW(TAG, "fopen(%s) failed (errno %d: %s), requesting remount...",
             path, errno, strerror(errno));
    if (bsp_storage_remount() == ESP_OK) {
      f = fopen(path, "wb");
    }
  }
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", path);
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

/**
 * @brief Stream audio over USB serial as Base64-encoded PCM16 (similar to camera streaming)
 * 
 * This function streams audio for testing purposes. Output format:
 * [USB_AUDIO_BEGIN] rate=16000 channels=1 bits=16 samples=NNNN duration_ms=MMMM b64=BBBB
 * <base64 encoded PCM16 data>
 * [USB_AUDIO_END]
 * 
 * @param samples Pointer to int32 audio samples
 * @param sample_count Number of samples
 * @return true if successful, false otherwise
 */
static bool stream_audio_over_usb_base64(const int32_t *samples, size_t sample_count) {
  if (!samples || sample_count == 0) {
    return false;
  }

  // Convert int32 to int16 PCM
  size_t pcm16_size = sample_count * sizeof(int16_t);
  int16_t *pcm16 = malloc(pcm16_size);
  if (!pcm16) {
    ESP_LOGE(TAG, "Failed to allocate PCM16 buffer for streaming");
    return false;
  }

  for (size_t i = 0; i < sample_count; i++) {
    pcm16[i] = pcm32_to_pcm16(samples[i]);
  }

  // Calculate base64 buffer size
  size_t b64_cap = 4 * ((pcm16_size + 2) / 3) + 1;
  unsigned char *b64 = malloc(b64_cap);
  if (!b64) {
    free(pcm16);
    ESP_LOGE(TAG, "Failed to allocate base64 buffer for streaming");
    return false;
  }

  // Encode to base64
  size_t out_len = 0;
  int rc = mbedtls_base64_encode(b64, b64_cap, &out_len, (const unsigned char *)pcm16, pcm16_size);
  if (rc != 0) {
    free(pcm16);
    free(b64);
    ESP_LOGE(TAG, "Base64 encoding failed: %d", rc);
    return false;
  }

  // Calculate duration in milliseconds
  uint32_t duration_ms = (uint32_t)((sample_count * 1000) / BSP_AUDIO_RATE_HZ);

  // Send header with metadata
  printf("[USB_AUDIO_BEGIN] rate=%u channels=1 bits=16 samples=%u duration_ms=%lu b64=%u\n",
         BSP_AUDIO_RATE_HZ, (unsigned)sample_count, (unsigned long)duration_ms, (unsigned)out_len);
  
  // Send base64 encoded audio data
  fwrite(b64, 1, out_len, stdout);
  printf("\n[USB_AUDIO_END]\n");
  fflush(stdout);

  free(pcm16);
  free(b64);
  
  ESP_LOGI(TAG, "Streamed %zu samples (%u ms) over USB", sample_count, duration_ms);
  return true;
}

/**
 * @brief Stream raw PCM16 audio over USB serial (alternative to base64, more efficient)
 * 
 * Output format:
 * [USB_AUDIO_RAW_BEGIN] rate=16000 channels=1 bits=16 samples=NNNN bytes=BBBB
 * <raw binary PCM16 data>
 * [USB_AUDIO_RAW_END]
 * 
 * @param samples Pointer to int32 audio samples
 * @param sample_count Number of samples
 * @return true if successful, false otherwise
 */
static bool stream_audio_over_usb_raw(const int32_t *samples, size_t sample_count) {
  if (!samples || sample_count == 0) {
    return false;
  }

  size_t pcm16_size = sample_count * sizeof(int16_t);
  int16_t *pcm16 = malloc(pcm16_size);
  if (!pcm16) {
    ESP_LOGE(TAG, "Failed to allocate PCM16 buffer for streaming");
    return false;
  }

  for (size_t i = 0; i < sample_count; i++) {
    pcm16[i] = pcm32_to_pcm16(samples[i]);
  }

  // Send header
  printf("[USB_AUDIO_RAW_BEGIN] rate=%u channels=1 bits=16 samples=%u bytes=%zu\n",
         BSP_AUDIO_RATE_HZ, (unsigned)sample_count, pcm16_size);
  
  // Send raw PCM16 data
  fwrite(pcm16, 1, pcm16_size, stdout);
  printf("\n[USB_AUDIO_RAW_END]\n");
  fflush(stdout);

  free(pcm16);
  
  ESP_LOGI(TAG, "Streamed %zu samples (%zu bytes) over USB (raw)", sample_count, pcm16_size);
  return true;
}

/**
 * @brief Test streaming mode - captures and streams audio for testing
 * 
 * This function runs a short audio capture session and streams the results
 * over USB serial for testing with external tools (like Python scripts or Audacity).
 */
static void run_streaming_test(bool use_base64) {
  ESP_LOGI(TAG, "Starting audio streaming test (%s mode)", use_base64 ? "base64" : "raw");
  
  if (bsp_audio_init() != ESP_OK) {
    ESP_LOGE(TAG, "Audio init failed for streaming test");
    return;
  }

  // Calculate total samples for the streaming duration
  size_t total_samples = (BSP_AUDIO_RATE_HZ * AUDIO_STREAM_DURATION_MS) / 1000;
  
  // Allocate buffer for captured audio
  int32_t *stream_buffer = heap_caps_malloc(total_samples * sizeof(int32_t), MALLOC_CAP_SPIRAM);
  if (!stream_buffer) {
    stream_buffer = malloc(total_samples * sizeof(int32_t));
  }
  if (!stream_buffer) {
    ESP_LOGE(TAG, "Failed to allocate streaming buffer (%zu samples)", total_samples);
    bsp_audio_deinit();
    return;
  }

  // Capture audio in chunks
  size_t samples_captured = 0;
  int32_t chunk[AUDIO_STREAM_CHUNK_SIZE];
  
  ESP_LOGI(TAG, "Capturing %zu samples (%.2f seconds)...", 
           total_samples, (float)AUDIO_STREAM_DURATION_MS / 1000.0f);

  while (samples_captured < total_samples) {
    size_t bytes_read = 0;
    esp_err_t err = bsp_audio_read(chunk, sizeof(chunk), &bytes_read, 200);
    
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Audio read failed during streaming: %s", esp_err_to_name(err));
      continue;
    }

    size_t read_samples = bytes_read / sizeof(int32_t);
    if (read_samples == 0) {
      continue;
    }

    // Copy to stream buffer
    size_t remaining = total_samples - samples_captured;
    size_t to_copy = (read_samples < remaining) ? read_samples : remaining;
    memcpy(stream_buffer + samples_captured, chunk, to_copy * sizeof(int32_t));
    samples_captured += to_copy;

    // Progress indicator
    if (samples_captured % (BSP_AUDIO_RATE_HZ / 2) == 0) {
      ESP_LOGI(TAG, "Captured %zu/%zu samples (%.1f%%)", 
               samples_captured, total_samples, 
               (float)samples_captured * 100.0f / total_samples);
    }
  }

  ESP_LOGI(TAG, "Capture complete. Streaming over USB...");

  // Stream the captured audio
  bool success;
  if (use_base64) {
    success = stream_audio_over_usb_base64(stream_buffer, samples_captured);
  } else {
    success = stream_audio_over_usb_raw(stream_buffer, samples_captured);
  }

  if (success) {
    ESP_LOGI(TAG, "Streaming test complete");
  } else {
    ESP_LOGE(TAG, "Streaming test failed");
  }

  // Cleanup
  heap_caps_free(stream_buffer);
  bsp_audio_deinit();
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

  bsp_audio_deinit();

  size_t total_samples = copied_pre + captured_post;
  err = save_clip_to_wav(clip, total_samples);
  heap_caps_free(clip);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Audio clip saved (%.2fs)", (float)total_samples / (float)BSP_AUDIO_RATE_HZ);
  }
  return err;
}

static esp_err_t record_requested_clip(void) {
  ring_buffer_reset(&s_ring);
  if (!bsp_storage_is_ready()) {
    ESP_LOGW(TAG, "Storage not ready, skipping scheduled recording");
    return ESP_ERR_INVALID_STATE;
  }

  if (bsp_audio_init() != ESP_OK) {
    ESP_LOGW(TAG, "Audio init failed for scheduled recording");
    return ESP_FAIL;
  }

  esp_err_t err = record_triggered_clip();
  bsp_audio_deinit();
  return err;
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

  ESP_LOGI(TAG, "Audio task ready. Waiting for recording commands.");

  while (1) {
    // Check if streaming is requested
    if (s_streaming_enabled) {
      ESP_LOGI(TAG, "Streaming mode activated");
      run_streaming_test(false);  // Use raw PCM for efficiency
      s_streaming_enabled = false;
    }

    if (!g_audio_req_queue) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    audio_msg_t msg = {0};
    if (xQueueReceive(g_audio_req_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
      switch (msg.type) {
        case AUDIO_CMD_START_RECORDING:
          ESP_LOGI(TAG, "Scheduled recording request received");
          if (record_requested_clip() != ESP_OK) {
            ESP_LOGW(TAG, "Scheduled recording failed");
          }
          break;

        case AUDIO_CMD_STREAM_TEST:
          sys_audio_enable_streaming();
          break;
      }
    }
  }
}

/**
 * @brief Enable audio streaming for testing
 * 
 * Call this function from another task or via a command interface
 * to trigger an audio streaming test.
 */
void sys_audio_enable_streaming(void) {
  s_streaming_enabled = true;
  ESP_LOGI(TAG, "Audio streaming requested");
}
