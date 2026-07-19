#pragma once

#include "DrumSynth.h"
#include "Voice.h"
#include <array>

namespace rb338 {

/**
 * Tr808Voice — TR-808 drum machine (Phase 1 synthetic MVP).
 *
 * Procedural analogue-style models for BD, SD, CH, OH, RS, CP.
 * Each instrument is monophonic; multiple instruments layer per step.
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
  enum class Channel : size_t {
    Kick = 0,
    Snare,
    ClosedHat,
    OpenHat,
    Rimshot,
    Clap,
    Count
  };

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  std::array<DrumVoiceChannel, static_cast<size_t>(Channel::Count)> m_channels{};

  void fire(Channel ch, DrumVoiceId id, bool accent);
};

} // namespace rb338
