# Evidence Workflow

This directory is for tracked proof artifacts that support entries in [TODO.md](/Users/johndoe/Desktop/Projects/FieldNodeFirmware/Field_Node_Firmware/TODO.md).

Use it to keep one small, representative bundle per test run instead of leaving proof scattered across `captures/`, screenshots, and pasted serial logs.

## Proof Levels

- `usb_monitor`: host-side artifacts captured by `tools/usb_image_monitor.py`
  - useful to prove that the firmware emitted an image or WAV
  - does **not** by itself prove the file was saved on SD card
- `sd_dump`: files copied from the device SD card
  - acceptable proof for TODO items that say "save on device"
- `serial_log`: console log showing the device saved or logged data on the SD card
  - strongest when paired with the actual file from SD

## Recommended Evidence Per TODO

- camera proof:
  - one JPEG from `/sdcard/pir/` or `/sdcard/timelapse/`
  - one serial log snippet showing the saved path
- audio proof:
  - one WAV from `/sdcard/audio/`
  - one serial log snippet showing the saved path
- env proof:
  - one `env_log.csv` line from `/sdcard/timelapse/env_log.csv`
- maintenance proof:
  - one `system_health.csv` line from `/sdcard/logs/system_health.csv`
  - if testing retention, one serial log snippet showing deletion

## Packaging Command

Package the most recent host-side monitor artifacts:

```bash
cd /Users/johndoe/Desktop/Projects/FieldNodeFirmware/Field_Node_Firmware
python3 tools/package_evidence.py 2026-03-20_usb_monitor --latest-captures --source usb_monitor --note "Latest USB monitor JPG/WAV captured during bench testing."
```

Package explicit files copied from SD:

```bash
cd /Users/johndoe/Desktop/Projects/FieldNodeFirmware/Field_Node_Firmware
python3 tools/package_evidence.py 2026-03-20_sd_dump \
  --file /path/to/pir_12345.jpg \
  --file /path/to/audio_12345.wav \
  --file /path/to/env_log.csv \
  --file /path/to/system_health.csv \
  --source sd_dump \
  --note "Representative SD-backed proof bundle."
```

Each bundle writes:

- copied artifact files
- `manifest.json` with byte size, SHA-256, and source path

## Current State

- `2026-03-20_usb_monitor/` is host-side proof only
- it is useful for capture/debug evidence
- it is **not** enough to close TODO items that explicitly require "saved on device"
