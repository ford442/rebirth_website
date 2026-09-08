#pragma once

#include "SamplePool.h"
#include "Voice.h"
#include "dsp/ZdfLadder.h"

namespace rb338 {

/**
 * Tb303Voice — monophonic TB-303 bassline emulation.
 *
 * Features:
 *   - PolyBLEP-corrected saw / square oscillator (see dsp/PolyBlep.h)
 *   - Zero-delay-feedback 4-pole diode-ladder low-pass (see dsp/ZdfLadder.h),
 *     resonance woven into the filter's coefficient path rather than
 *     applied as a bolt-on feedback scalar
 *   - Accent coupled into both the filter envelope (cutoff bump) and the
 *     VCA (extra gain), each with its own short decay — not a flat
 *     `1 + accent * knob` multiply
 *   - Slide: portamento in the log-frequency (semitone) domain, matching a
 *     CV slew circuit, without retriggering the envelope
 *   - Decay envelope (attack is instantaneous)
 *
 * Tune range: ReBirth's software 303 tune knob is documented as a coarse
 * transposition control, wider than a hardware 303's fine-pitch trim. We
 * don't have a legally obtainable manual or reference recording to match
 * exactly, so we've chosen ±12 semitones (one octave either way) as a
 * defensible, clearly-labelled default — revisit if a legal reference
 * turns up.
 */
class Tb303Voice : public Voice {
public:
  Tb303Voice() = default;
  ~Tb303Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state, const std::vector<Pattern>& patterns) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void setParameter(DeviceParamId param, float value) override;
  void setSamplePool(const SamplePool* pool) override;
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

  dsp::ZdfLadder m_filter;

  // Amplitude envelope (instant attack, exponential decay). Drives both the
  // VCA and (scaled by envMod) the filter cutoff sweep.
  float m_envelope = 0.0f;
  float m_decayCoeff = 0.9995f;
  float m_releaseCoeff = 0.995f;
  bool m_gateOpen = false;

  // Accent one-shot: fixed short decay, independent of the DECAY knob,
  // coupled into both cutoff (bump) and VCA (extra gain) — the 303's
  // accent sweep circuit rather than a flat level multiply.
  float m_accentEnvelope = 0.0f;
  float m_accentDecayCoeff = 0.999f;

  // Pitch (log2-Hz domain, i.e. semitone-linear) with portamento, matching
  // a CV slew circuit rather than a linear-in-Hz ramp.
  float m_currentLogPitch = 0.0f;
  float m_targetLogPitch = 0.0f;
  float m_glideCoeff = 0.01f;

  // Slide chain: previous step had slide flag set while active.
  bool m_prevStepActive = false;
  bool m_prevStepSlide = false;

  // Optional `.rbm` waveform replacement. Borrowed pointers into the
  // EngineSnapshot's SamplePool; null means "use the PolyBLEP oscillator".
  // Resolved once in setSamplePool() so render() does no slot lookups.
  const SamplePool::SlotData* m_sawWave = nullptr;
  const SamplePool::SlotData* m_squareWave = nullptr;

  void applyDeviceState(const DeviceState& state);
  void updateDecayCoeffs();
  float midiToHz(uint8_t midiNote) const;
  float renderSample();
};

} // namespace rb338
