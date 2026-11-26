#pragma once
#include <cmath>
#include <cstdint>

// Keep the high 16 bits of the signed 24-bit I²S sample.
inline int16_t tunerSample(int32_t leftSlot) {
  return static_cast<int16_t>(leftSlot >> 16);
}

class PitchDetector {
  float noise = 18.0f; // Initial estimate in 16-bit PCM units.
  bool active = false;

public:
  float process(const int16_t *samples, int n, float rate) {
    constexpr int maxLag = 250;
    if (!samples || n <= maxLag || !(rate >= 2800 && rate <= 16120)) {
      active = false;
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
    const float rms = sqrtf(energy / n);
    // The floor prevents silence from triggering; multipliers make the gate
    // track changing room noise.
    const float threshold = active ? fmaxf(16.0f, noise * 1.15f) : fmaxf(24.0f, noise * 1.3f);
    active = rms >= threshold;
    if (!active) {
      float bounded = fminf(rms, noise * 1.5f + 8.0f);
      noise += (bounded - noise) * (bounded < noise ? 0.08f : 0.015f);
      return 0;
    }
    // Compare the frame with delayed copies. A low difference means the
    // waveform repeats after that many samples. Normalize each score by the
    // average of the scores so far, as in the YIN pitch estimator.
    float previousRaw = 0;
    float candidateRaw = 0;
    float previousScore = 1;
    float candidateScore = 1;
    float cumulative = 0;
    const int firstLag = static_cast<int>(rate / 1400.0f);
    const int lastLag = static_cast<int>(ceilf(rate / 65.0f));
    for (int lag = 1; lag <= maxLag && lag <= lastLag + 1; ++lag) {
      float sum = 0;
      for (int i = 0; i < n - maxLag; ++i) {
        const float delta = static_cast<float>(samples[i]) - samples[i + lag];
        sum += delta * delta;
      }
      cumulative += sum;
      const float score = cumulative > 0 ? sum * lag / cumulative : 1;
      // Look one lag ahead to confirm a local minimum. Stop as soon as
      // one passes the threshold, so higher notes require less work.
      const int candidateLag = lag - 1;
      if (candidateLag >= firstLag && candidateScore < 0.18f &&
          candidateScore <= previousScore && candidateScore < score) {
        const float curvature = previousRaw - 2 * candidateRaw + sum;
        const float offset = fabsf(curvature) > 1e-9f
                                 ? 0.5f * (previousRaw - sum) / curvature : 0;
        return rate / (candidateLag + fminf(1, fmaxf(-1, offset)));
      }
      previousRaw = candidateRaw;
      candidateRaw = sum;
      previousScore = candidateScore;
      candidateScore = score;
    }
    return 0;
  }
};
