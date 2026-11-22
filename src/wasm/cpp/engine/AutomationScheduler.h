#pragma once

#include "../parser/RbsTypes.h"
#include "EngineCommands.h"
#include <cstddef>
#include <cstdint>

namespace rb338 {

class Mixer;
class Voice;

/**
 * AutomationScheduler — projects TRAK automation onto sample-accurate events.
 * Runs on the audio thread alongside the step sequencer.
 *
 * `song->automation` must be sorted by tick (RbsParser guarantees it): the
 * scheduler walks it with one forward cursor. An event whose time has
 * already passed when the cursor reaches it — floating-point slack at a
 * block edge, or the first events after the arrangement loops — fires at the
 * start of the block rather than being dropped.
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

  /**
   * Apply one event. `overrides` marks the knobs the user has taken over
   * this session; automation of those knobs is skipped.
   *
   * Controller ids are the byte offsets of the field in the track's DEVL
   * chunk (RbsFormat.md §7): device tracks follow the device header (so an
   * 808/909 controller ≥ 0x03 is a kit byte), FX tracks their FX chunk, and
   * the mixer track MIXR's first bytes then six controllers per channel.
   */
  static void applyEvent(Voice* const* voices, Mixer* mixer, const Event& ev,
                         const ParamMask& overrides = ParamMask{});

  /**
   * Apply, in order, every event strictly before `bar`/`step` — the knob and
   * mixer state playback would have reached there. Used on seek/stop so a
   * jump lands on the song's automated values instead of whatever the last
   * position left behind. Main thread (or offline) only: it walks the whole
   * automation list.
   */
  static void foldUntil(const ParsedSong& song, uint16_t bar, uint8_t step,
                        Voice* const* voices, Mixer* mixer,
                        const ParamMask& overrides = ParamMask{});

  void setPosition(const ParsedSong* song, uint16_t bar, uint8_t step);

  /** Index of the next event the cursor will consider (tests). */
  size_t cursor() const { return m_nextEventIndex; }

  /** Ticks in one 16-step bar (two per sixteenth). */
  static constexpr uint32_t TICKS_PER_BAR = 32;

private:
  static uint32_t tickFromPosition(const ParsedSong* song, uint16_t bar, uint8_t step);
  static double samplesPerTick(float bpm, float sampleRate);
  size_t m_nextEventIndex = 0;
  /** Loop-relative tick of the previous block's start, to detect a wrap. */
  uint32_t m_lastStartTick = 0;
};

} // namespace rb338
