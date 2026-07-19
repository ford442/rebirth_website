#include "Tb303Voice.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

namespace {

constexpr float kPi = 3.141592653589793f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kOutputGain = 0.22f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// One-pole smoothing coefficient for ~5 ms at the given sample rate.
float smoothCoeff(float sampleRate) {
  return 1.0f - std::exp(-1.0f / (0.005f * sampleRate));
}

float softClip(float x) {
  const float drive = 1.4f;
  return std::tanh(x * drive) / std::tanh(drive);
}

} // anonymous namespace

void Tb303Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  m_pitchGlideCoeff = 1.0f / (0.12f * sampleRate); // ~120 ms slide
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
  processFilterCoeffs(m_smoothedCutoff, m_smoothedResonance);
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
  // Tune knob: ±1 semitone over the knob range.
  const float tuneSemis = lerp(-1.0f, 1.0f, m_tune);
  const float note = static_cast<float>(midiNote) + tuneSemis;
  return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

void Tb303Voice::processFilterCoeffs(float cutoffNorm, float resonanceNorm) {
  // Cutoff maps logarithmically from ~120 Hz to ~8 kHz (below Nyquist/4).
  const float minHz = 120.0f;
  const float maxHz = 8000.0f;
  const float hz = minHz * std::pow(maxHz / minHz, clamp01(cutoffNorm));
  const float wc = kTwoPi * hz / m_sampleRate;
  // One-pole smoothing coefficient — always < 1 for stability.
  m_filterG = 1.0f - std::exp(-std::clamp(wc, 0.0f, 3.0f));
  (void)resonanceNorm;
}

float Tb303Voice::renderSample() {
  const float smoothK = smoothCoeff(m_sampleRate);
  m_smoothedCutoff += (m_cutoffKnob - m_smoothedCutoff) * smoothK;
  m_smoothedResonance += (m_resonanceKnob - m_smoothedResonance) * smoothK;

  // Portamento toward target pitch.
  if (std::fabs(m_targetPitch - m_currentPitch) > 0.01f) {
    const float delta = m_targetPitch - m_currentPitch;
    m_currentPitch += delta * std::min(1.0f, m_pitchGlideCoeff);
  } else {
    m_currentPitch = m_targetPitch;
  }

  // Oscillator — phase is not reset on note changes.
  const float phaseInc = m_currentPitch / m_sampleRate;
  m_phase += phaseInc;
  if (m_phase >= 1.0f) m_phase -= 1.0f;

  float osc = 0.0f;
  if (m_waveformSaw) {
    osc = 2.0f * m_phase - 1.0f;
  } else {
    osc = (m_phase < 0.5f) ? 1.0f : -1.0f;
  }

  // Envelope: instant attack while gate is open, exponential decay.
  if (m_gateOpen) {
    m_envelope = std::max(m_envelope * m_decayCoeff, 0.0001f);
  } else {
    m_envelope *= m_releaseCoeff;
  }

  // Filter cutoff: base knob + envelope modulation + accent boost.
  const float envCutoff = m_envModKnob * m_envelope;
  const float accentCutoff = m_stepAccent * 0.35f;
  const float modCutoff = clamp01(m_smoothedCutoff + envCutoff + accentCutoff);
  processFilterCoeffs(modCutoff, m_smoothedResonance);

  // 4-pole ladder with resonance feedback (stable coefficient range).
  const float res = lerp(0.0f, 3.2f, m_smoothedResonance);
  float input = osc * m_envelope;
  const float accentLevel = 1.0f + m_stepAccent * lerp(0.2f, 0.9f, m_accentKnob);
  input *= accentLevel;

  input -= m_filterStage[3] * res;

  float stage = input;
  const float g = m_filterG;
  for (int i = 0; i < 4; ++i) {
    m_filterStage[i] += g * (stage - m_filterStage[i]);
    stage = m_filterStage[i];
  }

  float out = m_filterStage[3];
  out = softClip(out);

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
    m_stepAccent = 0.0f;
    m_prevStepActive = false;
    m_prevStepSlide = false;
    return;
  }

  const bool slideFromPrev = m_prevStepActive && m_prevStepSlide;
  m_targetPitch = midiToHz(step.note);
  m_stepAccent = step.accent ? lerp(0.25f, 1.0f, m_accentKnob) : 0.0f;

  if (slideFromPrev) {
    // Portamento without retriggering the envelope.
    m_gateOpen = true;
  } else {
    m_currentPitch = m_targetPitch;
    m_envelope = 1.0f;
    m_gateOpen = true;
  }

  m_prevStepActive = true;
  m_prevStepSlide = step.slide;
}

void Tb303Voice::setParameter(const char* name, float value) {
  if (!name) return;
  const float v = clamp01(value);

  if (std::strcmp(name, "tune") == 0) {
    m_tune = v;
  } else if (std::strcmp(name, "cutoff") == 0) {
    m_cutoffKnob = v;
  } else if (std::strcmp(name, "resonance") == 0) {
    m_resonanceKnob = v;
  } else if (std::strcmp(name, "envMod") == 0 || std::strcmp(name, "env_mod") == 0) {
    m_envModKnob = v;
  } else if (std::strcmp(name, "decay") == 0) {
    m_decayKnob = v;
    updateDecayCoeffs();
  } else if (std::strcmp(name, "accent") == 0) {
    m_accentKnob = v;
  } else if (std::strcmp(name, "waveform") == 0) {
    m_waveformSaw = (v < 0.5f);
  }
}

void Tb303Voice::reset() {
  m_phase = 0.0f;
  m_envelope = 0.0f;
  m_gateOpen = false;
  m_currentPitch = 440.0f;
  m_targetPitch = 440.0f;
  m_stepAccent = 0.0f;
  m_prevStepActive = false;
  m_prevStepSlide = false;
  std::memset(m_filterStage, 0, sizeof(m_filterStage));
  m_smoothedCutoff = m_cutoffKnob;
  m_smoothedResonance = m_resonanceKnob;
  processFilterCoeffs(m_smoothedCutoff, m_smoothedResonance);
}

} // namespace rb338
