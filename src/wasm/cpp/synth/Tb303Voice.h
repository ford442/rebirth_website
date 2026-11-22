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
 *   - Two envelopes, as on the hardware: the VCA envelope has a fixed decay
 *     and is cut by the gate, while the VCF ("MEG") envelope follows the
 *     DECAY knob — forced to its shortest setting on an accented note
 *   - Gate: the sequencer closes it half a step after the trigger
 *     (releaseGate()); a sliding step holds it into the next note
 *   - Accent sweep: each accented note charges a one-pole "accent
 *     capacitor" from the VCF envelope; the charge feeds the cutoff, scaled
 *     up with resonance, and it does not drain between closely spaced
 *     accents — so a run of accents climbs, the 303's "wow"
 *   - Slide: portamento in the log-frequency (semitone) domain, matching a
 *     CV slew circuit, without retriggering the envelopes
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
  /** Portamento time constant (63 % of the interval), in seconds. */
  static constexpr float kSlideTimeSeconds = 0.06f;
  /** VCA envelope decay time constant — fixed, not the DECAY knob. */
  static constexpr float kVcaDecaySeconds = 1.0f;
  /** VCA release once the gate closes. */
  static constexpr float kVcaReleaseSeconds = 0.008f;
  /** VCF envelope decay range across the DECAY knob, and on an accent. */
  static constexpr float kVcfDecayMinSeconds = 0.2f;
  static constexpr float kVcfDecayMaxSeconds = 2.0f;

  Tb303Voice() = default;
  ~Tb303Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void releaseGate() override;
  void setParameter(DeviceParamId param, float value) override;
  void setSamplePool(const SamplePool* pool) override;
  void reset() override;

  // ── Probes (tests) ──
  /** Oscillator pitch right now, in Hz (slide included). */
  float currentPitchHz() const;
  /** Accent-capacitor charge, 0–1. */
  float accentCharge() const { return m_accentCap; }
  float vcaEnvelope() const { return m_vcaEnv; }
  float vcfEnvelope() const { return m_vcfEnv; }
  bool gateOpen() const { return m_gateOpen; }

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
  float m_smoothK = 0.0f;          // one-pole knob smoothing, cached in init()
  unsigned m_controlPhase = 0;     // control-rate filter coefficient counter
  float m_smoothedCutoff = 0.5f;
  float m_smoothedResonance = 0.5f;

  // Oscillator — phase kept continuous across note changes.
  float m_phase = 0.0f;

  dsp::ZdfLadder m_filter;

  // VCA envelope: instant attack, fixed decay while the gate is open, fast
  // release once it closes.
  float m_vcaEnv = 0.0f;
  float m_vcaDecayCoeff = 0.99998f;
  float m_vcaReleaseCoeff = 0.997f;
  bool m_gateOpen = false;

  // VCF envelope: instant attack, DECAY-knob decay (shortest on accents).
  float m_vcfEnv = 0.0f;
  float m_vcfDecayCoeff = 0.9999f;
  float m_vcfAccentDecayCoeff = 0.9998f;
  bool m_noteAccented = false;

  // Accent: the capacitor that sweeps the cutoff, and a short VCA boost.
  float m_accentCap = 0.0f;
  float m_accentChargeCoeff = 0.0f;
  float m_accentDrainCoeff = 0.0f;
  float m_accentVca = 0.0f;
  float m_accentVcaDecayCoeff = 0.999f;

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
