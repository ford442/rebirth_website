#pragma once

// GoldenCompare — distance between a fresh render and a golden reference.
//
// A ReBirth recording and this engine will never agree sample for sample
// (different oscillators, a different noise source, a soundcard in the
// path), so the comparison is perceptual rather than bitwise:
//
//   - spectralDistanceDb: a short-time spectrum in 32 log-spaced bands
//     (40 Hz – 16 kHz, 2048-point Hann frames, hop 512), each band in dB;
//     the mean absolute difference over every band and frame where either
//     signal is above −80 dB. Bands make it tolerant of small pitch and
//     filter offsets that a bin-by-bin distance would punish.
//   - envelopeCorrelation: Pearson correlation of the two RMS envelopes in
//     dB (256-sample windows, floored at −80 dB) — the rhythm, accents,
//     gates and decays. In dB a quiet hat counts as much as a loud kick, so
//     a shifted off-beat is not hidden under an unshifted downbeat.
//
// Test-only. It builds on SpectralAnalysis.h's radix-2 FFT, so nothing is
// vendored and nothing here links into rbsParser.wasm.

#include "SpectralAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rb338::test {

constexpr size_t kGoldenFrame = 2048;
constexpr size_t kGoldenHop = 512;
constexpr size_t kGoldenBands = 32;
constexpr double kGoldenFloorDb = -100.0;
constexpr double kGoldenGateDb = -80.0;

/** Band energies in dB, frame-major: result[frame * kGoldenBands + band]. */
inline std::vector<double> bandSpectrogram(const std::vector<float>& signal, float sampleRate) {
  std::vector<double> out;
  if (signal.size() < kGoldenFrame) return out;
  // Band edges: geometric between 40 Hz and 16 kHz (clamped to Nyquist).
  std::vector<size_t> edges(kGoldenBands + 1);
  const double lo = 40.0;
  const double hi = std::min(16000.0, sampleRate * 0.5);
  for (size_t b = 0; b <= kGoldenBands; ++b) {
    const double hz = lo * std::pow(hi / lo, static_cast<double>(b) / kGoldenBands);
    edges[b] = std::max<size_t>(1, static_cast<size_t>(hz * kGoldenFrame / sampleRate));
  }
  std::vector<float> window(kGoldenFrame);
  for (size_t i = 0; i < kGoldenFrame; ++i) {
    window[i] = 0.5f - 0.5f * std::cos(6.283185307179586f * i / (kGoldenFrame - 1));
  }
  std::vector<float> re(kGoldenFrame);
  std::vector<float> im(kGoldenFrame);
  for (size_t start = 0; start + kGoldenFrame <= signal.size(); start += kGoldenHop) {
    for (size_t i = 0; i < kGoldenFrame; ++i) {
      re[i] = signal[start + i] * window[i];
      im[i] = 0.0f;
    }
    fftRadix2(re, im);
    for (size_t b = 0; b < kGoldenBands; ++b) {
      double energy = 0.0;
      const size_t end = std::max(edges[b] + 1, edges[b + 1]);
      for (size_t k = edges[b]; k < end && k < kGoldenFrame / 2; ++k) {
        energy += static_cast<double>(re[k]) * re[k] + static_cast<double>(im[k]) * im[k];
      }
      // Normalised so a full-scale sine reads about 0 dB in its band.
      const double norm = energy / (kGoldenFrame * kGoldenFrame / 16.0);
      out.push_back(std::max(kGoldenFloorDb, 10.0 * std::log10(norm + 1e-30)));
    }
  }
  return out;
}

/** Mean |dB difference| over the bands and frames either signal fills. */
inline double spectralDistanceDb(const std::vector<float>& a, const std::vector<float>& b,
                                 float sampleRate) {
  const auto sa = bandSpectrogram(a, sampleRate);
  const auto sb = bandSpectrogram(b, sampleRate);
  const size_t n = std::min(sa.size(), sb.size());
  double sum = 0.0;
  size_t counted = 0;
  for (size_t i = 0; i < n; ++i) {
    if (std::max(sa[i], sb[i]) < kGoldenGateDb) continue;
    sum += std::fabs(sa[i] - sb[i]);
    ++counted;
  }
  return counted ? sum / static_cast<double>(counted) : 0.0;
}

inline std::vector<double> rmsEnvelope(const std::vector<float>& signal, size_t window = 256) {
  std::vector<double> env;
  for (size_t start = 0; start + window <= signal.size(); start += window) {
    double sum = 0.0;
    for (size_t i = 0; i < window; ++i) sum += static_cast<double>(signal[start + i]) * signal[start + i];
    const double rms = std::sqrt(sum / static_cast<double>(window));
    env.push_back(std::max(kGoldenGateDb, 20.0 * std::log10(rms + 1e-30)));
  }
  return env;
}

/** Pearson correlation of the two dB RMS envelopes (1 = the same shape). */
inline double envelopeCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
  const auto ea = rmsEnvelope(a);
  const auto eb = rmsEnvelope(b);
  const size_t n = std::min(ea.size(), eb.size());
  if (n < 2) return 0.0;
  double ma = 0.0;
  double mb = 0.0;
  for (size_t i = 0; i < n; ++i) {
    ma += ea[i];
    mb += eb[i];
  }
  ma /= static_cast<double>(n);
  mb /= static_cast<double>(n);
  double cov = 0.0;
  double va = 0.0;
  double vb = 0.0;
  for (size_t i = 0; i < n; ++i) {
    cov += (ea[i] - ma) * (eb[i] - mb);
    va += (ea[i] - ma) * (ea[i] - ma);
    vb += (eb[i] - mb) * (eb[i] - mb);
  }
  if (va <= 0.0 || vb <= 0.0) return (va == vb) ? 1.0 : 0.0;
  return cov / std::sqrt(va * vb);
}

} // namespace rb338::test
