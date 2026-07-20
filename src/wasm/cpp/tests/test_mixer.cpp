#include "../engine/Mixer.h"
#include "../third_party/doctest.h"
#include <array>
#include <cmath>
#include <cstring>

using namespace rb338;

namespace {

constexpr uint32_t kFrames = 128;

std::array<float, kFrames> makeImpulse(uint32_t position = 0) {
  std::array<float, kFrames> buf{};
  if (position < kFrames) {
    buf[position] = 1.0f;
  }
  return buf;
}

std::array<float, kFrames> makeConstant(float value) {
  std::array<float, kFrames> buf{};
  buf.fill(value);
  return buf;
}

float peakChannel(const float* stereo, uint32_t frames, bool right) {
  float peak = 0.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    const float sample = stereo[i * 2 + (right ? 1 : 0)];
    if (std::isnan(sample) || std::isinf(sample)) {
      return -1.0f;
    }
    peak = std::max(peak, std::fabs(sample));
  }
  return peak;
}

} // namespace

TEST_CASE("Mixer: pan hard-left and hard-right route to one channel") {
  Mixer mixer;
  mixer.init(44100.0f);

  auto leftOnly = makeConstant(0.5f);
  const float* leftInputs[NUM_DEVICES] = {leftOnly.data(), nullptr, nullptr, nullptr};

  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.0f; // hard left
  mixer.setDeviceStates(devices);

  float leftOut[kFrames * 2] = {0};
  mixer.process(leftInputs, leftOut, kFrames, 120.0f);
  const float leftOnlyLeft = peakChannel(leftOut, kFrames, false);
  const float leftOnlyRight = peakChannel(leftOut, kFrames, true);
  CHECK(leftOnlyLeft > leftOnlyRight);
  CHECK(leftOnlyLeft > 0.0f);

  devices[0].pan = 1.0f; // hard right
  mixer.setDeviceStates(devices);

  float rightOut[kFrames * 2] = {0};
  mixer.process(leftInputs, rightOut, kFrames, 120.0f);
  const float rightOnlyLeft = peakChannel(rightOut, kFrames, false);
  const float rightOnlyRight = peakChannel(rightOut, kFrames, true);
  CHECK(rightOnlyRight > rightOnlyLeft);
  CHECK(rightOnlyRight > 0.0f);
}

TEST_CASE("Mixer: silence produces no NaNs or denormals") {
  Mixer mixer;
  mixer.init(44100.0f);

  std::array<DeviceState, NUM_DEVICES> devices{};
  for (int i = 0; i < NUM_DEVICES; ++i) {
    devices[i].id = static_cast<DeviceId>(i);
    devices[i].level = 1.0f;
    devices[i].dist = true;
    devices[i].compressor = true;
    devices[i].delaySend = 1.0f;
  }
  mixer.setDeviceStates(devices);

  auto silence = makeConstant(0.0f);
  const float* inputs[NUM_DEVICES] = {
      silence.data(), silence.data(), silence.data(), silence.data()};

  float out[kFrames * 2] = {0};
  mixer.process(inputs, out, kFrames, 120.0f);

  for (uint32_t i = 0; i < kFrames * 2; ++i) {
    CHECK_FALSE(std::isnan(out[i]));
    CHECK_FALSE(std::isinf(out[i]));
    CHECK(out[i] == doctest::Approx(0.0f).epsilon(1e-12));
  }
}

TEST_CASE("Mixer: distortion send changes the output waveform") {
  Mixer mixer;
  mixer.init(44100.0f);
  mixer.setDistortionEnabled(true);
  mixer.setDelayEnabled(false);
  mixer.setCompressorEnabled(false);

  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  devices[0].dist = false;
  mixer.setDeviceStates(devices);

  auto tone = makeConstant(0.6f);
  const float* inputs[NUM_DEVICES] = {tone.data(), nullptr, nullptr, nullptr};

  float dryOut[kFrames * 2] = {0};
  mixer.process(inputs, dryOut, kFrames, 120.0f);
  const float dryPeak = peakChannel(dryOut, kFrames, false);

  devices[0].dist = true;
  mixer.setDeviceStates(devices);
  float wetOut[kFrames * 2] = {0};
  mixer.process(inputs, wetOut, kFrames, 120.0f);
  const float wetPeak = peakChannel(wetOut, kFrames, false);

  CHECK(wetPeak != doctest::Approx(dryPeak).epsilon(0.01));
  CHECK(wetPeak > dryPeak);
}

TEST_CASE("Mixer: delay send produces energy after the tap time") {
  Mixer mixer;
  mixer.init(44100.0f);
  mixer.setDistortionEnabled(false);
  mixer.setCompressorEnabled(false);
  mixer.setDelayEnabled(true);

  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  devices[0].delaySend = 1.0f;
  mixer.setDeviceStates(devices);

  auto impulse = makeImpulse(0);

  // 120 BPM → one 16th = 5512.5 samples; process enough blocks to hear echo.
  const uint32_t delayTap =
      static_cast<uint32_t>(std::round((60.0f / 120.0f) * 0.25f * 44100.0f));
  const uint32_t totalFrames = delayTap + 256;

  float earlyPeak = 0.0f;
  float latePeak = 0.0f;

  std::array<float, 512> block{};
  float* stereo = block.data();
  uint32_t rendered = 0;
  while (rendered < totalFrames) {
    const uint32_t frames = std::min(kFrames, totalFrames - rendered);
    std::memset(stereo, 0, frames * 2 * sizeof(float));
    const float* blockInputs[NUM_DEVICES] = {nullptr, nullptr, nullptr, nullptr};
    if (rendered < kFrames) {
      blockInputs[0] = impulse.data();
    }
    mixer.process(blockInputs, stereo, frames, 120.0f);

    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t globalFrame = rendered + i;
      const float sample = std::fabs(stereo[i * 2]);
      if (globalFrame < 4) {
        earlyPeak = std::max(earlyPeak, sample);
      }
      if (globalFrame >= delayTap && globalFrame < delayTap + 64) {
        latePeak = std::max(latePeak, sample);
      }
    }
    rendered += frames;
  }

  CHECK(earlyPeak > 0.0f);
  CHECK(latePeak > 0.0f);
}

TEST_CASE("Mixer: feature flags disable heavy FX") {
  Mixer mixer;
  mixer.init(44100.0f);
  mixer.setDistortionEnabled(false);
  mixer.setDelayEnabled(false);
  mixer.setCompressorEnabled(false);

  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  devices[0].dist = true;
  devices[0].delaySend = 1.0f;
  mixer.setDeviceStates(devices);

  auto tone = makeConstant(0.8f);
  const float* inputs[NUM_DEVICES] = {tone.data(), nullptr, nullptr, nullptr};

  float out[kFrames * 2] = {0};
  mixer.process(inputs, out, kFrames, 120.0f);

  const float peak = peakChannel(out, kFrames, false);
  CHECK(peak == doctest::Approx(0.8f * 0.70710678f).epsilon(0.05));
}
