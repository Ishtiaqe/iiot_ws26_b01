// Firmware orchestration: configure the board peripherals, acquire pitch on a
// background task, and render the latest tuning state on the OLED.
#include "microphone.h"
#include "pitch.h"
#include "tuner_display.h"
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ESP32 DevKit V1 GPIO assignments.
//   OLED  : SDA -> GPIO21, SCL -> GPIO22 (the board's default I2C pins; GPIO0
//           is the BOOT strapping pin and should not be used as a bus line).
//   INMP441: SCK -> GPIO26, WS -> GPIO25, SD -> GPIO34, L/R -> GND, VDD -> 3V3.
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_26;
constexpr gpio_num_t PIN_I2S_WS = GPIO_NUM_25;
constexpr gpio_num_t PIN_I2S_DATA = GPIO_NUM_34;

// Which I2S slot carries the microphone: 0 = left (INMP441 L/R pin to GND),
// 1 = right (L/R pin to 3V3). If the serial diagnostics show the other slot
// moving while this one is flat, change this value.
constexpr int MIC_SLOT = 0;
// Prints per-slot sample ranges and the pitch result once per second.
constexpr bool DEBUG_AUDIO = true;

// A 1024-sample analysis frame spans 64 ms at 16 kHz. Advancing 512 samples
// per read gives a 32 ms hop and reuses half of the previous frame.
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr int WINDOW_SAMPLES = 1024;
constexpr int HOP_SAMPLES = 512;
constexpr uint32_t OLED_I2C_HZ = 400000;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint32_t DISPLAY_INTERVAL_MS = 33;

struct PitchSample {
  PitchEstimate pitch;   // Latest estimate; frequency is zero if no pitch passed.
  uint32_t timestamp;    // millis() when this block was captured.
  MicStatus micStatus;   // Distinguishes silence from I²S read failure.
};

Microphone microphone;
Adafruit_SSD1306 display(128, 64, &Wire, -1, OLED_I2C_HZ, OLED_I2C_HZ);
QueueHandle_t latestPitch;

void readPitch(void *) {
  static int32_t stereoBlock[2 * HOP_SAMPLES];
  static float window[WINDOW_SAMPLES];
  static PitchDetector<WINDOW_SAMPLES> detector;
  bool havePreviousBlock = false;
  uint32_t lastChangingAudioAt = millis();
  uint32_t lastDebugAt = 0;
  // This FreeRTOS task owns all microphone reads and detector state. The
  // length-one queue intentionally keeps only the newest result for the UI.
  for (;;) {
    if (!microphone.read(stereoBlock, 2 * HOP_SAMPLES)) {
      havePreviousBlock = false;
      const PitchSample missing{{}, millis(), MicStatus::ReadError};
      xQueueOverwrite(latestPitch, &missing);
      delay(1);
      continue;
    }

    const uint32_t capturedAt = millis();
    PitchEstimate estimate;
    const int32_t firstMicWord = stereoBlock[MIC_SLOT] >> 8;
    bool changingAudio = false;
    for (int i = 0; i < HOP_SAMPLES; ++i) {
      // Shift the old frame's second half forward, then append this hop.
      window[i] = window[i + HOP_SAMPLES];
      // Interleaved words are L, R, L, R...; MIC_SLOT picks the live one.
      const int32_t micWord = stereoBlock[2 * i + MIC_SLOT];
      changingAudio |= (micWord >> 8) != firstMicWord;
      window[i + HOP_SAMPLES] = microphoneSample(micWord);
    }
    if (havePreviousBlock)
      estimate = detector.process(window, SAMPLE_RATE);
    havePreviousBlock = true;
    if (changingAudio)
      lastChangingAudioAt = capturedAt;
    const auto micStatus = capturedAt - lastChangingAudioAt >= 500
                               ? MicStatus::NoData : MicStatus::Ready;
    const PitchSample sample{estimate, capturedAt, micStatus};
    xQueueOverwrite(latestPitch, &sample);

    if (DEBUG_AUDIO && capturedAt - lastDebugAt >= 1000) {
      lastDebugAt = capturedAt;
      int32_t lo[2] = {INT32_MAX, INT32_MAX};
      int32_t hi[2] = {INT32_MIN, INT32_MIN};
      for (int i = 0; i < 2 * HOP_SAMPLES; ++i) {
        const int32_t value = stereoBlock[i] >> 8;
        const int slot = i & 1;
        if (value < lo[slot]) lo[slot] = value;
        if (value > hi[slot]) hi[slot] = value;
      }
      Serial.printf("L[%ld..%ld] R[%ld..%ld] using=%s rms=%.1f f=%.1fHz clarity=%.2f\n",
                    (long)lo[0], (long)hi[0], (long)lo[1], (long)hi[1],
                    MIC_SLOT == 0 ? "L" : "R", estimate.rms, estimate.frequency,
                    estimate.clarity);
    }
  }
}

// Stop in a visible, diagnosable state when a required peripheral or task
// cannot start. Serial output records the detailed reason for debugging.
void halt(const char *message) {
  Serial.println(message);
  for (;;)
    delay(1000);
}

void setup() {
  Serial.begin(115200);

  // Initialize and probe I²C separately so wiring/address failures get a
  // useful serial message before the firmware enters its halted state.
  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, OLED_I2C_HZ))
    halt("OLED I2C initialization failed; check SDA and SCL");
  Wire.beginTransmission(OLED_ADDRESS);
  if (Wire.endTransmission() != 0)
    halt("OLED not responding at 0x3C; check wiring and address");
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false))
    halt("SSD1306 initialization failed");
  const esp_err_t micError = microphone.begin(SAMPLE_RATE, PIN_I2S_BCLK,
                                              PIN_I2S_WS, PIN_I2S_DATA);
  if (micError != ESP_OK) {
    Serial.printf("I2S driver: %s\n", esp_err_to_name(micError));
    TuningReading reading;
    reading.micStatus = MicStatus::ReadError;
    drawTuner(display, reading);
    display.display();
    halt("INMP441 I2S initialization failed");
  }
  drawTuner(display, TuningReading{});
  display.display();

  // A one-element overwrite queue is a latest-value mailbox: display work
  // never makes audio acquisition wait for stale readings to be consumed.
  latestPitch = xQueueCreate(1, sizeof(PitchSample));
  // Audio acquisition and OLED transfers run independently. The stack is
  // generous because Serial.printf in the diagnostics needs several KB.
  if (!latestPitch || xTaskCreate(readPitch, "pitch", 8192, nullptr, 2, nullptr) != pdPASS)
    halt("Could not start pitch acquisition");
}

void loop() {
  // No START/STOP buttons: the tuner always runs.
  constexpr bool running = true;
  static PitchTracker tracker;
  static TuningReading displayed;
  static MicStatus micStatus = MicStatus::Ready;
  static uint32_t lastDisplayAt = 0;
  static uint32_t lastSampleAt = 0;
  static int inputLevel = 0;
  PitchSample sample;
  // Poll without blocking; this loop remains responsible for display timing.
  if (xQueueReceive(latestPitch, &sample, 0) == pdTRUE) {
    micStatus = sample.micStatus;
    lastSampleAt = sample.timestamp;
    inputLevel = micStatus == MicStatus::Ready ? inputMeterLevel(sample.pitch.rms) : 0;
    if (running)
      tracker.accept(sample.pitch.frequency, sample.pitch.clarity, sample.timestamp);
  }

  const uint32_t now = millis();
  // Refresh at the UI rate.
  if (now - lastDisplayAt >= DISPLAY_INTERVAL_MS || running == displayed.stopped) {
    lastDisplayAt = now;
    TuningReading reading = tracker.render(now, running);
    if (running) {
      reading.inputLevel = now - lastSampleAt <= 100 ? inputLevel : 0;
      if (reading.note < 0)
        reading.micStatus = micStatus;
    }
    if (!reading.sameDisplayAs(displayed)) {
      drawTuner(display, reading);
      display.display();
      displayed = reading;
    }
  }
  delay(1);
}