# Development Tools

## USB Visualizer

This project includes a serial visualizer that decodes base64 image/audio blocks from the device and writes live files to `captures/`.

### Setup

```bash
python3 -m pip install pyserial
```

### Run (recommended)

From the repository root:

```bash
python3 tools/usb_image_monitor.py --port /dev/cu.usbmodem101 --baud 115200 --live-web --out captures --keep-all --no-open
```

### Notes

- Open `http://127.0.0.1:8765/` if the browser does not auto-open.
- `latest.jpg` and `latest.wav` are updated in `captures/`.
- Do not run `idf monitor` at the same time on the same serial port.
