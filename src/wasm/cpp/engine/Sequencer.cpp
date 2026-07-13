#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

void Sequencer::load(const ParsedSong& song) {
  // Pick the first active pattern for each device. Until arrangement parsing is
  // implemented, this gives the audio thread a concrete loop to play.
  for (int i = 0; i < NUM_DEVICES; ++i) {
    m_currentPatterns[i] = nullptr;
    m_patternLengths[i] = 16;
    DeviceId devId = static_cast<DeviceId>(i);

    for (const auto& p : song.patterns) {
      if (p.deviceId != devId) continue;
      bool active = false;
      for (const auto& s : p.steps) {
        if (s.active) { active = true; break; }
      }
      if (active) {
        m_currentPatterns[i] = &p;
        m_patternLengths[i] = p.length;
        break;
      }
    }
  }

  m_sampleCounter = 0;
  m_currentBar = 1;
  m_currentStep = 0;
  m_samplesPerStep = 0.0f;
}

StepData Sequencer::getStepData(DeviceId device, uint8_t stepIndex) const {
  int idx = static_cast<int>(device);
  const Pattern* p = m_currentPatterns[idx];
  if (!p || stepIndex >= m_patternLengths[idx]) {
    return StepData{};
  }
  return p->steps[stepIndex];
}

uint32_t Sequencer::generateEvents(float bpm, float sampleRate, uint32_t numFrames,
                                    Event* out, uint32_t maxEvents) {
  uint32_t eventCount = 0;

  if (bpm <= 0.0f || sampleRate <= 0.0f || numFrames == 0 || !out || maxEvents == 0) {
    return eventCount;
  }

  // One step = one 16th note = 1/4 of a 4/4 bar.
  m_samplesPerStep = (60.0f / bpm) * 0.25f * sampleRate;
  if (m_samplesPerStep <= 0.0f) {
    return eventCount;
  }

  const float stepDuration = m_samplesPerStep;
  uint32_t framesRemaining = numFrames;
  uint32_t frameOffset = 0;

  while (framesRemaining > 0 && eventCount < maxEvents) {
    // Distance from current sample counter to next step boundary.
    float nextStepSample = (m_currentStep + 1) * stepDuration;
    uint32_t samplesToNext = 0;
    if (m_sampleCounter < nextStepSample) {
      samplesToNext = static_cast<uint32_t>(std::ceil(nextStepSample - static_cast<float>(m_sampleCounter)));
    }

    if (samplesToNext == 0) {
      // We are exactly on a step boundary (or have overshot). Emit events for
      // all devices at this offset, then advance.
      for (int i = 0; i < NUM_DEVICES && eventCount < maxEvents; ++i) {
        DeviceId dev = static_cast<DeviceId>(i);
        StepData step = getStepData(dev, m_currentStep);
        if (step.active) {
          Event ev;
          ev.sampleOffset = frameOffset;
          ev.device = dev;
          ev.stepIndex = m_currentStep;
          ev.step = step;
          out[eventCount++] = ev;
        }
      }

      m_currentStep++;
      if (m_currentStep >= 16) {
        m_currentStep = 0;
        m_currentBar++;
      }
      continue;
    }

    uint32_t advance = std::min<uint32_t>(samplesToNext, framesRemaining);
    m_sampleCounter += advance;
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
  m_currentStep = step % 16;
  m_sampleCounter = static_cast<uint64_t>(m_currentStep * m_samplesPerStep);
}

} // namespace rb338
