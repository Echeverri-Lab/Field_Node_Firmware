#include "bsp_halow.h"
#include "bsp_storage.h"
#include "system_app.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "SYS_COMMS";

/* ---- Configuration ------------------------------------------------ */

/*
 * Address of the upload receiver.  When testing with a laptop connected
 * to the ALFA Tube-AH, set this to the laptop's IP on the HaLow network.
 */
#define COMMS_RECEIVER_HOST  "192.168.1.100"
#define COMMS_RECEIVER_PORT  8080

/* HaLow AP credentials – must match the ALFA Tube-AH configuration */
#define COMMS_AP_SSID        "FieldNodeAP"
#define COMMS_AP_PASSWORD    "changeme123"

/* Below this voltage (≈ 20 % SoC on a 3.7 V LiPo) skip the upload */
#define COMMS_BATTERY_MIN_V  3.6f

/* Maximum files to upload in a single cycle (avoid monopolising radio) */
#define COMMS_MAX_UPLOADS_PER_CYCLE  10

/* Read-buffer size for streaming file data over HTTP */
#define COMMS_HTTP_BUF_SIZE  2048

/* ---- Helpers ------------------------------------------------------ */

/**
 * Extract just the filename from a full path,
 * e.g. "/sdcard/audio/rec_12345.wav" -> "rec_12345.wav"
 */
static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/**
 * HTTP-POST a single file to the receiver.
 * Returns ESP_OK when the server responds with 2xx.
 */
static esp_err_t upload_one_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size == 0) {
        ESP_LOGW(TAG, "Skipping empty or missing file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for reading", path);
        return ESP_FAIL;
    }

    char url[128];
    snprintf(url, sizeof(url),
             "http://%s:%d/upload", COMMS_RECEIVER_HOST, COMMS_RECEIVER_PORT);

    esp_http_client_config_t cfg = {
        .url    = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms       = 15000,
        .buffer_size      = COMMS_HTTP_BUF_SIZE,
        .buffer_size_tx   = COMMS_HTTP_BUF_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-Filename", basename_of(path));

    esp_err_t err = esp_http_client_open(client, (int)st.st_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed for %s: %s", path, esp_err_to_name(err));
        goto cleanup;
    }

    char *buf = malloc(COMMS_HTTP_BUF_SIZE);
    if (!buf) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    size_t total_sent = 0;
    while (total_sent < (size_t)st.st_size) {
        size_t to_read = (size_t)st.st_size - total_sent;
        if (to_read > COMMS_HTTP_BUF_SIZE) {
            to_read = COMMS_HTTP_BUF_SIZE;
        }
        size_t n = fread(buf, 1, to_read, f);
        if (n == 0) {
            break;
        }
        int written = esp_http_client_write(client, buf, (int)n);
        if (written < 0) {
            ESP_LOGE(TAG, "HTTP write error for %s", path);
            err = ESP_FAIL;
            free(buf);
            goto cleanup;
        }
        total_sent += (size_t)written;
    }
    free(buf);

    int content_len = esp_http_client_fetch_headers(client);
    (void)content_len;
    int status = esp_http_client_get_status_code(client);

    if (status >= 200 && status < 300) {
        ESP_LOGI(TAG, "Uploaded %s (%lu bytes, HTTP %d)",
                 basename_of(path), (unsigned long)total_sent, status);
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "Server rejected %s (HTTP %d)", basename_of(path), status);
        err = ESP_FAIL;
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(f);
    return err;
}

/* ---- Upload callback context -------------------------------------- */

typedef struct {
    int uploaded;
    int failed;
} upload_ctx_t;

/**
 * Called once for each pending file on the SD card.
 * Uploads the file and marks it as uploaded on success.
 */
static bool upload_file_cb(const char *path, void *ctx)
{
    upload_ctx_t *uc = (upload_ctx_t *)ctx;

    if (uc->uploaded >= COMMS_MAX_UPLOADS_PER_CYCLE) {
        ESP_LOGI(TAG, "Reached per-cycle upload limit (%d), deferring rest",
                 COMMS_MAX_UPLOADS_PER_CYCLE);
        return false;
    }

    esp_err_t err = upload_one_file(path);
    if (err == ESP_OK) {
        bsp_storage_mark_uploaded(path);
        uc->uploaded++;
    } else {
        uc->failed++;
        ESP_LOGW(TAG, "Will retry %s next cycle", basename_of(path));
    }
    return true;
}

/* ---- Main upload handler ------------------------------------------ */

static void handle_upload(void)
{
    float batt_v = sys_power_get_battery_voltage();
    if (batt_v > 0.0f && batt_v < COMMS_BATTERY_MIN_V) {
        ESP_LOGW(TAG, "Battery %.2fV < %.2fV threshold – skipping upload",
                 batt_v, COMMS_BATTERY_MIN_V);
        return;
    }

    ESP_LOGI(TAG, "Starting upload cycle (battery %.2fV)", batt_v);

    esp_err_t err = bsp_halow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HaLow init failed: %s", esp_err_to_name(err));
        return;
    }

    err = bsp_halow_connect(COMMS_AP_SSID, COMMS_AP_PASSWORD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HaLow connect failed: %s", esp_err_to_name(err));
        bsp_halow_deinit();
        return;
    }

    if (!bsp_halow_is_connected()) {
        ESP_LOGW(TAG, "HaLow reports disconnected after connect – aborting");
        bsp_halow_deinit();
        return;
    }

    upload_ctx_t uc = {0};
    bsp_storage_list_pending(upload_file_cb, &uc);

    ESP_LOGI(TAG, "Upload cycle complete: %d uploaded, %d failed",
             uc.uploaded, uc.failed);

    bsp_halow_disconnect();
}

/* ---- FreeRTOS task ------------------------------------------------ */

void sys_comms_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Task started on Core %d", xPortGetCoreID());

    while (1) {
        comms_msg_t msg = {0};
        if (g_comms_req_queue &&
            xQueueReceive(g_comms_req_queue, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (msg.type) {
                case COMMS_CMD_START_UPLOAD:
                    handle_upload();
                    break;
            }
        }
    }
}
