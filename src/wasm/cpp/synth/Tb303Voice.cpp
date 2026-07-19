#include "Tb303Voice.h"
#include <cmath>
#include <cstring>

namespace rb338 {

void Tb303Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  reset();
}

void Tb303Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  m_cutoff = state.cutoff;
  m_resonance = state.resonance;
  m_envMod = state.envMod;
  // Pattern reference is not needed for the test-tone implementation.
  (void)patterns;
}

void Tb303Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  // MIDI note to frequency: f = 440 * 2^((note - 69) / 12).
  // For the test tone we centre around C2 (MIDI 36) when note is small.
  const float baseNote = static_cast<float>(m_currentNote);
  const float freq = 440.0f * std::pow(2.0f, (baseNote - 69.0f) / 12.0f);
  const float phaseInc = freq / m_sampleRate;

  for (uint32_t i = 0; i < numFrames; ++i) {
    if (m_gateSamples == 0) {
      output[i] = 0.0f;
      continue;
    }

    // Sawtooth test tone.
    float sample = 2.0f * m_phase - 1.0f;

    m_phase += phaseInc;
    if (m_phase >= 1.0f) m_phase -= 1.0f;

    // Simple decay envelope while the gate is open.
    float env = static_cast<float>(m_gateSamples) / GATE_LENGTH_SAMPLES;
    env = std::sqrt(env); // faster-sounding decay
    output[i] = sample * env * 0.15f;

    if (m_gateSamples > 0) --m_gateSamples;
  }
}

void Tb303Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;
  if (!step.active) return;
  m_currentNote = step.note;
  m_gateSamples = GATE_LENGTH_SAMPLES;
  m_phase = 0.0f;
}

void Tb303Voice::setParameter(const char* name, float value) {
  (void)name;
  (void)value;
  // TODO: map parameter names to internal state.
}

void Tb303Voice::reset() {
  m_phase = 0.0f;
  m_envelope = 0.0f;
  m_currentPitch = 0.0f;
  m_targetPitch = 0.0f;
  m_currentNote = 0;
  m_gateSamples = 0;
  std::memset(m_filterState, 0, sizeof(m_filterState));
}

} // namespace rb338
