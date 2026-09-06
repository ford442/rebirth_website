#pragma once

#include "../parser/RbsTypes.h"
#include <cstdint>

namespace rb338 {

class Mixer;
class Voice;

/**
 * AutomationScheduler — projects TRAK automation onto sample-accurate events.
 * Runs on the audio thread alongside the step sequencer.
 */
class AutomationScheduler {
public:
  struct Event {
    uint32_t sampleOffset = 0;
    uint8_t trackIndex = 0;
    uint8_t controller = 0;
    uint8_t value = 0;
  };

  void reset();

  uint32_t generateEvents(const ParsedSong* song, float bpm, float sampleRate,
                          uint16_t bar, uint8_t step, double stepPhase,
                          uint32_t numFrames, Event* out, uint32_t maxEvents);

  void applyEvent(Voice* const* voices, Mixer* mixer, const Event& ev) const;

  void setPosition(const ParsedSong* song, uint16_t bar, uint8_t step);

private:
  static uint32_t tickFromPosition(uint16_t bar, uint8_t step);
  static double samplesPerTick(float bpm, float sampleRate);
  size_t m_nextEventIndex = 0;
};

} // namespace rb338
