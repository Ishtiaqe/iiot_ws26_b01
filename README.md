# ESP32 chromatic guitar tuner

Pluck one string near the INMP441 microphone. The OLED shows the nearest note, its octave, and how many cents the string is flat or sharp. For example, an open A string at 110 Hz reads **A2 +0c**. A weak or irregular frame clears the pointer and asks for another pluck.

## Start reading the firmware here

| To understand or change | Open | Where to look |
|---|---|---|
| Wiring, input and timing | [src/main.cpp](src/main.cpp) | `setup()` starts the devices. `loop()` checks the buttons and serial commands, gets a pitch, and sends one reading to the OLED. |
| Microphone samples and noise rejection | [src/pitch.h](src/pitch.h) | `tunerSample()` preserves the sign of the left I²S sample. `PitchDetector::process()` returns a frequency in hertz, or zero when the frame is too quiet or irregular. |
| What the player sees | [src/tuner_display.h](src/tuner_display.h) | `tuningReading()` chooses the note and instruction. `drawTuner()` positions the text, scale and pointer on the 128 × 64 screen. |
| Measurements used in the slides | [tools/verify_tuner.cpp](tools/verify_tuner.cpp) | `makeFrame()` generates the test audio. `main()` runs the clean, noisy, silent and held-note trials. |

**Example edit.** To widen the in-tune band to ±7 cents, change `IN_TUNE_CENTS` from 5 to 7 in `src/tuner_display.h`. A reading of +6c will then say **IN TUNE** and the two inner scale marks will move outward. This is a compile-time setting. Build again and upload to see it on the board. To move the action text, change its y-coordinate in `drawTuner()`. Pixel (0, 0) is the upper-left corner.

**How the detector decides.** One frame contains 1,024 samples. At 16,000 samples/second that is 64 ms. First the detector subtracts the average level and measures RMS loudness. Frames below the gate update the background estimate. For louder frames it compares the waveform with delayed copies. An A2 at 110 Hz repeats roughly every 145 samples. The detector refines the delay between samples, then divides the sample rate by the delay to get hertz. A single microphone can also pick up other repeating sounds, including voices.

## Build and upload

Connect the ESP32 with a USB data cable, then run:

```sh
~/.platformio/penv/bin/pio run -e direct_oled -t upload
```

For diagnostics, run `~/.platformio/penv/bin/pio device monitor -b 460800` after uploading. Close the monitor before uploading again. The optional `REC` and `LIVE` serial commands temporarily pause tuning.

## Physical circuit

Disconnect USB power before wiring. The firmware currently configures the pins below; the original physical OLED connections have not been independently verified. Confirm SDA and SCL against your actual wiring before connecting or uploading. GPIO0 is a boot strap pin, so the display may affect boot or upload.

| Component | Pin | ESP32 connection |
|---|---|---|
| OLED | VCC / GND | 3V3 / GND |
| OLED | SDA / SCL | GPIO4 / GPIO0 (configured; physical wiring unconfirmed) |
| INMP441 | VDD / GND | 3V3 / GND |
| INMP441 | L/R | GND |
| INMP441 | SCK / WS / SD | GPIO26 / GPIO25 / GPIO34 |
| Optional START button | one side / other side | GPIO27 / GND |
| Optional STOP button | one side / other side | GPIO14 / GND |

Use a 3.3 V-compatible OLED with 3.3 V I²C pull-ups, and share ground between the components. The tuner starts automatically. The buttons use internal pull-ups and should be normally open. See [inmp441_wiring.svg](inmp441_wiring.svg) for the configured connections; its OLED pins are likewise unconfirmed against the physical assembly.

The detector processes 1024-sample frames at 16 kHz. Real instrument accuracy and display latency require hardware testing.

### Reproduce the presentation measurements

Run from this directory on a computer with a C++17 compiler:

```sh
c++ -std=c++17 -O2 tools/verify_tuner.cpp -o /tmp/verify-tuner
/tmp/verify-tuner
```

The program uses generated signed audio, a fixed random seed, the firmware detector and the display decision logic. It also checks the I²S signed conversion. End-to-end accuracy, display latency and actual room noise require tests on the assembled tuner.

## Listen to the physical INMP441 live

Upload the `direct_oled` firmware, close any Serial Monitor, then install the Python tools with `python3 -m pip install -r tools/requirements.txt`. Run `python tools/listen_esp32.py --port /dev/cu.usbserial-110`; it streams audio to the Mac's default output until Ctrl+C. Use headphones at low volume to avoid acoustic feedback. Adjust playback loudness with `--gain 2` or `--gain 8`; this changes host playback only, not the microphone or tuner gain. The direct firmware uses 460800 baud for the PCM stream.
