#include "AutomationScheduler.h"
#include "Mixer.h"
#include "../synth/Voice.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

constexpr uint32_t kTicksPerBar = 32;

int deviceIndexFromTrack(uint8_t trackIndex) {
  if (trackIndex >= 1 && trackIndex <= NUM_DEVICES) {
    return static_cast<int>(trackIndex - 1);
  }
  return -1;
}

} // anonymous namespace

void AutomationScheduler::reset() {
  m_nextEventIndex = 0;
}

uint32_t AutomationScheduler::tickFromPosition(uint16_t bar, uint8_t step) {
  const uint32_t barTicks = static_cast<uint32_t>(std::max<uint16_t>(1, bar) - 1) * kTicksPerBar;
  const uint32_t stepTicks = static_cast<uint32_t>(step) * 2u;
  return barTicks + stepTicks;
}

double AutomationScheduler::samplesPerTick(float bpm, float sampleRate) {
  const float clampedBpm = std::clamp(bpm, 40.0f, 250.0f);
  const float samplesPerStep = (60.0f / clampedBpm) * 0.25f * sampleRate;
  return static_cast<double>(samplesPerStep) * 0.5;
}

void AutomationScheduler::setPosition(const ParsedSong* song, uint16_t bar, uint8_t step) {
  m_nextEventIndex = 0;
  if (!song) return;
  const uint32_t tick = tickFromPosition(bar, step);
  while (m_nextEventIndex < song->automation.size() &&
         song->automation[m_nextEventIndex].tickPosition < tick) {
    ++m_nextEventIndex;
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

  const uint32_t startTick = tickFromPosition(bar, step);
  const double startSamples =
      stepPhase + static_cast<double>(startTick) * tickSamples;
  const double endSamples = startSamples + static_cast<double>(numFrames);

  uint32_t count = 0;
  while (m_nextEventIndex < song->automation.size() && count < maxEvents) {
    const AutomationEvent& src = song->automation[m_nextEventIndex];
    const double eventSample = static_cast<double>(src.tickPosition) * tickSamples;
    if (eventSample < startSamples) {
      ++m_nextEventIndex;
      continue;
    }
    if (eventSample >= endSamples) break;

    Event& ev = out[count++];
    ev.sampleOffset = static_cast<uint32_t>(std::round(eventSample - startSamples));
    ev.trackIndex = src.trackIndex;
    ev.controller = src.controller;
    ev.value = src.value;
    ++m_nextEventIndex;
  }
  return count;
}

void AutomationScheduler::applyEvent(Voice* const* voices, Mixer* mixer,
                                     const Event& ev) const {
  const int deviceIdx = deviceIndexFromTrack(ev.trackIndex);
  if (deviceIdx >= 0 && deviceIdx < NUM_DEVICES && voices[deviceIdx]) {
    switch (ev.controller) {
      case 0x02: voices[deviceIdx]->setParameter("tune", ev.value / 127.0f); break;
      case 0x03: voices[deviceIdx]->setParameter("cutoff", ev.value / 127.0f); break;
      case 0x04: voices[deviceIdx]->setParameter("resonance", ev.value / 127.0f); break;
      case 0x05: voices[deviceIdx]->setParameter("envMod", ev.value / 127.0f); break;
      case 0x06: voices[deviceIdx]->setParameter("decay", ev.value / 127.0f); break;
      case 0x07: voices[deviceIdx]->setParameter("accent", ev.value / 127.0f); break;
      default: break;
    }
  }

  if (!mixer) return;

  if (ev.trackIndex == 0) {
    switch (ev.controller) {
      case 0x06:
        mixer->setChannelLevel(0, ev.value / 127.0f);
        break;
      case 0x07:
        mixer->setChannelPan(0, ev.value / 127.0f);
        break;
      case 0x0c:
        mixer->setChannelLevel(1, ev.value / 127.0f);
        break;
      case 0x0d:
        mixer->setChannelPan(1, ev.value / 127.0f);
        break;
      case 0x12:
        mixer->setChannelLevel(2, ev.value / 127.0f);
        break;
      case 0x13:
        mixer->setChannelPan(2, ev.value / 127.0f);
        break;
      case 0x18:
        mixer->setChannelLevel(3, ev.value / 127.0f);
        break;
      case 0x19:
        mixer->setChannelPan(3, ev.value / 127.0f);
        break;
      default:
        break;
    }
    return;
  }

  if (ev.trackIndex == 5) {
    mixer->setDelayEnabled(ev.controller == 0x00 ? (ev.value != 0) : true);
    if (ev.controller == 0x03) mixer->setDelayFeedback(ev.value / 127.0f);
  } else if (ev.trackIndex == 6) {
    if (ev.controller == 0x01) mixer->setDistortionDrive(ev.value / 127.0f);
  } else if (ev.trackIndex == 7) {
    if (ev.controller == 0x01) mixer->setPcfCutoff(ev.value / 127.0f);
    if (ev.controller == 0x02) mixer->setPcfResonance(ev.value / 127.0f);
  } else if (ev.trackIndex == 8) {
    if (ev.controller == 0x02) mixer->setCompressorThreshold(ev.value / 127.0f);
  }
}

} // namespace rb338
