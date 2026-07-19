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
  uint8_t drumExtra = 0; // Secondary drum hits: CP, MA, RS (see DrumBitfield.h)
  bool accent = false;
  bool slide = false;    // 303 only
};

// ─────────────────────────────────────────────────────────────────
// Pattern
// ─────────────────────────────────────────────────────────────────

struct Pattern {
  DeviceId deviceId;
  uint8_t bank = 0;          // 0-3
  uint8_t patternIndex = 0;  // 0-7
  uint8_t length = 16;       // 1-16
  std::array<StepData, MAX_STEPS> steps;
};

// ─────────────────────────────────────────────────────────────────
// Device State (knobs + mixer settings)
// ─────────────────────────────────────────────────────────────────

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
  // FX sends
  bool dist = false;
  bool pcf = false;
  bool compressor = false;
  float delaySend = 0.0f;
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
  bool showInfoOnOpen = false;

  std::array<DeviceState, NUM_DEVICES> devices;
  std::vector<Pattern> patterns;
  std::vector<ArrangementBar> arrangement;
};

} // namespace rb338
