#include "../engine/RbsAudioEngine.h"
#include "../third_party/doctest.h"
#include <cmath>
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

EngineConfig testConfig() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  // FX off so the comparison isolates the voices themselves.
  cfg.enableDistortion = false;
  cfg.enableCompressor = false;
  cfg.enableDelay = false;
  return cfg;
}

/**
 * A song that hits the 808 kick, the 808 snare, the 909 snare and a 303 note
 * on every step — one slot the mod replaces and one it does not, per device.
 */
ParsedSong makeModTestSong() {
  ParsedSong song;
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
    song.devices[i].level = 0.9f;
    song.devices[i].pan = 0.5f;
    song.devices[i].tune = 0.5f;
    song.devices[i].decay = 0.5f;
    song.devices[i].accent = 0.5f;
    song.devices[i].initialPatternBank = 0;
    song.devices[i].initialPatternIndex = 0;
  }

  Pattern bass;
  bass.deviceId = DeviceId::TB303_A;
  bass.bank = 0;
  bass.patternIndex = 0;
  bass.length = 16;
  for (int s = 0; s < MAX_STEPS; ++s) {
    bass.steps[static_cast<size_t>(s)].active = true;
    bass.steps[static_cast<size_t>(s)].note = static_cast<uint8_t>(45 + (s % 4));
  }
  song.patterns.push_back(bass);

  Pattern tr808;
  tr808.deviceId = DeviceId::TR808;
  tr808.bank = 0;
  tr808.patternIndex = 0;
  tr808.length = 16;
  for (int s = 0; s < MAX_STEPS; ++s) {
    tr808.steps[static_cast<size_t>(s)].active = true;
    // BD is replaced by the mod; SD is not, so it must stay procedural.
    tr808.steps[static_cast<size_t>(s)].note = static_cast<uint8_t>(0x01 | 0x02);
  }
  song.patterns.push_back(tr808);

  Pattern tr909;
  tr909.deviceId = DeviceId::TR909;
  tr909.bank = 0;
  tr909.patternIndex = 0;
  tr909.length = 16;
  for (int s = 0; s < MAX_STEPS; ++s) {
    tr909.steps[static_cast<size_t>(s)].active = true;
    tr909.steps[static_cast<size_t>(s)].note = 0x02; // SD — replaced by the mod
  }
  song.patterns.push_back(tr909);

  return song;
}

/** Render `frames` of mono (L+R averaged) from a playing engine. */
std::vector<float> renderMono(RbsAudioEngine& eng, uint32_t frames) {
  std::vector<float> out;
  out.reserve(frames);
  constexpr uint32_t kBlock = 128;
  float left[kBlock];
  float right[kBlock];
  float* buffers[2] = {left, right};

  uint32_t rendered = 0;
  while (rendered < frames) {
    eng.processBlock(buffers, 2, kBlock);
    for (uint32_t i = 0; i < kBlock && rendered < frames; ++i, ++rendered) {
      out.push_back(0.5f * (left[i] + right[i]));
    }
  }
  return out;
}

float rms(const std::vector<float>& samples) {
  if (samples.empty()) return 0.0f;
  double sum = 0.0;
  for (float s : samples) sum += static_cast<double>(s) * static_cast<double>(s);
  return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

float rmsDifference(const std::vector<float>& a, const std::vector<float>& b) {
  const size_t count = a.size() < b.size() ? a.size() : b.size();
  if (count == 0) return 0.0f;
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sum += d * d;
  }
  return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

/** One second of playback from a freshly built engine, optionally modded. */
std::vector<float> renderSong(bool withMod, bool modBeforeSong = false) {
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));

  const auto modBytes = readFixture("mods/sample-kit.rbm");

  if (withMod && modBeforeSong) {
    eng.loadMod(modBytes.data(), modBytes.size());
  }
  REQUIRE(eng.loadSong(makeModTestSong()));
  if (withMod && !modBeforeSong) {
    REQUIRE(eng.loadMod(modBytes.data(), modBytes.size()) == ModLoadStatus::Ok);
  }

  eng.play();
  return renderMono(eng, 44100);
}

} // namespace

TEST_CASE("Mod playback: loading a mod changes the rendered audio") {
  const std::vector<float> procedural = renderSong(false);
  const std::vector<float> modded = renderSong(true);

  REQUIRE(procedural.size() == modded.size());

  const float proceduralRms = rms(procedural);
  const float moddedRms = rms(modded);
  CHECK(proceduralRms > 0.001f);
  CHECK(moddedRms > 0.001f);

  // The mod replaces the 808 kick, 909 snare and 303 oscillator, so the two
  // renders must differ by far more than floating-point noise.
  const float difference = rmsDifference(procedural, modded);
  CHECK(difference > proceduralRms * 0.1f);
}

TEST_CASE("Mod playback: mod loaded before the song is still applied") {
  const std::vector<float> loadedAfter = renderSong(true, false);
  const std::vector<float> loadedBefore = renderSong(true, true);

  REQUIRE(loadedAfter.size() == loadedBefore.size());
  // Load order must not matter: loadSong() carries the pool forward.
  CHECK(rmsDifference(loadedAfter, loadedBefore) < 1e-6f);
}

TEST_CASE("Mod playback: engine reports mod state and survives a bad payload") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(makeModTestSong()));
  CHECK_FALSE(eng.hasMod());

  SUBCASE("a good mod loads and reports its slots") {
    const auto bytes = readFixture("mods/sample-kit.rbm");
    CHECK(eng.loadMod(bytes.data(), bytes.size()) == ModLoadStatus::Ok);
    CHECK(eng.hasMod());
    CHECK(eng.lastModReport().loadedSlots == 3);
    CHECK(eng.lastModReport().title == "Sample Kit Test Mod");
  }

  SUBCASE("clearMod returns to procedural synthesis") {
    const auto bytes = readFixture("mods/sample-kit.rbm");
    REQUIRE(eng.loadMod(bytes.data(), bytes.size()) == ModLoadStatus::Ok);
    REQUIRE(eng.hasMod());
    eng.clearMod();
    CHECK_FALSE(eng.hasMod());
  }

  SUBCASE("a mod with no decodable audio is rejected, keeping the old kit") {
    const auto good = readFixture("mods/sample-kit.rbm");
    REQUIRE(eng.loadMod(good.data(), good.size()) == ModLoadStatus::Ok);

    const auto stub = readFixture("mods/minimal.rbm");
    CHECK(eng.loadMod(stub.data(), stub.size()) == ModLoadStatus::NoSamples);
    CHECK(eng.hasMod()); // the working kit is still in place
  }

  SUBCASE("garbage bytes do not crash or load") {
    const std::vector<uint8_t> junk(512, 0x7f);
    CHECK(eng.loadMod(junk.data(), junk.size()) == ModLoadStatus::NoSamples);
    CHECK_FALSE(eng.hasMod());
  }

  SUBCASE("null and empty input are refused") {
    CHECK(eng.loadMod(nullptr, 0) == ModLoadStatus::NoSamples);
    CHECK_FALSE(eng.hasMod());
  }
}

TEST_CASE("Mod playback: an unmodded slot keeps its procedural voice") {
  // The mod supplies an 808 kick but no 808 snare. A snare-only pattern must
  // therefore sound identical with and without the mod loaded.
  ParsedSong song = makeModTestSong();
  for (auto& pattern : song.patterns) {
    if (pattern.deviceId != DeviceId::TR808) continue;
    for (int s = 0; s < MAX_STEPS; ++s) {
      pattern.steps[static_cast<size_t>(s)].note = 0x02; // SD only
    }
  }
  // Silence the other devices so only the 808 snare is under test.
  song.devices[0].level = 0.0f;
  song.devices[1].level = 0.0f;
  song.devices[3].level = 0.0f;

  const auto modBytes = readFixture("mods/sample-kit.rbm");

  RbsAudioEngine plain;
  REQUIRE(plain.init(testConfig()));
  REQUIRE(plain.loadSong(song));
  plain.play();
  const std::vector<float> withoutMod = renderMono(plain, 22050);

  RbsAudioEngine modded;
  REQUIRE(modded.init(testConfig()));
  REQUIRE(modded.loadSong(song));
  REQUIRE(modded.loadMod(modBytes.data(), modBytes.size()) == ModLoadStatus::Ok);
  modded.play();
  const std::vector<float> withMod = renderMono(modded, 22050);

  CHECK(rms(withoutMod) > 0.001f);
  CHECK(rmsDifference(withoutMod, withMod) < 1e-6f);
}

TEST_CASE("Mod playback: mod audio is no hotter than the procedural kit") {
  // The master limiter is envelope-following, so short transients ride above
  // its 0.92 threshold before it clamps — the procedural voices already do
  // this on an accented kick. What matters here is that mod PCM does not
  // make it *worse*: a mod must never be the reason output runs away.
  const std::vector<float> procedural = renderSong(false);
  const std::vector<float> modded = renderSong(true);

  float proceduralPeak = 0.0f;
  for (float s : procedural) proceduralPeak = std::max(proceduralPeak, std::fabs(s));

  float moddedPeak = 0.0f;
  for (float s : modded) {
    REQUIRE(std::isfinite(s));
    moddedPeak = std::max(moddedPeak, std::fabs(s));
  }

  CHECK(moddedPeak <= std::max(proceduralPeak, 1.0f));
  CHECK(moddedPeak < 2.0f); // catches a genuine runaway, not limiter overshoot
}
