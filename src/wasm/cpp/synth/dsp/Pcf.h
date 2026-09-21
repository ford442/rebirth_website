#pragma once

#include <algorithm>
#include <cmath>

namespace rb338::dsp {

/**
 * Pcf — ReBirth's "PCF" (pattern-controlled filter) send effect.
 *
 * ReBirth's PCF is not a generic tone control: it is a resonant low-pass
 * whose corner frequency is pushed by the signal's own envelope, which is
 * what gives it the rhythmic, pumping character on a drum bus. The old
 * `Mixer.cpp` implementation was a single one-pole smoother with a
 * hand-rolled "resonant" blend, which could neither self-resonate nor track
 * the envelope; `envAmount` was parsed out of the DEVL/PCF chunk and then
 * thrown away.
 *
 * Topology: a topology-preserving-transform (TPT) state-variable filter —
 * two trapezoid-integrated one-poles in a resonant loop (Zavalishin, "The
 * Art of VA Filter Design", freely published). The closed-form solve below
 * is the standard SVF cell; `lowpass` is taken as the output.
 *
 *   v3 = in - ic2eq
 *   v1 = a1*ic1eq + a2*v3
 *   v2 = ic2eq + a2*ic1eq + a3*v3     (= low-pass output)
 *   ic1eq = 2*v1 - ic1eq;  ic2eq = 2*v2 - ic2eq
 *
 * Coefficients are recomputed only when a parameter or the envelope moves
 * the cutoff by more than a small tolerance, so the audio-thread cost is a
 * handful of multiply-adds per sample in the common case.
 *
 * All three song bytes drive it: `cutoff` sets the base corner, `resonance`
 * sets the damping (k), and `envAmount` sets how many octaves the envelope
 * follower is allowed to open it.
 */
class Pcf {
public:
  /** Widest upward sweep the envelope follower may apply, in octaves. */
  static constexpr float kMaxEnvOctaves = 3.5f;

  void prepare(float sampleRate) {
    m_sampleRate = std::max(sampleRate, 1000.0f);
    // ~2 ms attack, ~120 ms release: fast enough to catch a kick transient,
    // slow enough that the sweep is audible rather than a click.
    m_envAttack = std::exp(-1.0f / (0.002f * m_sampleRate));
    m_envRelease = std::exp(-1.0f / (0.120f * m_sampleRate));
    reset();
    updateCoefficients(true);
  }

  /** Clear filter memory and the envelope follower; keep the settings. */
  void reset() {
    m_ic1eq = 0.0f;
    m_ic2eq = 0.0f;
    m_envelope = 0.0f;
  }

  /** @param norm 0–1 (the song's `cutoff` byte / 127). */
  void setCutoff(float norm) {
    m_cutoffNorm = std::clamp(norm, 0.0f, 1.0f);
    updateCoefficients(true);
  }

  /** @param norm 0–1 (the song's `resonance` byte / 127). */
  void setResonance(float norm) {
    m_resonance = std::clamp(norm, 0.0f, 1.0f);
    // k = 1/Q. 2 is critically damped, 0.1 is a sharp self-resonant peak.
    m_k = 2.0f - 1.9f * m_resonance;
    updateCoefficients(true);
  }

  /** @param norm 0–1 (the song's `envAmount` byte / 127). */
  void setEnvAmount(float norm) { m_envAmount = std::clamp(norm, 0.0f, 1.0f); }

  float process(float input) {
    const float absIn = std::fabs(input);
    const float coeff = (absIn > m_envelope) ? m_envAttack : m_envRelease;
    m_envelope = absIn + coeff * (m_envelope - absIn);

    if (m_envAmount > 0.0f) {
      const float openOctaves =
          m_envAmount * kMaxEnvOctaves * std::min(m_envelope, 1.0f);
      updateCoefficients(false, openOctaves);
    }

    const float v3 = input - m_ic2eq;
    const float v1 = m_a1 * m_ic1eq + m_a2 * v3;
    const float v2 = m_ic2eq + m_a2 * m_ic1eq + m_a3 * v3;
    m_ic1eq = 2.0f * v1 - m_ic1eq;
    m_ic2eq = 2.0f * v2 - m_ic2eq;
    return v2;
  }

private:
  /** Base corner frequency in Hz for the current `cutoff` byte. */
  float baseCutoffHz() const {
    // 80 Hz … ~10.2 kHz across the byte range, exponentially spaced.
    return 80.0f * std::exp2(m_cutoffNorm * 7.0f);
  }

  void updateCoefficients(bool force, float openOctaves = 0.0f) {
    const float nyquistLimit = m_sampleRate * 0.45f;
    const float hz =
        std::clamp(baseCutoffHz() * std::exp2(openOctaves), 20.0f, nyquistLimit);
    if (!force && std::fabs(hz - m_currentHz) < hz * 0.01f) {
      return;
    }
    m_currentHz = hz;
    const float g = std::tan(3.14159265358979323846f * hz / m_sampleRate);
    m_a1 = 1.0f / (1.0f + g * (g + m_k));
    m_a2 = g * m_a1;
    m_a3 = g * m_a2;
  }

  float m_sampleRate = 44100.0f;
  float m_cutoffNorm = 0.5f;
  float m_resonance = 0.0f;
  float m_envAmount = 0.0f;
  float m_k = 2.0f;

  float m_currentHz = -1.0f;
  float m_a1 = 1.0f;
  float m_a2 = 0.0f;
  float m_a3 = 0.0f;
  float m_ic1eq = 0.0f;
  float m_ic2eq = 0.0f;

  float m_envelope = 0.0f;
  float m_envAttack = 0.0f;
  float m_envRelease = 0.0f;
};

} // namespace rb338::dsp
