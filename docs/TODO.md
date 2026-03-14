# Firmware TODO

## Purpose

This is the live source of truth for firmware status and next work.
Use it to track what is actually done, what is blocked, and what comes next.

## Status Key

- `Complete`: code and proof exist
- `Implemented but Unverified`: code exists, proof missing
- `Partial`: meaningful work exists, some behavior missing
- `Claimed Complete but Not Supported`: prior claim overstates progress
- `Not Started`: missing or placeholder
- `Deferred`: intentionally out of current MVP path

## Top Priorities (2026-03-13)

- review `testing` and `Rachel` branch progress and merge what is real
- make the firmware build reproducible
- implement the power system firmware
- implement the orchestrator event flow

## Current Status

### Build/config

Status: `Claimed Complete but Not Supported`

- [ ] check in `sdkconfig.defaults`
- [ ] check in partition table if required
- [ ] confirm PSRAM and target settings
- [ ] capture one clean build log

### Power

Status: `Not Started`

- [ ] add a clear control path into the power task
- [ ] make the power task manage `idle`, `capture`, and `sleep` states
- [ ] read battery voltage from the ADC
- [ ] decide and document battery thresholds for normal, low, and critical voltage
- [ ] implement `check_battery()`
- [ ] reduce activity when battery is low
- [ ] block uploads when battery is too low
- [ ] force safe sleep when battery reaches the critical threshold
- [ ] handle wake sources for PIR, timer, and button
- [ ] restore the system cleanly after wake
- [ ] detect external or charging power if the hardware supports it
- [ ] support battery-only operation without USB power
- [ ] add any required firmware handshake for the watchdog / power-cut hardware

### Orchestrator

Status: `Partial`

- [ ] add a central event loop
- [ ] add a way for the orchestrator to send commands to vision, audio, and env tasks
- [ ] make the orchestrator manage the system state
- [ ] implement `load_config()`
- [ ] add SD-backed `config.json` support
- [ ] make PIR events trigger camera capture through the orchestrator
- [ ] make scheduled env sampling go through the orchestrator
- [ ] make scheduled audio recording go through the orchestrator
- [ ] make upload timing go through the orchestrator
- [ ] make GPS sync go through the orchestrator
- [ ] make low-battery handling go through the orchestrator
- [ ] connect the orchestrator to all active tasks (branch progress: `testing` wires in `sys_maint_task`)

### Storage

Status: `Claimed Complete but Not Supported`

- [ ] implement `disk_usage` (branch progress: `testing`)
- [ ] implement `find_oldest_file` (branch progress: `testing`)
- [ ] implement `delete_file` (branch progress: `testing`)
- [ ] implement `check_storage_space()`
- [ ] implement `mark_uploaded()`
- [ ] separate config/log files from media files cleanly
- [ ] track which files are pending upload vs already uploaded
- [ ] make automatic cleanup run during normal operation

### Camera

Status: `Implemented but Unverified`

- [x] `bsp_camera_init()`
- [x] `bsp_camera_capture()`
- [ ] add `set_resolution()` path if kept in MVP
- [ ] add IR LED control
- [ ] add a way for the orchestrator to tell the camera task to capture
- [ ] make the camera task save captured images cleanly to SD
- [ ] stop the camera task from triggering captures on its own
- [ ] prove one saved JPEG

### Audio

Status: `Implemented but Unverified`

- [x] `bsp_audio_init()`
- [x] read / WAV path
- [ ] add a way for the orchestrator to tell the audio task to record
- [x] `record_clip()` path
- [ ] stop the audio task from relying only on its own timing loop
- [ ] keep ring buffer path stable
- [ ] finalize how audio events trigger recordings (branch progress: `testing` lowers threshold and changes monitor cadence)
- [ ] decide whether streaming debug path stays in MVP (branch progress: `Rachel`, `testing`)
- [ ] prove one saved WAV

### Environment

Status: `Partial`

- [x] `bsp_env_read()`
- [ ] confirm actual sensor target
- [ ] add a way for the orchestrator to tell the env task to sample
- [ ] stop the env task from relying only on its own timing loop
- [ ] make env sampling write clean timestamped log entries
- [ ] prove env log output

### GPS

Status: `Partial`

- [x] `bsp_gps_get_latest_fix()`
- [ ] decide whether epoch time is required in MVP
- [ ] add GPS-based time update if required
- [ ] prove valid fix or clean no-fix path
- [ ] confirm timestamp behavior

### Maintenance

Status: `Not Started`

- [ ] add `sys_health_t`
- [ ] log system health on a regular interval (branch progress: `testing`, `sys_maint.c`)
- [ ] run storage cleanup on a regular interval (branch progress: `testing`, `sys_maint.c`)
- [ ] delete old files when storage gets too full (branch progress: `testing`, `sys_maint.c`)
- [ ] report free heap, uptime, and battery health

### Comms

Status: `Not Started`

- [ ] choose MVP transport: Wi-Fi or Wi-Fi HaLow
- [ ] bring up the wireless link
- [ ] define packet format
- [ ] send data over the chosen transport
- [ ] add retry logic
- [ ] add handshake
- [ ] fall back to store-and-forward when upload fails
- [ ] avoid connecting when battery is too low

## Rules for Updating

- Update this file only when code or evidence changes.
- Do not mark `Complete` without proof.
- Use `Implemented but Unverified` when code exists but proof does not.
- Use `Claimed Complete but Not Supported` when a prior checklist overstated reality.
- Keep cells short. Put deeper explanation in the audit doc.
