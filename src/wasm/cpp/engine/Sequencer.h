#pragma once

#include "../parser/RbsTypes.h"
#include <array>
#include <cstdint>

namespace rb338 {

/**
 * Sequencer — converts pattern data into timestamped step events.
 *
 * Runs inside the audio callback (processBlock). Must be lock-free and
 * non-blocking. It reads from an immutable ParsedSong snapshot supplied by the
 * engine every block, so a song can be swapped in on the main thread while the
 * audio thread keeps playing without tearing.
 *
 * Timing model
 * ------------
 *   - One master step = one 16th note. A 4/4 bar therefore has 16 master steps.
 *   - `samplesPerStep = (60 / bpm) * 0.25 * sampleRate`.
 *   - Step boundaries are tracked with a fractional sample accumulator so
 *     advancement stays sample-accurate over long runs (no per-block drift).
 *
 * Per-device patterns
 * -------------------
 *   - Each device plays its own pattern for the current bar, chosen from the
 *     song arrangement (falling back to the device's initial pattern, then the
 *     first active pattern).
 *   - A pattern length of 1–16 loops within the bar: the device reads
 *     `steps[masterStep % length]`.
 */
class Sequencer {
public:
  /** Master steps in one 4/4 bar (16th-note resolution). */
  static constexpr uint8_t STEPS_PER_BAR = 16;

  struct Event {
    uint32_t sampleOffset;   // samples from start of current block
    DeviceId device;
    uint8_t stepIndex;       // device-local step (0 .. length-1)
    StepData step;
  };

  Sequencer() = default;

  /** Reset transport to bar 1, step 0 and drop cached pattern resolution. */
  void reset();

  /**
   * Generate events for the next `numFrames` samples from `song`.
   *
   * @param song       Immutable song snapshot (may be nullptr → no events).
   * @param bpm        Effective tempo in BPM (already multiplied by any scrub
   *                   multiplier).
   * @param sampleRate Host sample rate.
   * @param numFrames  Number of frames to generate events for.
   * @param out        Caller-provided fixed-size buffer.
   * @param maxEvents  Capacity of `out`.
   * @return           Number of events written (capped at maxEvents).
   */
  uint32_t generateEvents(const ParsedSong* song, float bpm, float sampleRate,
                          uint32_t numFrames, Event* out, uint32_t maxEvents);

  /** Query current position (1-based bar, 0-based master step). */
  void getPosition(uint16_t& bar, uint8_t& step) const;

  /** Set position (seek). Resets the intra-step sample accumulator. */
  void setPosition(uint16_t bar, uint8_t step);

private:
  const Pattern* findPattern(const ParsedSong& song, DeviceId device,
                             uint8_t bank, uint8_t index) const;
  void resolvePatternsForBar(const ParsedSong& song, uint16_t bar);
  StepData deviceStepData(int deviceIdx, uint8_t masterStep) const;
  void advanceStep();

  // Snapshot currently resolved against + the bar that resolution is valid for.
  const ParsedSong* m_song = nullptr;
  uint16_t m_resolvedBar = 0;   // 0 = nothing resolved yet

  // Current pattern for each device (borrowed from the active snapshot).
  std::array<const Pattern*, NUM_DEVICES> m_patterns{};
  std::array<uint8_t, NUM_DEVICES> m_lengths{};

  // Transport position.
  uint16_t m_currentBar = 1;    // 1-based
  uint8_t  m_currentStep = 0;   // 0 .. STEPS_PER_BAR-1 (master step)

  // Fractional samples elapsed inside the current master step.
  double m_stepPhase = 0.0;
  // Whether the notes for the current step still need to be emitted.
  bool m_stepPending = true;
};

} // namespace rb338
