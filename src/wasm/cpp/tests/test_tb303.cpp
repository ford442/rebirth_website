#include "../engine/RbsAudioEngine.h"
#include "../synth/Tb303Voice.h"
#include "../third_party/doctest.h"
#include <cmath>
#include <cstdint>

using namespace rb338;

namespace {

float renderPeak(Tb303Voice& voice, uint32_t frames = 4096) {
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

DeviceState default303State() {
  DeviceState state{};
  state.cutoff = 0.65f;
  state.resonance = 0.45f;
  state.envMod = 0.55f;
  state.decay = 0.5f;
  state.accent = 0.7f;
  state.waveform = 0;
  return state;
}

StepData activeNote(uint8_t midi, bool accent = false, bool slide = false) {
  StepData step{};
  step.active = true;
  step.note = midi;
  step.accent = accent;
  step.slide = slide;
  return step;
}

ParsedSong make303Song() {
  ParsedSong song;
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
    song.devices[i].level = 0.9f;
    song.devices[i].pan = 0.5f;
  }

  Pattern p;
  p.deviceId = DeviceId::TB303_A;
  p.bank = 0;
  p.patternIndex = 0;
  p.length = 16;
  for (int s = 0; s < 16; ++s) {
    p.steps[s] = activeNote(static_cast<uint8_t>(48 + (s % 5) * 2));
    p.steps[s].accent = (s % 4 == 0);
    if (s > 0 && s % 3 == 0) {
      p.steps[static_cast<size_t>(s - 1)].slide = true;
    }
  }
  song.patterns.push_back(p);
  song.devices[0].initialPatternBank = 0;
  song.devices[0].initialPatternIndex = 0;
  return song;
}

EngineConfig tb303OnlyConfig() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  cfg.enableTb303A = true;
  cfg.enableTb303B = false;
  cfg.enableTr808 = false;
  cfg.enableTr909 = false;
  return cfg;
}

} // namespace

TEST_CASE("Tb303Voice: triggered note produces audible output") {
  Tb303Voice voice;
  voice.init(44100.0f);
  voice.load(default303State(), {});

  voice.triggerStep(0, activeNote(48));
  const float peak = renderPeak(voice);
  CHECK(peak > 0.01f);
}

TEST_CASE("Tb303Voice: square waveform produces output") {
  Tb303Voice voice;
  voice.init(44100.0f);
  DeviceState state = default303State();
  state.waveform = 1;
  voice.load(state, {});

  voice.triggerStep(0, activeNote(52));
  CHECK(renderPeak(voice) > 0.01f);
}

TEST_CASE("Tb303Voice: accent is louder than non-accent") {
  Tb303Voice voice;
  voice.init(44100.0f);
  voice.load(default303State(), {});

  voice.triggerStep(0, activeNote(48, false));
  const float normalPeak = renderPeak(voice, 2048);

  voice.reset();
  voice.triggerStep(0, activeNote(48, true));
  const float accentPeak = renderPeak(voice, 512);

  CHECK(accentPeak > normalPeak);
}

TEST_CASE("Tb303Voice: slide chain keeps sound alive across two notes") {
  Tb303Voice voice;
  voice.init(44100.0f);
  voice.load(default303State(), {});

  voice.triggerStep(0, activeNote(48, false, true));
  renderPeak(voice, 2205);

  voice.triggerStep(1, activeNote(55, false, false));
  const float afterSlidePeak = renderPeak(voice, 2205);
  CHECK(afterSlidePeak > 0.005f);
}

TEST_CASE("Engine: TB-303 pattern produces non-silent audio") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(tb303OnlyConfig()));
  REQUIRE(eng.loadSong(make303Song()));

  float buffer[256] = {0};
  float* buffers[1] = {buffer};

  eng.play();
  float peak = 0.0f;
  for (int i = 0; i < 200; ++i) {
    eng.processBlock(buffers, 2, 128);
    for (int s = 0; s < 256; ++s) {
      peak = std::max(peak, std::fabs(buffer[s]));
    }
  }
  CHECK(peak > 0.01f);
}
