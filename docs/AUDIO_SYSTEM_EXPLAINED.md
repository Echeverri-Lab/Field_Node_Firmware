# Audio System Explanation: Streaming vs SD Card Recording

## Overview: Two Separate Systems

Your Field Node has **TWO INDEPENDENT** audio capture systems:

```
┌─────────────────────────────────────────────────────────┐
│                  ESP32 Audio System                      │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  1. STREAMING (Testing Mode)                            │
│     ┌────────┐   USB    ┌──────────┐   WAV   ┌──────┐ │
│     │ SPH0645│ ─────→   │  Serial  │ ─────→  │  PC  │ │
│     └────────┘          └──────────┘         └──────┘ │
│     Purpose: Quick testing on your local machine        │
│     Destination: YOUR PC (not ESP32 SD card)           │
│                                                          │
│  2. EVENT RECORDING (Production Mode)                   │
│     ┌────────┐   Event   ┌──────────┐   WAV   ┌──────┐│
│     │ SPH0645│ ─────→    │  Buffer  │ ─────→  │SD Card││
│     └────────┘           └──────────┘         └──────┘│
│     Purpose: Long-term field deployment                 │
│     Destination: ESP32 SD card (not your PC)           │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

---

## 1️⃣ STREAMING MODE (What You Want for Testing)

### Purpose
**Quick microphone testing without needing SD card or removing it to check files.**

### How It Works

```
Step 1: ESP32 boots
   ↓
Step 2: Wait 3 seconds (system stabilization)
   ↓
Step 3: Capture 3 seconds of audio from SPH0645
   ↓
Step 4: Convert to PCM16 format
   ↓
Step 5: Send over USB serial with markers:
        [USB_AUDIO_RAW_BEGIN] rate=16000 ...
        <binary audio data>
        [USB_AUDIO_RAW_END]
   ↓
Step 6: Python script on YOUR PC receives data
   ↓
Step 7: Python saves WAV file on YOUR PC:
        captured_audio/audio_raw_TIMESTAMP.wav
```

### Key Points
- ✅ **DOES** send audio to your PC via USB
- ✅ **DOES** require Python script running on your PC
- ✅ **DOES** save files on your local machine
- ❌ **DOES NOT** save to ESP32 SD card
- ❌ **DOES NOT** require SD card to be present
- ✅ Perfect for quick testing and debugging

### Code Location
File: `MVP/main/sys_audio.c`

Functions:
- `run_streaming_test()` - Main streaming coordinator
- `stream_audio_over_usb_raw()` - Sends binary PCM data
- `stream_audio_over_usb_base64()` - Sends base64 encoded data

Triggered at line 520 in `sys_audio_task()`:
```c
// Run streaming test on startup for easy testing
ESP_LOGI(TAG, "Running initial streaming test in 3 seconds...");
vTaskDelay(pdMS_TO_TICKS(3000));
run_streaming_test(false);  // false = raw mode, true = base64
```

### Output Format (Raw Mode)
```
[USB_AUDIO_RAW_BEGIN] rate=16000 channels=1 bits=16 samples=48000 bytes=96000
<96000 bytes of binary PCM16 data>
[USB_AUDIO_RAW_END]
```

---

## 2️⃣ EVENT RECORDING MODE (Production)

### Purpose
**Field deployment - automatically record audio when loud sounds are detected.**

### How It Works

```
Step 1: System runs for hours in monitoring mode
   ↓
Step 2: Ring buffer continuously stores last 5 seconds
   ↓
Step 3: Event detected (amplitude > threshold)
   ↓
Step 4: Extract 5 seconds from ring buffer (pre-trigger)
   ↓
Step 5: Capture 3 more seconds (post-trigger)
   ↓
Step 6: Combine into 8-second clip
   ↓
Step 7: Save to ESP32 SD card:
        /sdcard/audio/audio_NNNN.wav
```

### Key Points
- ✅ **DOES** save to ESP32 SD card
- ✅ **DOES** require SD card to be present
- ✅ **DOES** include pre-trigger audio (5 sec before event)
- ❌ **DOES NOT** send data to your PC
- ❌ **DOES NOT** require Python script
- ✅ Perfect for unattended field deployment

### Code Location
File: `MVP/main/sys_audio.c`

Functions:
- `run_monitor_cycle()` - Continuous monitoring loop
- `detect_audio_event()` - Checks if sound is loud enough
- `record_triggered_clip()` - Captures pre+post trigger audio
- `save_clip_to_wav()` - Writes to SD card

Runs periodically (default: every 2 hours) from `sys_audio_task()`.

---

## Comparison Table

| Feature | **Streaming** | **Event Recording** |
|---------|---------------|---------------------|
| **Trigger** | Automatic on boot | Sound event detected |
| **Duration** | 3 seconds | 8 seconds (5+3) |
| **Destination** | Your PC via USB | ESP32 SD card |
| **Requires SD Card** | ❌ No | ✅ Yes |
| **Requires Python Script** | ✅ Yes | ❌ No |
| **Pre-trigger Buffer** | ❌ No | ✅ Yes (5 sec) |
| **Purpose** | Testing/debugging | Field deployment |
| **When to Use** | Development | Production |

---

## Your Use Case: Testing

For **quick microphone testing**, you want **STREAMING MODE**:

### Setup

1. **Flash firmware** to ESP32:
   ```bash
   cd MVP/
   idf.py build flash
   ```

2. **Open Python receiver** (in separate terminal):
   ```bash
   cd tools/
   python3 audio_stream_receiver.py /dev/ttyACM0
   ```

3. **Monitor output**:
   - ESP32 boots
   - Waits 3 seconds
   - Captures 3 seconds of audio
   - Streams to your PC
   - Python saves to `captured_audio/audio_raw_TIMESTAMP.wav`

4. **Test the audio**:
   ```bash
   # Play captured file on your local machine
   aplay captured_audio/audio_raw_*.wav  # Linux
   afplay captured_audio/audio_raw_*.wav  # macOS
   ```

### No SD Card Needed!
The streaming mode **completely bypasses** the SD card system. You can test the microphone without:
- ❌ SD card inserted
- ❌ Removing SD card to check files
- ❌ SD card file system setup
- ❌ Storage driver initialization

---

## Data Flow Diagrams

### Streaming Mode Data Flow
```
┌──────────┐
│ SPH0645  │ Microphone captures sound
│   Mic    │
└────┬─────┘
     │ I2S Digital Audio (32-bit frames @ 16kHz)
     ↓
┌────┴──────────┐
│  ESP32 I2S    │ DMA buffer receives audio
│    Driver     │
└────┬──────────┘
     │ bsp_audio_read()
     ↓
┌────┴──────────┐
│  sys_audio.c  │ Captures 3 seconds in buffer
│  streaming    │ Converts to PCM16
└────┬──────────┘
     │ stream_audio_over_usb_raw()
     ↓
┌────┴──────────┐
│  USB Serial   │ Binary data with markers
│   (stdout)    │
└────┬──────────┘
     │ Serial connection
     ↓
┌────┴──────────┐
│  Python Script│ Receives and decodes
│  (Your PC)    │
└────┬──────────┘
     │ Writes WAV file
     ↓
┌────┴──────────┐
│  Local Disk   │ audio_raw_TIMESTAMP.wav
│  (Your PC)    │ ← YOU LISTEN TO THIS FILE
└───────────────┘
```

### Event Recording Data Flow
```
┌──────────┐
│ SPH0645  │
│   Mic    │
└────┬─────┘
     │ I2S
     ↓
┌────┴──────────┐
│  sys_audio.c  │ Continuous monitoring
│  Ring Buffer  │ Stores last 5 seconds
└────┬──────────┘
     │ Loud sound detected!
     ↓
┌────┴──────────┐
│ record_clip() │ Extracts 5 sec pre-trigger
│               │ Captures 3 sec post-trigger
└────┬──────────┘
     │ save_clip_to_wav()
     ↓
┌────┴──────────┐
│  SD Card      │ /sdcard/audio/audio_NNNN.wav
│  (ESP32)      │ ← FILE STAYS ON DEVICE
└───────────────┘
```

---

## Why Two Systems?

### During Development (NOW)
You need **streaming** because:
- Quick iteration: Hear results in seconds
- No SD card needed
- Easy debugging: Check waveforms in Audacity
- Test different sounds and thresholds

### During Deployment (LATER)
You need **event recording** because:
- Unattended operation
- Local storage (no PC connection)
- Pre-trigger capture (context before event)
- Long-term data collection

---

## Summary

### Your Question: Does streaming save to SD card?
**Answer: NO!** Streaming sends audio to your PC via USB serial. The Python script saves it to your local machine.

### How to Use Streaming for Testing
1. Flash firmware
2. Run Python script: `python3 audio_stream_receiver.py /dev/ttyACM0`
3. Wait for capture (happens automatically 3 sec after boot)
4. Audio saves to `captured_audio/` **on your PC**
5. Play/analyze the WAV file **on your PC**

### Both Systems Coexist
- Streaming runs once at boot (for testing)
- Event recording runs periodically (for production)
- They don't interfere with each other
- You can use both or disable either one

The streaming feature is exactly what you want for quick microphone testing! 🎉

