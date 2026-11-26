#pragma once

#include <cmath>
#include <cstdint>

struct PitchEstimate {
  float frequency = 0;
  float clarity = 0;
  float rms = 0;
};

// The INMP441 sends signed 24-bit data in the high bits of its 32-bit slot.
// Express it in 16-bit PCM units without rounding away the quiet bits.
inline float microphoneSample(int32_t slot) {
  return (slot >> 8) * (1.0f / 256.0f);
}

template<int WindowSamples>
class PitchDetector {
  static_assert(WindowSamples >= 16 && WindowSamples % 2 == 0);
  static constexpr int MAX_LAG = WindowSamples / 2;
  static constexpr float MIN_CLARITY = 0.72f;
  static constexpr float PEAK_FRACTION = 0.90f;
  static constexpr float NUMERICAL_FLOOR = 1.0f / 256.0f;

  float centered[WindowSamples];
  float energy[WindowSamples + 2];
  float correlation[MAX_LAG + 1];

public:
  PitchEstimate process(const float (&samples)[WindowSamples], float sampleRate) {
    float mean = 0;
    for (float sample : samples)
      mean += sample;
    mean /= WindowSamples;

    // Separate cumulative sums for even and odd samples let the dot product
    // visit every other pair while still evaluating every possible lag.
    energy[0] = energy[1] = 0;
    for (int i = 0; i < WindowSamples; ++i) {
      const float sample = samples[i] - mean;
      centered[i] = sample;
      energy[i + 2] = energy[i] + sample * sample;
    }
    const float rms = sqrtf((energy[WindowSamples] + energy[WindowSamples + 1]) /
                            WindowSamples);
    if (rms <= NUMERICAL_FLOOR)
      return {0, 0, rms};

    correlation[0] = 1;
    for (int lag = 1; lag <= MAX_LAG; ++lag) {
      const int span = (WindowSamples - lag + 1) & ~1;
      float dot = 0;
      for (int i = 0; i < span; i += 2)
        dot += centered[i] * centered[i + lag];
      const float referenceEnergy = energy[span];
      const float shiftedEnergy = energy[lag + span] - energy[lag];
      const float product = referenceEnergy * shiftedEnergy;
      correlation[lag] = product > 0 ? dot / sqrtf(product) : 0;
    }

    // McLeod-style peak selection, using geometric energy normalization
    // so a change in amplitude does not itself count as a pitch mismatch.
    int firstPeak = 2;
    while (firstPeak < MAX_LAG && correlation[firstPeak - 1] > 0)
      ++firstPeak;
    float strongest = 0;
    for (int lag = firstPeak; lag < MAX_LAG; ++lag) {
      if (isPeak(lag))
        strongest = fmaxf(strongest, correlation[lag]);
    }
    const float minimumPeak = fmaxf(MIN_CLARITY, strongest * PEAK_FRACTION);
    for (int lag = firstPeak; lag < MAX_LAG; ++lag) {
      if (!isPeak(lag) || correlation[lag] < minimumPeak)
        continue;
      const float before = correlation[lag - 1];
      const float peak = correlation[lag];
      const float after = correlation[lag + 1];
      const float curvature = before - 2 * peak + after;
      const float shift = curvature < -1e-6f ? 0.5f * (before - after) / curvature : 0;
      return {sampleRate / fmaxf(2.0f, lag + shift), fminf(1.0f, peak), rms};
    }
    return {0, strongest, rms};
  }

private:
  bool isPeak(int lag) const {
    return correlation[lag] >= correlation[lag - 1] &&
           correlation[lag] > correlation[lag + 1];
  }
};
