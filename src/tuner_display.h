#pragma once
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>

// Displayed cents are rounded to whole numbers. Both limits are inclusive.
constexpr int IN_TUNE_CENTS = 5;
constexpr float REFERENCE_A4_HZ = 440.0f;
// The 112-pixel scale spans 100 cents, with zero at pixel 64.
constexpr float PIXELS_PER_CENT = 1.12f;

struct TuningReading {
  char note[8] = {};
  int cents = 0;
  int marker = 64;
  const char *action = "PLUCK ONE STRING";
};

inline TuningReading tuningReading(float hz, bool running) {
  TuningReading result;
  if (!running) {
    result.action = "STOPPED";
    return result;
  }
  // Allow small estimator rounding errors at the specified range boundaries.
  if (!(hz >= 64.8f && hz <= 1405.0f)) return result;
  // A4 is MIDI note 69. Each octave doubles the frequency and spans 12 notes.
  const float midi = 69 + 12 * log2f(hz / REFERENCE_A4_HZ);
  const int nearest = lroundf(midi);
  const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  snprintf(result.note, sizeof(result.note), "%s%d", names[nearest % 12], nearest / 12 - 1);
  result.cents = lroundf((midi - nearest) * 100);
  result.marker = 64 + lroundf(result.cents * PIXELS_PER_CENT);
  result.action = "IN TUNE";
  if (result.cents < -IN_TUNE_CENTS) result.action = "TUNE UP";
  if (result.cents > IN_TUNE_CENTS) result.action = "TUNE DOWN";
  return result;
}

template<typename Screen>
void drawTuner(Screen &screen, const TuningReading &reading) {
  screen.clearDisplay();
  screen.setTextColor(1);
  screen.setTextWrap(false);
  auto centered = [&](const char *text, int y, int size) {
    screen.setTextSize(size);
    screen.setCursor((128 - int(strlen(text)) * 6 * size) / 2, y);
    screen.print(text);
  };
  char title[20] = "--";
  if (reading.note[0])
    snprintf(title, sizeof(title), "%s %+dc", reading.note, reading.cents);
  centered(title, 0, 2);
  centered(reading.action, 19, 1);
  screen.drawFastHLine(8, 43, 113, 1);
  const int band = lroundf(IN_TUNE_CENTS * PIXELS_PER_CENT);
  for (int tick : {8, 64 - band, 64, 64 + band, 120})
    screen.drawFastVLine(tick, tick == 64 ? 36 : 40, tick == 64 ? 14 : 7, 1);
  if (reading.note[0])
    screen.fillTriangle(reading.marker, 41, reading.marker - 4, 32, reading.marker + 4, 32, 1);
  screen.setCursor(4, 55); screen.print("-50");
  centered("0", 55, 1);
  screen.setCursor(103, 55); screen.print("+50");
}
