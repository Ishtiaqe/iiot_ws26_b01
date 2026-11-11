#pragma once
#include <cmath>
#include <cstdint>

struct PitchDetector {
#ifdef DIRECT_OLED
  float noise = 6.2f; // Starting RMS noise estimate after MIC_SCALE_SHIFT;
                      // adjust if quiet-room readings differ greatly.
#else
  float noise = 3.0f;
#endif
  float rms = 0;
  float threshold = 0;
  float quality = 0;
  bool active = false;

  float process(const uint16_t *samples, int n, float rate) {
    float mean = 0;
    for (int i = 0; i < n; ++i)
      mean += samples[i];
    mean /= n;
    float energy = 0;
    for (int i = 0; i < n; ++i) {
      float v = samples[i] - mean;
      energy += v * v;
    }
    rms = sqrtf(energy / n);
#ifdef DIRECT_OLED
    // The floor prevents silence from triggering; multipliers make the gate
    // track changing room noise.
    threshold = active ? fmaxf(7.0f, noise * 1.15f) : fmaxf(8.0f, noise * 1.3f);
#else
    threshold = fmaxf(8.0f, noise * (active ? 2.0f : 3.0f));
#endif
    active = rms >= threshold;
    if (!active) {
      float bounded = fminf(rms, noise * 1.5f + 1.0f);
      noise += (bounded - noise) * (bounded < noise ? 0.08f : 0.015f);
      quality = 0;
      return 0;
    }
    constexpr int maxLag = 250; // At 16 kHz this covers notes down to about 64
                                // Hz; raise it for lower notes.
    float difference[maxLag + 1];
    difference[0] = 1;
    float cumulative = 0;
    for (int lag = 1; lag <= maxLag; ++lag) {
      float sum = 0;
      for (int i = 0; i < n - maxLag; ++i) {
        float d = (float)samples[i] - samples[i + lag];
        sum += d * d;
      }
      cumulative += sum;
      difference[lag] = cumulative > 0 ? sum * lag / cumulative : 1;
    }
    int firstLag = (int)(rate / 550.0f); // Search from 550 Hz down to 65 Hz;
                                         // adjust for the instrument's range.
    int lastLag = (int)(rate / 65.0f);
    if (firstLag < 2)
      firstLag = 2;
    if (lastLag > maxLag - 1)
      lastLag = maxLag - 1;
    for (int lag = firstLag; lag <= lastLag; ++lag) {
      if (difference[lag] <
          0.18f) { // Lower values reject more noisy/non-periodic windows;
                   // higher values are more permissive.
        while (lag < lastLag && difference[lag + 1] < difference[lag])
          ++lag;
        float a = difference[lag - 1], b = difference[lag],
              c = difference[lag + 1];
        float denom = a - 2 * b + c;
        float offset = fabsf(denom) > 1e-9f ? 0.5f * (a - c) / denom : 0;
        if (offset < -1)
          offset = -1;
        if (offset > 1)
          offset = 1;
        quality = 1 - b;
        return rate / (lag + offset);
      }
    }
    quality = 0;
    return 0;
  }
};
