# ESP32-PICO-KIT-1 guitar tuner — Wokwi project

This project is intended to be opened as a Wokwi ESP32 Arduino project. **Wokwi does not provide an ESP32-PICO-KIT-1 v1.0 board**, so `diagram.json` uses its ESP32 DevKit V1 stand-in. The firmware and wiring below use the *real PICO-KIT-1 J2/J3 header signal names and GPIO numbers* wherever they are connected. The stand-in's physical header positions are not the PICO-KIT-1 layout.

## Circuit and pins

For the direct OLED placement shown in the user's photos, build and upload the `direct_oled` environment. It uses OLED SDA=GPIO4 and SCL=GPIO0, and the INMP441 digital microphone over I²S: SCK/BCLK=GPIO26, WS=GPIO25, SD=GPIO34, with L/R tied to GND (left channel). It starts automatically. The `esp32dev` environment is for the Wokwi display and host-microphone bridge. GPIO0 is a boot strap pin, so a connected display may affect boot or upload.

The INMP441 is a digital I²S microphone. Power it from 3V3, connect its I²S pins as shown in `inmp441_wiring.svg`, and share ground with the ESP32. No analog divider is used.

The detector reads 16 kHz I²S audio in 1024-sample windows, scales samples to a signed 12-bit range for the existing RMS gate, and uses a YIN-style normalized difference function over approximately 65–550 Hz. The direct OLED build reports audio range, RMS, gate, sample rate, and periodicity over serial. Real instrument accuracy is not yet validated.

Use a full-size solderless breadboard (30 numbered rows is fine) with the controller across the center trench. The breadboard does not alter the signal mapping. In Wokwi the parts are shown as a logical wiring diagram; the `breadboard` visual part is a 30-row board, while individual wires still connect to the named pins.

| Device pin | Connect to | Purpose |
|---|---|---|
| OLED VCC / GND | PICO-KIT-1 3V3 / GND | display power |
| OLED SDA / SCL | GPIO4 / GPIO0 | direct OLED firmware I²C pins |
| INMP441 VDD / GND | 3V3 / common GND | microphone power |
| INMP441 L/R | GND | select left channel |
| INMP441 SCK / WS / SD | GPIO26 / GPIO25 / GPIO34 | I²S clock / word-select / data |

The Wokwi SSD1306 model labels VCC as a 5 V input; simulation uses it only as a supply pin. For the physical 3.3 V ESP32 kit, use a 3.3 V-compatible OLED module and power it from 3V3. Avoid 5 V pull-ups on SDA/SCL.

### INMP441 wiring

Use the labels on the microphone module. Connect VDD to 3V3, GND to ESP32 GND, L/R to GND, SCK to GPIO26, WS to GPIO25, and SD to GPIO34. L/R selects the left I²S slot read by the firmware. Do not connect VDD to 5V.

## Simulator substitutions and limitations

- **Board:** Wokwi `wokwi-esp32-devkit-v1` is a generic classic ESP32 DevKit, not the ESP32-PICO-KIT-1 v1.0. It runs the same classic ESP32 GPIO numbers used here; physical header order and onboard components differ.
- **Microphone:** Wokwi does not simulate the physical INMP441. Use the Mac microphone bridge below to send estimated pitch to the simulated tuner; this does not test the ESP32 I²S wiring or pitch detector.
- **Breadboard:** visual 30-row board included for reference; simulator wiring connects by logical pin names. It does not prove real-world mechanical placement or continuity.
- **Audio algorithm:** the Mac bridge tests the display and tuner scale only. Physical INMP441 audio sampling requires the real board.

## Run in Wokwi

1. Open [Wokwi ESP32 project](https://wokwi.com/projects/new/esp32) and create an Arduino ESP32 project.
2. Replace its `diagram.json` and `libraries.txt` with the included files, and copy the contents of `src/main.cpp` into its `sketch.ino`. The `wokwi.toml` is for the local Wokwi CLI/VS Code workflow after compiling to the listed build paths.
3. Start simulation. The OLED shows the tuner scale; use the bridge below to supply a pitch.
4. To make the simulated tuner show a note, use the Mac microphone bridge described below.
5. For local Wokwi CLI/VS Code, compile the Arduino sketch so its output matches `wokwi.toml`, or adjust the firmware/ELF paths to your selected build system.

## Hardware installation and test

1. Place the PICO-KIT-1 across the breadboard trench; identify headers J2 and J3 by the silkscreen and official pinout. The kit is wider than a typical breadboard may allow; use two breadboards or jumper wires if the body obstructs rows.
2. With USB disconnected, wire OLED and microphone ground to the shared ground rail; connect that rail to ESP32 GND.
3. Connect the OLED to 3V3, GND, GPIO4 (SDA), and GPIO0 (SCL). Keep I²C pull-ups at 3.3 V.
4. Connect the physical INMP441 according to the wiring table below.
5. Install PlatformIO with the Espressif ESP32 Arduino core; select the correct serial port. The declared OLED libraries install through PlatformIO.
6. With USB powering the kit, verify the tuner scale appears. Play one string near the microphone and observe the note and cents.

## Files

- `diagram.json` — Wokwi circuit visualization and wires
- `src/main.cpp` — complete Arduino ESP32 firmware
- `libraries.txt` — Wokwi Arduino libraries
- `wokwi.toml` — local Wokwi project configuration
- `inmp441_wiring.svg` — physical direct-OLED and INMP441 connection diagram

## Project setup in this folder

This folder is a PlatformIO project: `src/main.cpp` contains the sketch and `platformio.ini` declares the ESP32 target and display libraries. In VS Code, install PlatformIO and Wokwi extensions, open this folder, build the `esp32dev` environment, then run **Wokwi: Start Simulator**. The root `wokwi.toml` points to the PlatformIO build output. Wokwi may require a license depending on your account.

For Arduino IDE, create a new sketch named `GuitarTuner` and copy the contents of `src/main.cpp` into `GuitarTuner.ino`. Install the Espressif ESP32 board package through Boards Manager, select **ESP32 Dev Module** and the board's serial port, and install **Adafruit SSD1306** and **Adafruit GFX Library** through Library Manager. Connect the PICO-KIT-1 by a data-capable USB cable and press Upload. If connection fails, hold BOOT while upload begins, then release it. Open Serial Monitor at 115200 baud.

The display calculates the nearest chromatic note and octave from the measured frequency; its simple pitch detector has not been validated for all 24 notes or real guitar recordings. The earlier mention of standard strings describes only the serial output. Do not rely on its readings for accurate tuning without validating the audio circuit and algorithm.

## Mac microphone input in Wokwi

The simulated START and STOP buttons control tuning. START connects GPIO27 (PICO-KIT-1 J3-8) to ground; STOP connects GPIO14 (J3-9) to ground. Both use internal pull-ups. Use normally open pushbuttons on physical hardware.

Wokwi does not simulate the INMP441. The included bridge listens to the Mac microphone, estimates pitch on the Mac, and sends `HZ <frequency>` over Wokwi's RFC2217 serial port. This tests note display and buttons, but not the ESP32 I²S wiring or embedded pitch detector. The bridge uses the default macOS input device; select the desired microphone in System Settings. Use a single plucked note and a quiet room. Detection range is approximately 65–550 Hz, covering E2 through B4; notes outside this range need changes to the bridge. Pitch readings can make octave errors in noisy or harmonic-rich recordings.

1. Build the project in PlatformIO. Restart Wokwi after any change to `wokwi.toml`.
2. In the VS Code terminal, from the project folder, run:

   ```sh
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r tools/requirements.txt
   ```

3. Start the Wokwi simulator and keep its tab visible. Press the green START button. STOP pauses the display.
4. In the terminal run `python tools/microphone_bridge.py`. Allow microphone access for the application running the terminal if macOS asks; adjust Privacy & Security > Microphone if needed. Play a note and watch the OLED. Press Ctrl+C to stop the bridge.
5. If the bridge cannot connect, ensure Wokwi is running and port 4000 is available. Without the bridge, the simulated tuner shows no detected note.

The physical tuner uses the INMP441 over I²S. The Mac bridge is only for the simulator; validate real guitar readings on the connected microphone.
