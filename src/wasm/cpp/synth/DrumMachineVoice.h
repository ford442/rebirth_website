#pragma once

#include "DrumBitfield.h"
#include "DrumSynth.h"
#include "SamplePool.h"
#include "SampleVoice.h"
#include "Voice.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace rb338 {

/** Where one sequencer hit lands: a channel, its procedural model and its mod slot. */
struct DrumRoute {
  uint8_t channel;
  DrumVoiceId id;
};

/**
 * Index of each hit in a kit's `kRoutes` table, in StepData bit order: the
 * eight `DrumHit` bits of `note`, then the three `DrumExtra` bits.
 */
enum DrumHitIndex : uint8_t {
  kHitBD, kHitSD, kHitLT, kHitMT, kHitHT, kHitCH, kHitOH, kHitCL,
  kHitCP, kHitMA, kHitRS,
  kNumDrumHits
};

/**
 * DrumMachineVoice — the TR-808 and TR-909, which differ only in which
 * channel each sequencer bit plays, which mod slot backs each channel, and a
 * few model constants (DrumVoiceChannel's `is909`).
 *
 * `Kit` supplies, all `static constexpr`:
 *   - `bool kIs909`
 *   - `std::array<ModSampleSlot, N> kSlots`      — mod slot per channel
 *   - `std::array<DrumRoute, kNumDrumHits> kRoutes` — channel per hit bit
 *
 * Every instrument is monophonic; instruments layer per step. When a `.rbm`
 * mod supplies PCM for a channel's slot that channel plays the sample, and
 * every other channel keeps its procedural model, so a partial mod still
 * sounds like a complete kit.
 */
template <typename Kit>
class DrumMachineVoice : public Voice {
public:
  static constexpr size_t NUM_CHANNELS = Kit::kSlots.size();

  void init(float sampleRate) override {
    m_sampleRate = sampleRate;
    for (auto& ch : m_channels) ch.init(sampleRate);
    for (auto& sv : m_sampleVoices) sv.init(sampleRate);
    reset();
  }

  void load(const DeviceState& state) override {
    m_params.tune = state.tune;
    m_params.decay = state.decay;
    m_params.accent = state.accent;
  }

  void setSamplePool(const SamplePool* pool) override {
    m_pool = pool;
    for (auto& sv : m_sampleVoices) sv.reset();
  }

  void render(float* output, uint32_t numFrames) override {
    if (!output || numFrames == 0) return;
    for (uint32_t i = 0; i < numFrames; ++i) {
      float sample = 0.0f;
      for (auto& ch : m_channels) sample += ch.render();
      for (auto& sv : m_sampleVoices) sample += sv.render();
      output[i] = sample;
    }
  }

  void triggerStep(uint8_t stepIndex, const StepData& step) override {
    (void)stepIndex;
    if (!step.active) return;
    const bool accent = step.accent;
    const uint32_t bits = static_cast<uint32_t>(step.note) |
                          (static_cast<uint32_t>(step.drumExtra & 0x07u) << 8);
    for (size_t hit = 0; hit < kNumDrumHits; ++hit) {
      // Extra bits are CP (bit 8), MA (bit 9), RS (bit 10) — kHitCP.. order.
      if (bits & (1u << hit)) fire(Kit::kRoutes[hit], accent);
    }
  }

  void setParameter(DeviceParamId param, float value) override {
    switch (param) {
      case DeviceParamId::Tune: m_params.tune = value; break;
      case DeviceParamId::Decay: m_params.decay = value; break;
      case DeviceParamId::Accent: m_params.accent = value; break;
      default: break;
    }
  }

  void reset() override {
    for (auto& ch : m_channels) ch.reset();
    for (auto& sv : m_sampleVoices) sv.reset();
  }

private:
  void fire(const DrumRoute& route, bool accent) {
    const size_t index = route.channel;
    // Mod sample wins for this slot; otherwise fall back to the analogue model.
    if (m_pool) {
      if (const SamplePool::SlotData* slot = m_pool->slotData(Kit::kSlots[index])) {
        m_sampleVoices[index].trigger(*slot, drumPitchMul(m_params), m_params.decay,
                                      drumAccentGain(m_params, accent));
        return;
      }
    }
    m_channels[index].trigger(route.id, 1.0f, accent, m_params, Kit::kIs909);
  }

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  std::array<DrumVoiceChannel, NUM_CHANNELS> m_channels{};
  // Mod sample playback. Borrowed pointer — owned by the EngineSnapshot.
  const SamplePool* m_pool = nullptr;
  std::array<SampleVoice, NUM_CHANNELS> m_sampleVoices{};
};

// The DrumExtra bits must follow DrumHit's eight in kHitCP, kHitMA, kHitRS
// order for triggerStep's single bit walk.
static_assert(DrumExtra::CP == 1u << (kHitCP - 8) && DrumExtra::MA == 1u << (kHitMA - 8) &&
              DrumExtra::RS == 1u << (kHitRS - 8));
static_assert(DrumHit::BD == 1u << kHitBD && DrumHit::CL == 1u << kHitCL);

} // namespace rb338
