#pragma once

#include "Voice.h"

namespace rb338 {

/**
 * Tb303Voice — monophonic TB-303 bassline emulation.
 *
 * Features:
 *   - Saw / square waveform selection
 *   - 24dB/octave resonant low-pass filter (4-pole ladder)
 *   - Accent (increases envelope depth + filter cutoff)
 *   - Slide (portamento between consecutive active notes)
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

  // Device knobs (target values, 0–1 unless noted).
  float m_tune = 0.5f;
  float m_cutoffKnob = 0.5f;
  float m_resonanceKnob = 0.5f;
  float m_envModKnob = 0.5f;
  float m_decayKnob = 0.5f;
  float m_accentKnob = 0.5f;
  bool m_waveformSaw = true;

  // Smoothed runtime parameters (zipper-noise reduction).
  float m_smoothedCutoff = 0.5f;
  float m_smoothedResonance = 0.5f;

  // Oscillator — phase kept continuous across note changes.
  float m_phase = 0.0f;

  // 4-pole ladder filter state.
  float m_filterStage[4] = {0};

  // Amplitude envelope (instant attack, exponential decay).
  float m_envelope = 0.0f;
  float m_decayCoeff = 0.9995f;
  float m_releaseCoeff = 0.995f;
  bool m_gateOpen = false;

  // Pitch (Hz) with portamento.
  float m_currentPitch = 440.0f;
  float m_targetPitch = 440.0f;
  float m_pitchGlideCoeff = 0.002f;

  // Per-step accent boost (0 = normal, >0 = accented).
  float m_stepAccent = 0.0f;

  // Slide chain: previous step had slide flag set while active.
  bool m_prevStepActive = false;
  bool m_prevStepSlide = false;

  // Filter precomputed coefficient (updated when cutoff moves).
  float m_filterG = 0.1f;

  void applyDeviceState(const DeviceState& state);
  void updateDecayCoeffs();
  float midiToHz(uint8_t midiNote) const;
  float renderSample();
  void processFilterCoeffs(float cutoffNorm, float resonanceNorm);
};

} // namespace rb338
