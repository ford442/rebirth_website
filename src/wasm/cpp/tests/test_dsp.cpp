#include "../synth/dsp/PolyBlep.h"
#include "../synth/dsp/ZdfLadder.h"
#include "../third_party/doctest.h"
#include <cmath>
#include <cstdint>

using namespace rb338::dsp;

namespace {

constexpr float kSampleRate = 44100.0f;

bool allFinite(const float* buf, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    if (!std::isfinite(buf[i])) return false;
  }
  return true;
}

} // namespace

TEST_CASE("ZdfLadder: impulse response is finite, decaying, and bounded") {
  ZdfLadder filter;
  filter.reset();
  filter.setCoeffs(1000.0f, 0.3f, kSampleRate);

  constexpr uint32_t kFrames = 4096;
  float out[kFrames];
  out[0] = filter.process(1.0f);
  for (uint32_t i = 1; i < kFrames; ++i) {
    out[i] = filter.process(0.0f);
  }

  REQUIRE(allFinite(out, kFrames));
  for (float v : out) {
    CHECK(std::fabs(v) < 1.0f);
  }
  // A low-pass impulse response should have died away well within 4096
  // samples (~93 ms) at a 1 kHz cutoff with light resonance.
  CHECK(std::fabs(out[kFrames - 1]) < 1e-4f);
}

TEST_CASE("ZdfLadder: step response settles without ringing away") {
  // Zero resonance isolates the plain tanh-saturated one-pole cascade: DC
  // gain is 1, so the settled value should sit near tanh(1).
  ZdfLadder filter;
  filter.reset();
  filter.setCoeffs(500.0f, 0.0f, kSampleRate);

  constexpr uint32_t kFrames = 8192;
  float out[kFrames];
  for (uint32_t i = 0; i < kFrames; ++i) {
    out[i] = filter.process(1.0f);
  }

  REQUIRE(allFinite(out, kFrames));
  for (float v : out) {
    CHECK(std::fabs(v) < 1.0f);
  }
  CHECK(out[kFrames - 1] > 0.65f); // tanh(1) ~= 0.7616
  CHECK(out[kFrames - 1] < 0.85f);
}

TEST_CASE("ZdfLadder: resonance sweep never produces NaN/Inf and stays bounded") {
  // Drive at the cutoff frequency so higher resonance produces a taller
  // resonant peak (measured on the steady-state half, past the turn-on
  // transient) — the audible "turning the reso knob raises the Q" effect.
  constexpr uint32_t kFrames = 8192;
  constexpr float kCutoffHz = 800.0f;
  float lowResPeak = 0.0f;
  float highResPeak = 0.0f;

  for (int step = 0; step <= 10; ++step) {
    const float resonanceNorm = static_cast<float>(step) / 10.0f;
    ZdfLadder filter;
    filter.reset();
    filter.setCoeffs(kCutoffHz, resonanceNorm, kSampleRate);

    float peak = 0.0f;
    for (uint32_t i = 0; i < kFrames; ++i) {
      const float t = static_cast<float>(i) / kSampleRate;
      const float input = 0.5f * std::sin(2.0f * 3.14159265358979323846f * kCutoffHz * t);
      const float y = filter.process(input);
      REQUIRE(std::isfinite(y));
      CHECK(std::fabs(y) < 1.0f); // tanh-bounded feedback path — never clips
      if (i > kFrames / 2) peak = std::max(peak, std::fabs(y));
    }

    if (step == 0) lowResPeak = peak;
    if (step == 10) highResPeak = peak;
  }

  // Turning the resonance knob up should audibly increase the peak/Q.
  CHECK(highResPeak > lowResPeak);
}

TEST_CASE("PolyBlep: band-limited saw stays within range and reduces edge energy") {
  constexpr uint32_t kFrames = 2048;
  const float freq = 2000.0f; // high enough that naive aliasing would be obvious
  const float dt = freq / kSampleRate;
  float phase = 0.0f;

  float maxAbsDelta = 0.0f;
  float prev = polyBlepSaw(phase, dt);
  for (uint32_t i = 1; i < kFrames; ++i) {
    phase += dt;
    if (phase >= 1.0f) phase -= 1.0f;
    const float v = polyBlepSaw(phase, dt);
    REQUIRE(std::isfinite(v));
    CHECK(v >= -1.2f);
    CHECK(v <= 1.2f);
    maxAbsDelta = std::max(maxAbsDelta, std::fabs(v - prev));
    prev = v;
  }
  // The BLEP-corrected wrap should be smoother than a full ±2 naive jump.
  CHECK(maxAbsDelta < 2.0f);
}

TEST_CASE("PolyBlep: band-limited square is finite and bipolar") {
  constexpr uint32_t kFrames = 1024;
  const float freq = 1500.0f;
  const float dt = freq / kSampleRate;
  float phase = 0.0f;
  bool sawPositive = false;
  bool sawNegative = false;

  for (uint32_t i = 0; i < kFrames; ++i) {
    const float v = polyBlepSquare(phase, dt);
    REQUIRE(std::isfinite(v));
    if (v > 0.5f) sawPositive = true;
    if (v < -0.5f) sawNegative = true;
    phase += dt;
    if (phase >= 1.0f) phase -= 1.0f;
  }

  CHECK(sawPositive);
  CHECK(sawNegative);
}
