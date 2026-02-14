#include "bsp_env.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BSP_ENV";

esp_err_t bsp_env_init(void) {
  ESP_LOGI(TAG, "Initializing Sensors (I2C + PIR)...");

  /*
   * TODO: I2C Init
   * 1. Configure i2c_config_t (Master Mode)
   * 2. Call i2c_driver_install()
   */

  /*
   * TODO: PIR Init
   * 1. Configure gpio_config_t for Input
   * 2. (Optional) Setup interrupt for wakeup
   */

  return ESP_OK;
}

esp_err_t bsp_env_read(float *temp, float *hum) {
  /*
   * TODO: Implement AHT20 / SHTC3 Read Protocol
   * 1. Send Trigger Command
   * 2. Wait (vTaskDelay)
   * 3. Read bytes
   * 4. Parse math -> float
   */
  *temp = 25.0f; // Dummy
  *hum = 50.0f;  // Dummy
  return ESP_OK;
}

bool bsp_pir_check(void) { return gpio_get_level(BSP_PIR_IO); }
