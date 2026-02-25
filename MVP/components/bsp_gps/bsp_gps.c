#include "bsp_gps.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define BSP_GPS_UART    UART_NUM_1
#define BSP_GPS_TX_PIN  GPIO_NUM_43  // D6
#define BSP_GPS_RX_PIN  GPIO_NUM_44  // D7

static const char *TAG = "BSP_GPS";
static bool s_ready = false;

static bool parse_nmea_lat_lon(const char *lat_s, const char *ns,
                               const char *lon_s, const char *ew,
                               float *lat_out, float *lon_out) {
  if (!lat_s || !ns || !lon_s || !ew || !lat_out || !lon_out ||
      lat_s[0] == '\0' || lon_s[0] == '\0') {
    return false;
  }

  float lat_raw = strtof(lat_s, NULL);
  float lon_raw = strtof(lon_s, NULL);

  float lat_deg = floorf(lat_raw / 100.0f);
  float lon_deg = floorf(lon_raw / 100.0f);
  float lat_min = lat_raw - (lat_deg * 100.0f);
  float lon_min = lon_raw - (lon_deg * 100.0f);

  float lat = lat_deg + (lat_min / 60.0f);
  float lon = lon_deg + (lon_min / 60.0f);

  if (ns[0] == 'S') {
    lat = -lat;
  }
  if (ew[0] == 'W') {
    lon = -lon;
  }

  *lat_out = lat;
  *lon_out = lon;
  return true;
}

static void parse_rmc_line(const char *line, bsp_gps_fix_t *fix) {
  char copy[160] = {0};
  strncpy(copy, line, sizeof(copy) - 1);

  char *save = NULL;
  char *token = strtok_r(copy, ",", &save);
  int idx = 0;
  const char *status = NULL;
  const char *lat = NULL;
  const char *ns = NULL;
  const char *lon = NULL;
  const char *ew = NULL;

  while (token) {
    if (idx == 2) status = token;
    if (idx == 3) lat = token;
    if (idx == 4) ns = token;
    if (idx == 5) lon = token;
    if (idx == 6) ew = token;
    token = strtok_r(NULL, ",", &save);
    idx++;
  }

  if (status && status[0] == 'A' &&
      parse_nmea_lat_lon(lat, ns, lon, ew, &fix->latitude, &fix->longitude)) {
    fix->valid = true;
  }
}

esp_err_t bsp_gps_init(void) {
  if (s_ready) {
    return ESP_OK;
  }

  uart_config_t cfg = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_driver_install(BSP_GPS_UART, 4096, 0, 0, NULL, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_ERROR_CHECK(uart_param_config(BSP_GPS_UART, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(BSP_GPS_UART, BSP_GPS_TX_PIN, BSP_GPS_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  s_ready = true;
  ESP_LOGI(TAG, "GPS UART initialized");
  return ESP_OK;
}

esp_err_t bsp_gps_get_latest_fix(uint32_t timeout_ms, bsp_gps_fix_t *fix) {
  if (!s_ready || !fix) {
    return ESP_ERR_INVALID_STATE;
  }

  memset(fix, 0, sizeof(*fix));

  uint8_t ch = 0;
  char line[160] = {0};
  size_t pos = 0;
  int64_t start_ms = esp_timer_get_time() / 1000;

  while ((esp_timer_get_time() / 1000) - start_ms < (int64_t)timeout_ms) {
    int n = uart_read_bytes(BSP_GPS_UART, &ch, 1, pdMS_TO_TICKS(20));
    if (n <= 0) {
      continue;
    }

    if (ch == '\n') {
      if (pos > 0 && line[pos - 1] == '\r') {
        line[pos - 1] = '\0';
      } else {
        line[pos] = '\0';
      }

      if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) {
        strncpy(fix->raw, line, sizeof(fix->raw) - 1);
        parse_rmc_line(line, fix);
        return ESP_OK;
      }

      pos = 0;
      memset(line, 0, sizeof(line));
      continue;
    }

    if (pos < sizeof(line) - 1) {
      line[pos++] = (char)ch;
    }
  }

  return ESP_OK;
}
