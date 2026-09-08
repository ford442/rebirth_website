#pragma once

#include "../audio/SampleDecoder.h"
#include "../parser/RbmTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rb338 {

/** Outcome of loading a whole mod into the pool. */
enum class ModLoadStatus : uint8_t {
  Ok = 0,
  NotInitialised,  // init() was never called
  NoSamples,       // the mod parsed but held nothing decodable
  ArenaExhausted,  // fixed arena filled before every slot was decoded
  Partial,         // some slots decoded, at least one failed
};

const char* modLoadStatusName(ModLoadStatus status);

/** Per-resource decode outcome, for diagnostics and the JS-facing summary. */
struct ModSampleReportEntry {
  std::string name;
  ModResourceKind kind = ModResourceKind::Other;
  ModSampleSlot slot = ModSampleSlot::Unknown;
  uint32_t byteSize = 0;
  uint32_t frameCount = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint8_t bitDepth = 0;
  SampleDecodeStatus decodeStatus = SampleDecodeStatus::UnknownFormat;
  bool loaded = false;
};

/** Everything the UI needs about a load, without any PCM crossing to JS. */
struct ModLoadReport {
  std::string title;
  std::string description;
  std::string copyright;
  std::vector<ModSampleReportEntry> resources;
  ModLoadStatus status = ModLoadStatus::NotInitialised;
  uint32_t loadedSlots = 0;
  uint32_t skinCount = 0;
  uint32_t usedFrames = 0;
  uint32_t capacityFrames = 0;
};

/**
 * Describe a mod without decoding it or touching an arena.
 *
 * Reads sample headers only (probeSample), so it is cheap enough for a
 * catalogue view. In the returned report, `loaded` means "would decode",
 * and `usedFrames` is what a load *would* consume — letting the UI warn
 * about an oversized mod before committing to it. `capacityFrames` is the
 * arena size a load would run against.
 */
ModLoadReport summariseMod(const ParsedMod& mod);

/**
 * SamplePool — decoded mod PCM in a fixed arena.
 *
 * Allocation happens exactly once, in init(), on the main thread. Loading a
 * mod decodes straight into that arena and fails cleanly with
 * ArenaExhausted rather than growing — the shipping WASM build runs with
 * -sALLOW_MEMORY_GROWTH=0, so a mid-callback grow is not merely slow, it is
 * a hard failure.
 *
 * The audio thread only ever calls the const accessors, which hand back raw
 * pointers into the arena. Those pointers stay valid for the life of the
 * pool because the arena is never resized after init().
 */
class SamplePool {
public:
  /** 2M frames of float32 = 8 MiB — a full 24-slot mod plus 303 wavetables. */
  static constexpr size_t kDefaultArenaFrames = 2u * 1024u * 1024u;

  struct SlotData {
    const float* pcm = nullptr;
    uint32_t frameCount = 0;
    uint32_t sampleRate = 0;
  };

  /** Reserve the arena. Main thread only; safe to call once per pool. */
  bool init(size_t arenaFrames = kDefaultArenaFrames);

  /** Drop every loaded slot but keep the arena. Main thread only. */
  void clear();

  /**
   * Decode every sample resource in `mod` into the arena. Main thread only.
   *
   * Skins and bundled songs are catalogued in the report and never decoded —
   * the AudioWorklet must not carry JPEG bytes.
   */
  ModLoadStatus loadFromParsedMod(const ParsedMod& mod, ModLoadReport& report);

  // ── Audio-thread-safe reads ────────────────────────────────────────
  bool hasSlot(ModSampleSlot slot) const;
  const SlotData* slotData(ModSampleSlot slot) const;
  bool empty() const { return m_loadedSlots == 0; }

  size_t usedFrames() const { return m_used; }
  size_t capacityFrames() const { return m_arena.size(); }
  uint32_t loadedSlots() const { return m_loadedSlots; }

private:
  std::vector<float> m_arena;
  std::array<SlotData, NUM_MOD_SAMPLE_SLOTS> m_slots{};
  size_t m_used = 0;
  uint32_t m_loadedSlots = 0;
  bool m_initialised = false;
};

} // namespace rb338
