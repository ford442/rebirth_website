#pragma once

// Test-only spectral analysis helpers (RMS + spectral centroid via a small
// self-contained radix-2 FFT). Not part of the real-time audio path — used
// solely by the golden-audio regression test to compare renders without
// requiring bit-exact sample matches.

#include <cmath>
#include <cstddef>
#include <vector>

namespace rb338::test {

// Minimal iterative radix-2 Cooley-Tukey FFT (textbook algorithm, no
// external dependency). `re`/`im` must have the same power-of-two size.
inline void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
  const size_t n = re.size();
  if (n <= 1) return;

  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }

  constexpr float kTwoPi = 6.283185307179586f;
  for (size_t len = 2; len <= n; len <<= 1) {
    const float ang = -kTwoPi / static_cast<float>(len);
    const float wRe = std::cos(ang);
    const float wIm = std::sin(ang);
    const size_t half = len / 2;
    for (size_t i = 0; i < n; i += len) {
      float curRe = 1.0f;
      float curIm = 0.0f;
      for (size_t k = 0; k < half; ++k) {
        const float uRe = re[i + k];
        const float uIm = im[i + k];
        const float vRe = re[i + k + half] * curRe - im[i + k + half] * curIm;
        const float vIm = re[i + k + half] * curIm + im[i + k + half] * curRe;
        re[i + k] = uRe + vRe;
        im[i + k] = uIm + vIm;
        re[i + k + half] = uRe - vRe;
        im[i + k + half] = uIm - vIm;
        const float nextRe = curRe * wRe - curIm * wIm;
        const float nextIm = curRe * wIm + curIm * wRe;
        curRe = nextRe;
        curIm = nextIm;
      }
    }
  }
}

/** RMS level of a sample buffer. */
inline float rms(const std::vector<float>& samples) {
  if (samples.empty()) return 0.0f;
  double sumSq = 0.0;
  for (float s : samples) sumSq += static_cast<double>(s) * static_cast<double>(s);
  return static_cast<float>(std::sqrt(sumSq / static_cast<double>(samples.size())));
}

/**
 * Amplitude-weighted spectral centroid (Hz) of a Hann-windowed FFT over the
 * largest power-of-two prefix of `samples`.
 */
inline float spectralCentroidHz(const float* samples, size_t count, float sampleRate) {
  size_t n = 1;
  while (n * 2 <= count) n *= 2;
  if (n < 2) return 0.0f;

  constexpr float kTwoPi = 6.283185307179586f;
  std::vector<float> re(n);
  std::vector<float> im(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    const float w = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) /
                                            static_cast<float>(n - 1));
    re[i] = samples[i] * w;
  }
  fftRadix2(re, im);

  double weightedSum = 0.0;
  double magSum = 0.0;
  for (size_t k = 1; k < n / 2; ++k) {
    const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    const float freq = static_cast<float>(k) * sampleRate / static_cast<float>(n);
    weightedSum += static_cast<double>(mag) * static_cast<double>(freq);
    magSum += mag;
  }
  if (magSum <= 1e-9) return 0.0f;
  return static_cast<float>(weightedSum / magSum);
}

} // namespace rb338::test
