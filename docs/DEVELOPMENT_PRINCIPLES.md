# Development Principles

These are the core principles guiding the development of the Field Node Firmware.

### KISS (Keep It Simple, Stupid)
- Keep FreeRTOS tasks small and single-purpose.
- Don't over-optimize drivers or memory usage early; simplicity aids debugging.
- Use straightforward state machines for the Orchestrator.

### DRY (Don't Repeat Yourself)
- Centralize shared definitions:
  - Pin mappings (in `main/include/board_config.h` or similar).
  - Error codes and logging macros.
  - Common data structures (e.g., sensor reading structs) used across tasks.
- Avoid duplicating hardware initialization logic across different tasks.

### YAGNI (You Aren't Gonna Need It)
- Delay advanced features until basic functionality is proven and metrics justify them.
- Examples of what to delay: Remote OTA updates, complex mesh networking, advanced power profiling, predictive sleep scheduling.
- Focus on the MVP: Reliable data capture and storage/transmission.

### SOLID-ish Modularity
- **Single Responsibility**: Each `sys_*` task has one job (e.g., `sys_vision` handles the camera, `sys_comms` handles WiFi).
- **Interface Segregation**: Use the Board Support Package (BSP) layer (`components/bsp_*`) to abstract hardware specifics.
- **Dependency Injection**: Configure drivers via structs passed at initialization rather than hardcoding values inside the driver.

### Separation of Concerns
- **Orchestrator**: Coordinates high-level flow and data movement; does *not* talk to hardware directly.
- **Drivers/BSP**: Handle low-level register access and protocol specifics (I2C, SPI, I2S); do *not* contain business logic.
- **Data Types**: Use typed DTOs (Data Transfer Objects) for inter-task communication (Queues/Ring Buffers).

### Fail Fast + Validate Hard
- **Early Exits**: Check `esp_err_t` returns immediately; don't proceed if initialization fails.
- **Validation**:
  - Validate sensor ranges (e.g., reject humidity > 100%).
  - Verify SD card mounts and file integrity before writing.
  - If a critical check fails, log an error and enter a safe state or restart (Watchdog).

### Least Surprise
- **Deterministic Defaults**:
  - Predictable startup sequence (Power -> Init -> Scheduler).
  - Stable default configurations (e.g., safe transmit power, standard sample rates).
- **Logging**: Consistent log levels (INFO for state changes, ERROR for failures) and tags.
- **Routing**: Predictable data paths (Sensor -> Queue -> Storage -> Comms).
