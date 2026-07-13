#include "Tr808Voice.h"
#include <cmath>
#include <cstring>

namespace rb338 {

void Tr808Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  reset();
}

void Tr808Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  // TODO: load drum parameters and pattern data.
  (void)state;
  (void)patterns;
}

void Tr808Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    if (m_gateSamples == 0) {
      output[i] = 0.0f;
      continue;
    }

    // Simple filtered pseudo-noise (linear congruential-ish).
    m_noiseState = std::fmod(m_noiseState * 1.97f + 0.13f, 2.0f) - 1.0f;

    // Exponential decay envelope.
    float env = static_cast<float>(m_gateSamples) / GATE_LENGTH_SAMPLES;
    env = env * env; // sharper decay

    output[i] = m_noiseState * env * 0.2f;
    if (m_gateSamples > 0) --m_gateSamples;
  }
}

void Tr808Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;
  if (!step.active) return;
  m_gateSamples = GATE_LENGTH_SAMPLES;
}

void Tr808Voice::setParameter(const char* name, float value) {
  (void)name;
  (void)value;
  // TODO: map drum parameter names.
}

void Tr808Voice::reset() {
  m_gateSamples = 0;
  m_noiseState = 0.0f;
}

} // namespace rb338
