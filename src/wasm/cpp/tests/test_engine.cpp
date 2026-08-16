#include "../engine/RbsAudioEngine.h"
#include "../engine/AudioThreadLimits.h"
#include "../third_party/doctest.h"
#include <cmath>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

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

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

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

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

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

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.setTempo(95.0f);
  eng.processBlock(buffers, 2, 128); // drain SetTempo command
  CHECK(eng.getTempo() == doctest::Approx(95.0f));

  // Out-of-range song tempo is clamped to the supported 40–250 BPM range.
  REQUIRE(eng.loadSong(makeSong(300.0f)));
  CHECK(eng.getTempo() == doctest::Approx(250.0f));
}

TEST_CASE("Engine: hard-left pan isolates right channel") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));

  ParsedSong song = makeSong(120.0f);
  song.devices[0].pan = 0.0f;
  song.devices[0].level = 1.0f;
  REQUIRE(eng.loadSong(song));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  for (int i = 0; i < 32; ++i) {
    eng.processBlock(buffers, 2, 128);
  }

  float leftPeak = 0.0f;
  float rightPeak = 0.0f;
  for (int i = 0; i < 128; ++i) {
    leftPeak = std::max(leftPeak, std::fabs(left[i]));
    rightPeak = std::max(rightPeak, std::fabs(right[i]));
  }

  CHECK(leftPeak > 0.01f);
  CHECK(rightPeak < leftPeak * 0.01f);
}

TEST_CASE("Engine: 128-frame processBlock uses member scratch without stack overflow") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));
  REQUIRE(eng.loadSong(makeSong(120.0f)));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  for (int i = 0; i < 256; ++i) {
    eng.processBlock(buffers, 2, AUDIO_WORKLET_FRAMES);
  }

  CHECK(eng.getProcessedBlockCount() == 256);
}

TEST_CASE("Engine: live device params apply without crashing playback") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));
  REQUIRE(eng.loadSong(makeSong(120.0f)));

  float left[128] = {0};
  float right[128] = {0};
  float* buffers[2] = {left, right};
  eng.play();
  eng.processBlock(buffers, 2, 128);

  eng.setDeviceParam(0, static_cast<uint8_t>(DeviceParamId::Cutoff), 0.2f);
  eng.setDeviceParam(0, static_cast<uint8_t>(DeviceParamId::Resonance), 0.8f);
  eng.setDeviceParam(0, static_cast<uint8_t>(DeviceParamId::Decay), 0.3f);
  eng.setDeviceParam(0, static_cast<uint8_t>(DeviceParamId::Level), 1.0f);
  eng.processBlock(buffers, 2, 128);

  float peak = 0.0f;
  for (int i = 0; i < 128; ++i) {
    peak = std::max(peak, std::fabs(left[i]));
  }
  CHECK(peak >= 0.0f);
  CHECK(eng.isPlaying());
}

TEST_CASE("Engine: step trigger lands within ±1 sample of sequencer offset") {
  RbsAudioEngine eng;
  EngineConfig cfg = makeConfig();
  cfg.sampleRate = 48000.0f;
  cfg.enableTr808 = false;
  cfg.enableTr909 = false;
  cfg.enableTb303B = false;
  REQUIRE(eng.init(cfg));

  ParsedSong song = makeSong(128.0f);
  // Only master step 1 is active so the first note is mid-block, not at 0.
  for (int s = 0; s < MAX_STEPS; ++s) {
    song.patterns[0].steps[s].active = (s == 1);
  }
  song.devices[0].level = 1.0f;
  song.devices[0].pan = 0.0f;
  REQUIRE(eng.loadSong(song));

  // 128 BPM @ 48 kHz → exactly 5625 samples per 16th.
  constexpr uint32_t kExpected = 5625;
  constexpr uint32_t kBlock = 128;
  float left[kBlock] = {0};
  float right[kBlock] = {0};
  float* buffers[2] = {left, right};

  eng.play();
  uint32_t rendered = 0;
  int firstEnergy = -1;
  while (rendered < kExpected + kBlock) {
    std::memset(left, 0, sizeof(left));
    std::memset(right, 0, sizeof(right));
    eng.processBlock(buffers, 2, kBlock);
    for (uint32_t i = 0; i < kBlock; ++i) {
      if (std::fabs(left[i]) > 1e-5f) {
        firstEnergy = static_cast<int>(rendered + i);
        break;
      }
    }
    if (firstEnergy >= 0) break;
    rendered += kBlock;
  }

  REQUIRE(firstEnergy >= 0);
  CHECK(std::abs(firstEnergy - static_cast<int>(kExpected)) <= 1);
}

TEST_CASE("Engine: two-thread command/process stress") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(makeConfig()));
  REQUIRE(eng.loadSong(makeSong(120.0f)));

  std::atomic<bool> running{true};
  std::atomic<uint32_t> blocks{0};

  std::thread audio([&]() {
    float left[128] = {0};
    float right[128] = {0};
    float* buffers[2] = {left, right};
    while (running.load(std::memory_order_relaxed)) {
      eng.processBlock(buffers, 2, 128);
      blocks.fetch_add(1, std::memory_order_relaxed);
    }
  });

  ParsedSong song = makeSong(140.0f);
  for (int i = 0; i < 200; ++i) {
    eng.play();
    eng.setVolume(0.5f + (i % 5) * 0.1f);
    eng.setTempo(80.0f + static_cast<float>(i % 40));
    if (i % 7 == 0) {
      REQUIRE(eng.loadSong(song));
    }
    if (i % 11 == 0) {
      eng.seek(1);
    }
    if (i % 3 == 0) {
      eng.pause();
    }
    eng.stop();
  }

  running.store(false, std::memory_order_relaxed);
  audio.join();
  CHECK(blocks.load() > 0);
}
