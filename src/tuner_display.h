#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <cstring>

// Displayed cents are rounded to whole numbers. Both limits are inclusive.
constexpr int IN_TUNE_CENTS = 5;
constexpr float REFERENCE_A4_HZ = 440.0f;
// The 112-pixel scale spans 100 cents, with zero at pixel 64.
constexpr float PIXELS_PER_CENT = 1.12f;

constexpr uint32_t READING_HOLD_MS = 5000;
constexpr float NOTE_SWITCH_MARGIN_CENTS = 3.0f;

enum class MicStatus { Ready, NoData, ReadError };

struct TuningReading {
  int note = -1; // MIDI note number, or -1 when the dial is clear.
  int cents = 0;
  int marker = 64;
  bool held = false;
  bool stopped = false;
  MicStatus micStatus = MicStatus::Ready;

  bool sameDisplayAs(const TuningReading &other) const {
    return note == other.note && cents == other.cents &&
           marker == other.marker && held == other.held && stopped == other.stopped &&
           micStatus == other.micStatus;
  }
};

class PitchTracker {
public:
  void accept(float frequency, uint32_t sampleTime) {
    expire(sampleTime);
    if (!(frequency >= 64.8f && frequency <= 1405.0f)) {
      pendingNote = -1;
      reading.held = reading.note >= 0;
      return;
    }

    const float pitch = 69.0f + 12.0f * log2f(frequency / REFERENCE_A4_HZ);
    const float centsFromCurrent = 100.0f * (pitch - reading.note);
    const int note = fabsf(centsFromCurrent) <= 50.0f + NOTE_SWITCH_MARGIN_CENTS
                         ? reading.note : lroundf(pitch);
    if (note != reading.note) {
      // Confirm a new note before jumping across the dial or changing octaves.
      if (note != pendingNote) {
        pendingNote = note;
        reading.held = reading.note >= 0;
        return;
      }
      reading.note = note;
      shownCents = 100.0f * (pitch - note);
      reading.cents = lroundf(shownCents);
      reading.marker = lroundf(markerPosition());
    }

    pendingNote = -1;
    targetCents = 100.0f * (pitch - note);
    lastAcceptedAt = sampleTime;
    reading.held = false;
  }

  TuningReading render(uint32_t now, bool running) {
    if (!running) {
      reading = {};
      pendingNote = -1;
      reading.stopped = true;
    } else {
      reading.stopped = false;
      expire(now);
      if (reading.note >= 0 && now - lastAcceptedAt > TRACKING_GAP_MS)
        reading.held = true;
      if (reading.note >= 0 && !reading.held) {
        const float error = targetCents - shownCents;
        // Smooth small fluctuations more than deliberate tuning movements.
        const float timeConstantMs = fabsf(error) >= 10.0f ? 50.0f : 120.0f;
        const uint32_t elapsedMs = now - lastRenderAt;
        const float stepMs = elapsedMs > 50 ? 50.0f : elapsedMs;
        shownCents -= expm1f(-stepMs / timeConstantMs) * error;
        reading.cents = stableRound(shownCents, reading.cents);
        reading.marker = stableRound(markerPosition(), reading.marker);
      }
    }
    lastRenderAt = now;
    return reading;
  }

private:
  static constexpr uint32_t TRACKING_GAP_MS = 100;
  TuningReading reading;
  int pendingNote = -1;
  float targetCents = 0;
  float shownCents = 0;
  uint32_t lastAcceptedAt = 0;
  uint32_t lastRenderAt = 0;

  void expire(uint32_t now) {
    if (reading.note >= 0 && now - lastAcceptedAt >= READING_HOLD_MS) {
      reading = {};
      pendingNote = -1;
    }
  }

  float markerPosition() const {
    return fminf(120.0f, fmaxf(8.0f, 64.0f + shownCents * PIXELS_PER_CENT));
  }

  static int stableRound(float value, int previous) {
    // Hysteresis across pixel and cent boundaries prevents one-step chatter.
    return fabsf(value - previous) > 0.75f ? lroundf(value) : previous;
  }
};

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
  const char *action = reading.stopped ? "STOPPED" : "PLUCK ONE STRING";
  if (!reading.stopped && reading.micStatus == MicStatus::NoData)
    action = "CHECK MIC WIRING";
  if (!reading.stopped && reading.micStatus == MicStatus::ReadError)
    action = "MIC I2S ERROR";
  if (reading.note >= 0) {
    constexpr const char *names[] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    snprintf(title, sizeof(title), "%s%d %+dc", names[reading.note % 12],
             reading.note / 12 - 1, reading.cents);
    if (reading.held)
      action = "LAST READING";
    else if (reading.cents < -IN_TUNE_CENTS)
      action = "TUNE UP";
    else if (reading.cents > IN_TUNE_CENTS)
      action = "TUNE DOWN";
    else
      action = "IN TUNE";
  }
  centered(title, 0, 2);
  centered(action, 19, 1);
  screen.drawFastHLine(8, 43, 113, 1);
  const int band = lroundf(IN_TUNE_CENTS * PIXELS_PER_CENT);
  for (int tick : {8, 64 - band, 64, 64 + band, 120})
    screen.drawFastVLine(tick, tick == 64 ? 36 : 40, tick == 64 ? 14 : 7, 1);
  if (reading.note >= 0)
    screen.fillTriangle(reading.marker, 41, reading.marker - 4, 32, reading.marker + 4, 32, 1);
  screen.setCursor(4, 55); screen.print("-50");
  centered("0", 55, 1);
  screen.setCursor(103, 55); screen.print("+50");
}
