#include "../engine/RbsAudioEngine.h"
#include "../third_party/doctest.h"
#include <array>
#include <cstdint>

using namespace rb338;

namespace {

ParsedSong makeSong(float bpm) {
  ParsedSong song;
  song.bpm = bpm;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
  }
  Pattern p;
  p.deviceId = DeviceId::TB303_A;
  p.bank = 0;
  p.patternIndex = 0;
  p.length = 16;
  for (int s = 0; s < MAX_STEPS; ++s) {
    p.steps[s].active = true;
    p.steps[s].note = 60;
  }
  song.patterns.push_back(p);
  song.devices[0].initialPatternBank = 0;
  song.devices[0].initialPatternIndex = 0;
  return song;
}

EngineConfig makeConfig() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  return cfg;
}

} // namespace

TEST_CASE("Engine: play advances the transport, stop returns to bar 1") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));
  REQUIRE(eng.loadSong(makeSong(120.0f)));

  float buffer[256] = {0};
  float* buffers[1] = {buffer};

  eng.play();
  // ~1200 * 128 = 153600 samples > one 88200-sample bar.
  for (int i = 0; i < 1200; ++i) {
    eng.processBlock(buffers, 2, 128);
  }

  uint16_t bar = 0;
  uint8_t step = 0;
  eng.getPlaybackPosition(bar, step);
  CHECK(bar > 1);
  CHECK(eng.isPlaying());

  eng.stop();
  eng.processBlock(buffers, 2, 128); // drain Stop command
  eng.getPlaybackPosition(bar, step);
  CHECK(bar == 1);
  CHECK(step == 0);
  CHECK_FALSE(eng.isPlaying());
}

TEST_CASE("Engine: seek moves to the requested bar") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));
  REQUIRE(eng.loadSong(makeSong(120.0f)));

  float buffer[256] = {0};
  float* buffers[1] = {buffer};

  eng.seek(4);
  eng.processBlock(buffers, 2, 128); // drain Seek command

  uint16_t bar = 0;
  uint8_t step = 0;
  eng.getPlaybackPosition(bar, step);
  CHECK(bar == 4);
  CHECK(step == 0);
}

TEST_CASE("Engine: getTempo reflects loaded song, setTempo, and clamping") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));

  REQUIRE(eng.loadSong(makeSong(140.0f)));
  CHECK(eng.getTempo() == doctest::Approx(140.0f));

  float buffer[256] = {0};
  float* buffers[1] = {buffer};

  eng.setTempo(95.0f);
  eng.processBlock(buffers, 2, 128); // drain SetTempo command
  CHECK(eng.getTempo() == doctest::Approx(95.0f));

  // Out-of-range song tempo is clamped to the supported 40–250 BPM range.
  REQUIRE(eng.loadSong(makeSong(300.0f)));
  CHECK(eng.getTempo() == doctest::Approx(250.0f));
}
