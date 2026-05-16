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
  // TODO: store pattern reference
}

void Tb303Voice::render(float* output, uint32_t numFrames) {
  // TODO: real TB-303 synthesis
  // For now, output silence
  std::memset(output, 0, numFrames * sizeof(float));
}

void Tb303Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  // TODO: handle note on, accent, slide
}

void Tb303Voice::setParameter(const char* name, float value) {
  // TODO: map parameter names to internal state
}

void Tb303Voice::reset() {
  m_phase = 0.0f;
  m_envelope = 0.0f;
  m_currentPitch = 0.0f;
  m_targetPitch = 0.0f;
  std::memset(m_filterState, 0, sizeof(m_filterState));
}

} // namespace rb338
