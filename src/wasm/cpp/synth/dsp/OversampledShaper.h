#pragma once

#include "Denormal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace rb338::dsp {

/**
 * HalfBandFir — a 2× up/down-sampler built on one symmetric half-band FIR.
 *
 * A half-band filter has every other tap zero and the centre tap at 0.5, so
 * one polyphase branch of the interpolator is a pure delay and the other a
 * (taps + 1) / 2-tap FIR. Coefficients are a Kaiser-windowed sinc designed
 * once in design() on the main thread; upsample() / downsample() do no
 * allocation and no transcendental maths.
 *
 * `Taps` must be 4m + 3 so the centre tap sits on an odd index, which keeps
 * the odd interpolator phase a whole-sample delay (m base-rate samples) and
 * the round trip an integer (Taps − 1) / 2 base-rate samples.
 */
template <size_t Taps>
class HalfBandFir {
  static_assert(Taps % 4 == 3, "half-band taps must be 4m + 3");

public:
  static constexpr size_t kCentre = (Taps - 1) / 2;
  /** Nonzero taps on the FIR branch (the even indices). */
  static constexpr size_t kBranch = (Taps + 1) / 2;
  /** Up + down latency, in input-rate samples. */
  static constexpr size_t kRoundTripLatency = (Taps - 1) / 2;

  void design(float kaiserBeta) {
    std::array<double, Taps> h{};
    double sum = 0.0;
    const double i0Beta = besselI0(kaiserBeta);
    for (size_t j = 0; j < Taps; ++j) {
      const double n = static_cast<double>(j) - static_cast<double>(kCentre);
      const double t = n / static_cast<double>(kCentre);
      const double window = besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1.0 - t * t))) / i0Beta;
      const double x = n * 0.5;
      const double sinc = (n == 0.0) ? 1.0 : std::sin(kPi * x) / (kPi * x);
      h[j] = 0.5 * sinc * window;
      sum += h[j];
    }
    for (size_t k = 0; k < kBranch; ++k) {
      m_branch[k] = static_cast<float>(h[2 * k] / sum);
    }
    m_centreTap = static_cast<float>(h[kCentre] / sum);
    reset();
  }

  void reset() {
    m_upHistory.fill(0.0f);
    m_downHistory.fill(0.0f);
    m_upPos = 0;
    m_downPos = 0;
  }

  /** One input sample in, two output samples (earlier first) out. */
  void upsample(float x, float& first, float& second) {
    m_upPos = (m_upPos + kBranch - 1) % kBranch;
    m_upHistory[m_upPos] = x;
    // FIR branch: y[2n] = Σ 2·h[2k] · x[n − k].
    float acc = 0.0f;
    for (size_t k = 0; k < kBranch; ++k) {
      acc += m_branch[k] * m_upHistory[(m_upPos + k) % kBranch];
    }
    first = 2.0f * acc;
    // Delay branch: y[2n + 1] = 2·h[centre] · x[n − m] = x[n − m].
    second = 2.0f * m_centreTap * m_upHistory[(m_upPos + kCentre / 2) % kBranch];
  }

  /** Two input samples (earlier first) in, one output sample out. */
  float downsample(float first, float second) {
    push(first);
    push(second);
    // z = Σ h[j] · v[newest − j]: the centre tap lands on an even (older)
    // input, the branch taps on odd ones.
    float acc = m_centreTap * at(kCentre);
    for (size_t k = 0; k < kBranch; ++k) acc += m_branch[k] * at(2 * k);
    return flushDenormal(acc);
  }

private:
  static constexpr double kPi = 3.14159265358979323846;

  static double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 32; ++k) {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum += term;
      if (term < 1e-12 * sum) break;
    }
    return sum;
  }

  void push(float v) {
    m_downPos = (m_downPos + Taps - 1) % Taps;
    m_downHistory[m_downPos] = v;
  }
  /** The input `age` samples before the newest. */
  float at(size_t age) const { return m_downHistory[(m_downPos + age) % Taps]; }

  std::array<float, kBranch> m_branch{};
  float m_centreTap = 0.5f;
  std::array<float, kBranch> m_upHistory{};
  std::array<float, Taps> m_downHistory{};
  size_t m_upPos = 0;
  size_t m_downPos = 0;
};

/**
 * OversampledShaper — the distortion's diode waveshaper, anti-aliased.
 *
 * The shaper `sign(x) · (1 − e^(−drive·|x|))` is a soft clipper that turns
 * into a near-square wave at high drive, whose harmonics fall only as 1/n:
 * run at the host rate they fold back across the whole audio band. Two
 * cascaded half-band stages run it at 4× (a 47-tap stage carries the band
 * edge, a 19-tap stage the wide second transition), and the shaper itself
 * is evaluated with first-order antiderivative anti-aliasing (ADAA — Parker
 * et al., "Reducing the aliasing of nonlinear waveshaping using continuous-
 * time convolution", DAFx 2016), which suppresses what 4× alone leaves.
 *
 * Measured on a 3–7 kHz sine at drive 3.3–10 (tests/test_dsp.cpp), aliased
 * energy in 10–20 kHz drops 34–60 dB against the plain shaper. Chosen over
 * a vendored polyphase-IIR half-band (hiir): the FIR keeps the distorted
 * bus phase-linear, so the dry signal it is mixed back with can be
 * compensated with a whole-sample delay (kLatency) instead of comb filtering.
 *
 * Cost per input sample: ~70 multiply-adds and eight exp() — only paid
 * while the distortion is on and a channel feeds it.
 */
class OversampledShaper {
public:
  /** Input-to-output delay in host samples (both stages + ADAA, rounded). */
  static constexpr size_t kLatency =
      HalfBandFir<47>::kRoundTripLatency + (HalfBandFir<19>::kRoundTripLatency + 1) / 2;

  void prepare() {
    m_stage1.design(9.0f);
    m_stage2.design(7.0f);
    reset();
  }

  void reset() {
    m_stage1.reset();
    m_stage2.reset();
    m_prevX = 0.0;
    m_prevF = 0.0;
  }

  /** The plain shaper, as the mixer ran it before oversampling. */
  static float shape(float x, float drive) {
    const float d = x * drive;
    return d >= 0.0f ? 1.0f - std::exp(-d) : -1.0f + std::exp(d);
  }

  float process(float x, float drive) {
    float a = 0.0f;
    float b = 0.0f;
    m_stage1.upsample(x, a, b);
    float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
    m_stage2.upsample(a, a0, a1);
    m_stage2.upsample(b, b0, b1);
    a0 = adaa(a0, drive);
    a1 = adaa(a1, drive);
    b0 = adaa(b0, drive);
    b1 = adaa(b1, drive);
    const float da = m_stage2.downsample(a0, a1);
    const float db = m_stage2.downsample(b0, b1);
    return m_stage1.downsample(da, db);
  }

private:
  /**
   * Antiderivative of the shaper in units of `x` (drive folded in): for
   * u = drive·|x|, ∫ shape = (u + e^(−u) − 1) / drive. Double precision:
   * near zero it is u²/2 computed as a difference of O(u) terms, and the
   * difference quotient below magnifies that error by 1/dx.
   */
  static double antiderivative(double x, double drive) {
    const double u = std::fabs(x) * drive;
    return (u + std::expm1(-u)) / drive;
  }

  float adaa(float xIn, float drive) {
    const double x = xIn;
    const double d = drive;
    const double f = antiderivative(x, d);
    const double dx = x - m_prevX;
    double y;
    if (std::fabs(dx) < 1e-7) {
      // Ill-conditioned difference quotient: the midpoint is its limit.
      y = shape(static_cast<float>(0.5 * (x + m_prevX)), drive);
    } else {
      y = (f - m_prevF) / dx;
    }
    m_prevX = x;
    m_prevF = f;
    return static_cast<float>(y);
  }

  HalfBandFir<47> m_stage1;
  HalfBandFir<19> m_stage2;
  double m_prevX = 0.0;
  double m_prevF = 0.0;
};

} // namespace rb338::dsp
