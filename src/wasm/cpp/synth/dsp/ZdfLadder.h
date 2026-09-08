#pragma once

#include <algorithm>
#include <cmath>

namespace rb338::dsp {

/**
 * ZdfLadder — zero-delay-feedback 4-pole resonant low-pass ladder, voiced
 * for the TB-303's diode-ladder character.
 *
 * Topology: four cascaded topology-preserving-transform (TPT) one-pole
 * integrators (Zavalishin, "The Art of VA Filter Design", freely published)
 * wrapped in a global feedback loop. Because each one-pole stage is linear
 * in its instantaneous input, the whole loop has a closed-form solution
 * (no Newton-Raphson iteration needed): with y4 = G^4 * u + S, where S
 * folds in the four stage states, solving u = input - k * y4 for u gives
 *
 *   u = (input - k * S) / (1 + k * G^4)
 *
 * A tanh saturator sits in that feedback path, standing in for the
 * diode/transistor clipping described in Huovilainen ("Non-linear Digital
 * Implementation of the Moog Diode Ladder Filter", DAFx 2004) and
 * D'Angelo & Valimaki ("Generalized Moog Ladder Filter", 2013): both are
 * published DAFx/AES papers, not GPL source. This file is an original
 * implementation written from that public literature — no Open303 (GPL)
 * code was consulted or ported.
 *
 * Resonance participates directly in the coefficient path (it shapes the
 * closed-form solve above), not merely as a raw feedback scalar bolted on
 * after four independent one-pole stages.
 */
class ZdfLadder {
public:
  void reset() {
    for (float& s : m_state) s = 0.0f;
  }

  /**
   * `cutoffHz`     — filter corner frequency (pre-resonance-peak-shift).
   * `resonanceNorm`— 0..1 knob position; raises the resonant peak's Q.
   * `sampleRate`   — host sample rate.
   *
   * The tanh saturator in the feedback path (see process()) means this
   * stays stable and bounded even at resonanceNorm = 1 rather than
   * exploding or ringing forever — a stable resonant peak, not literal
   * self-oscillation.
   */
  void setCoeffs(float cutoffHz, float resonanceNorm, float sampleRate) {
    const float nyquistGuard = sampleRate * 0.45f;
    const float fc = std::clamp(cutoffHz, 20.0f, nyquistGuard);
    const float g = std::tan(kPi * fc / sampleRate);
    m_gain = g / (1.0f + g);
    // An ideal (non-saturating) 4-pole ladder self-oscillates near k = 4;
    // we use that as the top of our resonance range.
    m_feedback = std::clamp(resonanceNorm, 0.0f, 1.0f) * kMaxFeedback;
  }

  float process(float input) {
    const float G = m_gain;
    const float G2 = G * G;
    const float G3 = G2 * G;
    const float G4 = G3 * G;
    const float oneMinusG = 1.0f - G;

    // Contribution of the current stage states to y4 if u were 0.
    const float s = oneMinusG * (G3 * m_state[0] + G2 * m_state[1] +
                                  G * m_state[2] + m_state[3]);

    float u = (input - m_feedback * s) / (1.0f + m_feedback * G4);
    u = std::tanh(u); // diode/transistor clipping in the feedback loop

    float x = u;
    for (float& stage : m_state) {
      const float v = (x - stage) * G;
      const float y = v + stage;
      stage = y + v;
      x = y;
    }
    return x;
  }

private:
  static constexpr float kPi = 3.141592653589793f;
  static constexpr float kMaxFeedback = 4.3f;

  float m_gain = 0.0f;
  float m_feedback = 0.0f;
  float m_state[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace rb338::dsp
