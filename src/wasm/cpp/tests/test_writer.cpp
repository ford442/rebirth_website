// RbsWriter round-trip gate: parse → write → parse must be semantically
// identical. Bitwise identity is explicitly NOT required — padding, reserved
// fields and chunks the parser skips are not preserved (RbsFormat.md §10).

#include "../parser/RbsParser.h"
#include "../parser/RbsWriter.h"
#include "../third_party/doctest.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open fixture: " + path);
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

ParsedSong parseBytes(const std::vector<uint8_t>& bytes, const std::string& label) {
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error("Parse failed for " + label + ": " + parser.lastError());
  return *song;
}

ParsedSong parseFixture(const std::string& name) {
  return parseBytes(readFile("src/wasm/test-fixtures/" + name), name);
}

std::vector<uint8_t> writeSong(const ParsedSong& song) {
  RbsWriter writer;
  auto bytes = writer.write(song);
  if (!bytes) throw std::runtime_error("Write failed: " + writer.lastError());
  return *bytes;
}

void requireSameDevices(const ParsedSong& a, const ParsedSong& b) {
  for (int i = 0; i < NUM_DEVICES; ++i) {
    CAPTURE(i);
    const DeviceState& x = a.devices[i];
    const DeviceState& y = b.devices[i];
    CHECK(x.muted == y.muted);
    CHECK(x.waveform == y.waveform);
    CHECK(x.initialPatternBank == y.initialPatternBank);
    CHECK(x.initialPatternIndex == y.initialPatternIndex);
    // Knobs are stored as 0-127 bytes, so a normalised value survives the
    // round trip only to within half a step.
    CHECK(x.tune == doctest::Approx(y.tune).epsilon(0.005));
    CHECK(x.cutoff == doctest::Approx(y.cutoff).epsilon(0.005));
    CHECK(x.resonance == doctest::Approx(y.resonance).epsilon(0.005));
    CHECK(x.envMod == doctest::Approx(y.envMod).epsilon(0.005));
    CHECK(x.decay == doctest::Approx(y.decay).epsilon(0.005));
    CHECK(x.accent == doctest::Approx(y.accent).epsilon(0.005));
    CHECK(x.level == doctest::Approx(y.level).epsilon(0.005));
    CHECK(x.pan == doctest::Approx(y.pan).epsilon(0.005));
    CHECK(x.delaySend == doctest::Approx(y.delaySend).epsilon(0.005));
    CHECK(x.dist == y.dist);
    CHECK(x.pcf == y.pcf);
    CHECK(x.compressor == y.compressor);
  }
}

void requireSamePatterns(const ParsedSong& a, const ParsedSong& b) {
  REQUIRE(a.patterns.size() == b.patterns.size());
  for (size_t p = 0; p < a.patterns.size(); ++p) {
    CAPTURE(p);
    const Pattern& x = a.patterns[p];
    const Pattern& y = b.patterns[p];
    REQUIRE(x.deviceId == y.deviceId);
    REQUIRE(x.bank == y.bank);
    REQUIRE(x.patternIndex == y.patternIndex);
    REQUIRE(x.length == y.length);
    for (int s = 0; s < MAX_STEPS; ++s) {
      CAPTURE(s);
      const StepData& sx = x.steps[static_cast<size_t>(s)];
      const StepData& sy = y.steps[static_cast<size_t>(s)];
      CHECK(sx.active == sy.active);
      CHECK(sx.note == sy.note);
      CHECK(sx.drumExtra == sy.drumExtra);
      CHECK(sx.accent == sy.accent);
      CHECK(sx.slide == sy.slide);
    }
  }
}

/** Every pattern of `a` must appear, step for step, in `b`. */
void requirePatternsCarriedOver(const ParsedSong& a, const ParsedSong& b) {
  for (const Pattern& x : a.patterns) {
    CAPTURE(static_cast<int>(x.deviceId));
    CAPTURE(static_cast<int>(x.bank));
    CAPTURE(static_cast<int>(x.patternIndex));
    const Pattern* y = nullptr;
    for (const Pattern& candidate : b.patterns) {
      if (candidate.deviceId == x.deviceId && candidate.bank == x.bank &&
          candidate.patternIndex == x.patternIndex) {
        y = &candidate;
        break;
      }
    }
    REQUIRE(y != nullptr);
    REQUIRE(x.length == y->length);
    for (int s = 0; s < MAX_STEPS; ++s) {
      CAPTURE(s);
      CHECK(x.steps[static_cast<size_t>(s)].active == y->steps[static_cast<size_t>(s)].active);
      CHECK(x.steps[static_cast<size_t>(s)].note == y->steps[static_cast<size_t>(s)].note);
      CHECK(x.steps[static_cast<size_t>(s)].drumExtra == y->steps[static_cast<size_t>(s)].drumExtra);
      CHECK(x.steps[static_cast<size_t>(s)].accent == y->steps[static_cast<size_t>(s)].accent);
      CHECK(x.steps[static_cast<size_t>(s)].slide == y->steps[static_cast<size_t>(s)].slide);
    }
  }
}

std::string trimmed(const std::string& text) {
  const size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return std::string();
  return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

void requireSameFx(const SongFxSettings& x, const SongFxSettings& y) {
  CHECK(x.masterLevel == y.masterLevel);
  CHECK(x.delay.enabled == y.delay.enabled);
  CHECK(x.delay.time == y.delay.time);
  CHECK(x.delay.feedback == y.delay.feedback);
  CHECK(x.delay.wet == y.delay.wet);
  CHECK(x.pcf.enabled == y.pcf.enabled);
  CHECK(x.pcf.cutoff == y.pcf.cutoff);
  CHECK(x.pcf.resonance == y.pcf.resonance);
  CHECK(x.pcf.envAmount == y.pcf.envAmount);
  CHECK(x.dist.enabled == y.dist.enabled);
  CHECK(x.dist.drive == y.dist.drive);
  CHECK(x.dist.mix == y.dist.mix);
  CHECK(x.comp.enabled == y.comp.enabled);
  CHECK(x.comp.ratio == y.comp.ratio);
  CHECK(x.comp.threshold == y.comp.threshold);
}

void requireSameSong(const ParsedSong& a, const ParsedSong& b) {
  CHECK(a.title == b.title);
  CHECK(a.author == b.author);
  CHECK(a.infoText == b.infoText);
  CHECK(a.creatorUrl == b.creatorUrl);
  CHECK(a.showInfoOnOpen == b.showInfoOnOpen);
  CHECK(a.bpm == doctest::Approx(b.bpm).epsilon(0.0001));
  requireSameDevices(a, b);
  requireSamePatterns(a, b);
  requireSameFx(a.fx, b.fx);

  REQUIRE(a.arrangement.size() == b.arrangement.size());
  for (size_t bar = 0; bar < a.arrangement.size(); ++bar) {
    CAPTURE(bar);
    CHECK(a.arrangement[bar].barNumber == b.arrangement[bar].barNumber);
    for (int d = 0; d < NUM_DEVICES; ++d) {
      CAPTURE(d);
      CHECK(a.arrangement[bar].devicePatterns[d].bank ==
            b.arrangement[bar].devicePatterns[d].bank);
      CHECK(a.arrangement[bar].devicePatterns[d].index ==
            b.arrangement[bar].devicePatterns[d].index);
    }
  }

  REQUIRE(a.automation.size() == b.automation.size());
  for (size_t i = 0; i < a.automation.size(); ++i) {
    CAPTURE(i);
    CHECK(a.automation[i].trackIndex == b.automation[i].trackIndex);
    CHECK(a.automation[i].tickPosition == b.automation[i].tickPosition);
    CHECK(a.automation[i].controller == b.automation[i].controller);
    CHECK(a.automation[i].value == b.automation[i].value);
  }
}

} // namespace

TEST_CASE("Writer emits a container the parser accepts") {
  const ParsedSong song = parseFixture("standard-rebirth.rbs");
  const std::vector<uint8_t> bytes = writeSong(song);

  REQUIRE(bytes.size() > 16);
  CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "CAT ");
  CHECK(std::string(bytes.begin() + 8, bytes.begin() + 12) == "RB40");

  const uint32_t declared = (static_cast<uint32_t>(bytes[4]) << 24) |
                            (static_cast<uint32_t>(bytes[5]) << 16) |
                            (static_cast<uint32_t>(bytes[6]) << 8) |
                            static_cast<uint32_t>(bytes[7]);
  CHECK(declared + 8 == bytes.size());
}

TEST_CASE("v2 fixtures survive parse → write → parse semantically") {
  for (const char* name : {"standard-rebirth.rbs", "blue-planet.rbs", "no-remorse.rbs"}) {
    CAPTURE(name);
    const ParsedSong original = parseFixture(name);
    const ParsedSong reloaded = parseBytes(writeSong(original), std::string(name) + " (written)");
    requireSameSong(original, reloaded);
  }
}

TEST_CASE("Writing is idempotent — a second pass produces identical bytes") {
  const ParsedSong original = parseFixture("blue-planet.rbs");
  const std::vector<uint8_t> first = writeSong(original);
  const std::vector<uint8_t> second = writeSong(parseBytes(first, "first pass"));
  CHECK(first == second);
}

TEST_CASE("A v1 song is exported as a v2.x container") {
  const ParsedSong original = parseFixture("v1/retrograde.rbs");
  const std::vector<uint8_t> bytes = writeSong(original);
  CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "CAT ");

  const ParsedSong reloaded = parseBytes(bytes, "v1 (written)");
  // The v2 GLOB reader trims the title field; the v1 MIDI container does not.
  CHECK(reloaded.title == trimmed(original.title));
  CHECK(reloaded.bpm == doctest::Approx(original.bpm).epsilon(0.0001));
  // A v1 song carries fewer than four devices. Saving as v2 always emits all
  // four device chunks, so the reload gains empty patterns for the devices
  // the source did not have — every pattern it *did* have must survive.
  CHECK(reloaded.patterns.size() == 4 * 32);
  requirePatternsCarriedOver(original, reloaded);
}

TEST_CASE("A step edit survives the round trip") {
  ParsedSong song = parseFixture("standard-rebirth.rbs");
  Pattern* target = nullptr;
  for (Pattern& pattern : song.patterns) {
    if (pattern.deviceId == DeviceId::TB303_A && pattern.bank == 0 &&
        pattern.patternIndex == 0) {
      target = &pattern;
      break;
    }
  }
  REQUIRE(target != nullptr);
  target->steps[3] = StepData{true, 60, 0, true, true};
  target->steps[4] = StepData{false, 0, 0, false, false};

  const ParsedSong reloaded = parseBytes(writeSong(song), "edited");
  requireSameSong(song, reloaded);
}

TEST_CASE("Writer rejects values the parser would refuse to read back") {
  SUBCASE("pattern slot beyond the 32 the format has") {
    ParsedSong song = parseFixture("standard-rebirth.rbs");
    REQUIRE_FALSE(song.arrangement.empty());
    song.arrangement[0].devicePatterns[0] = PatternRef{4, 0};
    RbsWriter writer;
    CHECK_FALSE(writer.write(song).has_value());
    CHECK(writer.lastError().find("32 pattern slots") != std::string::npos);
  }

  SUBCASE("automation track beyond the nine TRAK tracks") {
    ParsedSong song = parseFixture("standard-rebirth.rbs");
    song.automation.push_back(AutomationEvent{NUM_TRAK_TRACKS, 0, 2, 3});
    RbsWriter writer;
    CHECK_FALSE(writer.write(song).has_value());
    CHECK(writer.lastError().find("TRAK tracks") != std::string::npos);
  }

  SUBCASE("automation colliding with the pattern-select controller") {
    ParsedSong song = parseFixture("standard-rebirth.rbs");
    song.automation.push_back(AutomationEvent{1, 0, 0x01, 3});
    RbsWriter writer;
    CHECK_FALSE(writer.write(song).has_value());
    CHECK(writer.lastError().find("pattern-select") != std::string::npos);
  }
}
