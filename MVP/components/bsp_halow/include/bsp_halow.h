#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

/*
 * SPI pin mapping for Wi-Fi HaLow Transceiver (Morse Micro WM6108).
 *
 * WARNING: with the current peripheral layout every XIAO D-pin is
 * already allocated (D0-D2 mic, D3 PIR, D4-D5 I2C, D6-D7 GPS,
 * D8-D10 SD card).  The only free GPIOs are:
 *     GPIO 41 (D12 / spare)
 *     GPIO 42 (D11 / MOSFET gate – relocate if needed)
 *
 * The HaLow transceiver needs at least 5 GPIOs (SCK, MOSI, MISO,
 * CS, IRQ).  You will need to resolve this pin shortage before
 * wiring the module.  Possible options:
 *   - Share the SD-card SPI bus (SPI2) and add a second CS line
 *   - Multiplex GPIOs at the board level
 *   - Free a pin by making the GPS or MOSFET optional
 *
 * The defines below are PLACEHOLDERS.  Update them once the
 * physical wiring is decided.
 */
#define BSP_HALOW_SPI_HOST    SPI3_HOST
#define BSP_HALOW_PIN_SCK     GPIO_NUM_NC   /* TBD */
#define BSP_HALOW_PIN_MOSI    GPIO_NUM_NC   /* TBD */
#define BSP_HALOW_PIN_MISO    GPIO_NUM_NC   /* TBD */
#define BSP_HALOW_PIN_CS      GPIO_NUM_NC   /* TBD */
#define BSP_HALOW_PIN_IRQ     GPIO_NUM_NC   /* TBD */
#define BSP_HALOW_PIN_RESETN  (-1)          /* Tie high if not wired */

#define BSP_HALOW_SPI_CLOCK_HZ  (10 * 1000 * 1000)  /* 10 MHz */

/**
 * Initialise SPI bus and HaLow radio hardware.
 * Call once at boot before bsp_halow_connect().
 */
esp_err_t bsp_halow_init(void);

/**
 * Associate with the HaLow AP identified by @p ssid.
 * Blocks until an IP address is obtained or an error occurs.
 */
esp_err_t bsp_halow_connect(const char *ssid, const char *password);

/** Returns true when the radio is associated and has an IP. */
bool bsp_halow_is_connected(void);

/** Disconnect from the current AP and power-save the radio. */
esp_err_t bsp_halow_disconnect(void);

/** Tear down the SPI bus and release all resources. */
esp_err_t bsp_halow_deinit(void);
