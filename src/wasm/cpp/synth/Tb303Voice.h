#pragma once

#include "Voice.h"

namespace rb338 {

/**
 * Tb303Voice — monophonic TB-303 bassline emulation.
 *
 * Features:
 *   - Saw / square waveform selection
 *   - 24dB/octave resonant low-pass filter
 *   - Accent (increases envelope depth + filter cutoff)
 *   - Slide (portamento between notes)
 *   - Decay envelope (attack is instantaneous)
 */
class Tb303Voice : public Voice {
public:
  Tb303Voice() = default;
  ~Tb303Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state, const std::vector<Pattern>& patterns) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void setParameter(const char* name, float value) override;
  void reset() override;

private:
  float m_sampleRate = 44100.0f;

  // Oscillator
  float m_phase = 0.0f;
  bool m_waveformSaw = true; // false = square

  // Filter (Moog-style ladder approximation)
  float m_cutoff = 0.5f;
  float m_resonance = 0.5f;
  float m_envMod = 0.5f;
  float m_filterState[4] = {0};

  // Envelope
  float m_envelope = 0.0f;
  float m_decayCoeff = 0.99f;

  // Accent / slide
  float m_accentAmount = 0.0f;
  float m_slideRate = 0.0f;
  float m_currentPitch = 0.0f;
  float m_targetPitch = 0.0f;

  // TODO: full DSP implementation
};

} // namespace rb338
