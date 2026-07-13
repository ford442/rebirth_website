#include "Tr909Voice.h"
#include <cmath>
#include <cstring>

namespace rb338 {

void Tr909Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  reset();
}

void Tr909Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  // TODO: load drum parameters and pattern data.
  (void)state;
  (void)patterns;
}

void Tr909Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    if (m_gateSamples == 0) {
      output[i] = 0.0f;
      continue;
    }

    // Slightly brighter pseudo-noise than the 808 test tone.
    m_noiseState = std::fmod(m_noiseState * 2.31f + 0.17f, 2.0f) - 1.0f;

    float env = static_cast<float>(m_gateSamples) / GATE_LENGTH_SAMPLES;
    env = env * env;

    output[i] = m_noiseState * env * 0.18f;
    if (m_gateSamples > 0) --m_gateSamples;
  }
}

void Tr909Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;
  if (!step.active) return;
  m_gateSamples = GATE_LENGTH_SAMPLES;
}

void Tr909Voice::setParameter(const char* name, float value) {
  (void)name;
  (void)value;
  // TODO: map drum parameter names.
}

void Tr909Voice::reset() {
  m_gateSamples = 0;
  m_noiseState = 0.0f;
}

} // namespace rb338
