# MVP Firmware Implementation Checklist

This document is your roadmap for building the BioMesh Field Node firmware. You will work primarily in the `MVP/` directory.

The code is scaffolded, meaning the files and functions exist, but the logic inside them is missing (marked with `TODO` comments). Your job is to fill in the blanks.

---

## 🟢 Phase 1: Environment Setup

- [ ]  **Install ESP-IDF v5.x**: Follow the [Espressif Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html).
- [ ]  **Verify Build**: Open a terminal in the `MVP/` directory and run:
    ```bash
    idf.py set-target esp32s3
    idf.py build
    ```
    *Success*: The project should compile (even with empty functions) without errors.

---

## 🟡 Phase 2: Hardware Drivers (BSP)

*Goal: Get the sensors talking to the ESP32.*

### 2.1 Camera Driver (`components/bsp_camera/`)
- [ ]  **`bsp_camera.c`**: Implement `bsp_camera_init()`.
    - [ ]  Define `camera_config_t` with the pins from `bsp_camera.h`.
    - [ ]  Call `esp_camera_init()`.
- [ ]  **`bsp_camera.c`**: Implement `bsp_camera_capture()`.
    - [ ]  Call `esp_camera_fb_get()`.
    - [ ]  Return the framebuffer pointer.

### 2.2 Audio Driver (`components/bsp_audio/`)
- [ ]  **`bsp_audio.c`**: Implement `bsp_audio_init()`.
    - [ ]  Configure `i2s_config_t` for Master RX mode (16kHz, 16-bit or 32-bit).
    - [ ]  Call `i2s_driver_install()` and `i2s_set_pin()`.
- [ ]  **`bsp_audio.c`**: Implement `bsp_audio_read()`.
    - [ ]  Call `i2s_read()` to fetch data from the DMA buffer.
    - [ ]  (Optional) Shift bits if using SPH0645 (which outputs 24-bit data in a 32-bit frame).

### 2.3 Environment Sensors (`components/bsp_env/`)
- [ ]  **`bsp_env.c`**: Implement `bsp_env_init()`.
    - [ ]  Configure I2C driver (Master mode).
    - [ ]  Configure GPIO for PIR sensor (Input mode).
- [ ]  **`bsp_env.c`**: Implement `bsp_env_read()`.
    - [ ]  Write I2C commands to trigger AHT20 measurement.
    - [ ]  Read 6 bytes of data and convert to float (Temperature & Humidity).

---

## 🟠 Phase 3: System Tasks (Application Logic)

*Goal: Make the system do things (Record, Detect, Upload).*

### 3.1 Orchestrator (`main/app_main.c`)
- [ ]  **Queue Creation**: Create FreeRTOS queues for inter-task communication.
    - [ ]  `vision_req_queue`: For sending "Take Picture" commands.
    - [ ]  `audio_req_queue`: For sending "Start Recording" commands.
- [ ]  **Task Creation**: Ensure all `sys_*` tasks are pinned to the correct cores.

### 3.2 Vision Task (`main/sys_vision.c`)
- [ ]  **Wait for Trigger**: Block on `vision_req_queue` until a message arrives.
- [ ]  **Capture & Save**:
    - [ ]  Turn on IR LEDs (if night).
    - [ ]  Call `bsp_camera_capture()`.
    - [ ]  Save the framebuffer to SD Card (using standard C `fopen`/`fwrite`).
    - [ ]  Return framebuffer to driver.

### 3.3 Audio Task (`main/sys_audio.c`)
- [ ]  **Ring Buffer**: Allocate a large buffer in PSRAM to hold the last 5-10 seconds of audio.
- [ ]  **Continuous Loop**:
    - [ ]  Read from I2S.
    - [ ]  Push to Ring Buffer.
    - [ ]  **Event Detection**: Check if amplitude > Threshold.
    - [ ]  If event: Write buffer to SD Card.

### 3.4 Power Task (`main/sys_power.c`)
- [ ]  **Battery Monitor**: Periodically read ADC to check battery voltage.
- [ ]  **Sleep Logic**:
    - [ ]  If battery < 3.3V, force Deep Sleep.
    - [ ]  If system is idle, coordinate with Orchestrator to enter Light/Deep Sleep.

---

## 🔴 Phase 4: Verification

- [ ]  **Bench Test**: Run the firmware on the breadboard.
- [ ]  **Verify Outputs**:
    - [ ]  Check SD Card for saved images.
    - [ ]  Check SD Card for `.wav` files.
    - [ ]  Check Serial Monitor for sensor logs.
