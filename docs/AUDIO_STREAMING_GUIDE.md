# Audio Streaming Testing Guide

This document explains how to use the audio streaming feature for testing the SPH0645 microphone on the Field Node.

## Overview

The audio system now includes **USB serial streaming** capabilities that allow you to:
- Test microphone functionality in real-time
- Capture audio samples for analysis
- Verify audio quality without needing SD card/storage
- Debug audio issues during development

## Streaming Modes

Two streaming modes are available:

### 1. **Raw PCM Mode** (Recommended for testing)
- Streams binary PCM16 audio data
- More efficient (no encoding overhead)
- Faster transmission
- Better for continuous testing

### 2. **Base64 Mode** (Compatible with text-based tools)
- Encodes audio as base64 text
- Useful for logging systems that expect text
- Similar to camera image streaming
- Slower due to encoding overhead

## How It Works

### Automatic Streaming on Boot

The audio task now automatically runs a **3-second streaming test** on startup:

```
1. Task starts
2. Waits 3 seconds
3. Captures 3 seconds of audio (16kHz, 16-bit PCM)
4. Streams over USB serial
5. Continues with normal event detection
```

### Output Format

#### Raw Mode Output:
```
[USB_AUDIO_RAW_BEGIN] rate=16000 channels=1 bits=16 samples=48000 bytes=96000
<binary PCM16 data>
[USB_AUDIO_RAW_END]
```

#### Base64 Mode Output:
```
[USB_AUDIO_BEGIN] rate=16000 channels=1 bits=16 samples=48000 duration_ms=3000 b64=128000
<base64 encoded PCM16 data>
[USB_AUDIO_END]
```

## Testing Methods

### Method 1: Python Script (Recommended)

We've provided a Python script that automatically captures and saves audio streams to WAV files.

#### Setup:
```bash
cd tools/
pip install pyserial
```

#### Usage:
```bash
# Linux/macOS
python3 audio_stream_receiver.py /dev/ttyACM0

# Windows
python3 audio_stream_receiver.py COM3
```

#### What it does:
1. Connects to ESP32 via USB serial
2. Monitors for audio stream markers
3. Captures streaming audio data
4. Saves to `captured_audio/audio_raw_TIMESTAMP.wav`
5. Files can be played with any audio player

### Method 2: Manual Serial Monitor

You can also use the Arduino IDE Serial Monitor or `screen`:

```bash
# Linux/macOS
screen /dev/ttyACM0 115200

# Or with minicom
minicom -D /dev/ttyACM0 -b 115200
```

You'll see the stream markers and data, but you'll need to manually extract and decode the audio.

### Method 3: Trigger Streaming Programmatically

You can trigger streaming from other tasks:

```c
// In another task or function
extern void sys_audio_enable_streaming(void);

// Trigger a streaming test
sys_audio_enable_streaming();
```

The audio task will capture and stream 3 seconds of audio on its next loop iteration.

## Testing Workflow

### Quick Test (Verify Microphone Works)

1. **Flash firmware** to ESP32-S3
2. **Open serial monitor** (115200 baud)
3. **Wait 3 seconds** after boot
4. **Make sounds** (clap, talk, whistle) during the 3-second capture
5. **Check serial output** for stream markers

Expected output:
```
I (3000) SYS_AUDIO: Starting audio streaming test (raw mode)
I (3010) BSP_AUDIO: Initializing I2S for SPH0645...
I (3015) BSP_AUDIO: I2S driver installed successfully
I (3020) SYS_AUDIO: Capturing 48000 samples (3.00 seconds)...
I (3520) SYS_AUDIO: Captured 24000/48000 samples (50.0%)
I (4020) SYS_AUDIO: Captured 48000/48000 samples (100.0%)
I (4025) SYS_AUDIO: Capture complete. Streaming over USB...
[USB_AUDIO_RAW_BEGIN] rate=16000 channels=1 bits=16 samples=48000 bytes=96000
<binary data>
[USB_AUDIO_RAW_END]
I (4150) SYS_AUDIO: Streamed 48000 samples (96000 bytes) over USB (raw)
```

### Full Test with Python Script

1. **Start the receiver script** (in another terminal):
   ```bash
   python3 tools/audio_stream_receiver.py /dev/ttyACM0
   ```

2. **Flash and run** firmware

3. **Make sounds** during capture

4. **Check the output directory**:
   ```bash
   ls -lh captured_audio/
   # Should see: audio_raw_YYYYMMDD_HHMMSS.wav
   ```

5. **Play the audio**:
   ```bash
   # Linux
   aplay captured_audio/audio_raw_*.wav
   
   # macOS
   afplay captured_audio/audio_raw_*.wav
   
   # Or open in Audacity for waveform visualization
   ```

### Verify Audio Quality

Open the WAV file in **Audacity** or any audio editor:

1. **Check waveform**:
   - Should see clear amplitude variations when sounds were made
   - Baseline should be near zero when silent

2. **Check for clipping**:
   - Waveform should not be flat at top/bottom (indicates clipping)
   - If clipping occurs, audio is too loud or gain is too high

3. **Check for noise**:
   - During silence, waveform should be relatively flat
   - Some background noise is normal
   - Excessive noise may indicate electrical interference

## Configuration

### Adjust Streaming Duration

In `sys_audio.c`:

```c
#define AUDIO_STREAM_DURATION_MS   3000    // Change to desired milliseconds
```

### Change Streaming Mode

In `sys_audio_task()`:

```c
// Raw mode (default)
run_streaming_test(false);

// Base64 mode
run_streaming_test(true);
```

### Disable Auto-Streaming on Boot

Comment out these lines in `sys_audio_task()`:

```c
// ESP_LOGI(TAG, "Running initial streaming test in 3 seconds...");
// vTaskDelay(pdMS_TO_TICKS(3000));
// run_streaming_test(false);
```

## Troubleshooting

### No Audio Stream Appears

1. **Check serial connection**:
   ```bash
   ls /dev/tty* | grep -E "(ACM|USB)"
   ```

2. **Verify baud rate**: Must be **115200**

3. **Check logs**: Look for I2S initialization errors

4. **Test I2S pins**: Verify GPIO connections match `bsp_audio.h`

### Audio is Distorted or Noisy

1. **Check power supply**: SPH0645 needs stable 3.3V
2. **Verify I2S clock speed**: Should be 16kHz
3. **Test with different sounds**: Try clapping vs speaking
4. **Check for ground loops**: Ensure proper grounding

### Python Script Doesn't Capture Audio

1. **Install dependencies**:
   ```bash
   pip install pyserial
   ```

2. **Check permissions** (Linux):
   ```bash
   sudo usermod -a -G dialout $USER
   # Logout and login again
   ```

3. **Verify port name**:
   ```bash
   python3 -m serial.tools.list_ports
   ```

### WAV File Won't Play

1. **Check file size**: Should be ~96 KB for 3 seconds
2. **Verify format**: 16kHz, 16-bit, mono PCM
3. **Try different player**: VLC, Audacity, or browser

## Advanced: Streaming Analysis

### Real-time Spectrum Analysis (Python)

```python
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

# Load captured audio
rate, data = wavfile.read('captured_audio/audio_raw_*.wav')

# Compute FFT
fft = np.fft.fft(data)
freq = np.fft.fftfreq(len(data), 1/rate)

# Plot
plt.plot(freq[:len(freq)//2], np.abs(fft[:len(fft)//2]))
plt.xlabel('Frequency (Hz)')
plt.ylabel('Magnitude')
plt.title('Audio Spectrum')
plt.show()
```

### Peak Detection

```python
# Find peaks in audio
peaks = []
threshold = 500  # Adjust based on your data

for i, sample in enumerate(data):
    if abs(sample) > threshold:
        peaks.append((i, sample))

print(f"Found {len(peaks)} peaks above threshold")
```

## Next Steps

Once audio streaming is working:

1. **Test event detection**: Make loud sounds and verify triggers
2. **Verify WAV file saving**: Check SD card for recordings
3. **Tune threshold**: Adjust `AUDIO_EVENT_THRESHOLD` for sensitivity
4. **Test ring buffer**: Verify pre-trigger capture works

## Summary

The audio streaming feature provides an easy way to:
- ✅ Verify microphone hardware is working
- ✅ Test audio quality without SD card
- ✅ Debug I2S configuration issues
- ✅ Analyze audio characteristics
- ✅ Tune event detection parameters

This significantly speeds up development and debugging!

