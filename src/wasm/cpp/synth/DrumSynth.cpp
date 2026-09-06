#include "DrumSynth.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float expDecay(uint32_t age, uint32_t length) {
  if (length == 0) return 0.0f;
  const float t = static_cast<float>(age) / static_cast<float>(length);
  return std::exp(-5.0f * t);
}

} // anonymous namespace

void DrumVoiceChannel::init(float sampleRate) {
  m_sampleRate = sampleRate;
  reset();
}

void DrumVoiceChannel::reset() {
  m_active = false;
  m_age = 0;
  m_length = 0;
  m_phase = 0.0f;
  m_env = 0.0f;
  m_velocity = 1.0f;
  m_clapBurstsLeft = 0;
  m_clapBurstAge = 0;
  m_clapBurstLen = 0;
  m_clapGap = 0;
}

void DrumVoiceChannel::trigger(DrumVoiceId id, float velocity, bool accent,
                               const DrumParams& params, bool is909) {
  m_active = true;
  m_id = id;
  m_is909 = is909;
  m_age = 0;
  m_phase = 0.0f;
  m_velocity = velocity;

  // Device knobs → per-hit scaling.
  m_pitchMul = lerp(0.88f, 1.12f, params.tune);
  m_decayMul = lerp(0.55f, 1.85f, params.decay);

  const float accentBoost = accent ? lerp(1.1f, 1.55f, params.accent) : 1.0f;
  m_velocity *= accentBoost;

  const float sr = m_sampleRate;

  switch (id) {
    case DrumVoiceId::Kick:
      m_length = static_cast<uint32_t>(sr * lerp(0.32f, 0.55f, params.decay));
      break;
    case DrumVoiceId::Snare:
      m_length = static_cast<uint32_t>(sr * lerp(0.12f, 0.22f, params.decay));
      break;
    case DrumVoiceId::ClosedHat:
      m_length = static_cast<uint32_t>(sr * lerp(0.025f, 0.055f, params.decay));
      break;
    case DrumVoiceId::OpenHat:
      m_length = static_cast<uint32_t>(sr * lerp(0.12f, 0.28f, params.decay));
      break;
    case DrumVoiceId::Rimshot:
      m_length = static_cast<uint32_t>(sr * 0.035f);
      break;
    case DrumVoiceId::Clap:
      m_clapBurstsLeft = is909 ? 3 : 4;
      m_clapBurstLen = static_cast<uint32_t>(sr * 0.012f);
      m_clapGap = static_cast<uint32_t>(sr * 0.008f);
      m_clapBurstAge = 0;
      m_length = m_clapBurstLen * m_clapBurstsLeft +
                 m_clapGap * (m_clapBurstsLeft - 1);
      break;
    case DrumVoiceId::Clave:
      m_length = static_cast<uint32_t>(sr * 0.025f);
      break;
    case DrumVoiceId::Maracas:
      m_length = static_cast<uint32_t>(sr * lerp(0.08f, 0.16f, params.decay));
      break;
    case DrumVoiceId::Crash:
      m_length = static_cast<uint32_t>(sr * lerp(0.35f, 0.75f, params.decay));
      break;
    case DrumVoiceId::Ride:
      m_length = static_cast<uint32_t>(sr * lerp(0.25f, 0.55f, params.decay));
      break;
    case DrumVoiceId::LowTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.18f, 0.32f, params.decay));
      break;
    case DrumVoiceId::MidTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.16f, 0.28f, params.decay));
      break;
    case DrumVoiceId::HighTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.14f, 0.24f, params.decay));
      break;
  }
}

float DrumVoiceChannel::nextNoise() {
  m_noiseState = std::fmod(m_noiseState * 1.97f + 0.13f, 2.0f) - 1.0f;
  return m_noiseState;
}

float DrumVoiceChannel::renderKick() {
  const float t = static_cast<float>(m_age) / static_cast<float>(std::max(1u, m_length));
  const float startHz = (m_is909 ? 180.0f : 165.0f) * m_pitchMul;
  const float endHz = (m_is909 ? 48.0f : 42.0f) * m_pitchMul;
  const float freq = lerp(startHz, endHz, t * t);
  m_phase += 2.0f * kPi * freq / m_sampleRate;
  const float env = expDecay(m_age, m_length);
  return std::sin(m_phase) * env * m_velocity * 0.95f;
}

float DrumVoiceChannel::renderSnare() {
  const float env = expDecay(m_age, m_length);
  const float toneHz = (m_is909 ? 210.0f : 185.0f) * m_pitchMul;
  m_phase += 2.0f * kPi * toneHz / m_sampleRate;
  const float tone = std::sin(m_phase) * 0.35f;
  const float noise = nextNoise() * 0.65f;
  return (tone + noise) * env * m_velocity * 0.75f;
}

float DrumVoiceChannel::renderHat(bool open) {
  const float env = expDecay(m_age, m_length);
  // High-pass-ish noise via differencing.
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  const float bright = (n0 - n1) * (open ? 0.55f : 0.42f);
  return bright * env * m_velocity * (open ? 0.5f : 0.38f);
}

float DrumVoiceChannel::renderRimshot() {
  const float env = expDecay(m_age, m_length);
  m_phase += 2.0f * kPi * 320.0f / m_sampleRate;
  const float click = std::sin(m_phase) * 0.25f;
  const float noise = nextNoise() * 0.55f;
  return (click + noise) * env * m_velocity * 0.55f;
}

float DrumVoiceChannel::renderClap() {
  if (m_clapBurstsLeft == 0) return 0.0f;

  if (m_clapBurstAge >= m_clapBurstLen) {
    if (m_clapGap > 0 && m_clapBurstAge < m_clapBurstLen + m_clapGap) {
      ++m_clapBurstAge;
      return 0.0f;
    }
    --m_clapBurstsLeft;
    m_clapBurstAge = 0;
    if (m_clapBurstsLeft == 0) return 0.0f;
  }

  const float env = expDecay(m_clapBurstAge, m_clapBurstLen);
  const float sample = nextNoise() * env * m_velocity * 0.42f;
  ++m_clapBurstAge;
  return sample;
}

float DrumVoiceChannel::renderClave() {
  const float env = expDecay(m_age, m_length);
  m_phase += 2.0f * kPi * 2400.0f / m_sampleRate;
  const float click = std::sin(m_phase) * 0.45f;
  const float noise = nextNoise() * 0.15f;
  return (click + noise) * env * m_velocity * 0.5f;
}

float DrumVoiceChannel::renderMaracas() {
  const float env = expDecay(m_age, m_length);
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  return (n0 - n1) * env * m_velocity * 0.28f;
}

float DrumVoiceChannel::renderCrash() {
  const float env = expDecay(m_age, m_length);
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  m_phase += 2.0f * kPi * 4200.0f / m_sampleRate;
  const float ring = std::sin(m_phase) * 0.08f;
  return ((n0 - n1) * 0.55f + ring) * env * m_velocity * 0.62f;
}

float DrumVoiceChannel::renderRide() {
  const float env = expDecay(m_age, m_length);
  m_phase += 2.0f * kPi * 6800.0f / m_sampleRate;
  const float tone = std::sin(m_phase) * 0.35f;
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  return (tone + (n0 - n1) * 0.25f) * env * m_velocity * 0.42f;
}

float DrumVoiceChannel::renderTom(float baseHz) {
  const float t = static_cast<float>(m_age) / static_cast<float>(std::max(1u, m_length));
  const float freq = baseHz * m_pitchMul * lerp(1.0f, 0.65f, t);
  m_phase += 2.0f * kPi * freq / m_sampleRate;
  const float env = expDecay(m_age, m_length);
  return std::sin(m_phase) * env * m_velocity * 0.7f;
}

float DrumVoiceChannel::render() {
  if (!m_active) return 0.0f;

  float sample = 0.0f;
  switch (m_id) {
    case DrumVoiceId::Kick:
      sample = renderKick();
      break;
    case DrumVoiceId::Snare:
      sample = renderSnare();
      break;
    case DrumVoiceId::ClosedHat:
      sample = renderHat(false);
      break;
    case DrumVoiceId::OpenHat:
      sample = renderHat(true);
      break;
    case DrumVoiceId::Rimshot:
      sample = renderRimshot();
      break;
    case DrumVoiceId::Clap:
      sample = renderClap();
      break;
    case DrumVoiceId::Clave:
      sample = renderClave();
      break;
    case DrumVoiceId::Maracas:
      sample = renderMaracas();
      break;
    case DrumVoiceId::Crash:
      sample = renderCrash();
      break;
    case DrumVoiceId::Ride:
      sample = renderRide();
      break;
    case DrumVoiceId::LowTom:
      sample = renderTom(95.0f);
      break;
    case DrumVoiceId::MidTom:
      sample = renderTom(130.0f);
      break;
    case DrumVoiceId::HighTom:
      sample = renderTom(175.0f);
      break;
  }

  ++m_age;
  if (m_age >= m_length) {
    m_active = false;
  }

  return sample;
}

} // namespace rb338
