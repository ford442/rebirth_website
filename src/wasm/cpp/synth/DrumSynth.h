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
  // 808-only voices: the cowbell and cymbal have their own rows, and the
  // three tom rows switch to congas with the LT/LC, MT/MC, HT/HC selectors.
  Cowbell,
  Cymbal,
  LowConga,
  MidConga,
  HighConga,
};

/**
 * Machine-wide knobs (0–1): the drum machine's ACCENT knob, plus TUNE and
 * DECAY macros (0.5 = neutral) that scale every instrument on top of its own
 * kit knob. ReBirth has no machine-wide tune/decay; they are the studio's
 * session macros and are never saved.
 */
struct DrumParams {
  float tune = 0.5f;
  float decay = 0.5f;
  float accent = 0.5f;
};

/**
 * One hit's resolved parameters: the instrument's kit knobs, the machine
 * macros and the step's accents folded together by the kit (Tr808Kit /
 * Tr909Kit::hitParams). Every field is 0.5 / 1.0 at a neutral knob.
 */
struct DrumHitParams {
  float gain = 1.0f;     // level × accents
  float pitchMul = 1.0f; // tune / tuning knob × TUNE macro
  float decay = 0.5f;    // 0–1, the instrument's decay knob shifted by the DECAY macro
  float tone = 0.5f;     // 0–1: 808 BD/SD/CY tone, 909 SD tone
  float snappy = 0.5f;   // 0–1: SD snappy (noise level)
  float attack = 0.5f;   // 0–1: 909 BD attack (beater click)
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

  void trigger(DrumVoiceId id, const DrumHitParams& hit, bool is909 = false);

  /**
   * Legacy entry point (tests): machine knobs only, every kit knob neutral.
   * `velocity` scales the hit, `accent` applies the ACCENT knob's boost.
   */
  void trigger(DrumVoiceId id, float velocity, bool accent, const DrumParams& params,
               bool is909 = false);

  /** Fade out within a few ms — the closed hat cutting the open hat. */
  void choke();

  bool active() const { return m_active; }

  float render();

private:
  float m_sampleRate = 44100.0f;
  bool m_active = false;
  bool m_is909 = false;
  DrumVoiceId m_id = DrumVoiceId::Kick;

  uint32_t m_age = 0;
  uint32_t m_length = 0;
  float m_phase = 0.0f;
  float m_phase2 = 0.0f; // second oscillator (cowbell)
  float m_env = 0.0f;
  float m_velocity = 1.0f;
  float m_pitchMul = 1.0f;
  float m_tone = 0.5f;
  float m_snappy = 0.5f;
  float m_attack = 0.5f;
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
  float renderConga(float baseHz);
  float renderCowbell();
  float renderCymbal808();
};

/** Map a 0–127 kit level byte to gain: 100 is unity (the default kit). */
inline float drumLevelGain(uint8_t level) { return static_cast<float>(level) / 100.0f; }

/** Map a 0–127 kit tune byte to a pitch multiplier: 64 is unity, ±half an octave. */
float drumTuneMul(uint8_t tune);

} // namespace rb338
