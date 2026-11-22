#include "DrumSynth.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

/** Per-sample multiplier for an exp(-5 t) decay over `length` samples. */
float decayCoeff(uint32_t length) {
  return length == 0 ? 0.0f : std::exp(-5.0f / static_cast<float>(length));
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
  m_phase2 = 0.0f;
  m_env = 0.0f;
  m_velocity = 1.0f;
  m_clapBurstsLeft = 0;
  m_clapBurstAge = 0;
  m_clapBurstLen = 0;
  m_clapGap = 0;
  // Reseed the noise generator. Leaving it alone made every replay of a snare
  // or hat draw a different noise sequence, so the same song bounced twice
  // produced two different files. kNoiseSeed is also what a freshly
  // constructed channel starts from, so a reset channel sounds exactly like
  // a new one.
  m_noiseState = kNoiseSeed;
}

void DrumVoiceChannel::trigger(DrumVoiceId id, float velocity, bool accent,
                               const DrumParams& params, bool is909) {
  DrumHitParams hit;
  hit.gain = velocity * drumAccentGain(params, accent);
  hit.pitchMul = drumPitchMul(params);
  hit.decay = params.decay;
  trigger(id, hit, is909);
}

void DrumVoiceChannel::choke() {
  if (!m_active) return;
  // A 3 ms fade rather than a hard cut, which would click.
  const auto fade = static_cast<uint32_t>(m_sampleRate * 0.003f) + 1u;
  m_length = std::min(m_length, m_age + fade);
  m_envCoeff = std::min(m_envCoeff, decayCoeff(fade));
}

void DrumVoiceChannel::trigger(DrumVoiceId id, const DrumHitParams& hit, bool is909) {
  m_active = true;
  m_id = id;
  m_is909 = is909;
  m_age = 0;
  m_phase = 0.0f;
  m_phase2 = 0.0f;
  m_velocity = hit.gain;
  m_pitchMul = std::max(0.05f, hit.pitchMul);
  m_tone = std::clamp(hit.tone, 0.0f, 1.0f);
  m_snappy = std::clamp(hit.snappy, 0.0f, 1.0f);
  m_attack = std::clamp(hit.attack, 0.0f, 1.0f);
  m_clapBurstsLeft = 0;

  const float decay = std::clamp(hit.decay, 0.0f, 1.0f);
  const float sr = m_sampleRate;

  switch (id) {
    case DrumVoiceId::Kick:
      m_length = static_cast<uint32_t>(sr * lerp(0.32f, 0.55f, decay));
      break;
    case DrumVoiceId::Snare:
      m_length = static_cast<uint32_t>(sr * lerp(0.12f, 0.22f, decay));
      break;
    case DrumVoiceId::ClosedHat:
      m_length = static_cast<uint32_t>(sr * lerp(0.025f, 0.055f, decay));
      break;
    case DrumVoiceId::OpenHat:
      m_length = static_cast<uint32_t>(sr * lerp(0.12f, 0.28f, decay));
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
      m_length = static_cast<uint32_t>(sr * lerp(0.08f, 0.16f, decay));
      break;
    case DrumVoiceId::Crash:
      m_length = static_cast<uint32_t>(sr * lerp(0.35f, 0.75f, decay));
      break;
    case DrumVoiceId::Ride:
      m_length = static_cast<uint32_t>(sr * lerp(0.25f, 0.55f, decay));
      break;
    case DrumVoiceId::LowTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.18f, 0.32f, decay));
      break;
    case DrumVoiceId::MidTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.16f, 0.28f, decay));
      break;
    case DrumVoiceId::HighTom:
      m_length = static_cast<uint32_t>(sr * lerp(0.14f, 0.24f, decay));
      break;
    case DrumVoiceId::Cowbell:
      m_length = static_cast<uint32_t>(sr * 0.16f);
      break;
    case DrumVoiceId::Cymbal:
      m_length = static_cast<uint32_t>(sr * lerp(0.30f, 1.10f, decay));
      break;
    case DrumVoiceId::LowConga:
    case DrumVoiceId::MidConga:
    case DrumVoiceId::HighConga:
      m_length = static_cast<uint32_t>(sr * lerp(0.10f, 0.18f, decay));
      break;
  }
  m_length = std::max(m_length, 1u);

  m_envGain = 1.0f;
  m_envCoeff = decayCoeff(m_length);
  m_burstEnvGain = 1.0f;
  m_burstEnvCoeff = decayCoeff(m_clapBurstLen);
}

float drumTuneMul(uint8_t tune) {
  return std::exp2((static_cast<float>(std::min<uint8_t>(tune, 127)) - 64.0f) / 128.0f);
}

float DrumVoiceChannel::nextNoise() {
  // xorshift32 (Marsaglia 2003): full 2^32-1 period, white, and three shifts
  // per sample. It replaced `fmod(x * 1.97 + 0.13, 2) - 1`, which is not
  // noise at all: fmod keeps the sign of a negative x, so the state fell
  // into roughly [-3.0, -0.87] and stayed there — snare, clap, rim, maracas
  // and cymbals were fed a large negative DC offset (a thump) instead of a
  // hiss.
  uint32_t x = m_noiseState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  m_noiseState = x;
  // Reinterpret as signed and scale: [-2^31, 2^31) → [-1, 1).
  return static_cast<float>(static_cast<int32_t>(x)) * (1.0f / 2147483648.0f);
}

float DrumVoiceChannel::renderKick() {
  const float t = static_cast<float>(m_age) / static_cast<float>(std::max(1u, m_length));
  // 808 TONE brightens the start of the sweep; the 909 has a TUNE knob
  // (pitchMul) and an ATTACK knob for the beater click instead.
  const float sweep = m_is909 ? 1.0f : (0.75f + 0.5f * m_tone);
  const float startHz = (m_is909 ? 180.0f : 165.0f) * m_pitchMul * sweep;
  const float endHz = (m_is909 ? 48.0f : 42.0f) * m_pitchMul;
  const float freq = lerp(startHz, endHz, t * t);
  m_phase += 2.0f * kPi * freq / m_sampleRate;
  const float env = m_envGain;
  float out = std::sin(m_phase) * env * 0.95f;
  if (m_is909) {
    // Beater click: a 2 ms decaying noise burst, scaled by ATTACK.
    const float clickLen = m_sampleRate * 0.002f;
    const float age = static_cast<float>(m_age);
    if (age < clickLen) {
      out += nextNoise() * (1.0f - age / clickLen) * 0.5f * m_attack;
    }
  }
  return out * m_velocity;
}

float DrumVoiceChannel::renderSnare() {
  const float env = m_envGain;
  // TONE moves the drum-head pitch, SNAPPY the level of the snare wires.
  const float toneHz = (m_is909 ? 210.0f : 185.0f) * m_pitchMul * (0.8f + 0.4f * m_tone);
  m_phase += 2.0f * kPi * toneHz / m_sampleRate;
  const float tone = std::sin(m_phase) * 0.35f;
  const float noise = nextNoise() * 0.65f * (0.4f + 1.2f * m_snappy);
  return (tone + noise) * env * m_velocity * 0.75f;
}

float DrumVoiceChannel::renderHat(bool open) {
  const float env = m_envGain;
  // High-pass-ish noise via differencing.
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  const float bright = (n0 - n1) * (open ? 0.55f : 0.42f);
  return bright * env * m_velocity * (open ? 0.5f : 0.38f);
}

float DrumVoiceChannel::renderRimshot() {
  const float env = m_envGain;
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
    m_burstEnvGain = 1.0f;
    if (m_clapBurstsLeft == 0) return 0.0f;
  }

  const float env = m_burstEnvGain;
  m_burstEnvGain *= m_burstEnvCoeff;
  const float sample = nextNoise() * env * m_velocity * 0.42f;
  ++m_clapBurstAge;
  return sample;
}

float DrumVoiceChannel::renderClave() {
  const float env = m_envGain;
  m_phase += 2.0f * kPi * 2400.0f / m_sampleRate;
  const float click = std::sin(m_phase) * 0.45f;
  const float noise = nextNoise() * 0.15f;
  return (click + noise) * env * m_velocity * 0.5f;
}

float DrumVoiceChannel::renderMaracas() {
  const float env = m_envGain;
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  return (n0 - n1) * env * m_velocity * 0.28f;
}

float DrumVoiceChannel::renderCrash() {
  const float env = m_envGain;
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  m_phase += 2.0f * kPi * 4200.0f * m_pitchMul / m_sampleRate;
  const float ring = std::sin(m_phase) * 0.08f;
  return ((n0 - n1) * 0.55f + ring) * env * m_velocity * 0.62f;
}

float DrumVoiceChannel::renderRide() {
  const float env = m_envGain;
  m_phase += 2.0f * kPi * 6800.0f * m_pitchMul / m_sampleRate;
  const float tone = std::sin(m_phase) * 0.35f;
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  return (tone + (n0 - n1) * 0.25f) * env * m_velocity * 0.42f;
}

float DrumVoiceChannel::renderTom(float baseHz) {
  const float t = static_cast<float>(m_age) / static_cast<float>(std::max(1u, m_length));
  const float freq = baseHz * m_pitchMul * lerp(1.0f, 0.65f, t);
  m_phase += 2.0f * kPi * freq / m_sampleRate;
  const float env = m_envGain;
  return std::sin(m_phase) * env * m_velocity * 0.7f;
}

float DrumVoiceChannel::renderConga(float baseHz) {
  // Congas: higher, purer and shorter than the toms, with almost no sweep.
  const float t = static_cast<float>(m_age) / static_cast<float>(std::max(1u, m_length));
  const float freq = baseHz * m_pitchMul * lerp(1.04f, 1.0f, t);
  m_phase += 2.0f * kPi * freq / m_sampleRate;
  return std::sin(m_phase) * m_envGain * m_velocity * 0.6f;
}

float DrumVoiceChannel::renderCowbell() {
  // Two detuned pulse oscillators (~540 / 800 Hz), as on the 808.
  m_phase += 540.0f * m_pitchMul / m_sampleRate;
  m_phase2 += 800.0f * m_pitchMul / m_sampleRate;
  m_phase -= std::floor(m_phase);
  m_phase2 -= std::floor(m_phase2);
  const float a = m_phase < 0.5f ? 1.0f : -1.0f;
  const float b = m_phase2 < 0.5f ? 1.0f : -1.0f;
  return (a + b) * 0.18f * m_envGain * m_velocity;
}

float DrumVoiceChannel::renderCymbal808() {
  // Metallic noise; TONE trades the low ring for top-end sizzle.
  const float n0 = nextNoise();
  const float n1 = nextNoise();
  m_phase += 2.0f * kPi * 3400.0f * m_pitchMul / m_sampleRate;
  const float ring = std::sin(m_phase) * 0.12f * (1.0f - m_tone);
  const float hiss = (n0 - n1) * (0.35f + 0.35f * m_tone);
  return (ring + hiss) * m_envGain * m_velocity * 0.55f;
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
    case DrumVoiceId::Cowbell:
      sample = renderCowbell();
      break;
    case DrumVoiceId::Cymbal:
      sample = renderCymbal808();
      break;
    case DrumVoiceId::LowConga:
      sample = renderConga(170.0f);
      break;
    case DrumVoiceId::MidConga:
      sample = renderConga(250.0f);
      break;
    case DrumVoiceId::HighConga:
      sample = renderConga(370.0f);
      break;
  }

  ++m_age;
  m_envGain *= m_envCoeff;
  if (m_age >= m_length) {
    m_active = false;
  }

  return sample;
}

} // namespace rb338
