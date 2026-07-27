#include "../parser/ParsedSongJson.h"
#include "../parser/RbsParser.h"
#include "../third_party/doctest.h"
#include <fstream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open fixture: " + path);
  }
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

ParsedSong parseFixture(const std::string& name) {
  std::string path = std::string("src/wasm/test-fixtures/") + name;
  auto buffer = readFile(path);
  RbsParser parser;
  auto song = parser.parse(buffer.data(), buffer.size());
  if (!song) {
    throw std::runtime_error("Parse failed for " + name + ": " + parser.lastError());
  }
  return *song;
}

size_t findChunk(const std::vector<uint8_t>& bytes, const char (&id)[5]) {
  const std::array<uint8_t, 4> needle = {
    static_cast<uint8_t>(id[0]), static_cast<uint8_t>(id[1]),
    static_cast<uint8_t>(id[2]), static_cast<uint8_t>(id[3])
  };
  const auto it = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
  if (it == bytes.end()) throw std::runtime_error(std::string("Missing chunk: ") + id);
  return static_cast<size_t>(it - bytes.begin());
}

void writeU32BE(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value >> 24);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 3] = static_cast<uint8_t>(value);
}

uint8_t selectedPattern(const ArrangementBar& bar, DeviceId device) {
  const PatternRef& ref = bar.devicePatterns[static_cast<int>(device)];
  return static_cast<uint8_t>(ref.bank * MAX_PATTERNS_PER_BANK + ref.index);
}

const Pattern* findPattern(const ParsedSong& song, DeviceId deviceId,
                           uint8_t bank, uint8_t index) {
  for (const auto& p : song.patterns) {
    if (p.deviceId == deviceId && p.bank == bank && p.patternIndex == index) {
      return &p;
    }
  }
  return nullptr;
}

size_t countPatterns(const ParsedSong& song, DeviceId deviceId) {
  size_t n = 0;
  for (const auto& p : song.patterns) {
    if (p.deviceId == deviceId) ++n;
  }
  return n;
}

} // anonymous namespace

TEST_CASE("Parser rejects truncated files") {
  RbsParser parser;
  uint8_t tiny[4] = {0};
  auto song = parser.parse(tiny, sizeof(tiny));
  CHECK(!song);
  CHECK(!parser.lastError().empty());
}

TEST_CASE("Parser rejects files without CAT / RB40 container") {
  RbsParser parser;
  std::vector<uint8_t> bad = {'R', 'E', 'B', 'I', 'R', 'T', 'H'};
  auto song = parser.parse(bad.data(), bad.size());
  CHECK(!song);
  CHECK(!parser.lastError().empty());
}

TEST_CASE("standard-rebirth.rbs metadata") {
  auto song = parseFixture("standard-rebirth.rbs");
  CHECK(song.title == "Standard ReBirth");
  CHECK(song.author == "> ARh+");
  CHECK(!song.infoText.empty());
  CHECK(song.version == RbsVersion::V2_0);
}

TEST_CASE("blue-planet.rbs metadata") {
  auto song = parseFixture("blue-planet.rbs");
  CHECK(song.title == "Blue Planet");
  CHECK(song.author == "[tnl+] 140: Channel was created by \xc2\xa9" "Drop");
  CHECK(!song.infoText.empty());
  CHECK(song.version == RbsVersion::V2_0_1);
}

TEST_CASE("no-remorse.rbs metadata") {
  auto song = parseFixture("no-remorse.rbs");
  CHECK(song.title == "No Remorse");
  CHECK(song.author == "[tnl+]:126 (live record edition) by\xc2\xa9" "Drop");
  CHECK(!song.infoText.empty());
  CHECK(song.version == RbsVersion::V2_0_1);
}

TEST_CASE("All fixtures extract non-empty info text") {
  for (const auto* name : {"standard-rebirth.rbs", "blue-planet.rbs", "no-remorse.rbs"}) {
    auto song = parseFixture(name);
    CAPTURE(name);
    CHECK(!song.infoText.empty());
  }
}

TEST_CASE("GLOB decodes fixture tempos instead of using the default") {
  CHECK(parseFixture("standard-rebirth.rbs").bpm == doctest::Approx(150.0f));
  CHECK(parseFixture("blue-planet.rbs").bpm == doctest::Approx(140.0f));
  CHECK(parseFixture("no-remorse.rbs").bpm == doctest::Approx(126.0f));
}

TEST_CASE("TRAK projects real pattern changes into per-bar arrangement") {
  struct FixtureExpectation {
    const char* name;
    size_t minimumBars;
  };
  for (const auto& fixture : {
         FixtureExpectation{"standard-rebirth.rbs", 140},
         FixtureExpectation{"blue-planet.rbs", 500},
         FixtureExpectation{"no-remorse.rbs", 400}}) {
    const auto song = parseFixture(fixture.name);
    CAPTURE(fixture.name);
    REQUIRE(song.arrangement.size() >= fixture.minimumBars);
    CHECK(song.arrangement.front().barNumber == 1);
    CHECK(song.arrangement.back().barNumber == song.arrangement.size());

    bool changed = false;
    for (size_t bar = 1; bar < song.arrangement.size(); ++bar) {
      for (int device = 0; device < NUM_DEVICES; ++device) {
        const auto id = static_cast<DeviceId>(device);
        if (selectedPattern(song.arrangement[bar - 1], id) !=
            selectedPattern(song.arrangement[bar], id)) {
          changed = true;
        }
      }
    }
    CHECK(changed);
  }

  const auto standard = parseFixture("standard-rebirth.rbs");
  CHECK(selectedPattern(standard.arrangement[0], DeviceId::TB303_A) == 12);
  CHECK(selectedPattern(standard.arrangement[4], DeviceId::TB303_A) == 13);
  CHECK(selectedPattern(standard.arrangement[6], DeviceId::TB303_A) == 14);
}

TEST_CASE("Parser rejects truncated GLOB and TRAK chunks with an error") {
  const auto path = std::string("src/wasm/test-fixtures/standard-rebirth.rbs");

  auto globBytes = readFile(path);
  writeU32BE(globBytes, findChunk(globBytes, "GLOB") + 4, 5);
  RbsParser globParser;
  CHECK(!globParser.parse(globBytes.data(), globBytes.size()));
  CHECK(globParser.lastError().find("GLOB") != std::string::npos);

  auto trakBytes = readFile(path);
  writeU32BE(trakBytes, findChunk(trakBytes, "TRAK") + 4, 4);
  RbsParser trakParser;
  CHECK(!trakParser.parse(trakBytes.data(), trakBytes.size()));
  CHECK(trakParser.lastError().find("TRAK") != std::string::npos);
}

TEST_CASE("v2.x fixtures expose 32 patterns per device") {
  for (const auto* name : {"standard-rebirth.rbs", "blue-planet.rbs", "no-remorse.rbs"}) {
    auto song = parseFixture(name);
    CAPTURE(name);
    CHECK(countPatterns(song, DeviceId::TB303_A) == 32);
    CHECK(countPatterns(song, DeviceId::TB303_B) == 32);
    CHECK(countPatterns(song, DeviceId::TR808) == 32);
    CHECK(countPatterns(song, DeviceId::TR909) == 32);
  }
}

TEST_CASE("standard-rebirth.rbs 303-A bank A pattern 1 decodes correctly") {
  auto song = parseFixture("standard-rebirth.rbs");
  const Pattern* p = findPattern(song, DeviceId::TB303_A, 0, 0);
  REQUIRE(p != nullptr);
  CHECK(p->length == 16);

  // Observed raw bytes for this pattern slot.
  const std::array<uint8_t, 16> expectedNotes = {
    7, 6, 6, 2, 7, 2, 7, 7, 6, 2, 7, 2, 6, 2, 7, 2
  };

  for (size_t i = 0; i < 16; ++i) {
    CAPTURE(i);
    CHECK(p->steps[i].active);
    CHECK(p->steps[i].note == expectedNotes[i]);
  }

  CHECK(p->steps[0].accent);
  CHECK(p->steps[1].accent);
  CHECK(p->steps[7].accent);
  CHECK(!p->steps[2].accent);

  CHECK(p->steps[11].slide);
  CHECK(!p->steps[0].slide);
}

TEST_CASE("parsedSongToJson emits WasmParsedSong-compatible fields") {
  auto song = parseFixture("standard-rebirth.rbs");
  const std::string json = parsedSongToJson(song);
  CHECK(json.find("\"title\":\"Standard ReBirth\"") != std::string::npos);
  CHECK(json.find("\"author\":") != std::string::npos);
  CHECK(json.find("\"patterns\":[") != std::string::npos);
  CHECK(json.find("\"bpm\":150") != std::string::npos);
  CHECK(json.find("\"arrangement\":[") != std::string::npos);
  CHECK(json.find("\"deviceId\":0") != std::string::npos);
  CHECK(json.find("\"length\":16") != std::string::npos);
}

TEST_CASE("standard-rebirth.rbs 808 has non-empty drum patterns") {
  auto song = parseFixture("standard-rebirth.rbs");
  size_t active808 = 0;
  for (const auto& p : song.patterns) {
    if (p.deviceId != DeviceId::TR808) continue;
    for (const auto& step : p.steps) {
      if (step.active) {
        ++active808;
        break;
      }
    }
  }
  CHECK(active808 >= 3);
}
