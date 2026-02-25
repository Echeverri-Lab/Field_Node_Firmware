#include "bsp_audio.h"

#include <stdbool.h>

#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "BSP_AUDIO";

static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_ready = false;

esp_err_t bsp_audio_init(void) {
  if (s_ready) {
    return ESP_OK;
  }

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 4;
  chan_cfg.dma_frame_num = 1024;

  esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
    return err;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BSP_AUDIO_RATE_HZ),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = BSP_AUDIO_BCLK_IO,
          .ws = BSP_AUDIO_WS_IO,
          .dout = I2S_GPIO_UNUSED,
          .din = BSP_AUDIO_DIN_IO,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
  if (err != ESP_OK) {
    i2s_del_channel(s_rx_chan);
    s_rx_chan = NULL;
    ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
    return err;
  }

  err = i2s_channel_enable(s_rx_chan);
  if (err != ESP_OK) {
    i2s_del_channel(s_rx_chan);
    s_rx_chan = NULL;
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    return err;
  }

  s_ready = true;
  ESP_LOGI(TAG, "I2S microphone initialized");
  return ESP_OK;
}

esp_err_t bsp_audio_read(void *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms) {
  if (!s_ready || !s_rx_chan) {
    return ESP_ERR_INVALID_STATE;
  }
  return i2s_channel_read(s_rx_chan, dest, len, bytes_read, timeout_ms);
}

void bsp_audio_deinit(void) {
  if (!s_rx_chan) {
    s_ready = false;
    return;
  }
  i2s_channel_disable(s_rx_chan);
  i2s_del_channel(s_rx_chan);
  s_rx_chan = NULL;
  s_ready = false;
}
