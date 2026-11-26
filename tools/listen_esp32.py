#!/usr/bin/env python3
"""Stream INMP441 audio from the ESP32 to the Mac's default audio output."""
import argparse
import time

import numpy as np
import serial
import sounddevice as sd

SAMPLE_RATE = 16000
BLOCK_SAMPLES = 1024
BLOCK_BYTES = BLOCK_SAMPLES * 2

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/cu.usbserial-110')
parser.add_argument('--gain', type=float, default=4.0,
                    help='playback-only gain multiplier (default: 4)')
args = parser.parse_args()

if args.gain <= 0:
    parser.error('--gain must be positive')

with serial.Serial(args.port, 460800, timeout=2, write_timeout=2) as port:
    time.sleep(2)
    port.reset_input_buffer()
    port.write(b'LIVE\n')
    # Normal tuner diagnostics can arrive between the buffer reset and LIVE.
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        response = port.readline().decode('ascii', errors='replace').strip()
        if response == f'LIVE_START {SAMPLE_RATE} {BLOCK_SAMPLES}':
            break
    else:
        port.write(b'STOP\n')
        raise SystemExit('No live audio header. Check the port and direct_oled firmware.')

    print('Live mic monitoring started. Ctrl+C stops it; use headphones at low volume.')
    try:
        with sd.OutputStream(samplerate=SAMPLE_RATE, channels=1, dtype='int16',
                             blocksize=BLOCK_SAMPLES) as output_stream:
            while True:
                data = bytearray()
                while len(data) < BLOCK_BYTES:
                    chunk = port.read(BLOCK_BYTES - len(data))
                    if not chunk:
                        raise SystemExit('Audio stream timed out; check the USB connection.')
                    data.extend(chunk)
                samples = np.frombuffer(data, dtype='<i2').astype(np.float32)
                samples *= args.gain
                np.clip(samples, -32768, 32767, out=samples)
                # Write each received block directly to the system audio output.
                output_stream.write(samples.astype(np.int16))
    except KeyboardInterrupt:
        print('\nStopping live mic monitoring...')
    finally:
        port.write(b'STOP\n')
