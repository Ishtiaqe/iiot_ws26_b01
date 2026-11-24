#pragma once
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>

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
  const float midi = 69 + 12 * log2f(hz / 440.0f);
  const int nearest = lroundf(midi);
  const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  snprintf(result.note, sizeof(result.note), "%s%d", names[nearest % 12], nearest / 12 - 1);
  result.cents = lroundf((midi - nearest) * 100);
  result.marker = 64 + lroundf(result.cents * 1.12f);
  result.action = result.cents < -5 ? "TUNE UP" : result.cents > 5 ? "TUNE DOWN" : "IN TUNE";
  return result;
}

template<typename Screen>
void drawTuner(Screen &screen, float hz, bool running) {
  const auto reading = tuningReading(hz, running);
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
  for (int tick : {8, 58, 64, 70, 120})
    screen.drawFastVLine(tick, tick == 64 ? 36 : 40, tick == 64 ? 14 : 7, 1);
  if (reading.note[0])
    screen.fillTriangle(reading.marker, 41, reading.marker - 4, 32, reading.marker + 4, 32, 1);
  screen.setCursor(4, 55); screen.print("-50");
  centered("0", 55, 1);
  screen.setCursor(103, 55); screen.print("+50");
}
