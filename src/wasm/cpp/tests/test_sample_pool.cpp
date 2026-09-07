#include "../parser/RbmParser.h"
#include "../synth/SamplePool.h"
#include "../third_party/doctest.h"
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readFixture(const std::string& name) {
  const std::string path = std::string("src/wasm/test-fixtures/") + name;
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open fixture: " + path);
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

ParsedMod parseModFixture(const std::string& name) {
  const auto bytes = readFixture(name);
  RbmParser parser;
  auto mod = parser.parse(bytes.data(), bytes.size());
  if (!mod) throw std::runtime_error("Parse failed for " + name + ": " + parser.lastError());
  return *mod;
}

const ModSampleReportEntry* findEntry(const ModLoadReport& report, const std::string& name) {
  for (const auto& entry : report.resources) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

} // namespace

TEST_CASE("SamplePool: loads decodable slots from sample-kit.rbm") {
  const ParsedMod mod = parseModFixture("mods/sample-kit.rbm");

  SamplePool pool;
  REQUIRE(pool.init());

  ModLoadReport report;
  const ModLoadStatus status = pool.loadFromParsedMod(mod, report);

  CHECK(status == ModLoadStatus::Ok);
  CHECK(report.title == "Sample Kit Test Mod");
  CHECK(report.loadedSlots == 3);
  CHECK(report.skinCount == 1);
  CHECK(report.usedFrames > 0);
  CHECK(report.capacityFrames == SamplePool::kDefaultArenaFrames);

  SUBCASE("808 kick came from the AIFF payload") {
    const SamplePool::SlotData* slot = pool.slotData(ModSampleSlot::Tr808Kick);
    REQUIRE(slot != nullptr);
    CHECK(slot->frameCount == 2048);
    CHECK(slot->sampleRate == 44100);
    CHECK(slot->pcm != nullptr);
  }

  SUBCASE("909 snare came from the WAV payload") {
    const SamplePool::SlotData* slot = pool.slotData(ModSampleSlot::Tr909Snare);
    REQUIRE(slot != nullptr);
    CHECK(slot->frameCount == 1536);
    CHECK(slot->sampleRate == 44100);
  }

  SUBCASE("303 wavetable is a single cycle") {
    const SamplePool::SlotData* slot = pool.slotData(ModSampleSlot::Tb303Saw);
    REQUIRE(slot != nullptr);
    CHECK(slot->frameCount == 128);
  }

  SUBCASE("skins are catalogued but never decoded") {
    const ModSampleReportEntry* skin = findEntry(report, "12522.jpg");
    REQUIRE(skin != nullptr);
    CHECK(skin->kind == ModResourceKind::Skin);
    CHECK_FALSE(skin->loaded);
    CHECK(skin->frameCount == 0);
  }

  SUBCASE("slots the mod does not supply stay empty") {
    CHECK_FALSE(pool.hasSlot(ModSampleSlot::Tr808Snare));
    CHECK_FALSE(pool.hasSlot(ModSampleSlot::Tr909Kick));
    CHECK(pool.slotData(ModSampleSlot::Tb303Square) == nullptr);
    CHECK(pool.slotData(ModSampleSlot::Unknown) == nullptr);
  }

  SUBCASE("PCM lands inside the arena and is audible") {
    const SamplePool::SlotData* slot = pool.slotData(ModSampleSlot::Tr808Kick);
    REQUIRE(slot != nullptr);
    float peak = 0.0f;
    for (uint32_t i = 0; i < slot->frameCount; ++i) {
      const float v = slot->pcm[i] < 0.0f ? -slot->pcm[i] : slot->pcm[i];
      if (v > peak) peak = v;
    }
    CHECK(peak > 0.1f);
    CHECK(peak <= 1.0f);
  }
}

TEST_CASE("SamplePool: a tiny arena reports exhaustion instead of growing") {
  const ParsedMod mod = parseModFixture("mods/sample-kit.rbm");

  SamplePool pool;
  // Room for the 303 wavetable (128 frames) but not the 2048-frame kick.
  REQUIRE(pool.init(256));

  ModLoadReport report;
  const ModLoadStatus status = pool.loadFromParsedMod(mod, report);

  CHECK(status == ModLoadStatus::ArenaExhausted);
  CHECK(pool.capacityFrames() == 256);
  CHECK(pool.usedFrames() <= 256);
  CHECK_FALSE(pool.hasSlot(ModSampleSlot::Tr808Kick));

  const ModSampleReportEntry* kick = findEntry(report, "tr808bd.aif");
  REQUIRE(kick != nullptr);
  CHECK(kick->decodeStatus == SampleDecodeStatus::DestinationTooSmall);
  CHECK_FALSE(kick->loaded);
}

TEST_CASE("SamplePool: minimal.rbm's stub AIFF fails without loading anything") {
  const ParsedMod mod = parseModFixture("mods/minimal.rbm");

  SamplePool pool;
  REQUIRE(pool.init());

  ModLoadReport report;
  const ModLoadStatus status = pool.loadFromParsedMod(mod, report);

  // One sample resource, but its payload is a 12-byte FORM/AIFF stub.
  CHECK(status == ModLoadStatus::NoSamples);
  CHECK(report.loadedSlots == 0);
  CHECK(pool.empty());
  REQUIRE(report.resources.size() == 1);
  CHECK(report.resources[0].decodeStatus == SampleDecodeStatus::Malformed);
}

TEST_CASE("SamplePool: uninitialised pool refuses to load") {
  const ParsedMod mod = parseModFixture("mods/sample-kit.rbm");

  SamplePool pool;
  ModLoadReport report;
  CHECK(pool.loadFromParsedMod(mod, report) == ModLoadStatus::NotInitialised);
  CHECK(pool.empty());
}

TEST_CASE("SamplePool: reloading replaces the previous kit") {
  const ParsedMod kit = parseModFixture("mods/sample-kit.rbm");
  const ParsedMod stub = parseModFixture("mods/minimal.rbm");

  SamplePool pool;
  REQUIRE(pool.init());

  ModLoadReport report;
  REQUIRE(pool.loadFromParsedMod(kit, report) == ModLoadStatus::Ok);
  REQUIRE(pool.hasSlot(ModSampleSlot::Tr808Kick));

  pool.loadFromParsedMod(stub, report);
  CHECK_FALSE(pool.hasSlot(ModSampleSlot::Tr808Kick));
  CHECK(pool.usedFrames() == 0);
}

TEST_CASE("SamplePool: status names are stable strings") {
  CHECK(std::string(modLoadStatusName(ModLoadStatus::Ok)) == "ok");
  CHECK(std::string(modLoadStatusName(ModLoadStatus::ArenaExhausted)) == "arena-exhausted");
  CHECK(std::string(modLoadStatusName(ModLoadStatus::NoSamples)) == "no-samples");
}
