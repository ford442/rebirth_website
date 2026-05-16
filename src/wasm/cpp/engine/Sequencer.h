#pragma once

#include "../parser/RbsTypes.h"
#include <vector>

namespace rb338 {

/**
 * Sequencer — converts pattern + arrangement data into timestamped events.
 *
 * Runs inside the audio callback (processBlock). Must be lock-free.
 */
class Sequencer {
public:
  struct Event {
    uint32_t sampleOffset;   // samples from start of current block
    DeviceId device;
    uint8_t stepIndex;       // 0–15
    StepData step;
  };

  Sequencer() = default;

  /** Load song data. Called from main thread only. */
  void load(const ParsedSong& song);

  /** Generate events for the next `numFrames` samples at `sampleRate`. */
  void generateEvents(float bpm, float sampleRate, uint32_t numFrames, std::vector<Event>& out);

  /** Query current position. */
  void getPosition(uint16_t& bar, uint8_t& step) const;

  /** Set position (seek). */
  void setPosition(uint16_t bar, uint8_t step);

private:
  // TODO: internal tick counter, pattern lookup table, etc.
  uint64_t m_sampleCounter = 0;
  uint16_t m_currentBar = 1;
  uint8_t  m_currentStep = 0;
};

} // namespace rb338
