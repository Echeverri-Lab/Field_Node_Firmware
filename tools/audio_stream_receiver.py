#!/usr/bin/env python3
"""
Audio Stream Receiver for Field Node Testing

This script listens to USB serial output from the ESP32 and captures
streamed audio data, saving it to WAV files for analysis.

Usage:
    python3 audio_stream_receiver.py /dev/ttyACM0
    python3 audio_stream_receiver.py COM3  (Windows)

The script supports two modes:
1. Base64 mode: Audio encoded as base64 text
2. Raw mode: Binary PCM16 data (more efficient)
"""

import sys
import serial
import base64
import wave
import struct
import time
from datetime import datetime
from pathlib import Path

class AudioStreamReceiver:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.output_dir = Path("captured_audio")
        self.output_dir.mkdir(exist_ok=True)
        
    def connect(self):
        """Connect to the serial port"""
        print(f"Connecting to {self.port} at {self.baudrate} baud...")
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Connected successfully")
            return True
        except Exception as e:
            print(f"Failed to connect: {e}")
            return False
    
    def parse_header(self, line):
        """Parse audio metadata from header line"""
        metadata = {}
        parts = line.split()
        for part in parts:
            if '=' in part:
                key, value = part.split('=')
                try:
                    metadata[key] = int(value)
                except ValueError:
                    metadata[key] = value
        return metadata
    
    def save_wav(self, pcm_data, sample_rate, channels, bits_per_sample, filename):
        """Save PCM data as a WAV file"""
        with wave.open(filename, 'wb') as wav_file:
            wav_file.setnchannels(channels)
            wav_file.setsampwidth(bits_per_sample // 8)
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(pcm_data)
        print(f"Saved: {filename} ({len(pcm_data)} bytes)")
    
    def process_base64_stream(self, metadata):
        """Process base64-encoded audio stream"""
        print(f"Receiving base64 audio: {metadata}")
        
        # Read base64 data until end marker
        b64_data = b""
        while True:
            line = self.serial.readline()
            if b"[USB_AUDIO_END]" in line:
                break
            b64_data += line.strip()
        
        # Decode base64
        try:
            pcm_data = base64.b64decode(b64_data)
            print(f"Decoded {len(pcm_data)} bytes of PCM data")
            
            # Generate filename
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = self.output_dir / f"audio_base64_{timestamp}.wav"
            
            # Save WAV file
            self.save_wav(
                pcm_data,
                metadata.get('rate', 16000),
                metadata.get('channels', 1),
                metadata.get('bits', 16),
                str(filename)
            )
            return True
        except Exception as e:
            print(f"Error decoding base64 audio: {e}")
            return False
    
    def process_raw_stream(self, metadata):
        """Process raw binary PCM stream"""
        print(f"Receiving raw audio: {metadata}")
        
        expected_bytes = metadata.get('bytes', 0)
        
        # Read raw binary data
        pcm_data = b""
        bytes_read = 0
        
        while bytes_read < expected_bytes:
            chunk = self.serial.read(min(4096, expected_bytes - bytes_read))
            if not chunk:
                time.sleep(0.01)
                continue
            pcm_data += chunk
            bytes_read += len(chunk)
            
            # Progress indicator
            if bytes_read % 10000 == 0:
                progress = (bytes_read / expected_bytes) * 100
                print(f"Progress: {bytes_read}/{expected_bytes} bytes ({progress:.1f}%)")
        
        # Read until end marker
        while True:
            line = self.serial.readline()
            if b"[USB_AUDIO_RAW_END]" in line:
                break
        
        print(f"Received {len(pcm_data)} bytes of PCM data")
        
        # Generate filename
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = self.output_dir / f"audio_raw_{timestamp}.wav"
        
        # Save WAV file
        self.save_wav(
            pcm_data,
            metadata.get('rate', 16000),
            metadata.get('channels', 1),
            metadata.get('bits', 16),
            str(filename)
        )
        return True
    
    def listen(self):
        """Main listening loop"""
        print(f"Listening for audio streams...")
        print(f"Audio files will be saved to: {self.output_dir.absolute()}")
        print("Press Ctrl+C to stop\n")
        
        try:
            while True:
                line = self.serial.readline()
                
                # Print regular log lines
                if line:
                    try:
                        decoded = line.decode('utf-8', errors='ignore').strip()
                        
                        # Check for audio stream markers
                        if "[USB_AUDIO_BEGIN]" in decoded:
                            metadata = self.parse_header(decoded)
                            self.process_base64_stream(metadata)
                        
                        elif "[USB_AUDIO_RAW_BEGIN]" in decoded:
                            metadata = self.parse_header(decoded)
                            self.process_raw_stream(metadata)
                        
                        else:
                            # Print regular log output
                            print(decoded)
                    
                    except Exception as e:
                        print(f"Error processing line: {e}")
                
        except KeyboardInterrupt:
            print("\nStopping...")
        finally:
            if self.serial:
                self.serial.close()
                print("Serial port closed")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 audio_stream_receiver.py <serial_port> [baudrate]")
        print("Example (Linux): python3 audio_stream_receiver.py /dev/ttyACM0")
        print("Example (macOS): python3 audio_stream_receiver.py /dev/cu.usbmodem*")
        print("Example (Windows): python3 audio_stream_receiver.py COM3")
        print("\nDefault baudrate: 115200")
        sys.exit(1)
    
    port = sys.argv[1]  # Fixed: was sys.argv[0]
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    
    receiver = AudioStreamReceiver(port, baudrate)
    if receiver.connect():
        receiver.listen()

if __name__ == "__main__":
    main()

