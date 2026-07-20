#include "../parser/ParsedSongJson.h"
#include "../parser/RbsParser.h"
#include "../third_party/doctest.h"
#include <fstream>
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
