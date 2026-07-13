#pragma once

#include "../parser/RbsTypes.h"
#include <array>
#include <vector>

namespace rb338 {

/**
 * Sequencer — converts pattern data into timestamped step events.
 *
 * Runs inside the audio callback (processBlock). Must be lock-free and
 * non-blocking. It owns a copy of the current pattern for each device so the
 * audio thread does not need to look up patterns in the shared ParsedSong
 * during the callback.
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

  /** Load song data and pick the first active pattern for each device. */
  void load(const ParsedSong& song);

  /**
   * Generate events for the next `numFrames` samples.
   *
   * @param bpm        Song tempo.
   * @param sampleRate Host sample rate.
   * @param numFrames  Number of frames to generate events for.
   * @param out        Caller-provided fixed-size buffer.
   * @param maxEvents  Capacity of `out`.
   * @return           Number of events written (capped at maxEvents).
   */
  uint32_t generateEvents(float bpm, float sampleRate, uint32_t numFrames,
                          Event* out, uint32_t maxEvents);

  /** Query current position. */
  void getPosition(uint16_t& bar, uint8_t& step) const;

  /** Set position (seek). Resets the intra-step sample counter. */
  void setPosition(uint16_t bar, uint8_t step);

private:
  StepData getStepData(DeviceId device, uint8_t stepIndex) const;

  // Current pattern for each device (borrowed from ParsedSong via pointer).
  // nullptr means no active pattern was found for that device.
  std::array<const Pattern*, NUM_DEVICES> m_currentPatterns{};

  // Current pattern length per device (cached to avoid pointer chase).
  std::array<uint8_t, NUM_DEVICES> m_patternLengths{};

  uint64_t m_sampleCounter = 0;
  uint16_t m_currentBar = 1;
  uint8_t  m_currentStep = 0;
  float    m_samplesPerStep = 0.0f;
};

} // namespace rb338
