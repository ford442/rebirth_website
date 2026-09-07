#include "Tb303Voice.h"
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

// TB-303 portamento is a fixed hardware time constant, not a knob; the
// current DeviceState / TRAK schema has no per-device slide-time field to
// read instead, so we use the commonly documented ~60 ms figure.
constexpr float kSlideTimeSeconds = 0.06f;

// Accent circuit: short, fixed decay independent of the DECAY knob.
constexpr float kAccentDecaySeconds = 0.18f;
constexpr float kAccentCutoffBump = 0.55f;   // extra normalised cutoff sweep
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
  m_glideCoeff = 1.0f - std::exp(-1.0f / (kSlideTimeSeconds * sampleRate));
  m_accentDecayCoeff = std::exp(-1.0f / (kAccentDecaySeconds * sampleRate));
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

void Tb303Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  (void)patterns;
  applyDeviceState(state);
}

void Tb303Voice::updateDecayCoeffs() {
  // Map decay knob to envelope time constants (roughly 80 ms – 2.5 s).
  const float decaySeconds = lerp(0.08f, 2.5f, m_decayKnob);
  m_decayCoeff = std::exp(-1.0f / (decaySeconds * m_sampleRate));
  m_releaseCoeff = std::exp(-1.0f / (0.04f * m_sampleRate));
}

float Tb303Voice::midiToHz(uint8_t midiNote) const {
  const float tuneSemis = lerp(-kTuneRangeSemitones, kTuneRangeSemitones, m_tune);
  const float note = static_cast<float>(midiNote) + tuneSemis;
  return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

float Tb303Voice::renderSample() {
  const float smoothK = smoothCoeff(m_sampleRate);
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

  // Main envelope: instant attack while gate is open, exponential decay.
  if (m_gateOpen) {
    m_envelope = std::max(m_envelope * m_decayCoeff, 0.0001f);
  } else {
    m_envelope *= m_releaseCoeff;
  }

  // Accent one-shot: fixed short decay, independent of the main envelope.
  m_accentEnvelope *= m_accentDecayCoeff;

  // Filter cutoff: base knob + envelope modulation + accent cutoff bump.
  const float envCutoff = m_envModKnob * m_envelope;
  const float accentCutoff = m_accentEnvelope * m_accentKnob * kAccentCutoffBump;
  const float modCutoffNorm = clamp01(m_smoothedCutoff + envCutoff + accentCutoff);
  m_filter.setCoeffs(cutoffNormToHz(modCutoffNorm), m_smoothedResonance, m_sampleRate);

  // VCA: main envelope with an accent-driven extra transient on top.
  const float vcaGain = m_envelope * (1.0f + m_accentEnvelope * m_accentKnob * kAccentVcaBoost);
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
    m_gateOpen = false;
    m_prevStepActive = false;
    m_prevStepSlide = false;
    return;
  }

  const bool slideFromPrev = m_prevStepActive && m_prevStepSlide;
  m_targetLogPitch = std::log2(midiToHz(step.note));

  if (step.accent) {
    m_accentEnvelope = 1.0f;
  }

  if (slideFromPrev) {
    // Portamento without retriggering the envelope.
    m_gateOpen = true;
  } else {
    m_currentLogPitch = m_targetLogPitch;
    m_envelope = 1.0f;
    m_gateOpen = true;
  }

  m_prevStepActive = true;
  m_prevStepSlide = step.slide;
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
  m_phase = 0.0f;
  m_envelope = 0.0f;
  m_accentEnvelope = 0.0f;
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
