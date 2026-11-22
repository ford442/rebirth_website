#include "../parser/DrumKit.h"
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

uint32_t readU32BE(const std::vector<uint8_t>& bytes, size_t offset) {
  return (static_cast<uint32_t>(bytes[offset]) << 24) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<uint32_t>(bytes[offset + 3]);
}

size_t findLastChunk(const std::vector<uint8_t>& bytes, const char (&id)[5]) {
  const std::array<uint8_t, 4> needle = {
    static_cast<uint8_t>(id[0]), static_cast<uint8_t>(id[1]),
    static_cast<uint8_t>(id[2]), static_cast<uint8_t>(id[3])
  };
  size_t last = static_cast<size_t>(-1);
  auto it = bytes.begin();
  while (true) {
    it = std::search(it, bytes.end(), needle.begin(), needle.end());
    if (it == bytes.end()) break;
    last = static_cast<size_t>(it - bytes.begin());
    ++it;
  }
  if (last == static_cast<size_t>(-1)) {
    throw std::runtime_error(std::string("Missing chunk: ") + id);
  }
  return last;
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

TEST_CASE("v2.x fixtures decode non-empty TRAK automation") {
  for (const auto* name : {"standard-rebirth.rbs", "blue-planet.rbs", "no-remorse.rbs"}) {
    const auto song = parseFixture(name);
    CAPTURE(name);
    CHECK(song.automation.size() > 0);
  }
}

TEST_CASE("5-byte STRAK id is parsed as a TRAK alias") {
  const auto original = parseFixture("no-remorse.rbs");
  auto bytes = readFile("src/wasm/test-fixtures/no-remorse.rbs");
  const size_t trak = findLastChunk(bytes, "TRAK");
  REQUIRE(bytes[trak] == 'T');
  const uint32_t trakSize = readU32BE(bytes, trak + 4);
  bytes[trak + 0] = 'S';
  bytes[trak + 1] = 'T';
  bytes[trak + 2] = 'R';
  bytes[trak + 3] = 'A';
  bytes.insert(bytes.begin() + trak + 4, static_cast<uint8_t>('K'));

  // 9-byte STRAK header + odd body is even, so the original IFF pad is gone.
  if ((trakSize & 1u) && !bytes.empty()) {
    bytes.pop_back();
  }

  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  REQUIRE(song);
  CHECK(song->automation.size() == original.automation.size());
  CHECK(song->arrangement.size() == original.arrangement.size());
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

TEST_CASE("standard-rebirth.rbs FX chunks decode into SongFxSettings") {
  const auto song = parseFixture("standard-rebirth.rbs");
  CHECK(song.fx.masterLevel == 100);
  // DELY 01 03 01 3e 50: on, 3 steps, straight, feedback, pan.
  CHECK(song.fx.delay.enabled);
  CHECK(song.fx.delay.steps == 3);
  CHECK_FALSE(song.fx.delay.triplet);
  CHECK(song.fx.delay.feedback == 0x3e);
  CHECK(song.fx.delay.pan == 0x50);
  // PCF 01 4b 4f 40 18 43 01: on, freq, Q, amount, pattern, decay, LP.
  CHECK(song.fx.pcf.enabled);
  CHECK(song.fx.pcf.cutoff == 0x4b);
  CHECK(song.fx.pcf.resonance == 0x4f);
  CHECK(song.fx.pcf.envAmount == 0x40);
  CHECK(song.fx.pcf.pattern == 0x18);
  CHECK(song.fx.pcf.decay == 0x43);
  CHECK(song.fx.pcf.lowpass);
  CHECK(song.fx.dist.enabled);
  CHECK(song.fx.dist.drive == 0x20);
  CHECK(song.fx.dist.mix == 0x1d);
  // COMP 01 20 7f 7f: on, ratio, threshold, attack.
  CHECK(song.fx.comp.enabled);
  CHECK(song.fx.comp.ratio == 0x20);
  CHECK(song.fx.comp.threshold == 0x7f);
  CHECK(song.fx.comp.attack == 0x7f);
  // MIXR bytes 1/2: neither insert is placed in this song.
  CHECK(song.fx.comp.target == FX_TARGET_NONE);
  CHECK(song.fx.pcf.target == FX_TARGET_NONE);
}

ParsedSong parseFixturePath(const std::string& path) {
  auto buffer = readFile(path);
  RbsParser parser;
  auto song = parser.parse(buffer.data(), buffer.size());
  if (!song) {
    throw std::runtime_error("Parse failed for " + path + ": " + parser.lastError());
  }
  return *song;
}

TEST_CASE("v1 MIDI-container golden fixtures parse successfully") {
  for (const auto& spec : {
         std::pair{"v1/just-15.rbs", RbsVersion::V1_0},
         std::pair{"v1/retrograde.rbs", RbsVersion::V1_0},
         std::pair{"v15/gurkensalat.rbs", RbsVersion::V1_5},
         std::pair{"v15/hermetico-absoluto.rbs", RbsVersion::V1_5},
         std::pair{"v15/cavey-3-acid.rbs", RbsVersion::V1_5},
       }) {
    CAPTURE(spec.first);
    const auto song = parseFixturePath(std::string("src/wasm/test-fixtures/") + spec.first);
    CHECK(song.version == spec.second);
    CHECK(song.bpm > 40.0f);
    CHECK(song.patterns.size() >= 96);
    CHECK(countPatterns(song, DeviceId::TB303_A) == 32);
    CHECK(countPatterns(song, DeviceId::TR808) == 32);
  }
}

TEST_CASE("reject/generic-smf.mid is rejected as standard MIDI") {
  RbsParser parser;
  const auto buffer = readFile("src/wasm/test-fixtures/reject/generic-smf.mid");
  const auto song = parser.parse(buffer.data(), buffer.size());
  CHECK(!song);
  CHECK(parser.lastError().find("Standard MIDI file") != std::string::npos);
}

// ── GLOB / USRI transport and groove fields (RbsFormat.md §4, §5) ─────

TEST_CASE("GLOB decodes play mode, loop locators, shuffle, vintage and the mod FTP URL") {
  const auto standard = parseFixture("standard-rebirth.rbs");
  CHECK(standard.playMode == PlayMode::Song);
  CHECK(standard.loopEnabled);
  CHECK(standard.loopStart == 0);
  CHECK(standard.loopEnd == 142 * GLOB_TICKS_PER_BAR);
  CHECK(standard.shuffle == 56);
  CHECK_FALSE(standard.vintage);
  CHECK(standard.modFtpUrl == "ftp.propellerheads.se");

  const auto blue = parseFixture("blue-planet.rbs");
  CHECK(blue.shuffle == 42);
  CHECK(blue.loopStart == 529 * GLOB_TICKS_PER_BAR);
  CHECK(blue.loopEnd == 530 * GLOB_TICKS_PER_BAR);
  CHECK(blue.modFtpUrl.empty());
}

TEST_CASE("USRI byte 0x21f is the show-info-on-open flag") {
  for (const char* name : {"standard-rebirth.rbs", "blue-planet.rbs", "no-remorse.rbs"}) {
    CAPTURE(name);
    CHECK(parseFixture(name).showInfoOnOpen);
  }
}

TEST_CASE("Pattern slot byte 0 is the per-pattern shuffle flag") {
  const auto song = parseFixture("standard-rebirth.rbs");
  // 303-A slots A1–A3 are flagged, A4 is not; the 909's first slots of
  // no-remorse are flagged too (RbsFormat.md §6e).
  CHECK(findPattern(song, DeviceId::TB303_A, 0, 0)->shuffle);
  CHECK(findPattern(song, DeviceId::TB303_A, 0, 2)->shuffle);
  CHECK_FALSE(findPattern(song, DeviceId::TB303_A, 0, 3)->shuffle);
  const auto noRemorse = parseFixture("no-remorse.rbs");
  CHECK_FALSE(findPattern(noRemorse, DeviceId::TR909, 0, 0)->shuffle);
  CHECK(findPattern(noRemorse, DeviceId::TR909, 0, 1)->shuffle);
}

TEST_CASE("MIXR bytes 1 and 2 name the compressor and PCF insert devices") {
  // blue-planet: both on 303-B (id 3); no-remorse: compressor on the 909.
  const auto blue = parseFixture("blue-planet.rbs");
  CHECK(blue.fx.comp.target == FX_TARGET_FIRST_DEVICE + 1);
  CHECK(blue.fx.pcf.target == FX_TARGET_FIRST_DEVICE + 1);
  const auto noRemorse = parseFixture("no-remorse.rbs");
  CHECK(noRemorse.fx.comp.target == FX_TARGET_FIRST_DEVICE + 3);
  CHECK(noRemorse.fx.pcf.target == FX_TARGET_NONE);
}

TEST_CASE("808 / 909 headers decode the accent knob and the named kit") {
  const auto song = parseFixture("standard-rebirth.rbs");
  const DeviceState& tr808 = song.devices[2];
  const DeviceState& tr909 = song.devices[3];
  REQUIRE(tr808.hasKit);
  REQUIRE(tr909.hasKit);
  // 808 header: 00 0d 24 7f 40 40 7f 63 2e 7f 40 01 …
  CHECK(tr808.accent == doctest::Approx(0x24 / 127.0f));
  CHECK(tr808.kit[kitIndex(Kit808::BassLevel)] == 0x7f);
  CHECK(tr808.kit[kitIndex(Kit808::SnareTone)] == 0x63);
  CHECK(tr808.kit[kitIndex(Kit808::SnareSnappy)] == 0x2e);
  CHECK(tr808.kit[kitIndex(Kit808::LowTomSelector)] == 1);
  CHECK(tr808.kit[kitIndex(Kit808::ClosedHatLevel)] == 0x7f);
  // 909 header: 01 00 20 64 40 40 7f 64 40 40 40 64 …, flam 0x40 at byte 29.
  CHECK(tr909.accent == doctest::Approx(0x20 / 127.0f));
  CHECK(tr909.kit[kitIndex(Kit909::BassLevel)] == 0x64);
  CHECK(tr909.kit[kitIndex(Kit909::BassDecay)] == 0x7f);
  CHECK(tr909.kit[kitIndex(Kit909::FlamInterval)] == 0x40);
  CHECK(tr909.kit[kitIndex(Kit909::Unknown1e)] == 0x64);
}

TEST_CASE("Drum rows are accent first; the 909 stores accents and flams per hit") {
  const auto blue = parseFixture("blue-planet.rbs");
  int strong = 0;
  int flams = 0;
  for (const Pattern& p : blue.patterns) {
    for (const StepData& s : p.steps) {
      if (p.deviceId == DeviceId::TR909) {
        strong += __builtin_popcount(s.drumAccent);
        flams += __builtin_popcount(s.drumFlam);
        // Dynamics only ever mark instruments the step actually hits.
        const unsigned hits = s.note | (s.drumExtra << 8u);
        CHECK((s.drumAccent & ~hits) == 0);
        CHECK((s.drumFlam & ~hits) == 0);
      } else {
        CHECK(s.drumAccent == 0);
        CHECK(s.drumFlam == 0);
      }
    }
  }
  CHECK(strong == 297);
  CHECK(flams == 3);

  // blue-planet's 808 (at mixer level 0) has 64 AC-row hits, 43 of them on
  // steps with no instrument: kept as inactive accents rather than read as
  // kicks (RbsFormat.md §6e).
  int accents = 0;
  int accentOnly = 0;
  for (const Pattern& p : blue.patterns) {
    if (p.deviceId != DeviceId::TR808) continue;
    for (const StepData& s : p.steps) {
      if (s.accent) ++accents;
      if (s.accent && !s.active) ++accentOnly;
    }
  }
  CHECK(accents == 64);
  CHECK(accentOnly == 43);
  CHECK(blue.devices[2].level == 0.0f);
}
