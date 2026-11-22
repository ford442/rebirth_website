#pragma once

#include "DrumSynth.h"
#include "SampleVoice.h"
#include "Voice.h"
#include <array>

namespace rb338 {

/**
 * Tr909Voice — TR-909 drum machine.
 *
 * Procedural models for every 909 voice: BD, SD, toms, CH, OH, hand clap
 * (CL), rimshot (RS), crash (CP) and ride (MA). A `.rbm` mod
 * can replace any individual slot with PCM; the rest stay procedural.
 */
class Tr909Voice : public Voice {
public:
  Tr909Voice() = default;
  ~Tr909Voice() override = default;

  void init(float sampleRate) override;
  void load(const DeviceState& state) override;
  void render(float* output, uint32_t numFrames) override;
  void triggerStep(uint8_t stepIndex, const StepData& step) override;
  void setParameter(DeviceParamId param, float value) override;
  void setSamplePool(const SamplePool* pool) override;
  void reset() override;

private:
  static constexpr size_t NUM_CHANNELS = 11;

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  std::array<DrumVoiceChannel, NUM_CHANNELS> m_channels{};

  // Mod sample playback. Borrowed pointer — owned by the EngineSnapshot.
  const SamplePool* m_pool = nullptr;
  std::array<SampleVoice, NUM_CHANNELS> m_sampleVoices{};

  void fire(DrumVoiceId id, bool accent);
};

} // namespace rb338
