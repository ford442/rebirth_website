// RbsTrak — TRAK / STRAK event-stream decoding and arrangement projection.
//
// TRAK carries two kinds of event: controller 0x01 on tracks 1-4 selects a
// pattern slot (sampled per bar into ParsedSong::arrangement), and everything
// else is automation, stored verbatim in ParsedSong::automation for the
// engine's AutomationScheduler. Nothing here is dropped.
//
// Chunk framing (including the 5-byte STRAK alias) lives in RbsByteStream.h;
// chunk dispatch lives in RbsParser.cpp.

#include "RbsParser.h"
#include "RbsByteStream.h"

#include <algorithm>
#include <array>
#include <limits>

namespace rb338 {

using detail::ByteStream;
using detail::readVlq;
using detail::TRAK_TICKS_PER_BAR;

bool RbsParser::parseTrak(const uint8_t* data, size_t size, ParsedSong& song) {
  const size_t trackIndex = m_trakIndex++;
  if (size < 4) {
    m_error = "TRAK chunk too small for event count";
    return false;
  }

  ByteStream stream(data, size);
  uint32_t eventCount = 0;
  if (!stream.readU32BE(eventCount)) {
    m_error = "Truncated TRAK event count";
    return false;
  }
  // Each event needs at least one delta byte, one controller byte and one
  // value byte. This also bounds work before processing hostile input.
  if (eventCount > stream.remaining() / 3) {
    m_error = "TRAK event count exceeds chunk bounds";
    return false;
  }

  uint32_t absolutePosition = 0;
  for (uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
    uint32_t delta = 0;
    uint8_t controller = 0;
    uint8_t value = 0;
    if (!readVlq(stream, delta) || !stream.readU8(controller) ||
        !stream.readU8(value)) {
      m_error = "Truncated or invalid variable-length TRAK event";
      return false;
    }
    if (delta > std::numeric_limits<uint32_t>::max() - absolutePosition) {
      m_error = "TRAK event position overflows 32 bits";
      return false;
    }
    absolutePosition += delta;

    // Tracks 1-4 are 303-A, 303-B, 808 and 909. Controller 1 selects
    // one of their 32 pattern slots; all other events are automation.
    if (trackIndex >= 1 && trackIndex <= NUM_DEVICES && controller == 0x01) {
      if (value >= 32) {
        m_error = "TRAK selected-pattern value exceeds the 32 pattern slots";
        return false;
      }
      m_patternChanges[trackIndex - 1].emplace_back(absolutePosition, value);
    } else {
      AutomationEvent ev;
      ev.trackIndex = static_cast<uint8_t>(trackIndex);
      ev.tickPosition = absolutePosition;
      ev.controller = controller;
      ev.value = value;
      song.automation.push_back(ev);
    }
  }

  if (!stream.atEnd()) {
    m_error = "TRAK chunk contains trailing bytes after declared events";
    return false;
  }
  m_maxTrakPosition = std::max(m_maxTrakPosition, absolutePosition);
  return true;
}

bool RbsParser::buildArrangement(ParsedSong& song) {
  bool hasPatternChanges = false;
  for (const auto& changes : m_patternChanges) {
    hasPatternChanges = hasPatternChanges || !changes.empty();
  }
  if (!hasPatternChanges) return true;

  // An event exactly at the terminal boundary restores state after the final
  // bar, so the ceiling intentionally excludes an extra empty bar there.
  const uint32_t roundedBars = m_maxTrakPosition / TRAK_TICKS_PER_BAR +
    (m_maxTrakPosition % TRAK_TICKS_PER_BAR != 0 ? 1u : 0u);
  const uint32_t barCount = std::max<uint32_t>(1, roundedBars);
  if (barCount > std::numeric_limits<uint16_t>::max()) {
    m_error = "TRAK arrangement exceeds the supported bar count";
    return false;
  }

  std::array<uint8_t, NUM_DEVICES> selected{};
  std::array<size_t, NUM_DEVICES> nextChange{};
  for (int device = 0; device < NUM_DEVICES; ++device) {
    const auto& state = song.devices[device];
    selected[device] = static_cast<uint8_t>(
      state.initialPatternBank * MAX_PATTERNS_PER_BANK + state.initialPatternIndex);
  }

  song.arrangement.clear();
  song.arrangement.reserve(barCount);
  for (uint32_t bar = 0; bar < barCount; ++bar) {
    const uint32_t position = bar * TRAK_TICKS_PER_BAR;
    ArrangementBar out;
    out.barNumber = static_cast<uint16_t>(bar + 1);
    for (int device = 0; device < NUM_DEVICES; ++device) {
      const auto& changes = m_patternChanges[device];
      while (nextChange[device] < changes.size() &&
             changes[nextChange[device]].first <= position) {
        selected[device] = changes[nextChange[device]].second;
        ++nextChange[device];
      }
      out.devicePatterns[device] = PatternRef{
        static_cast<uint8_t>(selected[device] / MAX_PATTERNS_PER_BANK),
        static_cast<uint8_t>(selected[device] % MAX_PATTERNS_PER_BANK)
      };
    }
    song.arrangement.push_back(out);
  }
  return true;
}

}  // namespace rb338
