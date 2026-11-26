#pragma once
#include <cmath>
#include <cstdint>

// The left I2S slot contains signed PCM in the high bits. Keep the sign
// through the gain adjustment so quiet negative values remain near zero.
inline int16_t tunerSample(int32_t leftSlot) {
  return static_cast<int16_t>(leftSlot >> 16) >> 3;
}

struct PitchDetector {
  float noise = 6.2f; // Starting RMS noise estimate after digital scaling;
                      // adjust if quiet-room readings differ greatly.
  float rms = 0;
  float threshold = 0;
  float quality = 0;
  bool active = false;

  float process(const int16_t *samples, int n, float rate) {
    constexpr int maxLag = 250;
    if (!samples || n <= maxLag || !(rate >= 2800 && rate <= 16120)) {
      active = false;
      quality = 0;
      return 0;
    }
    // Remove the DC offset before measuring loudness (root mean square).
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
    // The floor prevents silence from triggering; multipliers make the gate
    // track changing room noise.
    threshold = active ? fmaxf(7.0f, noise * 1.15f) : fmaxf(8.0f, noise * 1.3f);
    active = rms >= threshold;
    if (!active) {
      float bounded = fminf(rms, noise * 1.5f + 1.0f);
      noise += (bounded - noise) * (bounded < noise ? 0.08f : 0.015f);
      quality = 0;
      return 0;
    }
    // Compare the frame with delayed copies. A low difference means the
    // waveform repeats after that many samples. Normalize each score by the
    // average of the scores so far, as in the YIN pitch estimator.
    float difference[maxLag + 1];
    float raw[maxLag + 1];
    difference[0] = 1;
    float cumulative = 0;
    for (int lag = 1; lag <= maxLag; ++lag) {
      float sum = 0;
      for (int i = 0; i < n - maxLag; ++i) {
        float d = (float)samples[i] - samples[i + lag];
        sum += d * d;
      }
      raw[lag] = sum;
      cumulative += sum;
      difference[lag] = cumulative > 0 ? sum * lag / cumulative : 1;
    }
    // Search short delays first to prefer one period over a multiple of it.
    // A score below 0.18 admits a candidate only if it is also a local minimum.
    const int firstLag = (int)(rate / 1400.0f);
    const int lastLag = (int)ceilf(rate / 65.0f);
    for (int lag = firstLag; lag <= lastLag && lag < maxLag; ++lag) {
      if (difference[lag] < 0.18f && difference[lag] <= difference[lag - 1] &&
          difference[lag] < difference[lag + 1]) {
        // Refine on the unnormalized trough to avoid a short-period bias.
        float a = raw[lag - 1], b = raw[lag], c = raw[lag + 1];
        float denom = a - 2 * b + c;
        float offset = fabsf(denom) > 1e-9f ? 0.5f * (a - c) / denom : 0;
        if (offset < -1)
          offset = -1;
        if (offset > 1)
          offset = 1;
        quality = 1 - difference[lag];
        return rate / (lag + offset);
      }
    }
    quality = 0;
    return 0;
  }
};
