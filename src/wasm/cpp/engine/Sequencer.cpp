#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

void Sequencer::reset() {
  m_song = nullptr;
  m_resolvedBar = 0;
  m_patterns.fill(nullptr);
  m_lengths.fill(STEPS_PER_BAR);
  m_currentBar = 1;
  m_currentStep = 0;
  m_stepPhase = 0.0;
  m_stepPending = true;
}

const Pattern* Sequencer::findPattern(const ParsedSong& song, DeviceId device,
                                      uint8_t bank, uint8_t index) const {
  for (const auto& p : song.patterns) {
    if (p.deviceId == device && p.bank == bank && p.patternIndex == index) {
      return &p;
    }
  }
  return nullptr;
}

void Sequencer::resolvePatternsForBar(const ParsedSong& song, uint16_t bar) {
  const bool hasArrangement = !song.arrangement.empty();

  for (int i = 0; i < NUM_DEVICES; ++i) {
    const DeviceId dev = static_cast<DeviceId>(i);
    const Pattern* chosen = nullptr;

    // 1. Arrangement-driven selection. When the requested bar runs past the
    //    end of the arrangement, loop the arrangement so previews keep playing.
    if (hasArrangement) {
      const size_t barIdx = static_cast<size_t>(bar - 1) % song.arrangement.size();
      const PatternRef& ref = song.arrangement[barIdx].devicePatterns[i];
      chosen = findPattern(song, dev, ref.bank, ref.index);
    }

    // 2. Fall back to the device's initial pattern.
    if (!chosen) {
      const DeviceState& state = song.devices[i];
      chosen = findPattern(song, dev, state.initialPatternBank, state.initialPatternIndex);
    }

    // 3. Fall back to the first pattern that has any active step.
    if (!chosen) {
      for (const auto& p : song.patterns) {
        if (p.deviceId != dev) continue;
        for (const auto& s : p.steps) {
          if (s.active) { chosen = &p; break; }
        }
        if (chosen) break;
      }
    }

    m_patterns[i] = chosen;
    uint8_t len = chosen ? chosen->length : STEPS_PER_BAR;
    m_lengths[i] = static_cast<uint8_t>(std::clamp<int>(len, 1, STEPS_PER_BAR));
  }

  m_resolvedBar = bar;
}

StepData Sequencer::deviceStepData(int deviceIdx, uint8_t masterStep) const {
  const Pattern* p = m_patterns[deviceIdx];
  if (!p) return StepData{};
  const uint8_t len = m_lengths[deviceIdx];
  if (len == 0) return StepData{};
  const uint8_t local = static_cast<uint8_t>(masterStep % len);
  return p->steps[local];
}

void Sequencer::advanceStep() {
  m_currentStep++;
  if (m_currentStep >= STEPS_PER_BAR) {
    m_currentStep = 0;
    m_currentBar++;
  }
  m_stepPending = true;
}

uint32_t Sequencer::generateEvents(const ParsedSong* song, float bpm, float sampleRate,
                                   uint32_t numFrames, Event* out, uint32_t maxEvents) {
  uint32_t eventCount = 0;

  if (!song || bpm <= 0.0f || sampleRate <= 0.0f || numFrames == 0 || !out || maxEvents == 0) {
    return eventCount;
  }

  // Re-resolve the per-device patterns when the snapshot changed (song swapped
  // mid-play) or when the transport moved to a different bar.
  if (song != m_song || m_resolvedBar != m_currentBar) {
    m_song = song;
    resolvePatternsForBar(*song, m_currentBar);
  }

  // One master step = one 16th note = 1/4 of a 4/4 beat.
  const double samplesPerStep = (60.0 / static_cast<double>(bpm)) * 0.25 * static_cast<double>(sampleRate);
  if (samplesPerStep <= 0.0) {
    return eventCount;
  }

  uint32_t framesRemaining = numFrames;
  uint32_t frameOffset = 0;

  while (framesRemaining > 0) {
    // Emit the current step's notes once, at the sample where we entered it.
    if (m_stepPending) {
      for (int i = 0; i < NUM_DEVICES && eventCount < maxEvents; ++i) {
        const StepData step = deviceStepData(i, m_currentStep);
        if (step.active) {
          Event& ev = out[eventCount++];
          ev.sampleOffset = frameOffset;
          ev.device = static_cast<DeviceId>(i);
          ev.stepIndex = static_cast<uint8_t>(m_currentStep % std::max<uint8_t>(1, m_lengths[i]));
          ev.step = step;
        }
      }
      m_stepPending = false;
    }

    // Samples left before the current step ends.
    const double samplesToNext = samplesPerStep - m_stepPhase;

    if (samplesToNext <= 0.0) {
      // Boundary reached: carry the fractional remainder so long runs stay
      // sample-accurate (no per-block drift), then step forward.
      m_stepPhase -= samplesPerStep;
      if (m_stepPhase < 0.0) m_stepPhase = 0.0;
      advanceStep();

      // Bar changed → re-resolve arrangement patterns for the new bar.
      if (m_currentStep == 0 && m_resolvedBar != m_currentBar) {
        resolvePatternsForBar(*song, m_currentBar);
      }
      continue;
    }

    // Consume frames up to (but not past) the next boundary.
    const uint32_t framesToBoundary = static_cast<uint32_t>(std::ceil(samplesToNext));
    const uint32_t advance = std::min<uint32_t>(framesToBoundary, framesRemaining);
    m_stepPhase += static_cast<double>(advance);
    frameOffset += advance;
    framesRemaining -= advance;
  }

  return eventCount;
}

void Sequencer::getPosition(uint16_t& bar, uint8_t& step) const {
  bar = m_currentBar;
  step = m_currentStep;
}

void Sequencer::setPosition(uint16_t bar, uint8_t step) {
  m_currentBar = std::max<uint16_t>(1, bar);
  m_currentStep = static_cast<uint8_t>(step % STEPS_PER_BAR);
  m_stepPhase = 0.0;
  m_stepPending = true;
  // Force pattern re-resolution on the next generateEvents call.
  m_resolvedBar = 0;
}

} // namespace rb338
