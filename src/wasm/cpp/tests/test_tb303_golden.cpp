#include "../engine/RbsAudioEngine.h"
#include "../parser/RbsParser.h"
#include "../third_party/doctest.h"
#include "SpectralAnalysis.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readBinaryFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open: " + path);
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

std::vector<float> readRawFloat32(const std::string& path) {
  const auto bytes = readBinaryFile(path);
  std::vector<float> samples(bytes.size() / sizeof(float));
  std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(float));
  return samples;
}

// Renders the TB-303-B line of the standard-rebirth.rbs fixture at a fixed
// BPM (independent of the fixture's own tempo, so the golden comparison
// stays deterministic even if the fixture's metadata changes), mirroring
// how src/wasm/test-fixtures/golden/standard-rebirth-303b.f32raw was
// generated.
//
// TB-303-B rather than -A: in this specific fixture, device A's arranged
// patterns never end up producing sound over a full playthrough (a
// sequencer/pattern-selection question, not a DSP one — out of scope for
// this filter-fidelity pass). -B exercises the same Tb303Voice code path.
std::vector<float> renderStandardRebirth303b(uint32_t totalFrames) {
  const auto bytes = readBinaryFile("src/wasm/test-fixtures/standard-rebirth.rbs");
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error("parse failed: " + parser.lastError());

  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  cfg.enableTb303A = false;
  cfg.enableTb303B = true;
  cfg.enableTr808 = false;
  cfg.enableTr909 = false;

  RbsAudioEngine eng;
  if (!eng.init(cfg)) throw std::runtime_error("engine init failed");
  if (!eng.loadSong(*song)) throw std::runtime_error("loadSong failed");
  eng.setTempo(125.0f);
  eng.play();

  std::vector<float> mono;
  mono.reserve(totalFrames);
  constexpr uint32_t kBlock = 128;
  float left[kBlock];
  float right[kBlock];
  float* buffers[2] = {left, right};

  uint32_t rendered = 0;
  while (rendered < totalFrames) {
    eng.processBlock(buffers, 2, kBlock);
    for (uint32_t i = 0; i < kBlock && rendered < totalFrames; ++i, ++rendered) {
      mono.push_back(0.5f * (left[i] + right[i]));
    }
  }
  return mono;
}

} // namespace

// Regression net for issue "TB-303 fidelity pass": a refactor that silences
// the voice, blows it up (NaN / peak > 1 pre-limiter), or drastically
// changes its timbre should fail here even though the unit tests above
// still pass. Golden data lives in src/wasm/test-fixtures/golden/ and was
// rendered with the same engine config and a fixed 125 BPM tempo.
TEST_CASE("Golden: standard-rebirth.rbs 303-B render matches checked-in reference") {
  const std::vector<float> golden =
      readRawFloat32("src/wasm/test-fixtures/golden/standard-rebirth-303b.f32raw");
  REQUIRE(golden.size() == 88200); // 2.0s @ 44.1kHz

  const std::vector<float> fresh =
      renderStandardRebirth303b(static_cast<uint32_t>(golden.size()));
  REQUIRE(fresh.size() == golden.size());

  for (float s : fresh) {
    REQUIRE(std::isfinite(s));
    CHECK(std::fabs(s) <= 1.0f);
  }

  const float goldenRms = rb338::test::rms(golden);
  const float freshRms = rb338::test::rms(fresh);
  REQUIRE(goldenRms > 0.001f); // sanity: golden fixture isn't silent
  CHECK(freshRms > 0.001f);    // catches a refactor that silences the voice
  CHECK(freshRms == doctest::Approx(goldenRms).epsilon(0.2));

  const float goldenCentroid =
      rb338::test::spectralCentroidHz(golden.data(), golden.size(), 44100.0f);
  const float freshCentroid =
      rb338::test::spectralCentroidHz(fresh.data(), fresh.size(), 44100.0f);
  REQUIRE(goldenCentroid > 20.0f);
  CHECK(freshCentroid == doctest::Approx(goldenCentroid).epsilon(0.25));
}
