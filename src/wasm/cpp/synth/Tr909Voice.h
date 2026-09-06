#pragma once

#include "DrumSynth.h"
#include "Voice.h"
#include <array>

namespace rb338 {

/**
 * Tr909Voice — TR-909 drum machine (Phase 1 synthetic MVP).
 *
 * Procedural models for BD, SD, CH, OH, and clap (CL / CP).
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
  static constexpr size_t NUM_CHANNELS = 11;

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  std::array<DrumVoiceChannel, NUM_CHANNELS> m_channels{};

  void fire(DrumVoiceId id, bool accent);
};

} // namespace rb338
