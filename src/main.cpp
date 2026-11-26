#include "microphone.h"
#include "pitch.h"
#include "tuner_display.h"
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ESP32-PICO-KIT-1 GPIO numbers.
constexpr int PIN_I2C_SDA = 4;
constexpr int PIN_I2C_SCL = 0;
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_26;
constexpr gpio_num_t PIN_I2S_WS = GPIO_NUM_25;
constexpr gpio_num_t PIN_I2S_DATA = GPIO_NUM_34;
constexpr int PIN_START = 27;
constexpr int PIN_STOP = 14;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr int WINDOW_SAMPLES = 1024;
constexpr int HOP_SAMPLES = 512;
constexpr uint32_t OLED_I2C_HZ = 400000;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint32_t DISPLAY_INTERVAL_MS = 33;

struct PitchSample {
  float frequency;
  uint32_t timestamp;
  MicStatus micStatus;
};

Microphone microphone;
Adafruit_SSD1306 display(128, 64, &Wire, -1, OLED_I2C_HZ, OLED_I2C_HZ);
QueueHandle_t latestPitch;

void readPitch(void *) {
  static int32_t stereoBlock[2 * HOP_SAMPLES];
  static int16_t window[WINDOW_SAMPLES];
  PitchDetector detector;
  bool havePreviousBlock = false;
  uint32_t lastChangingAudioAt = millis();
  for (;;) {
    if (!microphone.read(stereoBlock, 2 * HOP_SAMPLES)) {
      havePreviousBlock = false;
      const PitchSample missing{0, millis(), MicStatus::ReadError};
      xQueueOverwrite(latestPitch, &missing);
      delay(1);
      continue;
    }

    const uint32_t capturedAt = millis();
    float frequency = 0;
    const int32_t firstMicWord = stereoBlock[0] >> 8;
    bool changingAudio = false;
    for (int i = 0; i < HOP_SAMPLES; ++i) {
      window[i] = window[i + HOP_SAMPLES];
      // L/R is grounded, so the microphone occupies the left I²S slot.
      const int32_t micWord = stereoBlock[2 * i];
      changingAudio |= (micWord >> 8) != firstMicWord;
      window[i + HOP_SAMPLES] = tunerSample(micWord);
    }
    if (havePreviousBlock)
      frequency = detector.process(window, WINDOW_SAMPLES, SAMPLE_RATE);
    havePreviousBlock = true;
    if (changingAudio)
      lastChangingAudioAt = capturedAt;
    const auto micStatus = capturedAt - lastChangingAudioAt >= 500
                               ? MicStatus::NoData : MicStatus::Ready;
    const PitchSample sample{frequency, capturedAt, micStatus};
    xQueueOverwrite(latestPitch, &sample);
  }
}

void halt(const char *message) {
  Serial.println(message);
  for (;;)
    delay(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);

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

  latestPitch = xQueueCreate(1, sizeof(PitchSample));
  // Audio acquisition and OLED transfers run independently.
  if (!latestPitch || xTaskCreate(readPitch, "pitch", 4096, nullptr, 2, nullptr) != pdPASS)
    halt("Could not start pitch acquisition");
}

void loop() {
  static bool running = true;
  static PitchTracker tracker;
  static TuningReading displayed;
  static MicStatus micStatus = MicStatus::Ready;
  static uint32_t lastDisplayAt = 0;
  if (digitalRead(PIN_STOP) == LOW)
    running = false;
  else if (digitalRead(PIN_START) == LOW)
    running = true;

  PitchSample sample;
  if (xQueueReceive(latestPitch, &sample, 0) == pdTRUE) {
    micStatus = sample.micStatus;
    if (running)
      tracker.accept(sample.frequency, sample.timestamp);
  }

  const uint32_t now = millis();
  if (now - lastDisplayAt >= DISPLAY_INTERVAL_MS || running == displayed.stopped) {
    lastDisplayAt = now;
    TuningReading reading = tracker.render(now, running);
    if (running && reading.note < 0)
      reading.micStatus = micStatus;
    if (!reading.sameDisplayAs(displayed)) {
      drawTuner(display, reading);
      display.display();
      displayed = reading;
    }
  }
  delay(1);
}
