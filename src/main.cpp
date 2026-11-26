#include "pitch.h"
#include "tuner_display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <ESP_I2S.h>

// ESP32-PICO-KIT-1 GPIO numbers.
constexpr int PIN_I2C_SDA = 4;
constexpr int PIN_I2C_SCL = 0;
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_WS = 25;
constexpr int PIN_I2S_DATA = 34;
constexpr uint32_t AUDIO_SAMPLE_RATE =
    16000; // Samples per second; keep this within the mic's supported I²S rate.
I2SClass microphoneI2S;
constexpr int FRAME_SAMPLES = 1024; // 64 ms of mono sound at 16 kHz.
static int32_t i2sFrames[2 * FRAME_SAMPLES]; // Interleaved left and right slots.
bool running = true;
constexpr uint32_t SERIAL_BAUD_RATE = 460800; // Enough bandwidth for 16 kHz, 16-bit mono PCM over USB serial.
constexpr int PIN_START = 27;            // J3-8 IO27
constexpr int PIN_STOP = 14;             // J3-9 IO14
char serialLine[40];
size_t serialLength = 0;
int16_t audioMin = 0, audioMax = 0;
float audioMean = 0;
uint32_t lastAudioReport = 0;
PitchDetector pitchDetector;

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

bool readMicFrame() {
  return microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames)) == sizeof(i2sFrames);
}

float estimateFrequency() {
  if (!readMicFrame()) return 0;
  int16_t samples[FRAME_SAMPLES];
  int32_t sum = 0;
  audioMin = INT16_MAX;
  audioMax = INT16_MIN;
  for (int i = 0; i < FRAME_SAMPLES; ++i) {
    // L/R is grounded on the INMP441, so slot 2*i holds our microphone.
    samples[i] = tunerSample(i2sFrames[2 * i]);
    sum += samples[i];
    if (samples[i] < audioMin)
      audioMin = samples[i];
    if (samples[i] > audioMax)
      audioMax = samples[i];
  }
  audioMean = (float)sum / FRAME_SAMPLES;
  return pitchDetector.process(samples, FRAME_SAMPLES, AUDIO_SAMPLE_RATE);
}

void recordAudio() {
  constexpr int count = AUDIO_SAMPLE_RATE; // One second of raw audio for the
                                           // optional serial REC command.
  int16_t *recording = (int16_t *)malloc(count * sizeof(int16_t));
  if (!recording) {
    Serial.println("RECORDING_ALLOC_FAILED");
    return;
  }
  Serial.println("RECORDING_IN_2_SECONDS");
  Serial.flush();
  for (int i = 0; i < 32; ++i)
    microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames));
  for (int offset = 0; offset < count; offset += FRAME_SAMPLES) {
    if (!readMicFrame()) {
      free(recording);
      Serial.println("RECORDING_READ_FAILED");
      return;
    }
    int block = min(FRAME_SAMPLES, count - offset);
    for (int i = 0; i < block; ++i)
      recording[offset + i] = (int16_t)(i2sFrames[i * 2] >> 16);
  }
  Serial.printf("RECORDING_DATA %d %lu\n", count,
                (unsigned long)AUDIO_SAMPLE_RATE);
  Serial.write((const uint8_t *)recording, count * sizeof(int16_t));
  Serial.flush();
  Serial.println();
  free(recording);
}

void streamLiveAudio() {
  static int16_t pcm[FRAME_SAMPLES];
  char command[8] = {};
  size_t commandLength = 0;
  Serial.printf("LIVE_START %lu %d\n", (unsigned long)AUDIO_SAMPLE_RATE, FRAME_SAMPLES);
  Serial.flush();
  while (true) {
    if (readMicFrame()) {
      for (int i = 0; i < FRAME_SAMPLES; ++i)
        pcm[i] = (int16_t)(i2sFrames[i * 2] >> 16);
      Serial.write((const uint8_t *)pcm, sizeof(pcm));
    }
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        command[commandLength] = 0;
        if (strcmp(command, "STOP") == 0) {
          Serial.println("LIVE_STOP");
          return;
        }
        commandLength = 0;
      } else if (commandLength < sizeof(command) - 1) {
        command[commandLength++] = c;
      } else {
        commandLength = 0;
      }
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);
  microphoneI2S.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DATA);
  if (!microphoneI2S.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("INMP441 I2S init failed; check BCLK/WS/SD wiring");
    while (true)
      delay(1000);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.printf(
        "SSD1306 init failed; check SDA=GPIO%d SCL=GPIO%d and address 0x3C\n",
        PIN_I2C_SDA, PIN_I2C_SCL);
    while (true)
      delay(1000);
  }
  Serial.printf(
      "ESP32 tuner ready. INMP441 I2S: BCLK=%d WS=%d SD=%d at %lu Hz.\n",
      PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DATA, (unsigned long)AUDIO_SAMPLE_RATE);
}

// Commands are newline-delimited. REC and LIVE temporarily take over the microphone.
void handleSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialLine[serialLength] = 0;
      if (strcmp(serialLine, "REC") == 0)
        recordAudio();
      else if (strcmp(serialLine, "LIVE") == 0)
        streamLiveAudio();
      serialLength = 0;
    } else if (c != '\r') {
      if (serialLength < sizeof(serialLine) - 1)
        serialLine[serialLength++] = c;
      else
        serialLength = 0;
    }
  }
}

void loop() {
  if (digitalRead(PIN_STOP) == LOW)
    running = false;
  else if (digitalRead(PIN_START) == LOW)
    running = true;
  handleSerialCommands();
  float hz = 0;
  if (running)
    hz = estimateFrequency();
  if (running &&
      (uint32_t)(millis() - lastAudioReport) >= 1000) {
    Serial.printf("I2S min=%d max=%d mean=%.0f span=%d rms=%.1f gate=%.1f "
                  "quality=%.2f rate=%lu pitch=%.1f Hz\n",
                  audioMin, audioMax, audioMean, audioMax - audioMin,
                  pitchDetector.rms, pitchDetector.threshold,
                  pitchDetector.quality, (unsigned long)AUDIO_SAMPLE_RATE, hz);
    lastAudioReport = millis();
  }
  const auto reading = tuningReading(hz, running);
  if (reading.note[0])
    Serial.printf("%.2f Hz, %s, %+d cents\n", hz, reading.note, reading.cents);
  drawTuner(display, reading);
  display.display();
  // Blocking I2S reads already pace the physical tuner.
  if (running) return;
  delay(20);
}
