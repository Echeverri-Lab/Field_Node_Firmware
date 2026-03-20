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

Status: `Partial`

Goal: another teammate should be able to clone the repo, open `MVP/`, and build the same firmware without relying on hidden local settings.

- [x] check in `sdkconfig.defaults` so the repo defines the baseline ESP-IDF build settings
- [x] confirm whether a custom `partitions.csv` is required; current build uses ESP-IDF's built-in `partitions_singleapp.csv`
- [x] check in config that targets `esp32s3` with PSRAM enabled so camera and audio buffering use the intended hardware setup
- local reference build log: `docs/build_logs/2026-03-20_mvp_build.log` (useful, but not a clean-checkout proof)
- [ ] prove that a second teammate can build successfully from a clean checkout using the checked-in config
- [ ] save one successful clean build log from that clean-checkout build

### Power

Status: `Not Started`

- [ ] add a way for the orchestrator to tell the power task to stay awake, enter capture mode, or go to sleep
- [ ] implement a power state machine with `boot`, `idle`, `capture`, `upload`, `survival`, and `sleep` states
- [ ] define each power state clearly:
  - [ ] `boot` = bring up hardware and decide whether normal operation can start
  - [ ] `idle` = awake briefly to check system state and decide whether to capture, upload, or return to sleep
  - [ ] `capture` = sensors are actively collecting data
  - [ ] `upload` = radio and transfer path are active
  - [ ] `survival` = stay running but reduce work when the battery or solar input is too weak for normal operation
  - [ ] `sleep` = true low-power standby until PIR, timer, or button wake occurs
- [ ] read battery voltage from the ADC
- [ ] define and document battery thresholds for normal, low, and critical voltage
- [ ] implement `check_battery()`
- [ ] slow down capture, sampling, and uploads when battery is low
- [ ] block uploads when battery is too low
- [ ] force safe sleep when battery reaches the critical threshold
- [ ] configure wake sources for PIR, timer, and button
- [ ] restore SD, sensors, and task state cleanly after wake
- [ ] detect charging or external power in firmware if the hardware exposes that signal
- [ ] support battery-only operation without USB power
- [ ] add any required firmware handshake for the watchdog / power-cut hardware

### Orchestrator

Status: `Partial`

- [ ] add one central event loop that receives PIR, timer, audio, GPS, power, and comms events
- [ ] add command paths from the orchestrator to camera, audio, env, GPS, power, and comms tasks
- [ ] track the current system mode in one place
- [ ] implement `load_config()` to read settings at startup
- [ ] add SD-backed `config.json` support
- [ ] make PIR events trigger camera capture through the orchestrator
- [ ] make scheduled env sampling go through the orchestrator
- [ ] make scheduled audio recording go through the orchestrator
- [ ] make scheduled uploads start from orchestrator timers
- [ ] make GPS time-sync requests start from the orchestrator
- [ ] make low-battery events change system behavior through the orchestrator
- [ ] connect every active task to orchestrator events and commands (branch progress: `testing` wires in `sys_maint_task`)

### Storage

Status: `Partial`

- [x] implement `disk_usage`
- [x] implement `find_oldest_file`
- [x] implement `delete_file`
- [x] implement periodic storage free-space enforcement (`sys_maint.c` enforces a 100 MB free-space floor)
- [x] implement `mark_uploaded()` so uploaded files can be clearly marked with a `.upl` suffix
- [ ] store config, logs, photos, and audio in separate SD directories (logs/photos/audio exist; config storage is still missing)
- [ ] track which files still need upload vs which files are already uploaded
- [x] run retention cleanup automatically during normal operation

### Camera

Status: `Implemented but Unverified`

- [x] `bsp_camera_init()` initializes the camera hardware so the node can use the sensor
- [x] `bsp_camera_capture()` captures one image frame from the camera
- [ ] decide whether variable image resolution is required in MVP and implement it if needed
- [ ] add IR LED control
- [ ] add a way for the orchestrator to tell the camera task to capture
- [x] save each capture to a unique JPEG file on SD
- [ ] stop the camera task from triggering captures on its own
- [ ] save one JPEG on device and record the proof log or artifact

### Audio

Status: `Implemented but Unverified`

- [x] `bsp_audio_init()` initializes the microphone and audio interface so recording can start
- [x] audio capture to WAV path
- [ ] add a way for the orchestrator to tell the audio task to record
- [x] `record_clip()` path records an audio clip and writes it to a WAV file
- [ ] make the audio task wait for orchestrator record commands instead of relying only on its own timing loop
- [ ] verify the ring buffer keeps the expected pre-trigger audio
- [ ] choose one audio trigger rule and make recordings start reliably from it
- [ ] decide whether USB audio streaming remains part of MVP debugging
- [ ] save one WAV on device and record the proof log or artifact

### Environment

Status: `Partial`

- [x] `bsp_env_read()` reads the current environmental sensor values for logging
- [x] decide whether firmware should target `SHTC3` or `AHT20` and align code/docs (`SHTC3` is the current implementation)
- [ ] add a way for the orchestrator to tell the env task to sample
- [ ] make the env task wait for orchestrator sample commands instead of relying only on its own timing loop
- [x] make env sampling write clean timestamped log entries to `env_log.csv`
- [ ] save one env log entry and record the proof log or artifact

### GPS

Status: `Partial`

- [x] `bsp_gps_get_latest_fix()` returns the most recent GPS fix data from the receiver
- [ ] decide whether GPS must provide epoch time in MVP
- [ ] add GPS-based time update if epoch time is required
- [ ] capture one log entry that shows a valid GPS fix with latitude and longitude
- [ ] capture one log entry that shows an explicit no-fix result when GPS has no signal
- [ ] use one timestamp source and one timestamp format for GPS logs, env logs, image filenames, and audio filenames

### Maintenance

Status: `Partial`

- [ ] add `sys_health_t`
- [x] log system health on a regular interval
- [x] run storage cleanup on a regular interval
- [x] delete old files when storage gets too full
- [ ] include free heap, uptime, and battery health in the health log

### Comms

Status: `Not Started`

- [ ] choose MVP transport: Wi-Fi or Wi-Fi HaLow
- [ ] connect the field node to the chosen wireless link
- [ ] define the minimal packet format for MVP uploads
- [ ] send one payload over the chosen transport
- [ ] add retry logic
- [ ] implement a basic handshake or ping/ack with the receiver
- [ ] keep data on SD for later upload when a send fails
- [ ] skip wireless connect attempts when battery is too low

## Rules for Updating

- Update this file only when code or evidence changes.
- Do not mark `Complete` without proof.
- Use `Implemented but Unverified` when code exists but proof does not.
- Use `Claimed Complete but Not Supported` when a prior checklist overstated reality.
- Keep cells short. Put deeper explanation in the audit doc.
