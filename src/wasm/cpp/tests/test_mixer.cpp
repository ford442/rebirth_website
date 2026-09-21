#include "../engine/Mixer.h"
#include "../third_party/doctest.h"
#include <array>
#include <chrono>
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

float peakPlanar(const float* channel, uint32_t frames) {
  float peak = 0.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    const float sample = channel[i];
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

  float leftOut[kFrames] = {0};
  float rightOut[kFrames] = {0};
  mixer.process(leftInputs, leftOut, rightOut, kFrames, 120.0f);
  const float leftOnlyLeft = peakPlanar(leftOut, kFrames);
  const float leftOnlyRight = peakPlanar(rightOut, kFrames);
  CHECK(leftOnlyLeft > leftOnlyRight);
  CHECK(leftOnlyLeft > 0.0f);

  devices[0].pan = 1.0f; // hard right
  mixer.setDeviceStates(devices);

  std::memset(leftOut, 0, sizeof(leftOut));
  std::memset(rightOut, 0, sizeof(rightOut));
  mixer.process(leftInputs, leftOut, rightOut, kFrames, 120.0f);
  const float rightOnlyLeft = peakPlanar(leftOut, kFrames);
  const float rightOnlyRight = peakPlanar(rightOut, kFrames);
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

  float leftOut[kFrames] = {0};
  float rightOut[kFrames] = {0};
  mixer.process(inputs, leftOut, rightOut, kFrames, 120.0f);

  for (uint32_t i = 0; i < kFrames; ++i) {
    CHECK_FALSE(std::isnan(leftOut[i]));
    CHECK_FALSE(std::isinf(leftOut[i]));
    CHECK(leftOut[i] == doctest::Approx(0.0f).epsilon(1e-12));
    CHECK_FALSE(std::isnan(rightOut[i]));
    CHECK_FALSE(std::isinf(rightOut[i]));
    CHECK(rightOut[i] == doctest::Approx(0.0f).epsilon(1e-12));
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

  float dryLeft[kFrames] = {0};
  float dryRight[kFrames] = {0};
  mixer.process(inputs, dryLeft, dryRight, kFrames, 120.0f);
  const float dryPeak = peakPlanar(dryLeft, kFrames);

  devices[0].dist = true;
  mixer.setDeviceStates(devices);
  float wetLeft[kFrames] = {0};
  float wetRight[kFrames] = {0};
  mixer.process(inputs, wetLeft, wetRight, kFrames, 120.0f);
  const float wetPeak = peakPlanar(wetLeft, kFrames);

  CHECK(wetPeak != doctest::Approx(dryPeak).epsilon(0.01));
  CHECK(wetPeak > dryPeak);
}

TEST_CASE("Mixer: PCF send changes the output waveform") {
  Mixer mixer;
  mixer.init(44100.0f);
  mixer.setDistortionEnabled(false);
  mixer.setDelayEnabled(false);
  mixer.setCompressorEnabled(false);

  SongFxSettings fx{};
  fx.pcf.enabled = true;
  fx.pcf.cutoff = 8;
  fx.pcf.resonance = 8;
  mixer.setSongFx(fx);

  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  devices[0].pcf = false;
  mixer.setDeviceStates(devices);

  auto tone = makeConstant(0.6f);
  const float* inputs[NUM_DEVICES] = {tone.data(), nullptr, nullptr, nullptr};
  float dryLeft[kFrames] = {0};
  float dryRight[kFrames] = {0};
  mixer.process(inputs, dryLeft, dryRight, kFrames, 120.0f);
  const float dryPeak = peakPlanar(dryLeft, kFrames);

  devices[0].pcf = true;
  mixer.setDeviceStates(devices);
  float wetLeft[kFrames] = {0};
  float wetRight[kFrames] = {0};
  mixer.process(inputs, wetLeft, wetRight, kFrames, 120.0f);
  const float wetPeak = peakPlanar(wetLeft, kFrames);

  CHECK(wetPeak != doctest::Approx(dryPeak).epsilon(0.01));
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

  std::array<float, 512> leftBlock{};
  std::array<float, 512> rightBlock{};
  uint32_t rendered = 0;
  while (rendered < totalFrames) {
    const uint32_t frames = std::min(kFrames, totalFrames - rendered);
    std::memset(leftBlock.data(), 0, frames * sizeof(float));
    std::memset(rightBlock.data(), 0, frames * sizeof(float));
    const float* blockInputs[NUM_DEVICES] = {nullptr, nullptr, nullptr, nullptr};
    if (rendered < kFrames) {
      blockInputs[0] = impulse.data();
    }
    mixer.process(blockInputs, leftBlock.data(), rightBlock.data(), frames, 120.0f);

    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t globalFrame = rendered + i;
      const float sample = std::fabs(leftBlock[i]);
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

  float leftOut[kFrames] = {0};
  float rightOut[kFrames] = {0};
  mixer.process(inputs, leftOut, rightOut, kFrames, 120.0f);

  const float peak = peakPlanar(leftOut, kFrames);
  CHECK(peak == doctest::Approx(0.8f * 0.70710678f).epsilon(0.05));
}

TEST_CASE("Mixer: scalar process benchmark (smoke)") {
  Mixer mixer;
  mixer.init(44100.0f);
  std::array<DeviceState, NUM_DEVICES> devices{};
  for (int i = 0; i < NUM_DEVICES; ++i) {
    devices[static_cast<size_t>(i)].level = 0.8f;
    devices[static_cast<size_t>(i)].pan = 0.5f;
  }
  mixer.setDeviceStates(devices);

  auto tone = makeConstant(0.2f);
  const float* inputs[NUM_DEVICES] = {tone.data(), tone.data(), tone.data(), tone.data()};
  float leftOut[kFrames] = {0};
  float rightOut[kFrames] = {0};

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 4000; ++i) {
    mixer.process(inputs, leftOut, rightOut, kFrames, 140.0f);
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  MESSAGE("Mixer 4000x128 frames: ", us, " us");
  CHECK(peakPlanar(leftOut, kFrames) >= 0.0f);
}

// ─────────────────────────────────────────────────────────────────
// Song-FX fidelity: every byte the parser reads must reach the DSP.
// ─────────────────────────────────────────────────────────────────

namespace {

/** RMS of a planar channel — the yardstick for "this FX did something". */
float rmsPlanar(const float* channel, uint32_t frames) {
  double sum = 0.0;
  for (uint32_t i = 0; i < frames; ++i) {
    sum += static_cast<double>(channel[i]) * channel[i];
  }
  return static_cast<float>(std::sqrt(sum / frames));
}

/** A device wired into every send, so one song's FX bytes decide the sound. */
std::array<DeviceState, NUM_DEVICES> allSendsOn() {
  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].id = DeviceId::TB303_A;
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  devices[0].dist = true;
  devices[0].compressor = true;
  devices[0].pcf = true;
  devices[0].delaySend = 1.0f;
  return devices;
}

/** A few seconds of a loud square wave — enough to trip every FX stage. */
std::vector<float> makeSquare(uint32_t frames, float periodFrames, float amplitude) {
  std::vector<float> buf(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    const float phase = std::fmod(static_cast<float>(i), periodFrames) / periodFrames;
    buf[i] = (phase < 0.5f) ? amplitude : -amplitude;
  }
  return buf;
}

/** Render `input` through a mixer configured with `fx`, in 128-frame blocks. */
std::vector<float> renderWithFx(const SongFxSettings& fx, const std::vector<float>& input,
                                float bpm) {
  Mixer mixer;
  mixer.init(44100.0f);
  mixer.setDeviceStates(allSendsOn());
  mixer.setSongFx(fx);

  std::vector<float> left(input.size(), 0.0f);
  std::vector<float> right(input.size(), 0.0f);
  for (size_t offset = 0; offset < input.size(); offset += kFrames) {
    const uint32_t block =
        static_cast<uint32_t>(std::min<size_t>(kFrames, input.size() - offset));
    const float* buffers[NUM_DEVICES] = {input.data() + offset, nullptr, nullptr, nullptr};
    mixer.process(buffers, left.data() + offset, right.data() + offset, block, bpm);
  }
  return left;
}

SongFxSettings allFxOff() {
  SongFxSettings fx{};
  fx.masterLevel = 127;
  fx.delay.enabled = false;
  fx.dist.enabled = false;
  fx.comp.enabled = false;
  fx.pcf.enabled = false;
  return fx;
}

} // namespace

TEST_CASE("Mixer: a song with DIST/PCF/COMP enabled differs from all-FX-off") {
  const auto input = makeSquare(44100, 120.0f, 0.6f);

  SongFxSettings on = allFxOff();
  on.dist.enabled = true;
  on.dist.drive = 96;
  on.dist.mix = 80;
  on.pcf.enabled = true;
  on.pcf.cutoff = 40;
  on.pcf.resonance = 100;
  on.pcf.envAmount = 90;
  on.comp.enabled = true;
  on.comp.threshold = 20;
  on.comp.ratio = 120;
  on.comp.attack = 0;

  const auto dry = renderWithFx(allFxOff(), input, 120.0f);
  const auto wet = renderWithFx(on, input, 120.0f);
  REQUIRE(dry.size() == wet.size());

  std::vector<float> difference(dry.size());
  for (size_t i = 0; i < dry.size(); ++i) {
    difference[i] = wet[i] - dry[i];
  }

  const float dryRms = rmsPlanar(dry.data(), static_cast<uint32_t>(dry.size()));
  const float diffRms =
      rmsPlanar(difference.data(), static_cast<uint32_t>(difference.size()));
  CHECK(dryRms > 0.01f);
  // Far above any rounding noise: the FX chain must be plainly audible.
  CHECK(diffRms > dryRms * 0.1f);
  CHECK(peakPlanar(wet.data(), static_cast<uint32_t>(wet.size())) >= 0.0f);
}

TEST_CASE("Mixer: PCF envAmount changes the sound") {
  const auto input = makeSquare(22050, 200.0f, 0.7f);

  SongFxSettings base = allFxOff();
  base.pcf.enabled = true;
  base.pcf.cutoff = 20;
  base.pcf.resonance = 64;
  base.pcf.envAmount = 0;

  SongFxSettings swept = base;
  swept.pcf.envAmount = 127;

  const auto still = renderWithFx(base, input, 120.0f);
  const auto moving = renderWithFx(swept, input, 120.0f);

  // A closed filter opened by the envelope passes strictly more energy.
  CHECK(rmsPlanar(moving.data(), static_cast<uint32_t>(moving.size())) >
        rmsPlanar(still.data(), static_cast<uint32_t>(still.size())) * 1.05f);
}

TEST_CASE("Mixer: compressor ratio and attack bytes reach the DSP") {
  const auto input = makeSquare(22050, 150.0f, 0.9f);

  SongFxSettings gentle = allFxOff();
  gentle.comp.enabled = true;
  gentle.comp.threshold = 10;
  gentle.comp.ratio = 0;   // ~1.5:1
  gentle.comp.attack = 0;  // fastest

  SongFxSettings hard = gentle;
  hard.comp.ratio = 127; // ~12:1

  const auto gentleOut = renderWithFx(gentle, input, 120.0f);
  const auto hardOut = renderWithFx(hard, input, 120.0f);
  CHECK(rmsPlanar(hardOut.data(), static_cast<uint32_t>(hardOut.size())) <
        rmsPlanar(gentleOut.data(), static_cast<uint32_t>(gentleOut.size())));

  SongFxSettings slow = gentle;
  slow.comp.attack = 127; // ~50 ms — transients pass before gain reduction
  const auto slowOut = renderWithFx(slow, input, 120.0f);
  CHECK(rmsPlanar(slowOut.data(), static_cast<uint32_t>(slowOut.size())) >
        rmsPlanar(gentleOut.data(), static_cast<uint32_t>(gentleOut.size())));
}

TEST_CASE("Mixer: a dotted-eighth tap at 80 BPM fits the delay line") {
  // (60/80) * 0.75 = 0.5625 s = 24806 samples @ 44.1 kHz — past the old
  // 20000-sample ceiling, which used to silently truncate the tap.
  const uint32_t tapSamples = 24806;
  const uint32_t frames = tapSamples + 8192;

  SongFxSettings fx = allFxOff();
  fx.delay.enabled = true;
  fx.delay.time = 100; // dotted eighth
  fx.delay.feedback = 90;
  fx.delay.wet = 127;

  std::vector<float> input(frames, 0.0f);
  input[0] = 1.0f; // single impulse into the delay send

  const auto out = renderWithFx(fx, input, 80.0f);

  // Nothing between the impulse and the tap...
  float beforeTap = 0.0f;
  for (uint32_t i = 64; i < tapSamples - 512; ++i) {
    beforeTap = std::max(beforeTap, std::fabs(out[i]));
  }
  CHECK(beforeTap < 1e-4f);

  // ...and a clear echo around it.
  float atTap = 0.0f;
  for (uint32_t i = tapSamples - 256; i < tapSamples + 256 && i < frames; ++i) {
    atTap = std::max(atTap, std::fabs(out[i]));
  }
  CHECK(atTap > 0.1f);
}

TEST_CASE("Mixer: the DELY time byte selects different subdivisions") {
  const uint32_t frames = 44100;
  std::vector<float> input(frames, 0.0f);
  input[0] = 1.0f;

  auto firstEchoIndex = [&](uint8_t time) {
    SongFxSettings fx = allFxOff();
    fx.delay.enabled = true;
    fx.delay.time = time;
    fx.delay.feedback = 0;
    fx.delay.wet = 127;
    const auto out = renderWithFx(fx, input, 120.0f);
    for (uint32_t i = 64; i < frames; ++i) {
      if (std::fabs(out[i]) > 0.1f) return i;
    }
    return frames;
  };

  const uint32_t sixteenth = firstEchoIndex(0);
  const uint32_t quarter = firstEchoIndex(127);
  CHECK(sixteenth < frames);
  CHECK(quarter < frames);
  CHECK(quarter > sixteenth * 3);
}
