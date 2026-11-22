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
 *   - Events inside a step (a shuffled trigger, a TB-303 gate release) are
 *     held as "fire when the step phase reaches X" and land on the first
 *     whole sample at or after X, whatever the block size.
 *
 * Transport (RbsFormat.md §4)
 * ---------------------------
 *   - Song mode plays the arrangement and **stops** after its last bar, or
 *     jumps from the loop end back to the loop start when the song's loop
 *     is on (and reachable: its start lies inside the arrangement). Past
 *     the arrangement's end, a loop that ends later replays the last bar.
 *   - Pattern mode (or a song with no arrangement) ignores the arrangement
 *     and plays each device's selected pattern; queuePattern() changes it at
 *     the next bar boundary, as on the hardware.
 *
 * Groove
 * ------
 *   - Shuffle delays every even sixteenth (the off-beat ones) by
 *     `shuffle / 127 × MAX_SHUFFLE_STEPS` of a step, on devices whose current
 *     pattern has its shuffle flag set. At 127 that is a third of a step:
 *     the off-beat sixteenth lands on the triplet grid.
 *   - A TB-303 gate closes GATE_FRACTION of a step after its trigger unless
 *     the step slides into the next (the slide holds the gate open).
 */
class Sequencer {
public:
  /** Master steps in one 4/4 bar (16th-note resolution). */
  static constexpr uint8_t STEPS_PER_BAR = 16;
  /** Off-beat delay at shuffle 127, as a fraction of a step (triplet feel). */
  static constexpr double MAX_SHUFFLE_STEPS = 1.0 / 3.0;
  /** TB-303 gate length as a fraction of a step. */
  static constexpr double GATE_FRACTION = 0.5;

  enum class EventKind : uint8_t {
    Trigger, // play `step`
    Release, // close the TB-303 gate
  };

  struct Event {
    uint32_t sampleOffset;   // samples from start of current block
    DeviceId device;
    uint8_t stepIndex;       // device-local step (0 .. length-1)
    EventKind kind = EventKind::Trigger;
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

  /** Fractional samples elapsed inside the current master step. */
  double getStepPhase() const { return m_stepPhase; }

  /** Set position (seek). Resets the intra-step sample accumulator. */
  void setPosition(uint16_t bar, uint8_t step);

  /**
   * Re-resolve patterns (and their cached lengths) at the next block without
   * moving the transport. A PatchLength edit needs this; a step edit does
   * not, since the sequencer reads steps through the pattern pointer.
   */
  void invalidatePatterns() { m_resolvedBar = 0; }

  /**
   * Pattern mode: make flat slot `slot` (bank × 8 + index) the device's
   * pattern from the next bar boundary on.
   */
  void queuePattern(int device, uint8_t slot);

  /** True once song mode has played past the end of the song. */
  bool ended() const { return m_ended; }

  /** True when `song` plays its arrangement (song mode with one to play). */
  static bool playsArrangement(const ParsedSong& song);

  /**
   * The song's loop as 0-based bars [start, end) when it is on and
   * reachable, else false. Exposed for songLengthBars() and the tests.
   */
  static bool loopRegion(const ParsedSong& song, uint32_t& startBar, uint32_t& endBar);

  /**
   * Bars one pass of the song lasts: the arrangement, cut at the loop end
   * when a loop would jump back first. 0 for pattern mode (it never ends).
   */
  static uint32_t songLengthBars(const ParsedSong& song);

private:
  struct Pending {
    bool armed = false;
    double at = 0.0;          // step phase (samples) at which it fires
    EventKind kind = EventKind::Trigger;
    uint8_t stepIndex = 0;
    StepData step{};
  };

  const Pattern* findPattern(const ParsedSong& song, DeviceId device,
                             uint8_t bank, uint8_t index) const;
  void adoptSong(const ParsedSong& song);
  void resolvePatternsForBar(const ParsedSong& song, uint16_t bar);
  StepData deviceStepData(int deviceIdx, uint8_t masterStep) const;
  void advanceStep(const ParsedSong& song);
  void emitStep(const ParsedSong& song, double samplesPerStep, uint32_t frameOffset,
                Event* out, uint32_t maxEvents, uint32_t& count);
  void firePending(bool all, uint32_t frameOffset, Event* out, uint32_t maxEvents,
                   uint32_t& count);
  static void push(Event* out, uint32_t maxEvents, uint32_t& count, uint32_t offset,
                   int device, const Pending& pending);

  // Snapshot currently resolved against + the bar that resolution is valid for.
  const ParsedSong* m_song = nullptr;
  uint16_t m_resolvedBar = 0;   // 0 = nothing resolved yet

  // Current pattern for each device (borrowed from the active snapshot).
  std::array<const Pattern*, NUM_DEVICES> m_patterns{};
  std::array<uint8_t, NUM_DEVICES> m_lengths{};

  // Pattern-mode selection: flat slot per device, and one queued for the
  // next bar boundary (-1 = none).
  std::array<int16_t, NUM_DEVICES> m_selected{};
  std::array<int16_t, NUM_DEVICES> m_queued{-1, -1, -1, -1};

  // In-step events, one trigger and one release per device at most.
  std::array<Pending, NUM_DEVICES> m_pendingTrigger{};
  std::array<Pending, NUM_DEVICES> m_pendingRelease{};

  // Transport position.
  uint16_t m_currentBar = 1;    // 1-based
  uint8_t  m_currentStep = 0;   // 0 .. STEPS_PER_BAR-1 (master step)
  bool m_ended = false;

  // Fractional samples elapsed inside the current master step.
  double m_stepPhase = 0.0;
  // Whether the notes for the current step still need to be emitted.
  bool m_stepPending = true;
};

} // namespace rb338
