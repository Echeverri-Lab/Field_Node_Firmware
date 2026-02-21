# BioMesh Field Node Firmware Architecture

## 1. System Overview

The Field Node firmware is built on **ESP-IDF** (FreeRTOS) for the ESP32-S3. It orchestrates multiple concurrent tasks to capture audio/visual data, monitor the environment, and upload data to a base station or cloud.

### Core Philosophy
- **Reliability**: Watchdogs, partition tables for OTA, and safe memory management.
- **Low Power**: Extensive use of deep sleep and light sleep modes.
- **Modularity**: Each subsystem (Audio, Vision, Comms) is an isolated FreeRTOS task (`sys_*`).
- **Hardware Abstraction**: All hardware access goes through a BSP layer (`bsp_*`).

---

## 2. Task Architecture

The system is divided into independent tasks, coordinated by a central Orchestrator.

| Task Name | Priority | Stack Size | Responsibility |
| :--- | :--- | :--- | :--- |
| **Main / Orchestrator** | High | 4KB | System init, state machine management (`IDLE` -> `CAPTURE` -> `SLEEP`), event routing. |
| **Vision Task (`sys_vision`)** | Medium | 8KB+ | Control **OV2640** (NoIR), capture JPEGs, manage IR LEDs (940nm). |
| **Audio Task (`sys_audio`)** | Real-time | 8KB | Continuous I2S recording (**SPH0645**), buffering (PSRAM Ring Buffer). |
| **Comms Task (`sys_comms`)** | Low | 6KB | **WiFi HaLow** management, Store-and-Forward upload logic. |
| **Sensors Task (`sys_env`)** | Low | 3KB | Poll I2C sensors (**AHT20**), read battery ADC. |
| **Power Task (`sys_power`)** | Critical | 2KB | PMIC management, sleep scheduling, battery protection. |
| **Maintenance Task (`sys_maint`)** | Low | 2KB | System health logging, storage retention (delete oldest). |

---

## 3. Data Flow & Communication

Tasks communicate via **FreeRTOS Primitives**:

1.  **Command Queues**:
    *   `Orchestrator` -> `Vision`: "Take Picture"
    *   `Orchestrator` -> `Comms`: "Upload Batch"
2.  **Data Ring Buffers**:
    *   `Audio Task` writes to `Audio Ring Buffer` (PSRAM).
    *   `Data Manager` reads from Buffer -> Writes to SD Card/Flash.
3.  **Event Groups**:
    *   Used for system state flags (e.g., `WIFI_CONNECTED`, `BATTERY_LOW`, `SD_CARD_MOUNTED`).

---

## 4. Hardware Abstraction Layer (HAL)

### Interfaces
*   **I2S**: **SPH0645** MEMS Microphone (`sys_audio`)
*   **SPI**: **OV2640** Camera (`sys_vision`) & SD Card (Storage)
*   **I2C**: **AHT20** Temp/Hum Sensor & PMIC (`sys_env`)
*   **UART**: **L76K** GNSS Module & Debug Console
*   **GPIO**:
    *   **Inputs**: PIR Sensor (Wakeup source)
    *   **Outputs**: IR LEDs (940nm), Status LEDs

### Directory Structure (Aligned with MVP)
```text
main/
  ├── app_main.c          # Orchestrator & Event Loop
  ├── sys_vision.c        # Vision Task
  ├── sys_audio.c         # Audio Task
  ├── sys_comms.c         # WiFi HaLow Task
  ├── sys_env.c           # Environment/Sensors Task
  ├── sys_power.c         # Power Management Task
  └── sys_maint.c         # Maintenance Task
components/
  ├── bsp_camera/         # OV2640 Driver Wrapper
  ├── bsp_audio/          # I2S/SPH0645 Driver
  ├── bsp_env/            # AHT20 & I2C Driver
  ├── bsp_gps/            # L76K / NMEA Parser
  └── bsp_storage/        # SD Card / SPIFFS Management
```
