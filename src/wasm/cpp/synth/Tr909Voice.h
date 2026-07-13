#pragma once

#include "Voice.h"

namespace rb338 {

/**
 * Tr909Voice — TR-909 drum machine emulation.
 *
 * Architecture:
 *   - 11 drum channels (909 has fewer sounds than 808 but with tune/attack controls)
 *   - Sample-based playback from .rbm mod files
 *   - BD has tune + attack + decay parameters
 *   - SD has tune + tone + snappy parameters
 *   - Accent channel controls overall level per step
 */
class Tr909Voice : public Voice {
public:
  Tr909Voice() = default;
  ~Tr909Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state, const std::vector<Pattern>& patterns) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void setParameter(const char* name, float value) override;
  void reset() override;

private:
  float m_sampleRate = 44100.0f;

  // Test-tone state: simple noise burst with decay (909 variant).
  uint32_t m_gateSamples = 0;
  float m_noiseState = 0.0f;
  static constexpr uint32_t GATE_LENGTH_SAMPLES = 4410; // ~0.1s at 44.1kHz

  // TODO: drum channel states, sample buffers, accent handling
};

} // namespace rb338
