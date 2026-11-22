// Golden comparisons: render each reference song in
// src/wasm/test-fixtures/golden/ and compare it with its checked-in
// reference by spectral and envelope distance (GoldenCompare.h). The cases,
// their sources and tolerances live in tools/GoldenCases.h; GOLDEN.md says
// how to add a recording from original ReBirth 2.0.1.

#include "../audio/SampleDecoder.h"
#include "../parser/RbsParser.h"
#include "../third_party/doctest.h"
#include "../tools/GoldenCases.h"
#include "../tools/RenderSong.h"
#include "GoldenCompare.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace rb338;

namespace {

constexpr float kRate = 44100.0f;
const std::string kDir = "src/wasm/test-fixtures/golden/";

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), "missing golden file: " << path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

std::vector<float> readWavMono(const std::string& path) {
  const auto bytes = readFile(path);
  SampleInfo info;
  REQUIRE(probeSample(bytes.data(), bytes.size(), info) == SampleDecodeStatus::Ok);
  std::vector<float> pcm(info.frameCount);
  REQUIRE(decodeSampleMono(bytes.data(), bytes.size(), pcm.data(), pcm.size(), info) ==
          SampleDecodeStatus::Ok);
  CHECK(info.sampleRate == static_cast<uint32_t>(kRate));
  return pcm;
}

std::vector<float> renderCase(const golden::Case& c) {
  const auto bytes = readFile(kDir + c.id + ".rbs");
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  REQUIRE_MESSAGE(song.has_value(), parser.lastError());
  tools::RenderOptions options;
  options.sampleRate = kRate;
  options.frames = static_cast<uint32_t>(std::lround(c.seconds * kRate));
  auto stereo = tools::renderStereo(*song, options);
  REQUIRE(stereo.has_value());
  return tools::toMono(*stereo);
}

} // namespace

TEST_CASE("Golden: the comparison metrics behave") {
  std::vector<float> tone(44100);
  for (size_t i = 0; i < tone.size(); ++i) {
    tone[i] = 0.5f * std::sin(6.2831853f * 220.0f * i / kRate) *
              static_cast<float>((i / 4410) % 2); // gated every 100 ms
  }
  CHECK(test::spectralDistanceDb(tone, tone, kRate) == 0.0);
  CHECK(test::envelopeCorrelation(tone, tone) == doctest::Approx(1.0));
  // An octave up, or the gate inverted, is far away.
  std::vector<float> octave(tone.size());
  std::vector<float> inverted(tone.size());
  for (size_t i = 0; i < tone.size(); ++i) {
    octave[i] = 0.5f * std::sin(6.2831853f * 440.0f * i / kRate) *
                static_cast<float>((i / 4410) % 2);
    inverted[i] = 0.5f * std::sin(6.2831853f * 220.0f * i / kRate) *
                  static_cast<float>(1 - (i / 4410) % 2);
  }
  CHECK(test::spectralDistanceDb(tone, octave, kRate) > 10.0);
  CHECK(test::envelopeCorrelation(tone, inverted) < 0.0);
  // Half the level is 6 dB in every band.
  std::vector<float> quieter(tone.size());
  for (size_t i = 0; i < tone.size(); ++i) quieter[i] = 0.5f * tone[i];
  CHECK(test::spectralDistanceDb(tone, quieter, kRate) == doctest::Approx(6.02).epsilon(0.02));
}

TEST_CASE("Golden: every reference song renders within its tolerances") {
  REQUIRE(golden::cases().size() >= 3);
  for (const golden::Case& c : golden::cases()) {
    CAPTURE(c.id);
    const auto reference = readWavMono(kDir + c.id + ".wav");
    const auto fresh = renderCase(c);
    REQUIRE(fresh.size() == reference.size());
    float peak = 0.0f;
    for (float s : fresh) {
      REQUIRE(std::isfinite(s));
      peak = std::max(peak, std::fabs(s));
    }
    CHECK(peak > 0.01f); // a refactor that silences the case fails here
    const double spectral = test::spectralDistanceDb(fresh, reference, kRate);
    const double envelope = test::envelopeCorrelation(fresh, reference);
    MESSAGE(std::string(c.id) << ": spectral " << spectral << " dB, envelope r " << envelope);
    CHECK(spectral <= c.maxSpectralDb);
    CHECK(envelope >= c.minEnvelopeCorrelation);
  }
}

TEST_CASE("Golden: the checked-in songs are what GoldenCases.h builds") {
  // A case edited in GoldenCases.h but not regenerated would compare a
  // different song from the one its reference was rendered from.
  for (const golden::Case& c : golden::cases()) {
    CAPTURE(c.id);
    const auto bytes = readFile(kDir + c.id + ".rbs");
    RbsParser parser;
    auto onDisk = parser.parse(bytes.data(), bytes.size());
    REQUIRE(onDisk.has_value());
    const ParsedSong built = golden::buildSong(c.id);
    CHECK(onDisk->title == built.title);
    CHECK(onDisk->shuffle == built.shuffle);
    CHECK(onDisk->automation.size() == built.automation.size());
    REQUIRE(!built.patterns.empty());
    const Pattern& want = built.patterns.front();
    bool found = false;
    for (const Pattern& p : onDisk->patterns) {
      if (p.deviceId != want.deviceId || p.bank != 0 || p.patternIndex != 0) continue;
      found = true;
      CHECK(p.shuffle == want.shuffle);
      for (int s = 0; s < MAX_STEPS; ++s) {
        CAPTURE(s);
        CHECK(p.steps[s].active == want.steps[s].active);
        CHECK(p.steps[s].note == want.steps[s].note);
        CHECK(p.steps[s].drumFlam == want.steps[s].drumFlam);
      }
    }
    CHECK(found);
  }
}

TEST_CASE("Golden: the harness tells the shuffle references apart") {
  // The same hats a third of a step late on every off-beat must fall outside
  // the tolerances on both metrics — otherwise the comparison could not
  // catch a groove regression at all.
  const auto& all = golden::cases();
  const auto find = [&](const char* id) -> const golden::Case& {
    for (const auto& c : all) {
      if (std::string(c.id) == id) return c;
    }
    FAIL("no case " << id);
    return all.front();
  };
  const auto straight = readWavMono(kDir + "909-shuffle-000.wav");
  const auto swung = renderCase(find("909-shuffle-127"));
  REQUIRE(straight.size() == swung.size());
  MESSAGE("shuffle 127 vs 0: envelope r " << test::envelopeCorrelation(swung, straight)
                                          << ", spectral "
                                          << test::spectralDistanceDb(swung, straight, kRate));
  CHECK(test::envelopeCorrelation(swung, straight) < golden::kEngineEnvelope);
  CHECK(test::spectralDistanceDb(swung, straight, kRate) > golden::kEngineSpectralDb);
}
