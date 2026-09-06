#include "Tr909Voice.h"
#include "DrumBitfield.h"
#include <cstring>

namespace rb338 {

namespace {

size_t channelIndex(DrumVoiceId id) {
  switch (id) {
    case DrumVoiceId::Kick: return 0;
    case DrumVoiceId::Snare: return 1;
    case DrumVoiceId::ClosedHat: return 2;
    case DrumVoiceId::OpenHat: return 3;
    case DrumVoiceId::Clap: return 4;
    case DrumVoiceId::Rimshot: return 5;
    case DrumVoiceId::Crash: return 6;
    case DrumVoiceId::Ride: return 7;
    case DrumVoiceId::LowTom: return 8;
    case DrumVoiceId::MidTom: return 9;
    case DrumVoiceId::HighTom: return 10;
    default: return 0;
  }
}

} // anonymous namespace

void Tr909Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  for (auto& ch : m_channels) {
    ch.init(sampleRate);
  }
  reset();
}

void Tr909Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  (void)patterns;
  m_params.tune = state.tune;
  m_params.decay = state.decay;
  m_params.accent = state.accent;
}

void Tr909Voice::fire(DrumVoiceId id, bool accent) {
  m_channels[channelIndex(id)].trigger(id, 1.0f, accent, m_params, true);
}

void Tr909Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    float sample = 0.0f;
    for (auto& ch : m_channels) {
      sample += ch.render();
    }
    output[i] = sample;
  }
}

void Tr909Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;
  if (!step.active) return;

  const uint8_t hits = step.note;
  const uint8_t extra = step.drumExtra;
  const bool accent = step.accent;

  if (hits & DrumHit::BD) fire(DrumVoiceId::Kick, accent);
  if (hits & DrumHit::SD) fire(DrumVoiceId::Snare, accent);
  if (hits & DrumHit::LT) fire(DrumVoiceId::LowTom, accent);
  if (hits & DrumHit::MT) fire(DrumVoiceId::MidTom, accent);
  if (hits & DrumHit::HT) fire(DrumVoiceId::HighTom, accent);
  if (hits & DrumHit::CH) fire(DrumVoiceId::ClosedHat, accent);
  if (hits & DrumHit::OH) fire(DrumVoiceId::OpenHat, accent);
  if (extra & DrumExtra::RS) fire(DrumVoiceId::Rimshot, accent);
  if (extra & DrumExtra::CP) fire(DrumVoiceId::Crash, accent);
  if (extra & DrumExtra::MA) fire(DrumVoiceId::Ride, accent);
}

void Tr909Voice::setParameter(const char* name, float value) {
  if (!name) return;
  if (std::strcmp(name, "tune") == 0) m_params.tune = value;
  else if (std::strcmp(name, "decay") == 0) m_params.decay = value;
  else if (std::strcmp(name, "accent") == 0) m_params.accent = value;
}

void Tr909Voice::reset() {
  for (auto& ch : m_channels) {
    ch.reset();
  }
}

} // namespace rb338
