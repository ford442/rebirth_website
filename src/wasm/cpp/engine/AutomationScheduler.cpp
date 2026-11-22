#include "AutomationScheduler.h"
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
  // The sequencer loops the arrangement once bar runs past its end, so the
  // automation has to loop with it, or every pass after the first would
  // play the patterns with no automation at all.
  uint32_t barIndex = static_cast<uint32_t>(std::max<uint16_t>(1, bar) - 1);
  if (song && !song->arrangement.empty()) {
    barIndex %= static_cast<uint32_t>(song->arrangement.size());
  }
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
                                    uint64_t overrideMask) {
  const uint32_t tick = tickFromPosition(&song, bar, step);
  for (const AutomationEvent& src : song.automation) {
    if (src.tickPosition >= tick) break;
    Event ev;
    ev.trackIndex = src.trackIndex;
    ev.controller = src.controller;
    ev.value = src.value;
    applyEvent(voices, mixer, ev, overrideMask);
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

void AutomationScheduler::applyEvent(Voice* const* voices, Mixer* mixer,
                                     const Event& ev, uint64_t overrideMask) {
  const auto overridden = [overrideMask](int device, DeviceParamId param) {
    const unsigned bit = static_cast<unsigned>(device) * NUM_DEVICE_PARAMS +
                         static_cast<unsigned>(param);
    return (overrideMask >> bit) & 1u;
  };
  const float norm = ev.value / 127.0f;

  const int deviceIdx = deviceIndexFromTrack(ev.trackIndex);
  if (deviceIdx >= 0 && deviceIdx < NUM_DEVICES && voices[deviceIdx]) {
    DeviceParamId param = DeviceParamId::Mute;
    switch (ev.controller) {
      case 0x02: param = DeviceParamId::Tune; break;
      case 0x03: param = DeviceParamId::Cutoff; break;
      case 0x04: param = DeviceParamId::Resonance; break;
      case 0x05: param = DeviceParamId::EnvMod; break;
      case 0x06: param = DeviceParamId::Decay; break;
      case 0x07: param = DeviceParamId::Accent; break;
      default: break;
    }
    if (param != DeviceParamId::Mute && !overridden(deviceIdx, param)) {
      voices[deviceIdx]->setParameter(param, norm);
    }
  }

  if (!mixer) return;

  if (ev.trackIndex == 0) {
    // Mixer track: six controllers per channel from 0x06; +0 level, +1 pan.
    if (ev.controller >= 0x06 && ev.controller < 0x06 + 6 * NUM_DEVICES) {
      const int channel = (ev.controller - 0x06) / 6;
      const int field = (ev.controller - 0x06) % 6;
      if (field == 0 && !overridden(channel, DeviceParamId::Level)) {
        mixer->setChannelLevel(channel, norm);
      } else if (field == 1 && !overridden(channel, DeviceParamId::Pan)) {
        mixer->setChannelPan(channel, norm);
      }
    }
    return;
  }

  // FX tracks: the controller number is the byte offset in that FX's DEVL
  // chunk (RbsFormat.md §5), so 0x00 is always the on/off switch. Only
  // controller 0 switches an effect — every song writes an initial value for
  // each controller at tick 0, and treating any delay controller as "delay
  // on" used to switch the delay on for every song.
  switch (ev.trackIndex) {
    case 5:  // DELY: enabled, time, ?, feedback, wet
      if (ev.controller == 0x00) mixer->setDelayEnabled(ev.value != 0);
      if (ev.controller == 0x03) mixer->setDelayFeedback(norm);
      if (ev.controller == 0x04) mixer->setDelayWet(norm);
      break;
    case 6:  // DIST: enabled, drive, mix
      if (ev.controller == 0x00) mixer->setDistortionEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setDistortionDrive(norm);
      if (ev.controller == 0x02) mixer->setDistortionMix(norm);
      break;
    case 7:  // PCF: enabled, cutoff, resonance, envAmount
      if (ev.controller == 0x00) mixer->setPcfEnabled(ev.value != 0);
      if (ev.controller == 0x01) mixer->setPcfCutoff(norm);
      if (ev.controller == 0x02) mixer->setPcfResonance(norm);
      if (ev.controller == 0x03) mixer->setPcfEnvAmount(norm);
      break;
    case 8:  // COMP: enabled, ratio, threshold
      if (ev.controller == 0x00) mixer->setCompressorEnabled(ev.value != 0);
      if (ev.controller == 0x02) mixer->setCompressorThreshold(norm);
      break;
    default:
      break;
  }
}

} // namespace rb338
