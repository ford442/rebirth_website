#pragma once

// Semantic equality of two parsed songs — the grade a `.rbs` write gets:
// parse → write → parse must give the same song, field for field, though not
// the same bytes (RbsFormat.md §10 lists what a save drops). Shared by the
// corpus conformance sweep (tools/rbs-conformance.cpp) and the round-trip
// fuzz target; tests/test_writer.cpp spells the same rules out as doctest
// checks.

#include "RbsTypes.h"

#include <cmath>
#include <string>

namespace rb338 {

namespace detail {

/** Knobs round-trip through a 0–127 byte, so allow half a step either way. */
inline bool sameKnob(float a, float b) { return std::fabs(a - b) <= 0.5f / 127.0f + 1e-6f; }

inline bool sameStep(const StepData& a, const StepData& b) {
  return a.active == b.active && a.note == b.note && a.drumExtra == b.drumExtra &&
         a.accent == b.accent && a.slide == b.slide;
}

inline const Pattern* findSlot(const ParsedSong& song, const Pattern& like) {
  for (const Pattern& p : song.patterns) {
    if (p.deviceId == like.deviceId && p.bank == like.bank &&
        p.patternIndex == like.patternIndex) {
      return &p;
    }
  }
  return nullptr;
}

/**
 * Text fields compare without surrounding whitespace: the v2 GLOB reader
 * trims its fixed-width fields and the v1 MIDI container does not, so a v1
 * song saved as v2 loses padding that was never part of the title.
 */
inline std::string trimmedText(const std::string& text) {
  const size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return std::string();
  return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

inline bool emptyPattern(const Pattern& p) {
  for (const StepData& s : p.steps) {
    if (s.active) return false;
  }
  return true;
}

} // namespace detail

/**
 * The first semantic difference between `a` (the original) and `b` (the
 * reparsed save), or an empty string when they are the same song. A slot
 * only `b` has must be empty: a writer may emit a device chunk the original
 * lacked (a v1 song with no 909), but not invent notes.
 */
inline std::string semanticDifference(const ParsedSong& a, const ParsedSong& b) {
  using namespace detail;
  if (trimmedText(a.title) != trimmedText(b.title)) return "title";
  if (trimmedText(a.author) != trimmedText(b.author)) return "author";
  if (trimmedText(a.infoText) != trimmedText(b.infoText)) return "infoText";
  if (trimmedText(a.creatorUrl) != trimmedText(b.creatorUrl)) return "creatorUrl";
  if (a.showInfoOnOpen != b.showInfoOnOpen) return "showInfoOnOpen";
  if (std::fabs(a.bpm - b.bpm) > 0.001f) return "bpm";

  for (int d = 0; d < NUM_DEVICES; ++d) {
    const DeviceState& x = a.devices[d];
    const DeviceState& y = b.devices[d];
    const std::string at = "devices[" + std::to_string(d) + "].";
    if (x.muted != y.muted) return at + "muted";
    if (x.waveform != y.waveform) return at + "waveform";
    if (x.initialPatternBank != y.initialPatternBank) return at + "initialPatternBank";
    if (x.initialPatternIndex != y.initialPatternIndex) return at + "initialPatternIndex";
    if (!sameKnob(x.tune, y.tune)) return at + "tune";
    if (!sameKnob(x.cutoff, y.cutoff)) return at + "cutoff";
    if (!sameKnob(x.resonance, y.resonance)) return at + "resonance";
    if (!sameKnob(x.envMod, y.envMod)) return at + "envMod";
    if (!sameKnob(x.decay, y.decay)) return at + "decay";
    if (!sameKnob(x.accent, y.accent)) return at + "accent";
    if (!sameKnob(x.level, y.level)) return at + "level";
    if (!sameKnob(x.pan, y.pan)) return at + "pan";
    if (!sameKnob(x.delaySend, y.delaySend)) return at + "delaySend";
    if (x.dist != y.dist || x.pcf != y.pcf || x.compressor != y.compressor) {
      return at + "fx sends";
    }
  }

  for (const Pattern& x : a.patterns) {
    const std::string at = "pattern " + std::to_string(static_cast<int>(x.deviceId)) + ":" +
                           std::to_string(x.bank) + ":" + std::to_string(x.patternIndex);
    const Pattern* y = findSlot(b, x);
    if (!y) {
      if (emptyPattern(x)) continue;
      return at + " missing";
    }
    if (x.length != y->length) return at + " length";
    for (int s = 0; s < MAX_STEPS; ++s) {
      if (!sameStep(x.steps[s], y->steps[s])) return at + " step " + std::to_string(s);
    }
  }
  for (const Pattern& y : b.patterns) {
    if (!findSlot(a, y) && !emptyPattern(y)) return "pattern invented by the save";
  }

  const SongFxSettings& fx = a.fx;
  const SongFxSettings& gx = b.fx;
  if (fx.masterLevel != gx.masterLevel) return "fx.masterLevel";
  if (fx.delay.enabled != gx.delay.enabled || fx.delay.time != gx.delay.time ||
      fx.delay.feedback != gx.delay.feedback || fx.delay.wet != gx.delay.wet) {
    return "fx.delay";
  }
  if (fx.pcf.enabled != gx.pcf.enabled || fx.pcf.cutoff != gx.pcf.cutoff ||
      fx.pcf.resonance != gx.pcf.resonance || fx.pcf.envAmount != gx.pcf.envAmount) {
    return "fx.pcf";
  }
  if (fx.dist.enabled != gx.dist.enabled || fx.dist.drive != gx.dist.drive ||
      fx.dist.mix != gx.dist.mix) {
    return "fx.dist";
  }
  if (fx.comp.enabled != gx.comp.enabled || fx.comp.ratio != gx.comp.ratio ||
      fx.comp.threshold != gx.comp.threshold) {
    return "fx.comp";
  }

  if (a.arrangement.size() != b.arrangement.size()) return "arrangement length";
  for (size_t bar = 0; bar < a.arrangement.size(); ++bar) {
    for (int d = 0; d < NUM_DEVICES; ++d) {
      const PatternRef& x = a.arrangement[bar].devicePatterns[d];
      const PatternRef& y = b.arrangement[bar].devicePatterns[d];
      if (x.bank != y.bank || x.index != y.index) {
        return "arrangement bar " + std::to_string(bar + 1);
      }
    }
  }

  if (a.automation.size() != b.automation.size()) return "automation count";
  for (size_t i = 0; i < a.automation.size(); ++i) {
    const AutomationEvent& x = a.automation[i];
    const AutomationEvent& y = b.automation[i];
    if (x.trackIndex != y.trackIndex || x.tickPosition != y.tickPosition ||
        x.controller != y.controller || x.value != y.value) {
      return "automation event " + std::to_string(i);
    }
  }
  return std::string();
}

} // namespace rb338
