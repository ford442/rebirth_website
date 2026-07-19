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
 * DrumVoiceChannel — single monophonic analogue-style drum voice.
 *
 * Models are procedural (no PCM) for small WASM size and clear licensing.
 */
class DrumVoiceChannel {
public:
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
  float m_noiseState = 0.0f;

  // Clap: staggered noise bursts.
  uint8_t m_clapBurstsLeft = 0;
  uint32_t m_clapBurstAge = 0;
  uint32_t m_clapBurstLen = 0;
  uint32_t m_clapGap = 0;

  float nextNoise();
  float renderKick();
  float renderSnare();
  float renderHat(bool open);
  float renderRimshot();
  float renderClap();
  float renderTom(float baseHz);
};

} // namespace rb338
