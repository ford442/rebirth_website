#pragma once

// GoldenCases — the reference songs behind src/wasm/test-fixtures/golden/.
//
// Each case is a tiny `.rbs` built here and written next to its reference
// render by `rbs-golden songs` / `rbs-golden render`. The `.rbs` is the
// contract: anyone with original ReBirth 2.0.1 can load it, record the
// output and drop the recording in as `<id>.wav` with `source` changed to
// "rebirth-2.0.1" (GOLDEN.md has the steps). The test reads the committed
// `.rbs`, renders it and compares against the `.wav` — see
// tests/test_golden.cpp and tests/GoldenCompare.h.

#include "../parser/DrumKit.h"
#include "../parser/RbsTypes.h"
#include "../synth/DrumBitfield.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rb338::golden {

/** Where a reference render came from — decides how close a fresh render must be. */
enum class Source : uint8_t {
  /** Rendered by this engine: a regression net, so the tolerances are tight. */
  Engine,
  /** Recorded from original ReBirth 2.0.1: the calibration target. */
  Rebirth201,
};

struct Case {
  const char* id;
  const char* description;
  float seconds;
  Source source;
  /** Largest mean banded log-magnitude distance, dB. */
  double maxSpectralDb;
  /** Smallest RMS-envelope correlation. */
  double minEnvelopeCorrelation;
};

/**
 * Engine references allow rounding differences between compilers and the
 * sanitizer builds, nothing more. A ReBirth recording will need per-case
 * tolerances once one exists — set them here when it lands.
 */
constexpr double kEngineSpectralDb = 1.0;
constexpr double kEngineEnvelope = 0.99;

inline const std::vector<Case>& cases() {
  static const std::vector<Case> all = {
      {"303-accent-run", "TB-303 A: accents in runs of four, high resonance", 1.0f, Source::Engine,
       kEngineSpectralDb, kEngineEnvelope},
      {"303-slide-run", "TB-303 A: a slid melody with a held tie", 1.0f, Source::Engine,
       kEngineSpectralDb, kEngineEnvelope},
      {"909-flam", "TR-909: snare and rim flams against plain hits", 1.0f, Source::Engine,
       kEngineSpectralDb, kEngineEnvelope},
      {"909-shuffle-000", "TR-909 closed hats on every sixteenth, shuffle 0", 0.5f,
       Source::Engine, kEngineSpectralDb, kEngineEnvelope},
      {"909-shuffle-064", "the same hats at shuffle 64", 0.5f, Source::Engine,
       kEngineSpectralDb, kEngineEnvelope},
      {"909-shuffle-127", "the same hats at shuffle 127", 0.5f, Source::Engine,
       kEngineSpectralDb, kEngineEnvelope},
      {"303-distortion-sweep", "TB-303 A through DIST, drive automated 0 → 127", 1.0f,
       Source::Engine, kEngineSpectralDb, kEngineEnvelope},
  };
  return all;
}

namespace detail {

/** A dry song at 125 BPM: every FX off, every device at full level. */
inline ParsedSong baseSong(const std::string& title) {
  ParsedSong song;
  song.title = title;
  song.author = "rbs-golden";
  song.infoText = "Golden reference song for the ReBirth web engine's comparison tests.";
  song.bpm = 125.0f;
  song.playMode = PlayMode::Pattern; // one pattern per device, looping
  for (int i = 0; i < NUM_DEVICES; ++i) {
    DeviceState& dev = song.devices[i];
    dev.id = static_cast<DeviceId>(i);
    dev.level = 1.0f;
    dev.pan = 0.5f;
    dev.delaySend = 0.0f;
  }
  song.fx.masterLevel = 100;
  song.fx.delay.enabled = false;
  song.fx.dist.enabled = false;
  song.fx.pcf.enabled = false;
  song.fx.comp.enabled = false;
  return song;
}

inline Pattern emptyPattern(DeviceId device) {
  Pattern p;
  p.deviceId = device;
  p.length = MAX_STEPS;
  return p;
}

inline StepData acid(uint8_t note, bool accent = false, bool slide = false) {
  StepData s;
  s.active = true;
  s.note = note;
  s.accent = accent;
  s.slide = slide;
  return s;
}

inline ParsedSong accentRun() {
  ParsedSong song = baseSong("303 Accent Run");
  DeviceState& acidDev = song.devices[0];
  acidDev.cutoff = 0.35f;
  acidDev.resonance = 0.85f;
  acidDev.envMod = 0.6f;
  acidDev.decay = 0.4f;
  acidDev.accent = 1.0f;
  Pattern p = emptyPattern(DeviceId::TB303_A);
  for (int s = 0; s < MAX_STEPS; ++s) {
    p.steps[s] = acid(static_cast<uint8_t>(36 + (s % 2) * 12), (s / 4) % 2 == 0);
  }
  song.patterns.push_back(p);
  return song;
}

inline ParsedSong slideRun() {
  ParsedSong song = baseSong("303 Slide Run");
  DeviceState& acidDev = song.devices[0];
  acidDev.cutoff = 0.45f;
  acidDev.resonance = 0.6f;
  acidDev.envMod = 0.5f;
  acidDev.waveform = 1;
  Pattern p = emptyPattern(DeviceId::TB303_A);
  const std::array<uint8_t, 8> melody = {36, 48, 43, 39, 36, 41, 46, 48};
  for (int s = 0; s < 8; ++s) {
    // Slides on steps 1, 2 and 5; step 6 → 7 is a tie at the same pitch.
    const bool slide = s == 1 || s == 2 || s == 5 || s == 6;
    p.steps[s] = acid(melody[s], s == 4, slide);
  }
  p.steps[7] = acid(46);
  song.patterns.push_back(p);
  return song;
}

inline ParsedSong flams() {
  ParsedSong song = baseSong("909 Flam");
  DeviceState& tr909 = song.devices[3];
  tr909.kit = defaultDrumKit(DeviceId::TR909);
  tr909.kit[kitIndex(Kit909::FlamInterval)] = 70;
  tr909.hasKit = true;
  Pattern p = emptyPattern(DeviceId::TR909);
  constexpr uint16_t kSD = 1u << 1;
  constexpr uint16_t kRS = 1u << 10;
  for (int s : {0, 2, 4, 6}) {
    StepData& step = p.steps[s];
    step.active = true;
    step.note = DrumHit::SD;
    step.drumFlam = (s % 4 == 2) ? kSD : 0; // flam every other snare
  }
  for (int s : {1, 5, 7}) {
    StepData& step = p.steps[s];
    step.active = true;
    step.drumExtra = DrumExtra::RS;
    step.drumFlam = s == 5 ? kRS : 0;
  }
  song.patterns.push_back(p);
  return song;
}

inline ParsedSong shuffledHats(uint8_t amount) {
  ParsedSong song = baseSong("909 Shuffle " + std::to_string(amount));
  song.shuffle = amount;
  Pattern p = emptyPattern(DeviceId::TR909);
  p.shuffle = true;
  for (StepData& step : p.steps) {
    step.active = true;
    step.note = DrumHit::CH;
  }
  p.steps[0].note |= DrumHit::BD;
  p.steps[8].note |= DrumHit::BD;
  song.patterns.push_back(p);
  return song;
}

inline ParsedSong distortionSweep() {
  ParsedSong song = baseSong("303 Distortion Sweep");
  song.playMode = PlayMode::Song; // automation plays in song mode only
  DeviceState& acidDev = song.devices[0];
  acidDev.cutoff = 0.7f;
  acidDev.resonance = 0.3f;
  acidDev.dist = true;
  song.fx.dist.enabled = true;
  song.fx.dist.drive = 0;
  song.fx.dist.mix = 127;
  Pattern p = emptyPattern(DeviceId::TB303_A);
  for (int s = 0; s < MAX_STEPS; ++s) p.steps[s] = acid(33, false, true); // one held note
  song.patterns.push_back(p);
  ArrangementBar bar;
  song.arrangement.push_back(bar);
  // DIST track (6), controller 1 = amount, one step up per TRAK tick
  // (two per sixteenth) across the bar.
  for (uint32_t tick = 0; tick < 32; ++tick) {
    song.automation.push_back(AutomationEvent{6, tick, 0x01, static_cast<uint8_t>(tick * 4)});
  }
  return song;
}

} // namespace detail

/** The song for case `id` (an empty title when the id is unknown). */
inline ParsedSong buildSong(const std::string& id) {
  if (id == "303-accent-run") return detail::accentRun();
  if (id == "303-slide-run") return detail::slideRun();
  if (id == "909-flam") return detail::flams();
  if (id == "909-shuffle-000") return detail::shuffledHats(0);
  if (id == "909-shuffle-064") return detail::shuffledHats(64);
  if (id == "909-shuffle-127") return detail::shuffledHats(127);
  if (id == "303-distortion-sweep") return detail::distortionSweep();
  return ParsedSong{};
}

} // namespace rb338::golden
