# Audio Streaming Feature - Quick Reference

## What Was Added

Audio streaming capability similar to the camera's Base64 streaming for easy testing and debugging.

## Key Features

1. **Automatic streaming test on boot** - Captures 3 seconds of audio after startup
2. **Two streaming modes**:
   - Raw PCM16 (efficient, binary)
   - Base64 encoded (text-based, compatible with logs)
3. **USB serial output** - No SD card needed for testing
4. **Python capture script** - Automatically saves to WAV files

## Quick Start

### 1. Flash Firmware
```bash
cd MVP/
idf.py build flash monitor
```

### 2. Capture Audio (separate terminal)
```bash
cd tools/
python3 audio_stream_receiver.py /dev/ttyACM0
```

### 3. Make Sounds
During the 3-second capture window (starts 3 seconds after boot), make noise near the microphone.

### 4. Check Output
```bash
ls captured_audio/
# Play the WAV file
aplay captured_audio/audio_raw_*.wav  # Linux
afplay captured_audio/audio_raw_*.wav  # macOS
```

## Implementation Details

### Functions Added to `sys_audio.c`:

- `stream_audio_over_usb_base64()` - Base64 encoding + streaming
- `stream_audio_over_usb_raw()` - Raw binary streaming  
- `run_streaming_test()` - Main streaming coordinator
- `sys_audio_enable_streaming()` - Trigger streaming from other tasks

### Output Format

**Raw mode** (used by default):
```
[USB_AUDIO_RAW_BEGIN] rate=16000 channels=1 bits=16 samples=48000 bytes=96000
<96000 bytes of binary PCM16 data>
[USB_AUDIO_RAW_END]
```

## Configuration

All in `sys_audio.c`:

```c
#define AUDIO_STREAM_DURATION_MS   3000    // Stream duration
#define AUDIO_STREAM_CHUNK_SIZE    512     // Samples per read
```

To disable auto-stream on boot, comment out in `sys_audio_task()`:
```c
// run_streaming_test(false);
```

## Use Cases

✅ **Quick microphone test** - Verify hardware works  
✅ **Audio quality check** - Listen to captured audio  
✅ **Waveform analysis** - Open in Audacity  
✅ **Threshold tuning** - Analyze amplitude ranges  
✅ **Debug I2S issues** - Verify data is being captured  

## Files Modified/Created

- ✏️ `MVP/main/sys_audio.c` - Added streaming functions
- ➕ `tools/audio_stream_receiver.py` - Python capture script
- ➕ `docs/AUDIO_STREAMING_GUIDE.md` - Full documentation

## Dependencies

Python script requires:
```bash
pip install pyserial
```

## Comparison to Camera Streaming

| Feature | Camera | Audio |
|---------|--------|-------|
| Protocol | USB Serial | USB Serial |
| Format | Base64 JPEG | Base64/Raw PCM16 |
| Trigger | PIR events | On-demand/Auto |
| Duration | Single frame | 3 seconds |
| Output | Python script | Python script |

Both use the same `[USB_*_BEGIN]` / `[USB_*_END]` marker pattern for easy parsing.

