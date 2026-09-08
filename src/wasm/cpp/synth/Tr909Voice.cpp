#include "Tr909Voice.h"
#include "DrumBitfield.h"

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

/** Channel → mod slot, matching channelIndex() above. */
ModSampleSlot slotForChannel(size_t channel) {
  switch (channel) {
    case 0: return ModSampleSlot::Tr909Kick;
    case 1: return ModSampleSlot::Tr909Snare;
    case 2: return ModSampleSlot::Tr909ClosedHat;
    case 3: return ModSampleSlot::Tr909OpenHat;
    case 4: return ModSampleSlot::Tr909Clap;
    case 5: return ModSampleSlot::Tr909Rimshot;
    case 6: return ModSampleSlot::Tr909Crash;
    case 7: return ModSampleSlot::Tr909Ride;
    case 8: return ModSampleSlot::Tr909LowTom;
    case 9: return ModSampleSlot::Tr909MidTom;
    case 10: return ModSampleSlot::Tr909HighTom;
    default: return ModSampleSlot::Unknown;
  }
}

} // anonymous namespace

void Tr909Voice::init(float sampleRate) {
  m_sampleRate = sampleRate;
  for (auto& ch : m_channels) {
    ch.init(sampleRate);
  }
  for (auto& sv : m_sampleVoices) {
    sv.init(sampleRate);
  }
  reset();
}

void Tr909Voice::setSamplePool(const SamplePool* pool) {
  m_pool = pool;
  for (auto& sv : m_sampleVoices) {
    sv.reset();
  }
}

void Tr909Voice::load(const DeviceState& state, const std::vector<Pattern>& patterns) {
  (void)patterns;
  m_params.tune = state.tune;
  m_params.decay = state.decay;
  m_params.accent = state.accent;
}

void Tr909Voice::fire(DrumVoiceId id, bool accent) {
  const size_t index = channelIndex(id);

  // Mod sample wins for this slot; otherwise fall back to the analogue model.
  if (m_pool) {
    if (const SamplePool::SlotData* slot = m_pool->slotData(slotForChannel(index))) {
      m_sampleVoices[index].trigger(*slot, drumPitchMul(m_params), m_params.decay,
                                    drumAccentGain(m_params, accent));
      return;
    }
  }
  m_channels[index].trigger(id, 1.0f, accent, m_params, true);
}

void Tr909Voice::render(float* output, uint32_t numFrames) {
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

void Tr909Voice::setParameter(DeviceParamId param, float value) {
  switch (param) {
    case DeviceParamId::Tune: m_params.tune = value; break;
    case DeviceParamId::Decay: m_params.decay = value; break;
    case DeviceParamId::Accent: m_params.accent = value; break;
    default: break;
  }
}

void Tr909Voice::reset() {
  for (auto& ch : m_channels) {
    ch.reset();
  }
  for (auto& sv : m_sampleVoices) {
    sv.reset();
  }
}

} // namespace rb338
