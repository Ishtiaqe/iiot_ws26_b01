#!/usr/bin/env python3
"""Record one second from the INMP441 and save raw and normalized WAV files."""
import argparse
import array
import math
import sys
import time
import wave
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='/dev/cu.usbserial-110')
args = parser.parse_args()
with serial.Serial(args.port, 115200, timeout=1) as port:
    time.sleep(2)
    port.reset_input_buffer()
    port.write(b'REC\n')
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        line = port.readline().decode('ascii', errors='replace').strip()
        if line == 'RECORDING_IN_2_SECONDS':
            print('Recording starts in 2 seconds; make a sound for 1 second.', flush=True)
        if line == 'RECORDING_DATA 16000 16000':
            break
    else:
        raise SystemExit('No recording response. Check the port and firmware.')
    data = bytearray()
    while len(data) < 32000 and time.monotonic() < deadline + 10:
        data.extend(port.read(32000 - len(data)))
    if len(data) != 32000:
        raise SystemExit(f'Incomplete recording: {len(data)} of 32000 bytes.')
values = array.array('h')
values.frombytes(data)
if sys.byteorder != 'little':
    values.byteswap()
mean = sum(values) / len(values)
centered = [v - mean for v in values]
rms = math.sqrt(sum(v * v for v in centered) / len(centered))
peak = max(abs(v) for v in centered)
print(f'I2S mean {mean:.1f}, range {min(values)}–{max(values)}, RMS {rms:.1f}, peak {peak:.1f}')
for name, gain in [('mic_raw.wav', 16), ('mic_normalized.wav', min(1000, 16000 / peak) if peak else 1)]:
    pcm = array.array('h', (max(-32768, min(32767, round(v * gain))) for v in centered))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(name, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(pcm.tobytes())
    print(f'Saved {name} (gain {gain:.1f}x)')
