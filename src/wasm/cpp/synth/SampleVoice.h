#pragma once

#include "Resample.h"
#include "SamplePool.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rb338 {

/**
 * SampleVoice — one-shot PCM playback for a single mod drum slot.
 *
 * Holds a borrowed pointer into the SamplePool arena; it never owns or
 * frees PCM, and render() does no allocation, so it is safe to run inside
 * processBlock(). The pool outlives every voice that points into it because
 * the EngineSnapshot holding both keeps a shared_ptr to the pool.
 *
 * TUNE and ACCENT use the same knob mappings as the procedural
 * DrumVoiceChannel, so the front-panel knobs behave the same whether or not
 * a mod is loaded. DECAY applies an exponential amplitude envelope scaled
 * to the sample's own length rather than a hard truncation.
 */
class SampleVoice {
public:
  void init(float sampleRate) {
    m_engineRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
    reset();
  }

  void reset() {
    m_pcm = nullptr;
    m_frames = 0;
    m_position = 0.0;
    m_increment = 1.0;
    m_gain = 1.0f;
    m_envelope = 1.0f;
    m_envelopeCoeff = 1.0f;
    m_active = false;
  }

  bool active() const { return m_active; }

  /**
   * Start playback of `slot`.
   *
   * `tune`, `decay` and `accentGain` are the already-mapped values from
   * DrumParams (see Tr808Voice/Tr909Voice), not raw knob positions.
   */
  void trigger(const SamplePool::SlotData& slot, float pitchMul, float decayNorm,
               float gain) {
    if (!slot.pcm || slot.frameCount == 0) {
      m_active = false;
      return;
    }

    m_pcm = slot.pcm;
    m_frames = slot.frameCount;
    m_position = 0.0;
    m_gain = gain;
    m_envelope = 1.0f;
    m_active = true;

    // Play at the sample's own rate relative to the host's, then apply TUNE.
    const float sourceRate = (slot.sampleRate > 0u) ? static_cast<float>(slot.sampleRate)
                                                    : m_engineRate;
    const float ratio = (sourceRate / m_engineRate) * std::max(0.01f, pitchMul);
    m_increment = static_cast<double>(ratio);

    // Envelope time constant scaled to this sample's duration: at decay=0.5
    // the sample plays essentially intact, lower shortens it smoothly.
    const float durationSeconds = static_cast<float>(m_frames) / std::max(1.0f, sourceRate);
    const float tauScale = 0.25f + (4.0f - 0.25f) * std::clamp(decayNorm, 0.0f, 1.0f);
    const float tau = std::max(0.001f, durationSeconds * tauScale);
    m_envelopeCoeff = std::exp(-1.0f / (tau * m_engineRate));
  }

  float render() {
    if (!m_active || !m_pcm) return 0.0f;

    const auto index = static_cast<uint32_t>(m_position);
    if (index >= m_frames) {
      m_active = false;
      return 0.0f;
    }

    const float frac = static_cast<float>(m_position - static_cast<double>(index));
    const float out = dsp::sampleAtClamped(m_pcm, m_frames, index, frac) * m_gain * m_envelope;

    m_position += m_increment;
    m_envelope *= m_envelopeCoeff;
    return out;
  }

private:
  float m_engineRate = 44100.0f;
  const float* m_pcm = nullptr;
  uint32_t m_frames = 0;
  double m_position = 0.0;
  double m_increment = 1.0;
  float m_gain = 1.0f;
  float m_envelope = 1.0f;
  float m_envelopeCoeff = 1.0f;
  bool m_active = false;
};

} // namespace rb338
