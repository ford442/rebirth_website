#include "Tr808Voice.h"
#include "DrumBitfield.h"

namespace rb338 {

namespace {

/**
 * Channel → mod slot. Tr808Cymbal has no channel in this machine (there is
 * no Phase-1 sequencer bit for it), so a mod's cymbal sample is catalogued
 * but never triggered.
 */
ModSampleSlot slotForChannel(size_t channel) {
  switch (channel) {
    case 0: return ModSampleSlot::Tr808Kick;
    case 1: return ModSampleSlot::Tr808Snare;
    case 2: return ModSampleSlot::Tr808ClosedHat;
    case 3: return ModSampleSlot::Tr808OpenHat;
    case 4: return ModSampleSlot::Tr808Rimshot;
    case 5: return ModSampleSlot::Tr808Clap;
    case 6: return ModSampleSlot::Tr808Clave;
    case 7: return ModSampleSlot::Tr808Maracas;
    case 8: return ModSampleSlot::Tr808LowTom;
    case 9: return ModSampleSlot::Tr808MidTom;
    case 10: return ModSampleSlot::Tr808HighTom;
    default: return ModSampleSlot::Unknown;
  }
}

} // anonymous namespace

void Tr808Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  for (auto& ch : m_channels) {
    ch.init(sampleRate);
  }
  for (auto& sv : m_sampleVoices) {
    sv.init(sampleRate);
  }
  reset();
}

void Tr808Voice::setSamplePool(const SamplePool* pool) {
  m_pool = pool;
  for (auto& sv : m_sampleVoices) {
    sv.reset();
  }
}

void Tr808Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  (void)patterns;
  m_params.tune = state.tune;
  m_params.decay = state.decay;
  m_params.accent = state.accent;
}

void Tr808Voice::fire(Channel ch, DrumVoiceId id, bool accent) {
  const auto index = static_cast<size_t>(ch);

  // Mod sample wins for this slot; otherwise fall back to the analogue model.
  if (m_pool) {
    if (const SamplePool::SlotData* slot = m_pool->slotData(slotForChannel(index))) {
      m_sampleVoices[index].trigger(*slot, drumPitchMul(m_params), m_params.decay,
                                    drumAccentGain(m_params, accent));
      return;
    }
  }
  m_channels[index].trigger(id, 1.0f, accent, m_params, false);
}

void Tr808Voice::render(float* output, uint32_t numFrames) {
  if (!output || numFrames == 0) return;

  for (uint32_t i = 0; i < numFrames; ++i) {
    float sample = 0.0f;
    for (auto& ch : m_channels) {
      sample += ch.render();
    }
    for (auto& sv : m_sampleVoices) {
      sample += sv.render();
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

void Tr808Voice::setParameter(DeviceParamId param, float value) {
  switch (param) {
    case DeviceParamId::Tune: m_params.tune = value; break;
    case DeviceParamId::Decay: m_params.decay = value; break;
    case DeviceParamId::Accent: m_params.accent = value; break;
    default: break;
  }
}

void Tr808Voice::reset() {
  for (auto& ch : m_channels) {
    ch.reset();
  }
  for (auto& sv : m_sampleVoices) {
    sv.reset();
  }
}

} // namespace rb338
