#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace rb338 {

// ─────────────────────────────────────────────────────────────────
// .rbs File Format Constants
// ─────────────────────────────────────────────────────────────────

constexpr char RBS_MAGIC[] = "ReBirth Song File";
constexpr size_t RBS_MAGIC_LEN = 17;

// Real archive files use a Propellerhead chunk container, not the plain header above.
constexpr char RBS_CONTAINER_MAGIC[] = "CAT ";
constexpr char RBS_FORMAT_MARKER[]   = "RB40";
constexpr char RBS_HEAD_MAGIC[]      = "HEAD";
constexpr char RBS_HEAD_SIGNATURE[]  = "[T[T";

constexpr int MAX_PATTERNS_PER_BANK = 8;
constexpr int MAX_BANKS = 4;              // A, B, C, D
constexpr int MAX_STEPS = 16;
constexpr int NUM_DEVICES = 4;            // 303-A, 303-B, 808, 909

// Tempo range, shared by the parser (GLOB validation), the engine (transport
// clamp, delay sizing, automation timing) and the TypeScript player
// (`TEMPO_MIN_BPM` / `TEMPO_MAX_BPM` in wasm-audio-config.ts — the contract
// check keeps the two in step). ReBirth documents 20–500 BPM; the engine
// used to clamp to 40–250, so a song the parser accepted could play at a
// tempo other than the one it was saved with.
constexpr float MIN_BPM = 20.0f;
constexpr float MAX_BPM = 500.0f;

// ─────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────

enum class DeviceId : uint8_t {
  TB303_A = 0,
  TB303_B = 1,
  TR808   = 2,
  TR909   = 3
};

enum class RbsVersion : uint8_t {
  V1_0   = 0x10,
  V1_5   = 0x15,
  V2_0   = 0x20,
  V2_0_1 = 0x21
};

// ─────────────────────────────────────────────────────────────────
// Step Data
// ─────────────────────────────────────────────────────────────────

struct StepData {
  bool active = false;
  uint8_t note = 0;      // MIDI note for 303; drum-hit bitfield for 808/909
  uint8_t drumExtra = 0; // Secondary drum hits: CP, CY/RD, RS (see DrumBitfield.h)
  /**
   * 303: the step's accent flag. 808/909: the machine's AC (total accent)
   * row, which accents every instrument hit on the step.
   */
  bool accent = false;
  bool slide = false;    // 303 only
  /**
   * TR-909 only — per-instrument dynamics, one bit per hit in DrumHitIndex
   * order (bits 0–7 mirror `note`, bits 8–10 mirror `drumExtra`). A 909 row
   * byte is 1 for a normal hit, 2 for an accented one and 3 for a flam
   * (RbsFormat.md §6e); the 808 has only the AC row, so both stay 0 there.
   */
  uint16_t drumAccent = 0;
  uint16_t drumFlam = 0;
};

// ─────────────────────────────────────────────────────────────────
// Pattern
// ─────────────────────────────────────────────────────────────────

struct Pattern {
  DeviceId deviceId;
  uint8_t bank = 0;          // 0-3
  uint8_t patternIndex = 0;  // 0-7
  uint8_t length = 16;       // 1-16
  /** Slot byte 0: this pattern plays with the song's shuffle amount. */
  bool shuffle = false;
  std::array<StepData, MAX_STEPS> steps;
};

// ─────────────────────────────────────────────────────────────────
// Device State (knobs + mixer settings)
// ─────────────────────────────────────────────────────────────────

/**
 * Drum-machine kit bytes: header bytes 3.. of the `808 ` / `909 ` chunk,
 * which are also the TRAK controller ids 0x03.. on that device's track.
 * The 808 uses 27 of them, the 909 all 28; DrumKit.h names each one.
 */
constexpr size_t DRUM_KIT_BYTES = 28;

struct DeviceState {
  DeviceId id;
  // Normalised knob values 0.0–1.0
  float tune = 0.5f;
  float cutoff = 0.5f;
  float resonance = 0.5f;
  float envMod = 0.5f;
  float decay = 0.5f;
  float accent = 0.5f;
  uint8_t waveform = 0;              // TB-303 only: 0 = saw, 1 = square
  uint8_t initialPatternBank = 0;    // 0–3
  uint8_t initialPatternIndex = 0;   // 0–7
  // Mixer
  bool muted = false;
  float level = 0.8f;
  float pan = 0.5f;
  // FX sends. The compressor and PCF are single inserts chosen by device id
  // in MIXR (SongFxSettings::comp.target / pcf.target), not per-channel flags.
  bool dist = false;
  float delaySend = 0.0f;
  /**
   * 808/909 per-instrument knobs and selector switches, raw 0–127 bytes
   * (DrumKit.h). Meaningless for a TB-303. `accent` above is the drum
   * machine's ACCENT knob (header byte 2).
   */
  std::array<uint8_t, DRUM_KIT_BYTES> kit{};
  /**
   * C++-only: false until a drum chunk (or an edit) supplied `kit`. Voices
   * and the writer then use the machine's default kit (defaultDrumKit), so
   * a hand-built song or a v1 song is never silent. Not an Embind field —
   * summariseSong() hands the UI the effective kit instead.
   */
  bool hasKit = false;
};

// ─────────────────────────────────────────────────────────────────
// Arrangement
// ─────────────────────────────────────────────────────────────────

struct PatternRef {
  uint8_t bank = 0;
  uint8_t index = 0;
};

struct ArrangementBar {
  uint16_t barNumber = 1;
  // One pattern reference per device
  std::array<PatternRef, NUM_DEVICES> devicePatterns;
};

// ─────────────────────────────────────────────────────────────────
// Song FX (DEVL chunks: DELY, PCF, DIST, COMP + MIXR master)
// ─────────────────────────────────────────────────────────────────

/**
 * Insert-target device ids, as MIXR bytes 1 (compressor) and 2 (PCF) store
 * them: 0 = none, 1 = master bus, 2–5 = TB-303 A, TB-303 B, TR-808, TR-909.
 */
constexpr uint8_t FX_TARGET_NONE = 0;
constexpr uint8_t FX_TARGET_MASTER = 1;
constexpr uint8_t FX_TARGET_FIRST_DEVICE = 2;
constexpr uint8_t FX_TARGET_MAX = FX_TARGET_FIRST_DEVICE + NUM_DEVICES - 1;

struct DelaySettings {
  bool enabled = false;
  uint8_t steps = 3;      // delay time in steps, 1–32 (byte 1)
  bool triplet = false;   // byte 2: 0 = triplet steps, 1 = straight sixteenths
  uint8_t feedback = 0;   // 0–127
  uint8_t pan = 64;       // delay return pan, 0 = left, 64 = centre, 127 = right
};

struct PcfSettings {
  bool enabled = false;
  uint8_t cutoff = 64;    // 0–127
  uint8_t resonance = 0;  // 0–127
  uint8_t envAmount = 0;  // 0–127
  uint8_t pattern = 0;    // byte 4: trigger pattern 0–54
  uint8_t decay = 64;     // byte 5: envelope decay 0–127
  bool lowpass = true;    // byte 6: 0 = band-pass, 1 = low-pass
  uint8_t target = FX_TARGET_NONE; // MIXR byte 2
};

struct DistSettings {
  bool enabled = false;
  uint8_t drive = 32;     // 0–127
  uint8_t mix = 29;       // 0–127
};

struct CompSettings {
  bool enabled = false;
  uint8_t threshold = 32; // 0–127
  uint8_t ratio = 127;    // 0–127
  uint8_t attack = 127;   // COMP byte 3, 0–127 (only 0 and 127 observed)
  uint8_t target = FX_TARGET_NONE; // MIXR byte 1
};

struct SongFxSettings {
  uint8_t masterLevel = 127; // MIXR byte 0, 0–127
  DelaySettings delay;
  PcfSettings pcf;
  DistSettings dist;
  CompSettings comp;
};

// ─────────────────────────────────────────────────────────────────
// TRAK automation (C++ playback only; not exported to JS yet)
// ─────────────────────────────────────────────────────────────────

struct AutomationEvent {
  uint8_t trackIndex = 0;   // 0=mixer, 1–4=devices, 5–8=FX
  uint32_t tickPosition = 0;
  uint8_t controller = 0;
  uint8_t value = 0;
};

constexpr int NUM_TRAK_TRACKS = 9;

/** GLOB loop locators are bar positions × 768 (4 beats × 192 PPQN). */
constexpr uint32_t GLOB_TICKS_PER_BAR = 768;

/** GLOB byte 0. */
enum class PlayMode : uint8_t {
  Pattern = 0,
  Song = 1,
};

// ─────────────────────────────────────────────────────────────────
// Parsed Song (top-level result)
// ─────────────────────────────────────────────────────────────────

struct ParsedSong {
  std::string title;
  std::string author;
  std::string infoText;
  std::string creatorUrl;
  float bpm = 125.0f;
  RbsVersion version = RbsVersion::V2_0_1;

  // Observed container sub-format markers (for diagnostics)
  uint8_t headVersion = 0;   // HEAD chunk byte at offset 0x0a (0x01 / 0x02)
  uint8_t globSubFormat = 0; // GLOB chunk byte at offset 0x03 (0x01 / 0x02)
  /** USRI byte 0x21f: open the song-information dialog on load. */
  bool showInfoOnOpen = false;

  // GLOB transport and groove (RbsFormat.md §4)
  PlayMode playMode = PlayMode::Song;
  bool loopEnabled = false;  // byte 0x01, honoured in song mode only
  uint32_t loopStart = 0;    // byte 0x06, bar × GLOB_TICKS_PER_BAR
  uint32_t loopEnd = 0;      // byte 0x0a, bar × GLOB_TICKS_PER_BAR
  uint8_t shuffle = 0;       // byte 0x0e, 0–127 (raw byte; consumers clamp)
  bool vintage = false;      // byte 0x1e2, ReBirth 1.x "vintage" sound
  std::string modFtpUrl;     // byte 0x50

  std::array<DeviceState, NUM_DEVICES> devices;
  std::vector<Pattern> patterns;
  std::vector<ArrangementBar> arrangement;
  SongFxSettings fx;
  std::vector<AutomationEvent> automation;
};

// ─────────────────────────────────────────────────────────────────
// Default drum kits (DrumKit.h names the bytes)
// ─────────────────────────────────────────────────────────────────

/**
 * The kit a drum machine uses when nothing supplied one: every level at 100,
 * every tune / tone / decay knob centred, the 808's tom selectors on TOM,
 * RS/CL on RIM SHOT and CP/MA on CLAP (the majority setting across the
 * archive corpus, so the "unswitched" one), and the 909 flam at 64.
 */
inline const std::array<uint8_t, DRUM_KIT_BYTES>& defaultDrumKit(DeviceId device) {
  // Index = TRAK controller id - 3 (header byte offset - 3).
  static constexpr std::array<uint8_t, DRUM_KIT_BYTES> k808 = {
      100, 64, 64,       // BD level, tone, decay
      100, 64, 64,       // SD level, tone, snappy
      100, 64, 0,        // LT level, tuning, selector (0 = tom)
      100, 64, 0,        // MT
      100, 64, 0,        // HT
      100, 1,            // RS level, selector (1 = rim shot)
      100, 1,            // CP level, selector (1 = hand clap)
      100,               // CB level
      100, 64, 64,       // CY level, tone, decay
      100, 64,           // OH level, decay
      100,               // CH level
      0,                 // 0x1d — not decoded
      0,                 // (no 28th byte on the 808)
  };
  static constexpr std::array<uint8_t, DRUM_KIT_BYTES> k909 = {
      100, 64, 64, 64,   // BD level, tune, attack, decay
      100, 64, 64, 64,   // SD level, tune, tone, snappy
      100, 64, 64,       // LT level, tune, decay
      100, 64, 64,       // MT
      100, 64, 64,       // HT
      100,               // RS level
      100,               // CP level
      100, 64, 64,       // HH level, CH decay, OH decay
      100, 64,           // crash level, tune
      100, 64,           // ride level, tune
      64,                // flam interval
      100,               // 0x1e — not decoded
  };
  return device == DeviceId::TR909 ? k909 : k808;
}

/** `state.kit`, or the machine's default kit when the song supplied none. */
inline std::array<uint8_t, DRUM_KIT_BYTES> effectiveDrumKit(const DeviceState& state,
                                                            DeviceId device) {
  return state.hasKit ? state.kit : defaultDrumKit(device);
}

// ─────────────────────────────────────────────────────────────────
// Song summary (what crosses Embind on load)
// ─────────────────────────────────────────────────────────────────

/**
 * Everything the UI needs at load time, and nothing it does not: metadata,
 * device knobs and FX, plus which pattern slots exist. Converting the whole
 * ParsedSong to JS on every load built thousands of value objects; patterns
 * and the arrangement are now fetched on demand (RbsAudioEngine::getPattern
 * / getArrangement), and `automation` never leaves C++ at all.
 */
struct SongSummary {
  std::string title;
  std::string author;
  std::string infoText;
  std::string creatorUrl;
  float bpm = 125.0f;
  RbsVersion version = RbsVersion::V2_0_1;
  uint8_t headVersion = 0;
  uint8_t globSubFormat = 0;
  bool showInfoOnOpen = false;
  uint8_t playMode = 1;      // PlayMode as a number: 0 = pattern, 1 = song
  bool loopEnabled = false;
  uint32_t loopStartBar = 0; // whole bars (GLOB locator / 768), 0-based
  uint32_t loopEndBar = 0;
  uint8_t shuffle = 0;
  bool vintage = false;
  std::array<DeviceState, NUM_DEVICES> devices;
  SongFxSettings fx;
  /** Bars in the arrangement (0 when the song has none). */
  uint32_t barCount = 0;
  /** Per device: bit `bank * 8 + index` is set when the song stores that slot. */
  std::array<uint32_t, NUM_DEVICES> patternSlots{};
};

inline SongSummary summariseSong(const ParsedSong& song) {
  SongSummary out;
  out.title = song.title;
  out.author = song.author;
  out.infoText = song.infoText;
  out.creatorUrl = song.creatorUrl;
  out.bpm = song.bpm;
  out.version = song.version;
  out.headVersion = song.headVersion;
  out.globSubFormat = song.globSubFormat;
  out.showInfoOnOpen = song.showInfoOnOpen;
  out.playMode = static_cast<uint8_t>(song.playMode);
  out.loopEnabled = song.loopEnabled;
  out.loopStartBar = song.loopStart / GLOB_TICKS_PER_BAR;
  out.loopEndBar = song.loopEnd / GLOB_TICKS_PER_BAR;
  out.shuffle = song.shuffle;
  out.vintage = song.vintage;
  out.devices = song.devices;
  // The UI always sees a real kit, never the "unset" placeholder.
  for (int d = 0; d < NUM_DEVICES; ++d) {
    out.devices[d].kit = effectiveDrumKit(song.devices[d], static_cast<DeviceId>(d));
    out.devices[d].hasKit = true;
  }
  out.fx = song.fx;
  out.barCount = static_cast<uint32_t>(song.arrangement.size());
  for (const Pattern& p : song.patterns) {
    const auto device = static_cast<size_t>(p.deviceId);
    if (device >= NUM_DEVICES || p.bank >= MAX_BANKS ||
        p.patternIndex >= MAX_PATTERNS_PER_BANK) {
      continue;
    }
    out.patternSlots[device] |= 1u << (p.bank * MAX_PATTERNS_PER_BANK + p.patternIndex);
  }
  return out;
}

} // namespace rb338
