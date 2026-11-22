#include "AutomationScheduler.h"
#include "../parser/DrumKit.h"
#include "EngineCommands.h"
#include "Mixer.h"
#include "../synth/Voice.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

int deviceIndexFromTrack(uint8_t trackIndex) {
  if (trackIndex >= 1 && trackIndex <= NUM_DEVICES) {
    return static_cast<int>(trackIndex - 1);
  }
  return -1;
}

} // anonymous namespace

void AutomationScheduler::reset() {
  m_nextEventIndex = 0;
  m_lastStartTick = 0;
}

uint32_t AutomationScheduler::tickFromPosition(const ParsedSong* song, uint16_t bar,
                                               uint8_t step) {
  // The sequencer's bar is the song position: it stops at the song's end or
  // jumps back to the loop start itself, and a jump back shows up here as a
  // tick earlier than the last block's (see generateEvents).
  (void)song;
  const uint32_t barIndex = static_cast<uint32_t>(std::max<uint16_t>(1, bar) - 1);
  return barIndex * TICKS_PER_BAR + static_cast<uint32_t>(step) * 2u;
}

double AutomationScheduler::samplesPerTick(float bpm, float sampleRate) {
  // Double precision throughout, exactly as Sequencer computes a step: a
  // float here drifted against the sequencer's clock by a fraction of a
  // sample per step, which opened sub-sample gaps between blocks that an
  // event could fall into.
  const double clampedBpm = std::clamp(static_cast<double>(bpm),
                                       static_cast<double>(MIN_BPM),
                                       static_cast<double>(MAX_BPM));
  const double samplesPerStep = (60.0 / clampedBpm) * 0.25 * static_cast<double>(sampleRate);
  return samplesPerStep * 0.5;
}

void AutomationScheduler::setPosition(const ParsedSong* song, uint16_t bar, uint8_t step) {
  m_nextEventIndex = 0;
  m_lastStartTick = 0;
  if (!song) return;
  const uint32_t tick = tickFromPosition(song, bar, step);
  m_lastStartTick = tick;
  while (m_nextEventIndex < song->automation.size() &&
         song->automation[m_nextEventIndex].tickPosition < tick) {
    ++m_nextEventIndex;
  }
}

void AutomationScheduler::foldUntil(const ParsedSong& song, uint16_t bar, uint8_t step,
                                    Voice* const* voices, Mixer* mixer,
                                    const ParamMask& overrides) {
  const uint32_t tick = tickFromPosition(&song, bar, step);
  for (const AutomationEvent& src : song.automation) {
    if (src.tickPosition >= tick) break;
    Event ev;
    ev.trackIndex = src.trackIndex;
    ev.controller = src.controller;
    ev.value = src.value;
    applyEvent(voices, mixer, ev, overrides);
  }
}

uint32_t AutomationScheduler::generateEvents(const ParsedSong* song, float bpm,
                                             float sampleRate, uint16_t bar,
                                             uint8_t step, double stepPhase,
                                             uint32_t numFrames, Event* out,
                                             uint32_t maxEvents) {
  if (!song || out == 0 || maxEvents == 0) return 0;

  const double tickSamples = samplesPerTick(bpm, sampleRate);
  if (tickSamples <= 0.0) return 0;

  const uint32_t startTick = tickFromPosition(song, bar, step);
  if (startTick < m_lastStartTick) {
    // The arrangement wrapped back to its top: start the list over. Events
    // between the loop point and this block's start fire immediately.
    m_nextEventIndex = 0;
  }
  m_lastStartTick = startTick;

  const double startSamples =
      stepPhase + static_cast<double>(startTick) * tickSamples;
  const double endSamples = startSamples + static_cast<double>(numFrames);

  uint32_t count = 0;
  while (m_nextEventIndex < song->automation.size() && count < maxEvents) {
    const AutomationEvent& src = song->automation[m_nextEventIndex];
    const double eventSample = static_cast<double>(src.tickPosition) * tickSamples;
    if (eventSample >= endSamples) break;

    Event& ev = out[count++];
    const double offset = std::round(eventSample - startSamples);
    ev.sampleOffset = offset > 0.0
        ? std::min(static_cast<uint32_t>(offset), numFrames - 1u)
        : 0u;
    ev.trackIndex = src.trackIndex;
    ev.controller = src.controller;
    ev.value = src.value;
    ++m_nextEventIndex;
  }
  return count;
}

namespace {

/** The voice param a device-track controller drives, or -1. */
int deviceParamForController(int deviceIdx, uint8_t controller) {
  const bool drum = deviceIdx >= static_cast<int>(DeviceId::TR808);
  if (drum) {
    if (controller == KIT_ACCENT_CONTROLLER) return static_cast<int>(DeviceParamId::Accent);
    if (controller >= KIT_FIRST_CONTROLLER &&
        controller < KIT_FIRST_CONTROLLER + DRUM_KIT_BYTES) {
      return static_cast<int>(kitParam(kitIndex(controller)));
    }
    return -1;
  }
  switch (controller) {
    case 0x02: return static_cast<int>(DeviceParamId::Tune);
    case 0x03: return static_cast<int>(DeviceParamId::Cutoff);
    case 0x04: return static_cast<int>(DeviceParamId::Resonance);
    case 0x05: return static_cast<int>(DeviceParamId::EnvMod);
    case 0x06: return static_cast<int>(DeviceParamId::Decay);
    case 0x07: return static_cast<int>(DeviceParamId::Accent);
    case 0x08: return static_cast<int>(DeviceParamId::Waveform);
    default: return -1;
  }
}

} // anonymous namespace

void AutomationScheduler::applyEvent(Voice* const* voices, Mixer* mixer,
                                     const Event& ev, const ParamMask& overrides) {
  const float norm = static_cast<float>(std::min<uint8_t>(ev.value, 127)) / 127.0f;
  const auto overridden = [&overrides](int device, DeviceParamId param) {
    return overrides.test(static_cast<size_t>(device), static_cast<size_t>(param));
  };

  const int deviceIdx = deviceIndexFromTrack(ev.trackIndex);
  if (deviceIdx >= 0 && deviceIdx < NUM_DEVICES) {
    // Controller 0x00 is the device's on/off switch (header byte 0, which
    // the parser folds into the channel mute).
    if (ev.controller == 0x00) {
      if (mixer && !overridden(deviceIdx, DeviceParamId::Mute)) {
        mixer->setChannelMuted(deviceIdx, ev.value == 0);
      }
      return;
    }
    const int param = deviceParamForController(deviceIdx, ev.controller);
    if (param >= 0 && voices[deviceIdx] &&
        !overridden(deviceIdx, static_cast<DeviceParamId>(param))) {
      // A switch (waveform) takes the raw value; a knob its 0–1 position.
      const float value = (param == static_cast<int>(DeviceParamId::Waveform))
                              ? (ev.value != 0 ? 1.0f : 0.0f)
                              : norm;
      voices[deviceIdx]->setParameter(static_cast<DeviceParamId>(param), value);
    }
    return;
  }

  if (!mixer) return;

  if (ev.trackIndex == 0) {
    // Mixer track: MIXR's master level and insert targets, then six
    // controllers per channel from 0x06 (+0 level, +1 pan, +2 delay send,
    // +3 distortion on; +4 and +5 are not decoded).
    if (ev.controller == 0x00) {
      mixer->setMasterLevel(norm);
    } else if (ev.controller == 0x01) {
      mixer->setCompressorTarget(ev.value);
    } else if (ev.controller == 0x02) {
      mixer->setPcfTarget(ev.value);
    } else if (ev.controller >= 0x06 && ev.controller < 0x06 + 6 * NUM_DEVICES) {
      const int channel = (ev.controller - 0x06) / 6;
      const int field = (ev.controller - 0x06) % 6;
      if (field == 0 && !overridden(channel, DeviceParamId::Level)) {
        mixer->setChannelLevel(channel, norm);
      } else if (field == 1 && !overridden(channel, DeviceParamId::Pan)) {
        mixer->setChannelPan(channel, norm);
      } else if (field == 2) {
        mixer->setChannelDelaySend(channel, norm);
      } else if (field == 3) {
        mixer->setChannelDistortion(channel, ev.value != 0);
      }
    }
    return;
  }

  // FX tracks: the controller number is the byte offset in that FX's DEVL
  // chunk (RbsFormat.md §6a-ii), so 0x00 is always the on/off switch. Only
  // controller 0 switches an effect — every song writes an initial value for
  // each controller at tick 0, and treating any delay controller as "delay
  // on" used to switch the delay on for every song.
  switch (ev.trackIndex) {
    case 5:  // DELY: enabled, steps, unit, feedback, pan
      if (ev.controller == 0x00) mixer->setDelayEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setDelaySteps(ev.value);
      if (ev.controller == 0x02) mixer->setDelayTriplet(ev.value == 0);
      if (ev.controller == 0x03) mixer->setDelayFeedback(norm);
      if (ev.controller == 0x04) mixer->setDelayPan(norm);
      break;
    case 6:  // DIST: enabled, amount, shape
      if (ev.controller == 0x00) mixer->setDistortionEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setDistortionDrive(norm);
      if (ev.controller == 0x02) mixer->setDistortionMix(norm);
      break;
    case 7:  // PCF: enabled, frequency, Q, amount, pattern, decay, type
      if (ev.controller == 0x00) mixer->setPcfEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setPcfCutoff(norm);
      if (ev.controller == 0x02) mixer->setPcfResonance(norm);
      if (ev.controller == 0x03) mixer->setPcfEnvAmount(norm);
      if (ev.controller == 0x05) mixer->setPcfDecay(norm);
      if (ev.controller == 0x06) mixer->setPcfLowpass(ev.value != 0);
      break;
    case 8:  // COMP: enabled, ratio, threshold
      if (ev.controller == 0x00) mixer->setCompressorEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setCompressorRatioByte(ev.value);
      if (ev.controller == 0x02) mixer->setCompressorThreshold(norm);
      break;
    default:
      break;
  }
}

} // namespace rb338
