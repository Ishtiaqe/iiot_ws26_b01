#!/usr/bin/env python3
"""Estimate monophonic pitch from the Mac microphone and send it to Wokwi."""
import time
import numpy as np
import sounddevice as sd
import serial

RATE = 22050
BLOCK = 4096
MIN_HZ, MAX_HZ = 65.0, 550.0

def pitch(samples):
    x = samples.astype(np.float64)
    x -= x.mean()
    if np.sqrt(np.mean(x * x)) < 0.008:
        return 0.0
    # Normalized autocorrelation reduces the preference for short lags.
    corr = np.correlate(x, x, mode='full')[len(x)-1:]
    energy = np.r_[0.0, np.cumsum(x*x)]
    lo, hi = int(RATE / MAX_HZ), int(RATE / MIN_HZ)
    lags = np.arange(lo, hi + 1)
    norm = np.sqrt(energy[len(x)-lags] * (energy[len(x)] - energy[lags]))
    scores = corr[lags] / np.maximum(norm, 1e-12)
    # Select the first strong local peak to limit octave errors.
    peaks = np.flatnonzero((scores[1:-1] > scores[:-2]) & (scores[1:-1] >= scores[2:])) + 1
    if not len(peaks):
        return 0.0
    best = scores[peaks].max()
    if best < 0.65:
        return 0.0
    idx = next(i for i in peaks if scores[i] >= 0.9 * best)
    delta = 0.5 * (scores[idx-1] - scores[idx+1]) / (scores[idx-1] - 2*scores[idx] + scores[idx+1] + 1e-12)
    return RATE / (lags[idx] + delta)

def main():
    print('Connecting to Wokwi on localhost:4000. Start the simulator first.')
    with serial.serial_for_url('rfc2217://localhost:4000', baudrate=115200, timeout=1) as port:
        print('Listening to the default microphone. Press Ctrl+C to stop.')
        with sd.InputStream(channels=1, samplerate=RATE, blocksize=BLOCK) as stream:
            while True:
                data, overflow = stream.read(BLOCK)
                hz = pitch(data[:, 0])
                port.write(f'HZ {hz:.2f}\n'.encode())
                print(f'\rPitch: {hz:7.2f} Hz', end='', flush=True)
                if overflow:
                    print(' (audio overflow)', flush=True)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped.')
    except Exception as exc:
        print(f'\nBridge error: {exc}')
        raise SystemExit(1)
