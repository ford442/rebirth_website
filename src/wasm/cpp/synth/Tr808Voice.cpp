#include "Tr808Voice.h"
#include "DrumBitfield.h"
#include <cstring>

namespace rb338 {

void Tr808Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  for (auto& ch : m_channels) {
    ch.init(sampleRate);
  }
  reset();
}

void Tr808Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  (void)patterns;
  m_params.tune = state.tune;
  m_params.decay = state.decay;
  m_params.accent = state.accent;
}

void Tr808Voice::fire(Channel ch, DrumVoiceId id, bool accent) {
  m_channels[static_cast<size_t>(ch)].trigger(id, 1.0f, accent, m_params, false);
}

void Tr808Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    float sample = 0.0f;
    for (auto& ch : m_channels) {
      sample += ch.render();
    }
    output[i] = sample;
  }
}

void Tr808Voice::triggerStep(uint8_t stepIndex, const StepData& step) {
  (void)stepIndex;
  if (!step.active) return;

  const uint8_t hits = step.note;
  const uint8_t extra = step.drumExtra;
  const bool accent = step.accent;

  if (hits & DrumHit::BD) fire(Channel::Kick, DrumVoiceId::Kick, accent);
  if (hits & DrumHit::SD) fire(Channel::Snare, DrumVoiceId::Snare, accent);
  if (hits & DrumHit::LT) fire(Channel::LowTom, DrumVoiceId::LowTom, accent);
  if (hits & DrumHit::MT) fire(Channel::MidTom, DrumVoiceId::MidTom, accent);
  if (hits & DrumHit::HT) fire(Channel::HighTom, DrumVoiceId::HighTom, accent);
  if (hits & DrumHit::CH) fire(Channel::ClosedHat, DrumVoiceId::ClosedHat, accent);
  if (hits & DrumHit::OH) fire(Channel::OpenHat, DrumVoiceId::OpenHat, accent);
  if (extra & DrumExtra::RS) fire(Channel::Rimshot, DrumVoiceId::Rimshot, accent);
  if (hits & DrumHit::CL) fire(Channel::Clave, DrumVoiceId::Clave, accent);
  if (extra & DrumExtra::CP) fire(Channel::Clap, DrumVoiceId::Clap, accent);
  if (extra & DrumExtra::MA) fire(Channel::Maracas, DrumVoiceId::Maracas, accent);
}

void Tr808Voice::setParameter(const char* name, float value) {
  if (!name) return;
  if (std::strcmp(name, "tune") == 0) m_params.tune = value;
  else if (std::strcmp(name, "decay") == 0) m_params.decay = value;
  else if (std::strcmp(name, "accent") == 0) m_params.accent = value;
}

void Tr808Voice::reset() {
  for (auto& ch : m_channels) {
    ch.reset();
  }
}

} // namespace rb338
