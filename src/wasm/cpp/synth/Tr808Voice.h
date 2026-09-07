#pragma once

#include "DrumSynth.h"
#include "SampleVoice.h"
#include "Voice.h"
#include <array>

namespace rb338 {

/**
 * Tr808Voice — TR-808 drum machine.
 *
 * Procedural analogue-style models for BD, SD, CH, OH, RS, CP. When a
 * `.rbm` mod supplies PCM for a slot, that slot plays the sample instead;
 * every other slot keeps its procedural model, so a partial mod still
 * sounds like a complete kit.
 *
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
  void setParameter(DeviceParamId param, float value) override;
  void setSamplePool(const SamplePool* pool) override;
  void reset() override;

private:
  enum class Channel : size_t {
    Kick = 0,
    Snare,
    ClosedHat,
    OpenHat,
    Rimshot,
    Clap,
    Clave,
    Maracas,
    LowTom,
    MidTom,
    HighTom,
    Count
  };

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  std::array<DrumVoiceChannel, static_cast<size_t>(Channel::Count)> m_channels{};

  // Mod sample playback. Borrowed pointer — owned by the EngineSnapshot.
  const SamplePool* m_pool = nullptr;
  std::array<SampleVoice, static_cast<size_t>(Channel::Count)> m_sampleVoices{};

  void fire(Channel ch, DrumVoiceId id, bool accent);
};

} // namespace rb338
