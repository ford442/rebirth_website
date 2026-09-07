#pragma once

#include <cstdint>

namespace rb338::dsp {

/**
 * hermite4 — 4-point, 3rd-order Hermite interpolation.
 *
 * The standard formulation from the public DSP literature (Laurent de Soras'
 * "Polynomial Interpolators for High-Quality Resampling of Oversampled
 * Audio"). Cheap enough for a per-sample inner loop and far smoother than
 * linear interpolation when a mod sample is pitched by the TUNE knob.
 *
 * `frac` is the position between x0 and x1 in [0, 1).
 */
inline float hermite4(float frac, float xm1, float x0, float x1, float x2) {
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b = w + a;
  return ((((a * frac) - b) * frac + c) * frac + x0);
}

/**
 * Read `buffer` at a fractional position with Hermite interpolation.
 *
 * Out-of-range neighbours clamp to the endpoints, so a one-shot sample
 * decays into its final value rather than wrapping or reading out of bounds.
 */
inline float sampleAtClamped(const float* buffer, uint32_t frames, uint32_t index, float frac) {
  if (!buffer || frames == 0) return 0.0f;
  const uint32_t last = frames - 1;
  const uint32_t i0 = (index > last) ? last : index;
  const uint32_t im1 = (i0 == 0) ? 0 : (i0 - 1);
  const uint32_t i1 = (i0 + 1 > last) ? last : (i0 + 1);
  const uint32_t i2 = (i0 + 2 > last) ? last : (i0 + 2);
  return hermite4(frac, buffer[im1], buffer[i0], buffer[i1], buffer[i2]);
}

/**
 * Read `buffer` at a fractional position, wrapping at the ends.
 *
 * Used for single-cycle 303 wavetables, where the waveform must loop
 * seamlessly rather than decay.
 */
inline float sampleAtWrapped(const float* buffer, uint32_t frames, uint32_t index, float frac) {
  if (!buffer || frames == 0) return 0.0f;
  const uint32_t i0 = index % frames;
  const uint32_t im1 = (i0 == 0) ? (frames - 1) : (i0 - 1);
  const uint32_t i1 = (i0 + 1) % frames;
  const uint32_t i2 = (i0 + 2) % frames;
  return hermite4(frac, buffer[im1], buffer[i0], buffer[i1], buffer[i2]);
}

} // namespace rb338::dsp
