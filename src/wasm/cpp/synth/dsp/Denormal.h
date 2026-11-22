#pragma once

#include <cmath>

namespace rb338::dsp {

/**
 * Flush a decaying value to zero well before it reaches the subnormal range.
 *
 * WebAssembly has no FTZ/DAZ mode, so an exponential tail (an envelope after
 * note-off, a filter ringing out, a feedback state) keeps shrinking until it
 * is subnormal, and every operation on a subnormal float is many times
 * slower on most CPUs. 1e-15 is ~-300 dB: far below anything audible and far
 * above FLT_MIN (~1.2e-38).
 */
constexpr float kDenormalFloor = 1e-15f;

inline float flushDenormal(float x) {
  return std::fabs(x) < kDenormalFloor ? 0.0f : x;
}

/**
 * How many samples between control-rate coefficient updates (filter cutoff
 * from the envelope, PCF sweep): 16 samples is 0.36 ms at 44.1 kHz — inaudible
 * as stepping, and it takes a pow + tan (or exp2 + tan) off every sample.
 */
constexpr unsigned kControlRateMask = 15u;

} // namespace rb338::dsp
