#pragma once

#include <cstdint>

namespace rb338 {

enum class DrumVoiceId : uint8_t {
  Kick,
  Snare,
  ClosedHat,
  OpenHat,
  Rimshot,
  Clap,
  Clave,
  Maracas,
  Crash,
  Ride,
  LowTom,
  MidTom,
  HighTom,
};

/** Normalised device knobs (0–1) shared by all channels in a drum machine. */
struct DrumParams {
  float tune = 0.5f;
  float decay = 0.5f;
  float accent = 0.5f;
};

/**
 * Knob → per-hit scaling, shared by the procedural voices and the mod
 * sample player so TUNE/ACCENT behave identically either way.
 */
inline float drumPitchMul(const DrumParams& params) {
  return 0.88f + (1.12f - 0.88f) * params.tune;
}

inline float drumAccentGain(const DrumParams& params, bool accent) {
  return accent ? (1.1f + (1.55f - 1.1f) * params.accent) : 1.0f;
}

/**
 * DrumVoiceChannel — single monophonic analogue-style drum voice.
 *
 * Models are procedural (no PCM) for small WASM size and clear licensing.
 */
class DrumVoiceChannel {
public:
  /** Seed restored by reset(), so a replay draws the same noise. */
  static constexpr uint32_t kNoiseSeed = 0x9e3779b9u;

  void init(float sampleRate);
  void reset();

  void trigger(DrumVoiceId id, float velocity, bool accent, const DrumParams& params,
               bool is909 = false);

  float render();

private:
  float m_sampleRate = 44100.0f;
  bool m_active = false;
  bool m_is909 = false;
  DrumVoiceId m_id = DrumVoiceId::Kick;

  uint32_t m_age = 0;
  uint32_t m_length = 0;
  float m_phase = 0.0f;
  float m_env = 0.0f;
  float m_velocity = 1.0f;
  float m_pitchMul = 1.0f;
  float m_decayMul = 1.0f;
  // exp(-5 t) envelope, advanced by one multiply per sample instead of an
  // exp() per sample. m_envGain = exp(-5 * age / length).
  float m_envGain = 1.0f;
  float m_envCoeff = 1.0f;
  float m_burstEnvGain = 1.0f;   // the same for the current clap burst
  float m_burstEnvCoeff = 1.0f;
  /** xorshift32 state; never zero (zero is xorshift's fixed point). */
  uint32_t m_noiseState = kNoiseSeed;

  // Clap: staggered noise bursts.
  uint8_t m_clapBurstsLeft = 0;
  uint32_t m_clapBurstAge = 0;
  uint32_t m_clapBurstLen = 0;
  uint32_t m_clapGap = 0;

public:
  /** White noise in [-1, 1). Public for the spectral tests. */
  float nextNoise();

private:
  float renderKick();
  float renderSnare();
  float renderHat(bool open);
  float renderRimshot();
  float renderClap();
  float renderClave();
  float renderMaracas();
  float renderCrash();
  float renderRide();
  float renderTom(float baseHz);
};

} // namespace rb338
