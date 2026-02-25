#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

static const char *TAG = "SYS_AUDIO";

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// Audio Configuration
#define AUDIO_SAMPLE_RATE       16000      // Hz - matches bsp_audio_init()
#define AUDIO_BITS_PER_SAMPLE   32         // bits - SPH0645 uses 32-bit frames
#define AUDIO_BYTES_PER_SAMPLE  (AUDIO_BITS_PER_SAMPLE / 8)  // 4 bytes

// Ring Buffer Configuration
#define RING_BUFFER_DURATION_SEC  5        // Seconds of audio to keep in buffer
#define RING_BUFFER_SIZE         (AUDIO_SAMPLE_RATE * AUDIO_BYTES_PER_SAMPLE * RING_BUFFER_DURATION_SEC)

// Chunk sizes for I2S reads
#define I2S_READ_CHUNK_SAMPLES   512       // Read 512 samples at a time
#define I2S_READ_CHUNK_BYTES     (I2S_READ_CHUNK_SAMPLES * AUDIO_BYTES_PER_SAMPLE)

// Event Detection Threshold
#define AMPLITUDE_THRESHOLD      500       // Trigger when abs(sample) > this value
#define TRIGGER_COUNT_THRESHOLD  10        // Number of samples above threshold to trigger

// Recording Configuration
#define POST_TRIGGER_DURATION_SEC  3       // Seconds to record AFTER trigger
#define POST_TRIGGER_SAMPLES      (AUDIO_SAMPLE_RATE * POST_TRIGGER_DURATION_SEC)

// Queue Configuration
#define AUDIO_QUEUE_LENGTH       5         // Max pending commands

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Audio command message types
 */
typedef enum {
    AUDIO_CMD_START_RECORDING,  // Force start recording regardless of threshold
    AUDIO_CMD_STOP_CONTINUOUS,  // Stop continuous monitoring
    AUDIO_CMD_SET_THRESHOLD     // Update threshold value
} audio_cmd_type_t;

/**
 * @brief Audio command message
 */
typedef struct {
    audio_cmd_type_t type;
    uint32_t param;  // Optional parameter (e.g., new threshold value)
} audio_cmd_t;

/**
 * @brief Ring Buffer structure for circular audio storage
 */
typedef struct {
    int32_t *buffer;        // Pointer to PSRAM buffer
    size_t size;            // Total size in samples
    size_t write_idx;       // Current write position
    bool is_full;           // Track if we've wrapped around at least once
} ring_buffer_t;

/**
 * @brief WAV file header structure (44 bytes)
 */
typedef struct __attribute__((packed)) {
    // RIFF Chunk
    char riff_tag[4];           // "RIFF"
    uint32_t riff_length;       // File size - 8
    char wave_tag[4];           // "WAVE"
    
    // Format Chunk
    char fmt_tag[4];            // "fmt "
    uint32_t fmt_length;        // 16 for PCM
    uint16_t audio_format;      // 1 = PCM
    uint16_t num_channels;      // 1 = Mono
    uint32_t sample_rate;       // 16000 Hz
    uint32_t byte_rate;         // sample_rate * num_channels * bytes_per_sample
    uint16_t block_align;       // num_channels * bytes_per_sample
    uint16_t bits_per_sample;   // 16 bits
    
    // Data Chunk
    char data_tag[4];           // "data"
    uint32_t data_length;       // Number of data bytes
} wav_header_t;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

static ring_buffer_t g_ring_buffer = {0};
static QueueHandle_t g_audio_queue = NULL;
static volatile bool g_is_monitoring = true;
static uint32_t g_amplitude_threshold = AMPLITUDE_THRESHOLD;
static uint32_t g_file_counter = 0;  // For generating unique filenames

// ============================================================================
// RING BUFFER FUNCTIONS
// ============================================================================

/**
 * @brief Initialize the ring buffer in PSRAM
 * 
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t ring_buffer_init(ring_buffer_t *rb, size_t size_samples) {
    // Allocate buffer in PSRAM (external SPIRAM)
    rb->buffer = (int32_t *)heap_caps_malloc(
        size_samples * sizeof(int32_t), 
        MALLOC_CAP_SPIRAM
    );
    
    if (rb->buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes in PSRAM", 
                 size_samples * sizeof(int32_t));
        return ESP_ERR_NO_MEM;
    }
    
    rb->size = size_samples;
    rb->write_idx = 0;
    rb->is_full = false;
    
    // Zero out the buffer
    memset(rb->buffer, 0, size_samples * sizeof(int32_t));
    
    ESP_LOGI(TAG, "Ring buffer allocated: %zu samples (%zu KB) in PSRAM", 
             size_samples, (size_samples * sizeof(int32_t)) / 1024);
    
    return ESP_OK;
}

/**
 * @brief Write samples to the ring buffer
 * 
 * This implements a circular buffer - when we reach the end, we wrap to the beginning.
 * This allows us to always have the last N seconds of audio available.
 * 
 * @param rb Ring buffer structure
 * @param samples Array of audio samples
 * @param num_samples Number of samples to write
 */
static void ring_buffer_write(ring_buffer_t *rb, const int32_t *samples, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        rb->buffer[rb->write_idx] = samples[i];
        rb->write_idx++;
        
        // Wrap around when we reach the end
        if (rb->write_idx >= rb->size) {
            rb->write_idx = 0;
            rb->is_full = true;  // We've wrapped at least once
        }
    }
}

/**
 * @brief Read the entire ring buffer contents in chronological order
 * 
 * This extracts data from the ring buffer in the correct order (oldest to newest).
 * If the buffer hasn't filled yet, only returns valid data.
 * 
 * @param rb Ring buffer structure
 * @param dest Destination buffer (must be large enough)
 * @param max_samples Maximum samples to read
 * @return size_t Number of samples actually read
 */
static size_t ring_buffer_read_all(ring_buffer_t *rb, int32_t *dest, size_t max_samples) {
    size_t samples_available;
    size_t read_idx;
    
    if (rb->is_full) {
        // Buffer is full - we have the complete history
        samples_available = rb->size;
        read_idx = rb->write_idx;  // Oldest data is right after current write position
    } else {
        // Buffer not full yet - only read what we've written
        samples_available = rb->write_idx;
        read_idx = 0;  // Start from beginning
    }
    
    // Limit to destination size
    if (samples_available > max_samples) {
        samples_available = max_samples;
    }
    
    // Copy data in chronological order
    for (size_t i = 0; i < samples_available; i++) {
        dest[i] = rb->buffer[read_idx];
        read_idx++;
        
        // Wrap around if needed
        if (read_idx >= rb->size) {
            read_idx = 0;
        }
    }
    
    return samples_available;
}

// ============================================================================
// WAV FILE FUNCTIONS
// ============================================================================

/**
 * @brief Create a WAV file header
 * 
 * @param num_samples Number of audio samples in the file
 * @return wav_header_t Populated WAV header structure
 */
static wav_header_t create_wav_header(uint32_t num_samples) {
    wav_header_t header = {0};
    
    // We'll convert 32-bit samples to 16-bit for storage (to save space)
    uint16_t output_bits_per_sample = 16;
    uint16_t output_bytes_per_sample = output_bits_per_sample / 8;
    uint32_t data_size = num_samples * output_bytes_per_sample;
    
    // RIFF Chunk
    memcpy(header.riff_tag, "RIFF", 4);
    header.riff_length = 36 + data_size;  // File size - 8
    memcpy(header.wave_tag, "WAVE", 4);
    
    // Format Chunk
    memcpy(header.fmt_tag, "fmt ", 4);
    header.fmt_length = 16;               // PCM format chunk size
    header.audio_format = 1;              // 1 = PCM (uncompressed)
    header.num_channels = 1;              // Mono
    header.sample_rate = AUDIO_SAMPLE_RATE;
    header.bits_per_sample = output_bits_per_sample;
    header.block_align = header.num_channels * output_bytes_per_sample;
    header.byte_rate = header.sample_rate * header.block_align;
    
    // Data Chunk
    memcpy(header.data_tag, "data", 4);
    header.data_length = data_size;
    
    return header;
}

/**
 * @brief Write audio samples to a WAV file on SD card
 * 
 * This function:
 * 1. Creates a unique filename with timestamp
 * 2. Writes WAV header
 * 3. Converts 32-bit samples to 16-bit
 * 4. Writes audio data
 * 
 * @param samples Array of audio samples
 * @param num_samples Number of samples
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t save_audio_to_wav(const int32_t *samples, size_t num_samples) {
    char filename[64];
    FILE *f = NULL;
    esp_err_t ret = ESP_OK;
    
    // Generate filename with timestamp
    // Format: /sdcard/audio_NNNN.wav
    snprintf(filename, sizeof(filename), "/sdcard/audio_%04lu.wav", g_file_counter++);
    
    ESP_LOGI(TAG, "Saving %zu samples to %s", num_samples, filename);
    
    // Open file for writing
    f = fopen(filename, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing", filename);
        return ESP_FAIL;
    }
    
    // Create and write WAV header
    wav_header_t header = create_wav_header(num_samples);
    if (fwrite(&header, sizeof(wav_header_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    // Convert 32-bit samples to 16-bit and write to file
    // This saves disk space and is sufficient for most audio applications
    int16_t sample_16bit;
    for (size_t i = 0; i < num_samples; i++) {
        // Convert 32-bit to 16-bit by scaling down
        // Our samples are already normalized (shifted by 14 bits in bsp_audio_read)
        sample_16bit = (int16_t)(samples[i] & 0xFFFF);
        
        if (fwrite(&sample_16bit, sizeof(int16_t), 1, f) != 1) {
            ESP_LOGE(TAG, "Failed to write sample %zu", i);
            ret = ESP_FAIL;
            goto cleanup;
        }
    }
    
    ESP_LOGI(TAG, "Successfully saved %zu samples to %s", num_samples, filename);
    
cleanup:
    if (f != NULL) {
        fclose(f);
    }
    return ret;
}

// ============================================================================
// EVENT DETECTION & RECORDING
// ============================================================================

/**
 * @brief Analyze audio chunk for event detection
 * 
 * Checks if the audio amplitude exceeds the threshold for enough samples
 * to indicate a real sound event (not just noise).
 * 
 * @param samples Array of audio samples
 * @param num_samples Number of samples
 * @return bool true if event detected
 */
static bool detect_audio_event(const int32_t *samples, size_t num_samples) {
    uint32_t trigger_count = 0;
    
    for (size_t i = 0; i < num_samples; i++) {
        if (abs(samples[i]) > g_amplitude_threshold) {
            trigger_count++;
            
            // If enough samples exceed threshold, it's an event
            if (trigger_count >= TRIGGER_COUNT_THRESHOLD) {
                return true;
            }
        }
    }
    
    return false;
}

/**
 * @brief Record a complete audio clip (pre-trigger + post-trigger)
 * 
 * This is the main recording function that:
 * 1. Extracts pre-trigger audio from ring buffer
 * 2. Records additional post-trigger audio
 * 3. Saves everything to a WAV file
 * 
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t record_clip(void) {
    esp_err_t ret = ESP_OK;
    int32_t *clip_buffer = NULL;
    size_t total_samples;
    size_t pre_trigger_samples;
    
    ESP_LOGI(TAG, "Recording triggered! Capturing clip...");
    
    // Calculate total clip size: ring buffer (pre-trigger) + post-trigger
    total_samples = g_ring_buffer.size + POST_TRIGGER_SAMPLES;
    
    // Allocate temporary buffer for complete clip
    clip_buffer = (int32_t *)heap_caps_malloc(
        total_samples * sizeof(int32_t),
        MALLOC_CAP_SPIRAM
    );
    
    if (clip_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate clip buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Step 1: Copy pre-trigger audio from ring buffer
    pre_trigger_samples = ring_buffer_read_all(&g_ring_buffer, clip_buffer, g_ring_buffer.size);
    ESP_LOGI(TAG, "Captured %zu pre-trigger samples from ring buffer", pre_trigger_samples);
    
    // Step 2: Record post-trigger audio
    size_t post_samples_captured = 0;
    int32_t *post_buffer_ptr = clip_buffer + pre_trigger_samples;
    
    while (post_samples_captured < POST_TRIGGER_SAMPLES) {
        int32_t temp_buffer[I2S_READ_CHUNK_SAMPLES];
        size_t bytes_read = 0;
        
        // Read a chunk from I2S
        ret = bsp_audio_read(temp_buffer, 
                            I2S_READ_CHUNK_BYTES, 
                            &bytes_read, 
                            1000);  // 1 second timeout
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read post-trigger audio");
            goto cleanup;
        }
        
        size_t samples_read = bytes_read / AUDIO_BYTES_PER_SAMPLE;
        
        // Copy to clip buffer
        memcpy(post_buffer_ptr, temp_buffer, samples_read * sizeof(int32_t));
        post_buffer_ptr += samples_read;
        post_samples_captured += samples_read;
        
        // Also continue writing to ring buffer for continuous monitoring
        ring_buffer_write(&g_ring_buffer, temp_buffer, samples_read);
    }
    
    ESP_LOGI(TAG, "Captured %zu post-trigger samples", post_samples_captured);
    
    // Step 3: Save complete clip to WAV file
    total_samples = pre_trigger_samples + post_samples_captured;
    ret = save_audio_to_wav(clip_buffer, total_samples);
    
    if (ret == ESP_OK) {
        float duration_sec = (float)total_samples / AUDIO_SAMPLE_RATE;
        ESP_LOGI(TAG, "Clip saved successfully (%.2f seconds, %zu samples)", 
                 duration_sec, total_samples);
    }
    
cleanup:
    if (clip_buffer != NULL) {
        heap_caps_free(clip_buffer);
    }
    
    return ret;
}

// ============================================================================
// MAIN TASK
// ============================================================================

void sys_audio_task(void *pvParameters) {
    ESP_LOGI(TAG, "Task Started on Core %d (Real-time)", xPortGetCoreID());
    
    esp_err_t ret;
    
    // Step 1: Initialize Ring Buffer in PSRAM
    size_t ring_buffer_samples = RING_BUFFER_SIZE / AUDIO_BYTES_PER_SAMPLE;
    ret = ring_buffer_init(&g_ring_buffer, ring_buffer_samples);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ring buffer. Task exiting.");
        vTaskDelete(NULL);
        return;
    }
    
    // Step 2: Create command queue
    //g_audio_queue accepts START_RECORDING commands from any other task in system
    g_audio_queue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(audio_cmd_t));
    if (g_audio_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio queue. Task exiting.");
        heap_caps_free(g_ring_buffer.buffer);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Audio system initialized. Starting continuous monitoring...");
    ESP_LOGI(TAG, "Configuration: Sample Rate=%d Hz, Buffer=%d sec, Threshold=%lu",
             AUDIO_SAMPLE_RATE, RING_BUFFER_DURATION_SEC, g_amplitude_threshold);
    
    // Allocate temporary buffer for I2S reads
    int32_t *temp_buffer = (int32_t *)heap_caps_malloc(
        I2S_READ_CHUNK_BYTES,
        MALLOC_CAP_DMA  // Must be DMA-capable for I2S
    );
    
    if (temp_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate temporary buffer");
        vTaskDelete(NULL);
        return;
    }
    
    // Main continuous recording loop
    while (1) {
        // Check for commands (non-blocking)
        audio_cmd_t cmd;
        if (xQueueReceive(g_audio_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case AUDIO_CMD_START_RECORDING:
                    ESP_LOGI(TAG, "Manual recording triggered");
                    record_clip();
                    break;
                    
                case AUDIO_CMD_STOP_CONTINUOUS:
                    ESP_LOGI(TAG, "Stopping continuous monitoring");
                    g_is_monitoring = false;
                    break;
                    
                case AUDIO_CMD_SET_THRESHOLD:
                    g_amplitude_threshold = cmd.param;
                    ESP_LOGI(TAG, "Threshold updated to %lu", g_amplitude_threshold);
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown command type: %d", cmd.type);
                    break;
            }
        }
        
        // Skip monitoring if disabled
        if (!g_is_monitoring) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Read audio chunk from I2S
        size_t bytes_read = 0;
        ret = bsp_audio_read(temp_buffer, 
                            I2S_READ_CHUNK_BYTES, 
                            &bytes_read, 
                            100);  // 100ms timeout
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read audio: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        size_t samples_read = bytes_read / AUDIO_BYTES_PER_SAMPLE;
        
        // Write to ring buffer
        ring_buffer_write(&g_ring_buffer, temp_buffer, samples_read);
        
        // Check for audio event
        if (detect_audio_event(temp_buffer, samples_read)) {
            ESP_LOGI(TAG, "Audio event detected! Amplitude threshold exceeded.");
            
            // Record full clip (pre + post trigger)
            ret = record_clip();
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to record clip");
            }
            
            // Add cooldown to prevent multiple triggers for same event
            vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second cooldown
        }
    }
    
    // Cleanup (never reached in normal operation)
    heap_caps_free(temp_buffer);
    heap_caps_free(g_ring_buffer.buffer);
    vQueueDelete(g_audio_queue);
    vTaskDelete(NULL);
}
