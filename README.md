# ESP32 chromatic guitar tuner

Pluck one string near the INMP441 microphone. The OLED shows the nearest note, its octave, and how many cents the string is flat or sharp. For example, an open A string at 110 Hz reads **A2 +0c**. When the note fades, the tuner holds the last reading for up to five seconds. It then clears the needle and asks for another pluck.

## Start reading the firmware here

| To understand or change | Open | Where to look |
|---|---|---|
| Wiring, input and timing | [src/main.cpp](src/main.cpp) | `setup()` starts the devices. `loop()` checks the buttons, gets a pitch, and sends one reading to the OLED. |
| Microphone samples and noise rejection | [src/pitch.h](src/pitch.h) | `tunerSample()` preserves the sign of the left I²S sample. `PitchDetector::process()` returns a frequency in hertz, or zero when the frame is too quiet or irregular. |
| What the player sees | [src/tuner_display.h](src/tuner_display.h) | `PitchTracker` selects and smooths the reading. `drawTuner()` positions the text, scale and pointer on the 128 × 64 screen. |

**Example edit.** To widen the in-tune band to ±7 cents, change `IN_TUNE_CENTS` from 5 to 7 in `src/tuner_display.h`. A reading of +6c will then say **IN TUNE** and the two inner scale marks will move outward. This is a compile-time setting. Build again and upload to see it on the board. To move the action text, change its y-coordinate in `drawTuner()`. Pixel (0, 0) is the upper-left corner.

**How the detector decides.** One frame contains 1,024 samples. At 16,000 samples/second that is 64 ms. First the detector subtracts the average level and measures RMS loudness. Frames below the gate update the background estimate. For louder frames it compares the waveform with delayed copies. An A2 at 110 Hz repeats roughly every 145 samples. The detector refines the delay between samples, then divides the sample rate by the delay to get hertz. A single microphone can also pick up other repeating sounds, including voices.

## Build and upload

Connect the ESP32 with a USB data cable, then run:

```sh
~/.platformio/penv/bin/pio run -e direct_oled -t upload
```

Close any serial monitor before uploading.

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

## Tracking and display

The microphone runs at 16 kHz. Pitch estimates use a 64 ms window and advance by 32 ms, for about 31 estimates per second when processing keeps up. The OLED updates on its own schedule, up to about 30 frames per second, and only transfers a frame when something visible changes.

While a note is valid, an exponential moving average follows large pitch changes more quickly and dampens smaller ones. The needle and cents readout resist one-step fluctuations. The nearest note has a 3-cent margin beyond its nominal boundary, and a new note takes two consecutive estimates to confirm. When the reference note changes, the needle takes the new note's position directly.

A missing estimate freezes the last rendered reading and shows **LAST READING**. Five seconds after the last accepted estimate, the tuner clears the needle and shows **PLUCK ONE STRING**. STOP clears the reading immediately. Actual update rate, latency, and rejection of background sounds need verification on the assembled tuner.
