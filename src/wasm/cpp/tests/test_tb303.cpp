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

TEST_CASE("Tb303Voice: resonance knob audibly changes filter Q") {
  // Fix the cutoff (envMod = 0) near the note's fundamental so the sweep
  // isolates Q rather than the envelope's own cutoff movement, and measure
  // past the filter's brief turn-on transient.
  auto peakForResonance = [](float resonance) {
    Tb303Voice voice;
    voice.init(44100.0f);
    DeviceState state = default303State();
    state.cutoff = 0.5f;   // ~980 Hz corner
    state.resonance = resonance;
    state.envMod = 0.0f;
    state.decay = 0.9f;    // envelope stays near 1 across the measurement window
    state.accent = 0.0f;
    voice.load(state, {});
    voice.triggerStep(0, activeNote(83, false)); // ~988 Hz, near the fixed cutoff

    constexpr uint32_t kSkip = 2205;    // 50 ms settle
    constexpr uint32_t kMeasure = 8820; // 200 ms measurement window
    float buffer[512];
    uint32_t rendered = 0;
    float peak = 0.0f;
    while (rendered < kSkip + kMeasure) {
      const uint32_t block = std::min<uint32_t>(512, kSkip + kMeasure - rendered);
      voice.render(buffer, block);
      for (uint32_t i = 0; i < block; ++i) {
        if (rendered + i >= kSkip) peak = std::max(peak, std::fabs(buffer[i]));
      }
      rendered += block;
    }
    return peak;
  };

  const float lowResPeak = peakForResonance(0.0f);
  const float highResPeak = peakForResonance(1.0f);
  CHECK(std::isfinite(lowResPeak));
  CHECK(std::isfinite(highResPeak));
  CHECK(highResPeak > lowResPeak);
}

TEST_CASE("Tb303Voice: tune knob spans a wider range than +/-1 semitone") {
  // Estimate fundamental frequency via zero-crossing rate over a fixed
  // window; a +/-1-semitone range (the old MVP behaviour) would produce a
  // barely-measurable difference between tune=0 and tune=1.
  auto zeroCrossingHz = [](float tune) {
    Tb303Voice voice;
    voice.init(44100.0f);
    DeviceState state = default303State();
    state.tune = tune;
    state.waveform = 1; // square: unambiguous zero crossings
    voice.load(state, {});
    voice.triggerStep(0, activeNote(57));

    constexpr uint32_t kFrames = 44100 / 2;
    float buffer[512];
    uint32_t rendered = 0;
    uint32_t crossings = 0;
    float prev = 0.0f;
    bool havePrev = false;
    while (rendered < kFrames) {
      const uint32_t block = std::min<uint32_t>(512, kFrames - rendered);
      voice.render(buffer, block);
      for (uint32_t i = 0; i < block; ++i) {
        if (havePrev && ((prev < 0.0f) != (buffer[i] < 0.0f))) ++crossings;
        prev = buffer[i];
        havePrev = true;
      }
      rendered += block;
    }
    return static_cast<float>(crossings) / 2.0f / (static_cast<float>(kFrames) / 44100.0f);
  };

  const float lowHz = zeroCrossingHz(0.0f);
  const float highHz = zeroCrossingHz(1.0f);
  CHECK(std::isfinite(lowHz));
  CHECK(std::isfinite(highHz));
  // +/-1 semitone total range would be < 6% low-to-high; require noticeably more.
  CHECK(highHz > lowHz * 1.5f);
}

TEST_CASE("Tb303Voice: extreme knob settings never produce NaN or runaway output") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(tb303OnlyConfig()));
  REQUIRE(eng.loadSong(make303Song()));

  eng.setDeviceParam(0, 1, 1.0f); // cutoff = max
  eng.setDeviceParam(0, 2, 1.0f); // resonance = max (near self-oscillation)
  eng.setDeviceParam(0, 3, 1.0f); // envMod = max
  eng.setDeviceParam(0, 4, 0.0f); // decay = min (fastest sweep)
  eng.setDeviceParam(0, 5, 1.0f); // accent = max

  eng.play();
  float peak = 0.0f;
  for (int i = 0; i < 400; ++i) {
    const float blockPeak = eng.renderTestBlock(128);
    REQUIRE(std::isfinite(blockPeak));
    peak = std::max(peak, blockPeak);
  }
  CHECK(peak <= 1.0f);
}

TEST_CASE("Engine: TB-303 pattern produces non-silent audio") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(tb303OnlyConfig()));
  REQUIRE(eng.loadSong(make303Song()));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  float peak = 0.0f;
  for (int i = 0; i < 200; ++i) {
    eng.processBlock(buffers, 2, 128);
    for (int s = 0; s < 128; ++s) {
      peak = std::max(peak, std::fabs(left[s]));
      peak = std::max(peak, std::fabs(right[s]));
    }
  }
  CHECK(peak > 0.01f);
}
