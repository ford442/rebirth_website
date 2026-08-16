#include "../engine/RbsAudioEngine.h"
#include "../synth/DrumBitfield.h"
#include "../synth/Tr808Voice.h"
#include "../third_party/doctest.h"
#include <cmath>

using namespace rb338;

namespace {

float renderPeak(Tr808Voice& voice, uint32_t frames = 4096) {
  float peak = 0.0f;
  float buffer[512];
  uint32_t rendered = 0;
  while (rendered < frames) {
    const uint32_t block = std::min<uint32_t>(512, frames - rendered);
    voice.render(buffer, block);
    for (uint32_t i = 0; i < block; ++i) {
      peak = std::max(peak, std::fabs(buffer[i]));
    }
    rendered += block;
  }
  return peak;
}

ParsedSong makeDrumSong(DeviceId device, uint8_t hits, uint8_t extra = 0) {
  ParsedSong song;
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
    song.devices[i].level = 0.9f;
    song.devices[i].pan = 0.5f;
  }

  Pattern p;
  p.deviceId = device;
  p.bank = 0;
  p.patternIndex = 0;
  p.length = 16;
  p.steps[0].active = true;
  p.steps[0].note = hits;
  p.steps[0].drumExtra = extra;
  song.patterns.push_back(p);

  song.devices[static_cast<int>(device)].initialPatternBank = 0;
  song.devices[static_cast<int>(device)].initialPatternIndex = 0;
  return song;
}

EngineConfig drumsOnlyConfig() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  cfg.enableTb303A = false;
  cfg.enableTb303B = false;
  cfg.enableTr808 = true;
  cfg.enableTr909 = true;
  return cfg;
}

} // namespace

TEST_CASE("Tr808Voice: kick/snare/hat pattern produces audio") {
  Tr808Voice voice;
  voice.init(44100.0f);
  DeviceState state{};
  state.tune = 0.5f;
  state.decay = 0.5f;
  state.accent = 0.5f;
  voice.load(state, {});

  StepData step{};
  step.active = true;
  step.note = DrumHit::BD | DrumHit::SD | DrumHit::CH;
  voice.triggerStep(0, step);

  const float peak = renderPeak(voice);
  CHECK(peak > 0.01f);
}

TEST_CASE("Tr808Voice: rimshot and clap extra hits sound") {
  Tr808Voice voice;
  voice.init(44100.0f);
  DeviceState state{};
  voice.load(state, {});

  StepData step{};
  step.active = true;
  step.drumExtra = DrumExtra::RS | DrumExtra::CP;
  voice.triggerStep(0, step);

  const float peak = renderPeak(voice);
  CHECK(peak > 0.005f);
}

TEST_CASE("Engine: 808 drum pattern produces non-silent mix output") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(drumsOnlyConfig()));
  REQUIRE(eng.loadSong(makeDrumSong(DeviceId::TR808, DrumHit::BD | DrumHit::SD | DrumHit::CH)));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  float peak = 0.0f;
  for (int i = 0; i < 8; ++i) {
    eng.processBlock(buffers, 2, 128);
    for (int s = 0; s < 128; ++s) {
      peak = std::max(peak, std::fabs(left[s]));
      peak = std::max(peak, std::fabs(right[s]));
    }
  }

  CHECK(peak > 0.01f);
}

TEST_CASE("Engine: mixer honors device mute") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(drumsOnlyConfig()));

  ParsedSong song = makeDrumSong(DeviceId::TR808, DrumHit::BD);
  song.devices[static_cast<int>(DeviceId::TR808)].muted = true;
  REQUIRE(eng.loadSong(song));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  for (int i = 0; i < 8; ++i) {
    eng.processBlock(buffers, 2, 128);
  }

  float peak = 0.0f;
  for (int s = 0; s < 128; ++s) {
    peak = std::max(peak, std::fabs(left[s]));
    peak = std::max(peak, std::fabs(right[s]));
  }
  CHECK(peak == doctest::Approx(0.0f));
}

TEST_CASE("Engine: mixer pan shifts energy between channels") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(drumsOnlyConfig()));

  ParsedSong song = makeDrumSong(DeviceId::TR909, DrumHit::BD | DrumHit::SD);
  song.devices[static_cast<int>(DeviceId::TR909)].pan = 0.0f; // hard left
  song.devices[static_cast<int>(DeviceId::TR909)].level = 1.0f;
  REQUIRE(eng.loadSong(song));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  for (int i = 0; i < 16; ++i) {
    eng.processBlock(buffers, 2, 128);
  }

  float leftPeak = 0.0f;
  float rightPeak = 0.0f;
  for (int i = 0; i < 128; ++i) {
    leftPeak = std::max(leftPeak, std::fabs(left[i]));
    rightPeak = std::max(rightPeak, std::fabs(right[i]));
  }

  CHECK(leftPeak > rightPeak);
}
