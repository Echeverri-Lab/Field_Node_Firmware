#include "bsp_env.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_ENV";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_shtc3_dev = NULL;
static bool s_ready = false;

static uint8_t shtc3_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static esp_err_t shtc3_write_cmd(uint16_t cmd) {
  uint8_t bytes[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
  return i2c_master_transmit(s_shtc3_dev, bytes, sizeof(bytes), 100);
}

esp_err_t bsp_env_init(void) {
  if (s_ready) {
    return ESP_OK;
  }

  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = BSP_I2C_PORT_NUM,
      .sda_io_num = BSP_I2C_SDA_IO,
      .scl_io_num = BSP_I2C_SCL_IO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags.enable_internal_pullup = true,
      .flags.allow_pd = false,
  };

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    return err;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = BSP_SHTC3_ADDR,
      .scl_speed_hz = 100000,
      .scl_wait_us = 0,
      .flags.disable_ack_check = 0,
  };

  err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_shtc3_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
    return err;
  }

  gpio_config_t pir_cfg = {
      .pin_bit_mask = (1ULL << BSP_PIR_IO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  err = gpio_config(&pir_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PIR GPIO init failed: %s", esp_err_to_name(err));
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(2));
  if (shtc3_write_cmd(0x3517) != ESP_OK) {
    ESP_LOGW(TAG, "SHTC3 wakeup command failed during init");
  }
  vTaskDelay(pdMS_TO_TICKS(1));
  (void)shtc3_write_cmd(0xB098);

  s_ready = true;
  ESP_LOGI(TAG, "Environment sensors initialized");
  return ESP_OK;
}

esp_err_t bsp_env_read(float *temp, float *hum) {
  if (!s_ready || !s_shtc3_dev || !temp || !hum) {
    return ESP_ERR_INVALID_STATE;
  }

  if (shtc3_write_cmd(0x3517) != ESP_OK) {
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(1));

  if (shtc3_write_cmd(0x7866) != ESP_OK) {
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(20));

  uint8_t buf[6] = {0};
  if (i2c_master_receive(s_shtc3_dev, buf, sizeof(buf), 100) != ESP_OK) {
    return ESP_FAIL;
  }

  (void)shtc3_write_cmd(0xB098);

  if (shtc3_crc8(&buf[0], 2) != buf[2] || shtc3_crc8(&buf[3], 2) != buf[5]) {
    return ESP_ERR_INVALID_CRC;
  }

  uint16_t raw_t = (uint16_t)((buf[0] << 8) | buf[1]);
  uint16_t raw_rh = (uint16_t)((buf[3] << 8) | buf[4]);

  *temp = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
  *hum = 100.0f * ((float)raw_rh / 65535.0f);
  return ESP_OK;
}

bool bsp_pir_check(void) {
  return gpio_get_level(BSP_PIR_IO) == 1;
}
