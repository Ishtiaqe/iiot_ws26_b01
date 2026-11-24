#include "pitch.h"
#include "tuner_display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#ifdef DIRECT_OLED
#include <ESP_I2S.h>
#endif

// Wiring preserves the ESP32-PICO-KIT-1 signal names. Wokwi uses an ESP32
// DevKit stand-in.
#ifdef DIRECT_OLED
constexpr int PIN_I2C_SDA = 4;
constexpr int PIN_I2C_SCL = 0;
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_WS = 25;
constexpr int PIN_I2S_DATA = 34;
constexpr uint32_t AUDIO_SAMPLE_RATE =
    16000; // Samples per second; keep this within the mic's supported I²S rate.
I2SClass microphoneI2S;
static int32_t i2sFrames[2048];
constexpr uint32_t SERIAL_BAUD_RATE = 460800; // Enough bandwidth for 16 kHz, 16-bit mono PCM over USB serial.
#else
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
#endif
constexpr int PIN_START = 27;            // J3-8 IO27
constexpr int PIN_STOP = 14;             // J3-9 IO14
bool running =
#ifdef DIRECT_OLED
    true;
#else
    false;
#endif
float bridgeHz = 0;
uint32_t bridgeAt = 0;
bool bridgeSeen = false;
char serialLine[40];
size_t serialLength = 0;
int16_t audioMin = 0, audioMax = 0;
float audioMean = 0;
uint32_t lastAudioReport = 0;
PitchDetector pitchDetector;
float sampleRate = 0;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

float estimateFrequency() {
#ifdef DIRECT_OLED
  constexpr int N = 1024; // Samples per pitch estimate; larger windows improve
                          // low-note resolution but update slower.
  int16_t samples[N];
  size_t bytesNeeded = sizeof(i2sFrames);
  size_t bytesRead = microphoneI2S.readBytes((char *)i2sFrames, bytesNeeded);
  if (bytesRead != bytesNeeded)
    return 0;
  for (int i = 0; i < N; ++i) {
    samples[i] = tunerSample(i2sFrames[i * 2]);
  }
  sampleRate = AUDIO_SAMPLE_RATE;
  int32_t sum = 0;
  audioMin = INT16_MAX;
  audioMax = INT16_MIN;
  for (int i = 0; i < N; ++i) {
    sum += samples[i];
    if (samples[i] < audioMin)
      audioMin = samples[i];
    if (samples[i] > audioMax)
      audioMax = samples[i];
  }
  audioMean = (float)sum / N;
  return pitchDetector.process(samples, N, sampleRate);
#else
  return 0;
#endif
}

#ifdef DIRECT_OLED
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
  for (int offset = 0; offset < count; offset += 1024) {
    microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames));
    int block = min(1024, count - offset);
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
  static int16_t pcm[1024];
  char command[8] = {};
  size_t commandLength = 0;
  Serial.printf("LIVE_START %lu 1024\n", (unsigned long)AUDIO_SAMPLE_RATE);
  Serial.flush();
  while (true) {
    size_t bytesRead = microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames));
    if (bytesRead != sizeof(i2sFrames)) continue;
    for (int i = 0; i < 1024; ++i) pcm[i] = (int16_t)(i2sFrames[i * 2] >> 16);
    Serial.write((const uint8_t *)pcm, sizeof(pcm));
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
#endif

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);
#ifdef DIRECT_OLED
  microphoneI2S.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DATA);
  if (!microphoneI2S.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("INMP441 I2S init failed; check BCLK/WS/SD wiring");
    while (true)
      delay(1000);
  }
#endif
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.printf(
        "SSD1306 init failed; check SDA=GPIO%d SCL=GPIO%d and address 0x3C\n",
        PIN_I2C_SDA, PIN_I2C_SCL);
    while (true)
      delay(1000);
  }
#ifdef DIRECT_OLED
  Serial.printf(
      "ESP32 tuner ready. INMP441 I2S: BCLK=%d WS=%d SD=%d at %lu Hz.\n",
      PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DATA, (unsigned long)AUDIO_SAMPLE_RATE);
#endif
}

void loop() {
  if (digitalRead(PIN_STOP) == LOW)
    running = false;
  else if (digitalRead(PIN_START) == LOW)
    running = true;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialLine[serialLength] = 0;
      if (strncmp(serialLine, "HZ ", 3) == 0) {
        bridgeHz = strtof(serialLine + 3, nullptr);
        bridgeAt = millis();
        bridgeSeen = true;
      }
#ifdef DIRECT_OLED
      else if (strcmp(serialLine, "REC") == 0)
        recordAudio();
      else if (strcmp(serialLine, "LIVE") == 0)
        streamLiveAudio();
#endif
      serialLength = 0;
    } else if (c != '\r') {
      if (serialLength < sizeof(serialLine) - 1)
        serialLine[serialLength++] = c;
      else
        serialLength = 0;
    }
  }
  bool bridgeActive = bridgeSeen && (uint32_t)(millis() - bridgeAt) < 230;
  float hz = running ? (bridgeActive ? bridgeHz : estimateFrequency()) : 0;
#ifdef DIRECT_OLED
  if (running && !bridgeActive &&
      (uint32_t)(millis() - lastAudioReport) >= 1000) {
    Serial.printf("I2S min=%d max=%d mean=%.0f span=%d rms=%.1f gate=%.1f "
                  "quality=%.2f rate=%.0f pitch=%.1f Hz\n",
                  audioMin, audioMax, audioMean, audioMax - audioMin,
                  pitchDetector.rms, pitchDetector.threshold,
                  pitchDetector.quality, sampleRate, hz);
    lastAudioReport = millis();
  }
#endif
  const auto reading = tuningReading(hz, running);
  if (reading.note[0])
    Serial.printf("%.2f Hz, %s, %+d cents\n", hz, reading.note, reading.cents);
  drawTuner(display, hz, running);
  display.display();
  // Blocking I2S reads already pace the physical tuner.
  if (!running || bridgeActive) delay(20);
#ifndef DIRECT_OLED
  else delay(20);
#endif
}
