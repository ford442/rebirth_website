#include "Tb303Voice.h"
#include "dsp/Denormal.h"
#include "Resample.h"
#include "dsp/PolyBlep.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

namespace {

constexpr float kOutputGain = 0.3f;

// Coarse-transposition tune range (see Tb303Voice.h for rationale).
constexpr float kTuneRangeSemitones = 12.0f;

// Accent circuit. The capacitor charges from the VCF envelope in ~30 ms and
// drains in ~180 ms, so accents a sixteenth or two apart stack; its charge
// opens the cutoff by up to kAccentCutoffBump (normalised), more at high
// resonance. The VCA boost is a separate short one-shot.
constexpr float kAccentChargeSeconds = 0.03f;
constexpr float kAccentDrainSeconds = 0.18f;
constexpr float kAccentCutoffBump = 0.55f;
constexpr float kAccentVcaDecaySeconds = 0.18f;
constexpr float kAccentVcaBoost = 1.1f;      // extra VCA gain at full accent

constexpr float kMinCutoffHz = 120.0f;
constexpr float kMaxCutoffHz = 8000.0f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// One-pole smoothing coefficient for ~5 ms at the given sample rate.
float smoothCoeff(float sampleRate) {
  return 1.0f - std::exp(-1.0f / (0.005f * sampleRate));
}

float cutoffNormToHz(float norm) {
  return kMinCutoffHz * std::pow(kMaxCutoffHz / kMinCutoffHz, clamp01(norm));
}

} // anonymous namespace

void Tb303Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  // The time constants are in seconds, so they hold at any device rate.
  m_glideCoeff = 1.0f - std::exp(-1.0f / (kSlideTimeSeconds * sampleRate));
  m_smoothK = smoothCoeff(sampleRate); // was an exp() of a constant per sample
  m_vcaDecayCoeff = std::exp(-1.0f / (kVcaDecaySeconds * sampleRate));
  m_vcaReleaseCoeff = std::exp(-1.0f / (kVcaReleaseSeconds * sampleRate));
  m_vcfAccentDecayCoeff = std::exp(-1.0f / (kVcfDecayMinSeconds * sampleRate));
  m_accentChargeCoeff = 1.0f - std::exp(-1.0f / (kAccentChargeSeconds * sampleRate));
  m_accentDrainCoeff = std::exp(-1.0f / (kAccentDrainSeconds * sampleRate));
  m_accentVcaDecayCoeff = std::exp(-1.0f / (kAccentVcaDecaySeconds * sampleRate));
  reset();
}

void Tb303Voice::applyDeviceState(const DeviceState& state) {
  m_tune = clamp01(state.tune);
  m_cutoffKnob = clamp01(state.cutoff);
  m_resonanceKnob = clamp01(state.resonance);
  m_envModKnob = clamp01(state.envMod);
  m_decayKnob = clamp01(state.decay);
  m_accentKnob = clamp01(state.accent);
  m_waveformSaw = (state.waveform == 0);
  m_smoothedCutoff = m_cutoffKnob;
  m_smoothedResonance = m_resonanceKnob;
  updateDecayCoeffs();
  m_filter.setCoeffs(cutoffNormToHz(m_smoothedCutoff), m_smoothedResonance, m_sampleRate);
}

void Tb303Voice::load(const DeviceState& state) {
  applyDeviceState(state);
}

void Tb303Voice::updateDecayCoeffs() {
  // DECAY sets the VCF envelope only, exponentially across 200 ms – 2 s.
  const float decaySeconds =
      kVcfDecayMinSeconds * std::pow(kVcfDecayMaxSeconds / kVcfDecayMinSeconds, m_decayKnob);
  m_vcfDecayCoeff = std::exp(-1.0f / (decaySeconds * m_sampleRate));
}

float Tb303Voice::currentPitchHz() const {
  return std::exp2(m_currentLogPitch);
}

float Tb303Voice::midiToHz(uint8_t midiNote) const {
  const float tuneSemis = lerp(-kTuneRangeSemitones, kTuneRangeSemitones, m_tune);
  const float note = static_cast<float>(midiNote) + tuneSemis;
  return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

float Tb303Voice::renderSample() {
  const float smoothK = m_smoothK;
  m_smoothedCutoff += (m_cutoffKnob - m_smoothedCutoff) * smoothK;
  m_smoothedResonance += (m_resonanceKnob - m_smoothedResonance) * smoothK;

  // Portamento in the log-frequency domain (semitone-linear), matching a
  // CV slew circuit rather than a linear ramp in Hz.
  m_currentLogPitch += (m_targetLogPitch - m_currentLogPitch) * m_glideCoeff;
  const float currentHz = std::exp2(m_currentLogPitch);

  // Oscillator — phase is not reset on note changes.
  const float phaseInc = currentHz / m_sampleRate;
  m_phase += phaseInc;
  if (m_phase >= 1.0f) m_phase -= 1.0f;

  // A mod can replace the oscillator with a single-cycle wavetable, which
  // the phase sweeps end-to-end. Unlike the PolyBLEP path these are not
  // band-limited — the same tradeoff the original hardware/software made —
  // so they alias at high notes by design rather than by oversight.
  const SamplePool::SlotData* wavetable = m_waveformSaw ? m_sawWave : m_squareWave;
  float osc;
  if (wavetable) {
    const float position = m_phase * static_cast<float>(wavetable->frameCount);
    const auto index = static_cast<uint32_t>(position);
    osc = dsp::sampleAtWrapped(wavetable->pcm, wavetable->frameCount, index,
                               position - static_cast<float>(index));
  } else {
    osc = m_waveformSaw ? dsp::polyBlepSaw(m_phase, phaseInc)
                        : dsp::polyBlepSquare(m_phase, phaseInc);
  }

  // VCA envelope: fixed decay while the gate is open, fast release after.
  m_vcaEnv = dsp::flushDenormal(m_vcaEnv * (m_gateOpen ? m_vcaDecayCoeff : m_vcaReleaseCoeff));

  // VCF envelope: free-running decay, the accent's at its shortest setting.
  m_vcfEnv = dsp::flushDenormal(m_vcfEnv * (m_noteAccented ? m_vcfAccentDecayCoeff
                                                           : m_vcfDecayCoeff));

  // Accent capacitor: charges towards the VCF envelope while the note is
  // accented, drains otherwise. Charge left over from the previous accent is
  // where the next one starts, so a run of accents climbs.
  if (m_noteAccented) {
    m_accentCap += (m_vcfEnv - m_accentCap) * m_accentChargeCoeff;
  } else {
    m_accentCap = dsp::flushDenormal(m_accentCap * m_accentDrainCoeff);
  }
  m_accentVca = dsp::flushDenormal(m_accentVca * m_accentVcaDecayCoeff);

  // Filter cutoff: base knob + VCF envelope + accent sweep. Control rate —
  // the pow + tan used to run every sample; the envelopes and knob
  // smoothing it follows move far slower than 16 samples. A new note resets
  // the counter so its attack lands on the very first sample.
  if ((m_controlPhase++ & dsp::kControlRateMask) == 0) {
    const float envCutoff = m_envModKnob * m_vcfEnv;
    // Higher resonance, bigger sweep: on the hardware the resonance pot is
    // ganged into the accent circuit.
    const float accentCutoff = m_accentCap * m_accentKnob * kAccentCutoffBump *
                               (0.5f + m_smoothedResonance);
    const float modCutoffNorm = clamp01(m_smoothedCutoff + envCutoff + accentCutoff);
    m_filter.setCoeffs(cutoffNormToHz(modCutoffNorm), m_smoothedResonance, m_sampleRate);
  }

  // VCA: its envelope with the accent's short extra transient on top.
  const float vcaGain = m_vcaEnv * (1.0f + m_accentVca * m_accentKnob * kAccentVcaBoost);
  const float input = osc * vcaGain;

  const float out = m_filter.process(input);
  return out * kOutputGain;
}

void Tb303Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    output[i] = renderSample();
  }
}

void Tb303Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;

  if (!step.active) {
    // A rest closes the gate, ending a held (slid) note.
    m_gateOpen = false;
    m_noteAccented = false;
    m_prevStepActive = false;
    m_prevStepSlide = false;
    return;
  }

  const bool slideFromPrev = m_prevStepActive && m_prevStepSlide;
  m_controlPhase = 0;
  m_targetLogPitch = std::log2(midiToHz(step.note));

  if (slideFromPrev) {
    // Portamento: the gate stays open and neither envelope retriggers.
    m_gateOpen = true;
  } else {
    m_currentLogPitch = m_targetLogPitch;
    m_vcaEnv = 1.0f;
    m_vcfEnv = 1.0f;
    m_gateOpen = true;
    m_noteAccented = step.accent;
    if (step.accent) m_accentVca = 1.0f;
  }

  m_prevStepActive = true;
  m_prevStepSlide = step.slide;
}

void Tb303Voice::releaseGate() {
  m_gateOpen = false;
}

void Tb303Voice::setParameter(DeviceParamId param, float value) {
  const float v = clamp01(value);

  switch (param) {
    case DeviceParamId::Tune:
      m_tune = v;
      break;
    case DeviceParamId::Cutoff:
      m_cutoffKnob = v;
      break;
    case DeviceParamId::Resonance:
      m_resonanceKnob = v;
      break;
    case DeviceParamId::EnvMod:
      m_envModKnob = v;
      break;
    case DeviceParamId::Decay:
      m_decayKnob = v;
      updateDecayCoeffs();
      break;
    case DeviceParamId::Accent:
      m_accentKnob = v;
      break;
    case DeviceParamId::Waveform:
      m_waveformSaw = (v < 0.5f);
      break;
    default:
      break;
  }
}

void Tb303Voice::setSamplePool(const SamplePool* pool) {
  // Resolve once here rather than per-sample in render().
  m_sawWave = pool ? pool->slotData(ModSampleSlot::Tb303Saw) : nullptr;
  m_squareWave = pool ? pool->slotData(ModSampleSlot::Tb303Square) : nullptr;
}

void Tb303Voice::reset() {
  m_controlPhase = 0;
  m_phase = 0.0f;
  m_vcaEnv = 0.0f;
  m_vcfEnv = 0.0f;
  m_accentCap = 0.0f;
  m_accentVca = 0.0f;
  m_noteAccented = false;
  m_gateOpen = false;
  m_currentLogPitch = std::log2(440.0f);
  m_targetLogPitch = m_currentLogPitch;
  m_prevStepActive = false;
  m_prevStepSlide = false;
  m_filter.reset();
  m_smoothedCutoff = m_cutoffKnob;
  m_smoothedResonance = m_resonanceKnob;
  m_filter.setCoeffs(cutoffNormToHz(m_smoothedCutoff), m_smoothedResonance, m_sampleRate);
}

} // namespace rb338
