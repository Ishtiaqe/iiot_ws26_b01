#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <cstring>

// Tuning and display policy. The in-tune thresholds apply to integer cents
// shown to the player and are inclusive.
constexpr int IN_TUNE_CENTS = 5;
constexpr float REFERENCE_A4_HZ = 440.0f;
// The 112-pixel scale spans 100 cents, with zero at pixel 64.
constexpr float PIXELS_PER_CENT = 1.12f;

constexpr uint32_t READING_HOLD_MS = 5000;
constexpr float NOTE_SWITCH_MARGIN_CENTS = 3.0f;
constexpr float FIRST_NOTE_CLARITY = 0.90f;

// Ready means changing samples were seen recently; NoData means the I²S stream
// stayed constant; ReadError means the driver failed to return a whole block.
enum class MicStatus { Ready, NoData, ReadError };

/** Map frame RMS to a compact 0–12 level meter for the OLED. */
inline int inputMeterLevel(float rms) {
  if (rms < 0.5f)
    return 0;
  // Each bar represents approximately 6 dB of input level.
  return static_cast<int>(fminf(12.0f, 1.0f + floorf(log2f(rms * 2.0f))));
}

struct TuningReading {
  int note = -1; // MIDI note number, or -1 when the dial is clear.
  int cents = 0;
  int marker = 64;
  bool held = false;
  bool stopped = false;
  int inputLevel = 0;
  MicStatus micStatus = MicStatus::Ready;

  // Equality is based on visible state so the firmware can skip redundant
  // OLED transfers, which are slower than updating this small structure.
  bool sameDisplayAs(const TuningReading &other) const {
    return note == other.note && cents == other.cents &&
           marker == other.marker && held == other.held && stopped == other.stopped &&
           micStatus == other.micStatus && inputLevel == other.inputLevel;
  }
};

/** Convert noisy per-frame estimates into a stable, time-aware UI reading.
 *
 * This class handles note acquisition/switch confirmation, cents smoothing,
 * brief dropouts, and expiry. Call accept() when a new audio estimate arrives
 * and render() at the display refresh rate. Timestamps are millis() values;
 * unsigned subtraction keeps elapsed-time checks safe across timer rollover.
 */
class PitchTracker {
public:
  /** Incorporate one detector result captured at sampleTime. */
  void accept(float frequency, float clarity, uint32_t sampleTime) {
    expire(sampleTime);
    if (!(frequency > 0) || !std::isfinite(frequency)) {
      pendingNote = -1;
      reading.held = reading.note >= 0;
      return;
    }

    // Convert frequency to the fractional MIDI note scale, where MIDI 69 is
    // A4. The chosen note is held near its half-semitone boundary to avoid
    // flickering between adjacent labels as the pitch wobbles.
    const float pitch = 69.0f + 12.0f * log2f(frequency / REFERENCE_A4_HZ);
    const float centsFromCurrent = 100.0f * (pitch - reading.note);
    const int note = fabsf(centsFromCurrent) <= 50.0f + NOTE_SWITCH_MARGIN_CENTS
                         ? reading.note : lroundf(pitch);
    if (note != reading.note) {
      // A clear pluck can acquire from idle in one window. Confirm weaker
      // estimates and changes to a note that is already on screen.
      const bool clearFirstNote = reading.note < 0 && clarity >= FIRST_NOTE_CLARITY;
      if (note != pendingNote && !clearFirstNote) {
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

  /** Update smoothing/expiry and return the state to render at time now.
   * When running is false the returned reading is cleared and marked stopped.
   */
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

  // Drop a stale note after its hold interval; brief losses are handled by
  // render() as a held reading before this longer timeout is reached.
  void expire(uint32_t now) {
    if (reading.note >= 0 && now - lastAcceptedAt >= READING_HOLD_MS) {
      reading = {};
      pendingNote = -1;
    }
  }

  // Clamp the pointer to the visible scale even when the pitch is far out.
  float markerPosition() const {
    return fminf(120.0f, fmaxf(8.0f, 64.0f + shownCents * PIXELS_PER_CENT));
  }

  static int stableRound(float value, int previous) {
    // Hysteresis across pixel and cent boundaries prevents one-step chatter.
    return fabsf(value - previous) > 0.75f ? lroundf(value) : previous;
  }
};

/** Draw one complete tuner frame to an Adafruit_SSD1306-compatible display. */
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
  // Level 5 is roughly rms 8; lower levels are within the INMP441 noise floor.
  if (!reading.stopped && reading.inputLevel >= 5)
    action = "SOUND, NO PITCH";
  if (!reading.stopped && reading.micStatus == MicStatus::NoData)
    action = "CHECK MIC WIRING";
  if (!reading.stopped && reading.micStatus == MicStatus::ReadError)
    action = "MIC I2S ERROR";
  if (reading.note >= 0) {
    constexpr const char *names[] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    snprintf(title, sizeof(title), "%s%d %+dc", names[(reading.note % 12 + 12) % 12],
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
  if (reading.inputLevel > 0)
    screen.fillRect(8, 51, reading.inputLevel * 3, 2, 1);
  screen.setCursor(4, 55); screen.print("-50");
  centered("0", 55, 1);
  screen.setCursor(103, 55); screen.print("+50");
}
