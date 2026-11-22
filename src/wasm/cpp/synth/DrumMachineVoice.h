#pragma once

#include "../engine/EngineCommands.h"
#include "../parser/DrumKit.h"
#include "DrumBitfield.h"
#include "DrumSynth.h"
#include "SamplePool.h"
#include "SampleVoice.h"
#include "Voice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rb338 {

/** Where one sequencer hit lands: a channel and the procedural model it plays. */
struct DrumRoute {
  uint8_t channel;
  DrumVoiceId id;
};

/**
 * Index of each hit in StepData bit order: the eight `DrumHit` bits of
 * `note`, then the three `DrumExtra` bits. `drumAccent` / `drumFlam` use the
 * same order.
 */
enum DrumHitIndex : uint8_t {
  kHitBD, kHitSD, kHitLT, kHitMT, kHitHT, kHitCH, kHitOH, kHitCB,
  kHitCP, kHitCY, kHitRS,
  kNumDrumHits
};

using DrumKitBytes = std::array<uint8_t, DRUM_KIT_BYTES>;

/** A kit byte by its TRAK controller id (DrumKit.h). */
inline uint8_t kitByte(const DrumKitBytes& kit, uint8_t controller) {
  return kit[kitIndex(controller)];
}

/** A 0–127 kit knob as 0–1. */
inline float kitKnob(const DrumKitBytes& kit, uint8_t controller) {
  return static_cast<float>(std::min<uint8_t>(kitByte(kit, controller), 127)) / 127.0f;
}

/** An instrument's DECAY knob, shifted by the machine's DECAY macro (0.5 = neutral). */
inline float kitDecay(const DrumKitBytes& kit, uint8_t controller, const DrumParams& macros) {
  return std::clamp(kitKnob(kit, controller) + (macros.decay - 0.5f), 0.0f, 1.0f);
}

/**
 * DrumMachineVoice — the TR-808 and TR-909.
 *
 * `Kit` supplies, all static:
 *   - `DeviceId kDevice`, `bool kIs909`
 *   - `std::array<ModSampleSlot, N> kSlots` — mod slot per channel
 *   - `uint8_t kClosedHat, kOpenHat` — the closed hat chokes the open one
 *   - `DrumRoute route(size_t hit, const DrumKitBytes&)` — channel per hit,
 *     after the 808's selector switches
 *   - `DrumHitParams hitParams(size_t hit, const DrumKitBytes&, const DrumParams&)`
 *   - `float flamSeconds(const DrumKitBytes&)`
 *
 * Every instrument is monophonic; instruments layer per step. When a `.rbm`
 * mod supplies PCM for a channel's slot that channel plays the sample, and
 * every other channel keeps its procedural model, so a partial mod still
 * sounds like a complete kit.
 *
 * Dynamics: the step's AC (total accent) row scales every hit on the step by
 * the ACCENT knob; on the 909 a hit can also be accented on its own
 * (`drumAccent`) or flammed (`drumFlam`). A flam plays a quieter grace hit
 * on the step and the full hit FLAM INTERVAL later — the 909 sequencer can
 * only fire on or after the grid, so the grace note leads.
 */
template <typename Kit>
class DrumMachineVoice : public Voice {
public:
  static constexpr size_t NUM_CHANNELS = Kit::kSlots.size();

  /** Grace-note level relative to the main hit of a flam. */
  static constexpr float kFlamGraceGain = 0.5f;
  /** Gain of a 909 per-instrument accent over a normal hit. */
  static constexpr float kInstrumentAccentGain = 1.35f;

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
    m_kit = effectiveDrumKit(state, Kit::kDevice);
  }

  void setSamplePool(const SamplePool* pool) override {
    m_pool = pool;
    for (auto& sv : m_sampleVoices) sv.reset();
  }

  void render(float* output, uint32_t numFrames) override {
    if (!output || numFrames == 0) return;
    for (uint32_t i = 0; i < numFrames; ++i) {
      if (m_pendingFlams != 0) advanceFlams();
      float sample = 0.0f;
      for (auto& ch : m_channels) sample += ch.render();
      for (auto& sv : m_sampleVoices) sample += sv.render();
      output[i] = sample;
    }
  }

  void triggerStep(uint8_t stepIndex, const StepData& step) override {
    (void)stepIndex;
    if (!step.active) return;
    const uint32_t bits = static_cast<uint32_t>(step.note) |
                          (static_cast<uint32_t>(step.drumExtra & 0x07u) << 8);
    const float total = drumAccentGain(m_params, step.accent);
    for (size_t hit = 0; hit < kNumDrumHits; ++hit) {
      const uint32_t bit = 1u << hit;
      if (!(bits & bit)) continue;
      DrumHitParams params = Kit::hitParams(hit, m_kit, m_params);
      params.gain *= total;
      if (Kit::kIs909 && (step.drumAccent & bit)) params.gain *= kInstrumentAccentGain;
      const DrumRoute route = Kit::route(hit, m_kit);
      if (Kit::kIs909 && (step.drumFlam & bit)) {
        DrumHitParams grace = params;
        grace.gain *= kFlamGraceGain;
        fire(route, grace);
        scheduleFlam(route, params);
      } else {
        fire(route, params);
      }
    }
  }

  void setParameter(DeviceParamId param, float value) override {
    const float v = std::clamp(value, 0.0f, 1.0f);
    switch (param) {
      case DeviceParamId::Tune: m_params.tune = v; return;
      case DeviceParamId::Decay: m_params.decay = v; return;
      case DeviceParamId::Accent: m_params.accent = v; return;
      default: break;
    }
    const auto id = static_cast<uint8_t>(param);
    if (id >= static_cast<uint8_t>(DeviceParamId::KitBase) &&
        id < static_cast<uint8_t>(DeviceParamId::KitBase) + DRUM_KIT_BYTES) {
      m_kit[id - static_cast<uint8_t>(DeviceParamId::KitBase)] =
          static_cast<uint8_t>(std::lround(v * 127.0f));
    }
  }

  void reset() override {
    for (auto& ch : m_channels) ch.reset();
    for (auto& sv : m_sampleVoices) sv.reset();
    m_pendingFlams = 0;
  }

  /** Kit bytes as the voice currently plays them (tests). */
  const DrumKitBytes& kit() const { return m_kit; }

private:
  struct PendingFlam {
    uint32_t samplesLeft = 0;
    DrumRoute route{};
    DrumHitParams params{};
  };

  void fire(const DrumRoute& route, const DrumHitParams& params) {
    const size_t index = route.channel;
    if (index == Kit::kClosedHat) chokeOpenHat();
    // Mod sample wins for this slot; otherwise fall back to the analogue model.
    if (m_pool) {
      if (const SamplePool::SlotData* slot = m_pool->slotData(Kit::kSlots[index])) {
        m_sampleVoices[index].trigger(*slot, params.pitchMul, params.decay, params.gain);
        return;
      }
    }
    m_channels[index].trigger(route.id, params, Kit::kIs909);
  }

  void chokeOpenHat() {
    m_channels[Kit::kOpenHat].choke();
    m_sampleVoices[Kit::kOpenHat].reset();
  }

  void scheduleFlam(const DrumRoute& route, const DrumHitParams& params) {
    const auto delay = static_cast<uint32_t>(
        std::max(1.0f, std::round(Kit::flamSeconds(m_kit) * m_sampleRate)));
    PendingFlam& slot = m_flams[route.channel];
    slot.samplesLeft = delay;
    slot.route = route;
    slot.params = params;
    m_pendingFlams |= 1u << route.channel;
  }

  void advanceFlams() {
    for (size_t c = 0; c < NUM_CHANNELS; ++c) {
      const uint32_t bit = 1u << c;
      if (!(m_pendingFlams & bit)) continue;
      PendingFlam& flam = m_flams[c];
      if (--flam.samplesLeft == 0) {
        m_pendingFlams &= ~bit;
        fire(flam.route, flam.params);
      }
    }
  }

  float m_sampleRate = 44100.0f;
  DrumParams m_params{};
  DrumKitBytes m_kit = defaultDrumKit(Kit::kDevice);
  std::array<DrumVoiceChannel, NUM_CHANNELS> m_channels{};
  // Mod sample playback. Borrowed pointer — owned by the EngineSnapshot.
  const SamplePool* m_pool = nullptr;
  std::array<SampleVoice, NUM_CHANNELS> m_sampleVoices{};
  std::array<PendingFlam, NUM_CHANNELS> m_flams{};
  uint32_t m_pendingFlams = 0;
  static_assert(NUM_CHANNELS <= 32, "flam mask is 32-bit");
};

// The DrumExtra bits must follow DrumHit's eight in kHitCP, kHitCY, kHitRS
// order for triggerStep's single bit walk.
static_assert(DrumExtra::CP == 1u << (kHitCP - 8) && DrumExtra::CY == 1u << (kHitCY - 8) &&
              DrumExtra::RS == 1u << (kHitRS - 8));
static_assert(DrumHit::BD == 1u << kHitBD && DrumHit::CB == 1u << kHitCB);

} // namespace rb338
