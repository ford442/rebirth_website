#include "../engine/Sequencer.h"
#include "../engine/AutomationScheduler.h"
#include "../third_party/doctest.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "../parser/RbsParser.h"

using namespace rb338;

namespace {

// Build a pattern with a single active step (used to probe timing / selection).
Pattern makePattern(DeviceId dev, uint8_t bank, uint8_t index, uint8_t length,
                    std::initializer_list<uint8_t> activeSteps) {
  Pattern p;
  p.deviceId = dev;
  p.bank = bank;
  p.patternIndex = index;
  p.length = length;
  for (uint8_t s : activeSteps) {
    if (s < MAX_STEPS) {
      p.steps[s].active = true;
      p.steps[s].note = 60;
    }
  }
  return p;
}

ParsedSong makeSong() {
  ParsedSong song;
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
  }
  return song;
}

ParsedSong parseFixture(const char* name) {
  const std::string path = std::string("src/wasm/test-fixtures/") + name;
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open fixture: " + path);
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(size);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error(parser.lastError());
  return *song;
}

// Feed the sequencer one frame at a time, recording the cumulative frame index
// at which the transport first reaches bars 2 and 3.
void barBoundaries(Sequencer& seq, const ParsedSong& song, float bpm,
                   float sampleRate, long maxFrames, long& idx2, long& idx3) {
  Sequencer::Event ev[64];
  uint16_t bar = 0;
  uint8_t step = 0;
  idx2 = -1;
  idx3 = -1;
  for (long f = 1; f <= maxFrames; ++f) {
    seq.generateEvents(&song, bpm, sampleRate, 1, ev, 64);
    seq.getPosition(bar, step);
    if (bar >= 2 && idx2 < 0) idx2 = f;
    if (bar >= 3 && idx3 < 0) { idx3 = f; return; }
  }
}

} // namespace

TEST_CASE("Sequencer: bar is ~2s at 120 BPM / 44.1 kHz (within 1 ms)") {
  ParsedSong song = makeSong();
  song.patterns.push_back(makePattern(DeviceId::TB303_A, 0, 0, 16, {0, 4, 8, 12}));

  Sequencer seq;
  seq.setPosition(1, 0);

  const float sr = 44100.0f;
  const long expected = 88200; // 16 * (60/120)*0.25*44100
  const long tolerance = 44;   // ~1 ms at 44.1 kHz

  long idx2 = -1, idx3 = -1;
  barBoundaries(seq, song, 120.0f, sr, 400000, idx2, idx3);
  REQUIRE(idx2 > 0);
  REQUIRE(idx3 > 0);
  CHECK(std::labs(idx2 - expected) <= tolerance);
  // Second bar should take the same number of samples (no drift).
  CHECK(std::labs((idx3 - idx2) - expected) <= tolerance);
}

TEST_CASE("Sequencer: bar timing scales with tempo (140 BPM)") {
  ParsedSong song = makeSong();
  song.bpm = 140.0f;
  song.patterns.push_back(makePattern(DeviceId::TB303_A, 0, 0, 16, {0}));

  Sequencer seq;
  seq.setPosition(1, 0);

  const float sr = 44100.0f;
  // 16 * (60/140)*0.25*44100 = 75600 samples.
  const long expected = 75600;
  long idx2 = -1, idx3 = -1;
  barBoundaries(seq, song, 140.0f, sr, 400000, idx2, idx3);
  REQUIRE(idx2 > 0);
  CHECK(std::labs(idx2 - expected) <= 44);
}

TEST_CASE("Sequencer: seek sets position and resets intra-step phase") {
  Sequencer seq;
  seq.setPosition(5, 7);

  uint16_t bar = 0;
  uint8_t step = 0;
  seq.getPosition(bar, step);
  CHECK(bar == 5);
  CHECK(step == 7);

  // Bar clamps to >= 1; step wraps modulo 16.
  seq.setPosition(0, 20);
  seq.getPosition(bar, step);
  CHECK(bar == 1);
  CHECK(step == 20 % 16);
}

TEST_CASE("Sequencer: honors per-device pattern length (loops within the bar)") {
  ParsedSong song = makeSong();
  // A length-4 pattern with only local step 0 active should fire at master
  // steps 0, 4, 8, 12 within a 16-step bar.
  song.patterns.push_back(makePattern(DeviceId::TB303_A, 0, 0, 4, {0}));
  song.devices[0].initialPatternBank = 0;
  song.devices[0].initialPatternIndex = 0;

  Sequencer seq;
  seq.setPosition(1, 0);

  Sequencer::Event ev[64];
  std::vector<int> firedMasterSteps;
  uint16_t bar = 0;
  uint8_t step = 0;

  // Feed frame-by-frame for just under one bar (88200 samples) so the first
  // fire of bar 2 (master step 0 again) is not counted.
  for (long f = 1; f <= 80000; ++f) {
    uint32_t n = seq.generateEvents(&song, 120.0f, 44100.0f, 1, ev, 64);
    // Position after the call reflects the master step of any emitted event.
    seq.getPosition(bar, step);
    for (uint32_t i = 0; i < n; ++i) {
      if (ev[i].device == DeviceId::TB303_A) {
        firedMasterSteps.push_back(step);
        // Device-local index must stay within the pattern length.
        CHECK(ev[i].stepIndex < 4);
      }
    }
  }

  REQUIRE(firedMasterSteps.size() == 4);
  CHECK(firedMasterSteps[0] == 0);
  CHECK(firedMasterSteps[1] == 4);
  CHECK(firedMasterSteps[2] == 8);
  CHECK(firedMasterSteps[3] == 12);
}

TEST_CASE("Sequencer: arrangement drives per-bar pattern selection") {
  ParsedSong song = makeSong();
  // Two full-length patterns: idx0 fires on step 0, idx1 fires on step 8.
  song.patterns.push_back(makePattern(DeviceId::TB303_A, 0, 0, 16, {0}));
  song.patterns.push_back(makePattern(DeviceId::TB303_A, 0, 1, 16, {8}));

  ArrangementBar b1;
  b1.barNumber = 1;
  b1.devicePatterns[0] = PatternRef{0, 0}; // bar 1 -> pattern idx0
  ArrangementBar b2;
  b2.barNumber = 2;
  b2.devicePatterns[0] = PatternRef{0, 1}; // bar 2 -> pattern idx1
  song.arrangement.push_back(b1);
  song.arrangement.push_back(b2);

  Sequencer seq;
  seq.setPosition(1, 0);

  Sequencer::Event ev[64];
  std::vector<std::pair<int, int>> fired; // (bar, masterStep)
  uint16_t bar = 0;
  uint8_t step = 0;

  for (long f = 1; f <= 200000; ++f) {
    uint32_t n = seq.generateEvents(&song, 120.0f, 44100.0f, 1, ev, 64);
    // Position after the call reflects the bar + master step of the emission.
    seq.getPosition(bar, step);
    for (uint32_t i = 0; i < n; ++i) {
      if (ev[i].device == DeviceId::TB303_A) {
        fired.emplace_back(bar, step);
      }
    }
    if (bar >= 3) break;
  }

  REQUIRE(fired.size() >= 2);
  // Bar 1 fires on step 0 (pattern idx0); bar 2 fires on step 8 (pattern idx1).
  CHECK(fired[0].first == 1);
  CHECK(fired[0].second == 0);
  CHECK(fired[1].first == 2);
  CHECK(fired[1].second == 8);
}

TEST_CASE("Sequencer: parsed TRAK switches real fixture patterns across bars") {
  const ParsedSong song = parseFixture("standard-rebirth.rbs");
  REQUIRE(song.arrangement.size() > 4);

  Sequencer seq;
  Sequencer::Event events[NUM_DEVICES]{};

  seq.setPosition(1, 0);
  const uint32_t firstCount = seq.generateEvents(
    &song, song.bpm, 44100.0f, 1, events, NUM_DEVICES);
  REQUIRE(firstCount > 0);
  uint8_t bar1Note = 0;
  bool foundBar1 = false;
  for (uint32_t i = 0; i < firstCount; ++i) {
    if (events[i].device == DeviceId::TB303_A) {
      bar1Note = events[i].step.note;
      foundBar1 = true;
    }
  }
  REQUIRE(foundBar1);

  seq.setPosition(5, 0);
  const uint32_t fifthCount = seq.generateEvents(
    &song, song.bpm, 44100.0f, 1, events, NUM_DEVICES);
  REQUIRE(fifthCount > 0);
  uint8_t bar5Note = 0;
  bool foundBar5 = false;
  for (uint32_t i = 0; i < fifthCount; ++i) {
    if (events[i].device == DeviceId::TB303_A) {
      bar5Note = events[i].step.note;
      foundBar5 = true;
    }
  }
  REQUIRE(foundBar5);

  CHECK(bar1Note == 7);
  CHECK(bar5Note == 10);
  CHECK(bar1Note != bar5Note);
}

TEST_CASE("Sequencer: null snapshot emits no events") {
  Sequencer seq;
  seq.setPosition(1, 0);
  Sequencer::Event ev[16];
  CHECK(seq.generateEvents(nullptr, 120.0f, 44100.0f, 128, ev, 16) == 0);
}

TEST_CASE("AutomationScheduler: seek applies the last event at or before the bar") {
  ParsedSong song;
  song.bpm = 120.0f;
  song.automation.push_back({1, 0, 0x03, 40});
  song.automation.push_back({1, 32, 0x03, 80});

  AutomationScheduler scheduler;
  scheduler.setPosition(&song, 2, 0);

  alignas(16) AutomationScheduler::Event events[8];
  const uint32_t count = scheduler.generateEvents(
      &song, 120.0f, 44100.0f, 2, 0, 0.0, 128, events, 8);
  REQUIRE(count == 1);
  CHECK(events[0].controller == 0x03);
  CHECK(events[0].value == 80);
  CHECK(events[0].sampleOffset == 0);
}
