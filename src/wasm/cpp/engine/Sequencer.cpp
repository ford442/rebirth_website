#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

bool is303(int device) {
  return device == static_cast<int>(DeviceId::TB303_A) ||
         device == static_cast<int>(DeviceId::TB303_B);
}

int16_t initialSlot(const DeviceState& state) {
  return static_cast<int16_t>(state.initialPatternBank * MAX_PATTERNS_PER_BANK +
                              state.initialPatternIndex);
}

} // anonymous namespace

void Sequencer::reset() {
  m_song = nullptr;
  m_resolvedBar = 0;
  m_patterns.fill(nullptr);
  m_lengths.fill(STEPS_PER_BAR);
  m_selected.fill(0);
  m_queued.fill(-1);
  m_pendingTrigger.fill(Pending{});
  m_pendingRelease.fill(Pending{});
  m_currentBar = 1;
  m_currentStep = 0;
  m_ended = false;
  m_stepPhase = 0.0;
  m_stepPending = true;
}

bool Sequencer::playsArrangement(const ParsedSong& song) {
  return song.playMode == PlayMode::Song && !song.arrangement.empty();
}

bool Sequencer::loopRegion(const ParsedSong& song, uint32_t& startBar, uint32_t& endBar) {
  if (!song.loopEnabled || !playsArrangement(song)) return false;
  startBar = song.loopStart / GLOB_TICKS_PER_BAR;
  endBar = song.loopEnd / GLOB_TICKS_PER_BAR;
  // A loop that starts past the song's end can never be reached from bar 1
  // without replaying the last bar for minutes; treat it as off.
  return endBar > startBar && startBar < song.arrangement.size();
}

uint32_t Sequencer::songLengthBars(const ParsedSong& song) {
  if (!playsArrangement(song)) return 0;
  uint32_t start = 0;
  uint32_t end = 0;
  const auto bars = static_cast<uint32_t>(song.arrangement.size());
  if (loopRegion(song, start, end)) return end;
  return bars;
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

void Sequencer::adoptSong(const ParsedSong& song) {
  m_song = &song;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    m_selected[i] = initialSlot(song.devices[i]);
  }
}

void Sequencer::queuePattern(int device, uint8_t slot) {
  if (device < 0 || device >= NUM_DEVICES || slot >= MAX_BANKS * MAX_PATTERNS_PER_BANK) return;
  m_queued[static_cast<size_t>(device)] = slot;
}

void Sequencer::resolvePatternsForBar(const ParsedSong& song, uint16_t bar) {
  const bool arrangement = playsArrangement(song);

  for (int i = 0; i < NUM_DEVICES; ++i) {
    const DeviceId dev = static_cast<DeviceId>(i);
    const Pattern* chosen = nullptr;

    // 1. Song mode: the arrangement's pattern for this bar. Past the end (a
    //    loop that ends later than the song) the last bar repeats.
    if (arrangement) {
      const size_t barIdx = std::min<size_t>(static_cast<size_t>(bar - 1),
                                             song.arrangement.size() - 1);
      const PatternRef& ref = song.arrangement[barIdx].devicePatterns[i];
      chosen = findPattern(song, dev, ref.bank, ref.index);
    } else {
      // Pattern mode: the device's selected pattern.
      const auto slot = static_cast<uint8_t>(std::max<int16_t>(0, m_selected[i]));
      chosen = findPattern(song, dev, static_cast<uint8_t>(slot / MAX_PATTERNS_PER_BANK),
                           static_cast<uint8_t>(slot % MAX_PATTERNS_PER_BANK));
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

void Sequencer::advanceStep(const ParsedSong& song) {
  m_currentStep++;
  m_stepPending = true;
  if (m_currentStep < STEPS_PER_BAR) return;

  m_currentStep = 0;
  // Pattern changes queued during the bar take effect on the downbeat.
  for (int i = 0; i < NUM_DEVICES; ++i) {
    if (m_queued[i] >= 0) {
      m_selected[i] = m_queued[i];
      m_queued[i] = -1;
      m_resolvedBar = 0;
    }
  }

  uint32_t next = static_cast<uint32_t>(m_currentBar); // 0-based index of the next bar
  if (playsArrangement(song)) {
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    const bool loop = loopRegion(song, loopStart, loopEnd);
    if (loop && next == loopEnd) {
      next = loopStart;
    } else if (next >= song.arrangement.size() && !(loop && next < loopEnd)) {
      // Song mode, no loop to take: the song is over.
      m_ended = true;
    }
  }
  m_currentBar = static_cast<uint16_t>(std::min<uint32_t>(next + 1, UINT16_MAX));
}

void Sequencer::push(Event* out, uint32_t maxEvents, uint32_t& count, uint32_t offset,
                     int device, const Pending& pending) {
  if (count >= maxEvents) return;
  Event& ev = out[count++];
  ev.sampleOffset = offset;
  ev.device = static_cast<DeviceId>(device);
  ev.stepIndex = pending.stepIndex;
  ev.kind = pending.kind;
  ev.step = pending.step;
}

void Sequencer::emitStep(const ParsedSong& song, double samplesPerStep, uint32_t frameOffset,
                         Event* out, uint32_t maxEvents, uint32_t& count) {
  const bool offBeat = (m_currentStep & 1u) != 0;
  const double swing = std::min<double>(song.shuffle, 127) / 127.0 * MAX_SHUFFLE_STEPS *
                       samplesPerStep;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    const StepData step = deviceStepData(i, m_currentStep);
    if (!step.active) {
      // A rest still reaches the voice: it closes a held (slid) 303 note.
      if (is303(i)) {
        Pending rest;
        rest.stepIndex = static_cast<uint8_t>(m_currentStep % std::max<uint8_t>(1, m_lengths[i]));
        rest.step = step;
        push(out, maxEvents, count, frameOffset, i, rest);
      }
      continue;
    }

    Pending trigger;
    trigger.armed = true;
    trigger.kind = EventKind::Trigger;
    trigger.stepIndex = static_cast<uint8_t>(m_currentStep % std::max<uint8_t>(1, m_lengths[i]));
    trigger.step = step;
    const Pattern* pattern = m_patterns[i];
    trigger.at = (offBeat && pattern && pattern->shuffle) ? swing : 0.0;

    if (trigger.at <= 0.0) {
      push(out, maxEvents, count, frameOffset, i, trigger);
    } else {
      m_pendingTrigger[i] = trigger;
    }

    if (is303(i) && !step.slide) {
      Pending release;
      release.armed = true;
      release.kind = EventKind::Release;
      release.stepIndex = trigger.stepIndex;
      release.at = trigger.at + GATE_FRACTION * samplesPerStep;
      m_pendingRelease[i] = release;
    }
  }
}

void Sequencer::firePending(bool all, uint32_t frameOffset, Event* out, uint32_t maxEvents,
                            uint32_t& count) {
  // Triggers before releases: a release always belongs to the trigger that
  // armed it, which fires first.
  for (auto* list : {&m_pendingTrigger, &m_pendingRelease}) {
    for (int i = 0; i < NUM_DEVICES; ++i) {
      Pending& pending = (*list)[static_cast<size_t>(i)];
      if (!pending.armed || (!all && pending.at > m_stepPhase)) continue;
      push(out, maxEvents, count, frameOffset, i, pending);
      pending.armed = false;
    }
  }
}

uint32_t Sequencer::generateEvents(const ParsedSong* song, float bpm, float sampleRate,
                                   uint32_t numFrames, Event* out, uint32_t maxEvents) {
  uint32_t eventCount = 0;

  if (!song || bpm <= 0.0f || sampleRate <= 0.0f || numFrames == 0 || !out || maxEvents == 0) {
    return eventCount;
  }

  // Re-resolve the per-device patterns when the snapshot changed (song swapped
  // mid-play) or when the transport moved to a different bar.
  if (song != m_song) {
    adoptSong(*song);
    m_resolvedBar = 0;
  }
  if (m_resolvedBar != m_currentBar) {
    resolvePatternsForBar(*song, m_currentBar);
  }
  if (m_ended) return eventCount;

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
      emitStep(*song, samplesPerStep, frameOffset, out, maxEvents, eventCount);
      m_stepPending = false;
    }

    // In-step events due by now (a shuffled trigger, a gate release).
    firePending(false, frameOffset, out, maxEvents, eventCount);

    // The next thing that happens: an armed in-step event, or the boundary.
    double target = samplesPerStep;
    for (const auto* list : {&m_pendingTrigger, &m_pendingRelease}) {
      for (const Pending& pending : *list) {
        if (pending.armed) target = std::min(target, pending.at);
      }
    }

    const double samplesToTarget = target - m_stepPhase;
    if (samplesToTarget <= 0.0) {
      // Boundary reached: anything still armed belongs to the step that is
      // ending, so it fires now. Carry the fractional remainder so long runs
      // stay sample-accurate (no per-block drift), then step forward.
      firePending(true, frameOffset, out, maxEvents, eventCount);
      m_stepPhase -= samplesPerStep;
      if (m_stepPhase < 0.0) m_stepPhase = 0.0;
      advanceStep(*song);
      if (m_ended) break;

      // Bar changed (or a queued pattern landed) → re-resolve.
      if (m_resolvedBar != m_currentBar) {
        resolvePatternsForBar(*song, m_currentBar);
      }
      continue;
    }

    // Consume frames up to (but not past) the next target.
    const uint32_t framesToTarget = static_cast<uint32_t>(std::ceil(samplesToTarget));
    const uint32_t advance = std::min<uint32_t>(framesToTarget, framesRemaining);
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
  m_ended = false;
  m_pendingTrigger.fill(Pending{});
  m_pendingRelease.fill(Pending{});
  // Force pattern re-resolution on the next generateEvents call.
  m_resolvedBar = 0;
}

} // namespace rb338
