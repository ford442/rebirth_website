#pragma once

#include "Voice.h"

namespace rb338 {

/**
 * Tr808Voice — TR-808 drum machine emulation.
 *
 * Architecture:
 *   - 12 independent drum channels
 *   - Each channel is either a sampled AIFF (from .rbm mod) or a simple
 *     synthesis model (sine burst for kick, filtered noise for snare, etc.)
 *   - Monophonic per instrument (except BD which can overlap slightly)
 */
class Tr808Voice : public Voice {
public:
  Tr808Voice() = default;
  ~Tr808Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state, const std::vector<Pattern>& patterns) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void setParameter(const char* name, float value) override;
  void reset() override;

private:
  float m_sampleRate = 44100.0f;

  // Test-tone state: simple noise burst with decay.
  uint32_t m_gateSamples = 0;
  float m_noiseState = 0.0f;
  static constexpr uint32_t GATE_LENGTH_SAMPLES = 5512; // ~0.125s at 44.1kHz

  // TODO: drum channel states, sample playback buffers, synthesis models
};

} // namespace rb338
