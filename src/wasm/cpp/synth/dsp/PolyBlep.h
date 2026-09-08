#pragma once

namespace rb338::dsp {

/**
 * polyBlep — polynomial band-limited step (PolyBLEP) correction, as
 * popularised by Valimaki & Huovilainen ("Antialiasing Oscillators in
 * Subtractive Synthesis", IEEE Signal Processing Magazine, 2007) and widely
 * reproduced in synth DSP tutorials. Corrects the discontinuity a naive saw
 * or square introduces at each cycle edge, without needing lookup tables.
 *
 * `t`  — oscillator phase in [0, 1) at the sample being generated.
 * `dt` — phase increment per sample (frequency / sampleRate).
 */
inline float polyBlep(float t, float dt) {
  if (dt <= 0.0f) return 0.0f;
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

/** Band-limited sawtooth in [-1, 1], rising ramp, corrected at the wrap edge. */
inline float polyBlepSaw(float phase, float dt) {
  float value = 2.0f * phase - 1.0f;
  value -= polyBlep(phase, dt);
  return value;
}

/**
 * Band-limited pulse/square in [-1, 1], corrected at both edges.
 * `pulseWidth` in (0, 1); 0.5 = square.
 */
inline float polyBlepSquare(float phase, float dt, float pulseWidth = 0.5f) {
  float value = (phase < pulseWidth) ? 1.0f : -1.0f;
  value += polyBlep(phase, dt);

  float fallPhase = phase + (1.0f - pulseWidth);
  if (fallPhase >= 1.0f) fallPhase -= 1.0f;
  value -= polyBlep(fallPhase, dt);

  return value;
}

} // namespace rb338::dsp
