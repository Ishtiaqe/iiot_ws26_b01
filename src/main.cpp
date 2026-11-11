#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "pitch.h"
#ifdef DIRECT_OLED
#include <ESP_I2S.h>
#endif

// Wiring preserves the ESP32-PICO-KIT-1 signal names. Wokwi uses an ESP32 DevKit stand-in.
#ifdef DIRECT_OLED
constexpr int PIN_I2C_SDA = 4;
constexpr int PIN_I2C_SCL = 0;
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_WS = 25;
constexpr int PIN_I2S_DATA = 34;
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
I2SClass microphoneI2S;
static int32_t i2sFrames[2048];
#else
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr int PIN_AUDIO = 36;
#endif
constexpr int PIN_START = 27;       // J3-8 IO27
constexpr int PIN_STOP = 14;        // J3-9 IO14
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
uint16_t audioMin = 0, audioMax = 0;
float audioMean = 0;
uint32_t lastAudioReport = 0;
PitchDetector pitchDetector;
float sampleRate = 0;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

float estimateFrequency() {
  constexpr int N = 1024;
  uint16_t samples[N];
#ifdef DIRECT_OLED
  size_t bytesNeeded = sizeof(i2sFrames);
  size_t bytesRead = microphoneI2S.readBytes((char *)i2sFrames, bytesNeeded);
  if (bytesRead != bytesNeeded) return 0;
  for (int i = 0; i < N; ++i) {
    int16_t signedSample = (int16_t)(i2sFrames[i * 2] >> 16);
    samples[i] = (uint16_t)(signedSample >> 4);
  }
  sampleRate = AUDIO_SAMPLE_RATE;
#else
  constexpr int SAMPLE_US = 125;
  uint32_t start = micros();
  uint32_t lastSample = start;
  for (int i = 0; i < N; ++i) {
    while ((uint32_t)(micros() - start) < (uint32_t)i * SAMPLE_US) {}
    lastSample = micros();
    samples[i] = analogRead(PIN_AUDIO);
  }
  uint32_t elapsed = lastSample - start;
  sampleRate = elapsed ? (N - 1) * 1000000.0f / elapsed : 8000.0f;
#endif
  uint32_t sum = 0;
  audioMin = 4095;
  audioMax = 0;
  for (int i = 0; i < N; ++i) {
    sum += samples[i];
    if (samples[i] < audioMin) audioMin = samples[i];
    if (samples[i] > audioMax) audioMax = samples[i];
  }
  audioMean = (float)sum / N;
  return pitchDetector.process(samples, N, sampleRate);
}

#ifdef DIRECT_OLED
void recordAudio() {
  constexpr int count = AUDIO_SAMPLE_RATE;
  int16_t *recording = (int16_t *)malloc(count * sizeof(int16_t));
  if (!recording) {
    Serial.println("RECORDING_ALLOC_FAILED");
    return;
  }
  Serial.println("RECORDING_IN_2_SECONDS");
  Serial.flush();
  for (int i = 0; i < 32; ++i) microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames));
  for (int offset = 0; offset < count; offset += 1024) {
    microphoneI2S.readBytes((char *)i2sFrames, sizeof(i2sFrames));
    int block = min(1024, count - offset);
    for (int i = 0; i < block; ++i) recording[offset + i] = (int16_t)(i2sFrames[i * 2] >> 16);
  }
  Serial.printf("RECORDING_DATA %d %lu\n", count, (unsigned long)AUDIO_SAMPLE_RATE);
  Serial.write((const uint8_t *)recording, count * sizeof(int16_t));
  Serial.flush();
  Serial.println();
  free(recording);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);
#ifdef DIRECT_OLED
  microphoneI2S.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DATA);
  if (!microphoneI2S.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("INMP441 I2S init failed; check BCLK/WS/SD wiring");
    while (true) delay(1000);
  }
#else
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_AUDIO, ADC_11db);
#endif
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.printf("SSD1306 init failed; check SDA=GPIO%d SCL=GPIO%d and address 0x3C\n", PIN_I2C_SDA, PIN_I2C_SCL);
    while (true) delay(1000);
  }
 #ifdef DIRECT_OLED
  Serial.printf("ESP32 tuner ready. INMP441 I2S: BCLK=%d WS=%d SD=%d at %lu Hz.\n", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DATA, (unsigned long)AUDIO_SAMPLE_RATE);
 #else
  Serial.printf("ESP32 tuner ready. Audio input: GPIO%d.\n", PIN_AUDIO);
 #endif
}

void loop() {
  if (digitalRead(PIN_STOP) == LOW) running = false;
  else if (digitalRead(PIN_START) == LOW) running = true;
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
      else if (strcmp(serialLine, "REC") == 0) recordAudio();
#endif
      serialLength = 0;
    } else if (c != '\r') {
      if (serialLength < sizeof(serialLine) - 1) serialLine[serialLength++] = c;
      else serialLength = 0;
    }
  }
  bool bridgeActive = bridgeSeen && (uint32_t)(millis() - bridgeAt) < 1500;
  float hz = running ? (bridgeActive ? bridgeHz : estimateFrequency()) : 0;
#ifdef DIRECT_OLED
  if (running && !bridgeActive && (uint32_t)(millis() - lastAudioReport) >= 1000) {
    Serial.printf("I2S min=%u max=%u mean=%.0f span=%u rms=%.1f gate=%.1f quality=%.2f rate=%.0f pitch=%.1f Hz\n", audioMin, audioMax, audioMean, audioMax - audioMin, pitchDetector.rms, pitchDetector.threshold, pitchDetector.quality, sampleRate, hz);
    lastAudioReport = millis();
  }
#endif
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  char noteLine[32] = "--  --c";
  float cents = 0;
  bool pitchFound = running && hz > 0 && hz <= 1000;
  if (pitchFound) {
    float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    float nearest = roundf(midi);
    cents = (midi - nearest) * 100.0f;
    const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int note = (int)nearest;
    int pitchClass = ((note % 12) + 12) % 12;
    char noteLabel[16];
    snprintf(noteLabel, sizeof(noteLabel), "%s%d", notes[pitchClass], note / 12 - 1);
    int centsRounded = (int)roundf(constrain(cents, -50.0f, 50.0f));
    snprintf(noteLine, sizeof(noteLine), "%s %+dc", noteLabel, centsRounded);
    Serial.printf("%.2f Hz, %s, %+.1f cents\n", hz, noteLabel, cents);
  }
  display.setTextSize(2);
  display.setCursor((128 - (int)strlen(noteLine) * 12) / 2, 0);
  display.print(noteLine);

  // The top 16 OLED rows are the note readout; keep the tuning scale below.
  display.drawFastHLine(8, 42, 112, SSD1306_WHITE);
  display.drawFastVLine(64, 20, 34, SSD1306_WHITE);
  display.drawFastVLine(58, 35, 15, SSD1306_WHITE);
  display.drawFastVLine(70, 35, 15, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("-50c");
  display.setCursor(50, 56);
  display.print("-5");
  display.setCursor(69, 56);
  display.print("+5");
  display.setCursor(101, 56);
  display.print("+50c");
  if (pitchFound) {
    int marker = 64 + (int)roundf(constrain(cents, -50.0f, 50.0f) * 1.12f);
    display.fillTriangle(marker, 38, marker - 4, 30, marker + 4, 30, SSD1306_WHITE);
  }
  display.display();
  delay(80);
}
