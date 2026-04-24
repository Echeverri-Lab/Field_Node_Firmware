#include "bsp_halow.h"

#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "BSP_HALOW";

/*
 * Set BSP_HALOW_STUB to 0 once the Morse Micro MM-IoT-SDK is integrated
 * and you are ready to drive real hardware.
 */
#define BSP_HALOW_STUB 1

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */
static bool s_initialised = false;
static bool s_connected   = false;

#if BSP_HALOW_STUB
/* ================================================================== */
/*  STUB implementation  –  no real radio, logs every call             */
/* ================================================================== */

esp_err_t bsp_halow_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "[STUB] init – SPI bus and radio NOT initialised (stub mode)");
    s_initialised = true;
    return ESP_OK;
}

esp_err_t bsp_halow_connect(const char *ssid, const char *password)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "[STUB] connect to '%s' – simulated (no real radio)", ssid ? ssid : "");
    s_connected = true;
    return ESP_OK;
}

bool bsp_halow_is_connected(void)
{
    return s_connected;
}

esp_err_t bsp_halow_disconnect(void)
{
    ESP_LOGW(TAG, "[STUB] disconnect – simulated");
    s_connected = false;
    return ESP_OK;
}

esp_err_t bsp_halow_deinit(void)
{
    ESP_LOGW(TAG, "[STUB] deinit – simulated");
    s_connected   = false;
    s_initialised = false;
    return ESP_OK;
}

#else
/* ================================================================== */
/*  REAL implementation  –  Morse Micro MM-IoT-SDK                     */
/* ================================================================== */
/*
 * TODO: integrate MM-IoT-SDK here.  The typical flow is:
 *
 *   1.  spi_bus_initialize(BSP_HALOW_SPI_HOST, ...)
 *   2.  spi_bus_add_device(...)  – CS, clock, mode for WM6108
 *   3.  mmhal_init()             – register SPI read/write callbacks
 *   4.  mmwlan_init()            – bring up 802.11ah MAC
 *   5.  mmwlan_sta_enable()      – enter station mode
 *   6.  mmwlan_connect(ssid, password)
 *   7.  Wait for MMWLAN_EVENT_CONNECTED + IP via DHCP
 *
 *  The HAL callbacks (mmhal_spi_read, mmhal_spi_write, mmhal_irq_*,
 *  mmhal_sleep_ms) must be mapped to ESP-IDF SPI master and GPIO ISR
 *  APIs.  See the MM-IoT-SDK porting guide for details.
 */

static spi_device_handle_t s_spi_dev = NULL;

esp_err_t bsp_halow_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = BSP_HALOW_PIN_MOSI,
        .miso_io_num   = BSP_HALOW_PIN_MISO,
        .sclk_io_num   = BSP_HALOW_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(BSP_HALOW_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = BSP_HALOW_SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = BSP_HALOW_PIN_CS,
        .queue_size     = 4,
    };
    err = spi_bus_add_device(BSP_HALOW_SPI_HOST, &dev_cfg, &s_spi_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(err));
        return err;
    }

    if (BSP_HALOW_PIN_RESETN >= 0) {
        gpio_set_direction((gpio_num_t)BSP_HALOW_PIN_RESETN, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)BSP_HALOW_PIN_RESETN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)BSP_HALOW_PIN_RESETN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /*
     * TODO: call mmhal_init() / mmwlan_init() here to bring up the
     * Morse Micro firmware.  Register SPI HAL callbacks, configure
     * the IRQ pin, etc.
     */
    ESP_LOGE(TAG, "MM-IoT-SDK not yet integrated – cannot init radio");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_halow_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    ESP_LOGE(TAG, "MM-IoT-SDK not yet integrated – cannot connect");
    return ESP_ERR_NOT_SUPPORTED;
}

bool bsp_halow_is_connected(void)
{
    return s_connected;
}

esp_err_t bsp_halow_disconnect(void)
{
    s_connected = false;
    return ESP_OK;
}

esp_err_t bsp_halow_deinit(void)
{
    if (s_spi_dev) {
        spi_bus_remove_device(s_spi_dev);
        s_spi_dev = NULL;
    }
    spi_bus_free(BSP_HALOW_SPI_HOST);
    s_initialised = false;
    s_connected   = false;
    return ESP_OK;
}

#endif /* BSP_HALOW_STUB */
