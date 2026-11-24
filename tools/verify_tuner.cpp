// Build from the project root: c++ -std=c++17 -O2 -DDIRECT_OLED tools/verify_tuner.cpp -o /tmp/verify-tuner
#include "../src/pitch.h"
#include "../src/tuner_display.h"
#include <array>
#include <random>
#include <vector>
#include <algorithm>
#include <string>
using Frame = std::array<int16_t, 1024>;
constexpr double rate = 16000;
constexpr double tau = 6.283185307179586;
std::mt19937 rng(20261124);
std::normal_distribution<double> gaussian(0, 1);

Frame makeFrame(double hz, double start, double snr) {
  Frame samples;
  double tone[1024], noise[1024], signalEnergy = 0, noiseEnergy = 0;
  for (int i = 0; i < 1024; ++i) {
    double phase = tau * hz * (start + i / rate);
    tone[i] = 500 * (sin(phase) + 0.55*sin(2*phase+0.2) + 0.25*sin(3*phase+0.3));
    noise[i] = gaussian(rng);
    signalEnergy += tone[i]*tone[i];
    noiseEnergy += noise[i]*noise[i];
  }
  double gain = sqrt(signalEnergy/noiseEnergy) * pow(10, -snr/20);
  for (int i = 0; i < 1024; ++i) samples[i] = lround(tone[i]+gain*noise[i]);
  return samples;
}

int main() {
  if (tunerSample(-8 * 65536) != -1 || tunerSample(8 * 65536) != 1) return 1;
  bool passed = true;
  PitchDetector detector;
  for (double snr : {300., 15., 5.}) {
    int count = 0, accurate = 0, correct = 0, rejected = 0;
    std::vector<double> errors;
    for (int midi = 36; midi <= 89; ++midi) {
      for (int cents : {-20, 0, 20}) {
        double hz = 440 * exp2((midi-69+cents/100.)/12.);
        if (hz < 65 || hz > 1400) continue;
        Frame samples = makeFrame(hz, count*0.037, snr);
        double found = detector.process(samples.data(), samples.size(), rate);
        ++count;
        if (!tuningReading(found,true).note[0]) { ++rejected; continue; }
        double error = fabs(1200*log2(found/hz));
        errors.push_back(error);
        accurate += error <= 5;
        correct += lround(69+12*log2(found/440.)) == midi;
      }
    }
    std::sort(errors.begin(), errors.end());
    printf("SNR %.0f: %d trials, %d within 5c, %d correct IDs, %d rejected",
           snr,count,accurate,correct,rejected);
    if (!errors.empty()) printf(", max accepted error %.2fc", errors.back());
    puts("");
    if (snr > 100) passed &= accurate >= 0.9 * count;
    if (snr == 15) passed &= correct >= 0.9 * count;
  }
  for (double hz : {65.,1400.}) {
    auto frame = makeFrame(hz,0,300);
    printf("Boundary %.0f Hz: %.3f Hz\n", hz, detector.process(frame.data(),1024,rate));
  }
  int falseLabels = 0;
  for (int i = 0; i < 90; ++i) {
    Frame frame;
    for (auto &x : frame) x = i < 30 ? 0 : i < 60 ? lround(2*gaussian(rng)) : lround(60*gaussian(rng));
    falseLabels += tuningReading(detector.process(frame.data(),1024,rate),true).note[0] != 0;
  }
  printf("Silent/weak/noise-only frames: %d false labels / 90\n",falseLabels);
  passed &= falseLabels == 0;
  int maxFlips = 0, lost = 0;
  for (int run = 0; run < 30; ++run) {
    double hz = 440 * exp2((40+run-69)/12.);
    std::string previous;
    int flips = 0;
    for (int j = 0; j < 47; ++j) {
      auto frame = makeFrame(hz, j*1024/rate,15);
      auto reading = tuningReading(detector.process(frame.data(),1024,rate),true);
      if (!reading.note[0]) ++lost;
      if (j && previous != reading.note) ++flips;
      previous = reading.note;
    }
    maxFlips = std::max(flips,maxFlips);
  }
  printf("30 stable notes x 3.008 s: max %d flips, %d empty frames\n",maxFlips,lost);
  passed &= maxFlips <= 1 && lost == 0;
  for(double cents : {-20.,0.,20.}) {
    auto frame = makeFrame(110*exp2(cents/1200),0,300);
    auto r = tuningReading(detector.process(frame.data(),1024,rate),true);
    passed &= std::string(r.note) == "A2" && r.cents == cents;
    passed &= std::string(r.action) == (cents < 0 ? "TUNE UP" : cents > 0 ? "TUNE DOWN" : "IN TUNE");
    printf("A2 %+.0fc: %s %+dc, %s, x=%d\n",cents,r.note,r.cents,r.action,r.marker);
  }
  return passed ? 0 : 1;
}
