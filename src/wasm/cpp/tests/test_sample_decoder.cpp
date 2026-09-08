#include "../audio/SampleDecoder.h"
#include "../third_party/doctest.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace rb338;

namespace {

// ── Byte-level builders, so decoder tests do not depend on any fixture ──

void pushBe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>(v & 0xff));
}

void pushBe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>(v & 0xff));
}

void pushLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void pushLe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void pushId(std::vector<uint8_t>& out, const char* id) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(id[i]));
}

/** 44100 Hz as an 80-bit IEEE extended float (the canonical AIFF encoding). */
void pushRate44100(std::vector<uint8_t>& out) {
  const uint8_t bytes[10] = {0x40, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (uint8_t b : bytes) out.push_back(b);
}

std::vector<uint8_t> buildAiff(const std::vector<int16_t>& samples, uint16_t channels = 1) {
  const auto frames = static_cast<uint32_t>(samples.size() / channels);

  std::vector<uint8_t> comm;
  pushBe16(comm, channels);
  pushBe32(comm, frames);
  pushBe16(comm, 16);
  pushRate44100(comm);

  std::vector<uint8_t> ssnd;
  pushBe32(ssnd, 0); // offset
  pushBe32(ssnd, 0); // blockSize
  for (int16_t s : samples) pushBe16(ssnd, static_cast<uint16_t>(s));

  std::vector<uint8_t> body;
  pushId(body, "AIFF");
  pushId(body, "COMM");
  pushBe32(body, static_cast<uint32_t>(comm.size()));
  body.insert(body.end(), comm.begin(), comm.end());
  pushId(body, "SSND");
  pushBe32(body, static_cast<uint32_t>(ssnd.size()));
  body.insert(body.end(), ssnd.begin(), ssnd.end());

  std::vector<uint8_t> out;
  pushId(out, "FORM");
  pushBe32(out, static_cast<uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::vector<uint8_t> buildWav(const std::vector<int16_t>& samples, uint16_t channels = 1) {
  std::vector<uint8_t> body;
  pushId(body, "WAVE");

  pushId(body, "fmt ");
  pushLe32(body, 16);
  pushLe16(body, 1); // PCM
  pushLe16(body, channels);
  pushLe32(body, 44100);
  pushLe32(body, 44100u * 2u * channels);
  pushLe16(body, static_cast<uint16_t>(2 * channels));
  pushLe16(body, 16);

  pushId(body, "data");
  pushLe32(body, static_cast<uint32_t>(samples.size() * 2));
  for (int16_t s : samples) pushLe16(body, static_cast<uint16_t>(s));

  std::vector<uint8_t> out;
  pushId(out, "RIFF");
  pushLe32(out, static_cast<uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::vector<int16_t> ramp(size_t count) {
  std::vector<int16_t> out(count);
  for (size_t i = 0; i < count; ++i) {
    out[i] = static_cast<int16_t>(-16000 + static_cast<int>(i * 32000 / (count ? count : 1)));
  }
  return out;
}

} // namespace

TEST_CASE("SampleDecoder: AIFF round-trips to mono float") {
  const std::vector<int16_t> samples = {0, 16384, -16384, 32767, -32768};
  const auto bytes = buildAiff(samples);

  SampleInfo info;
  REQUIRE(probeSample(bytes.data(), bytes.size(), info) == SampleDecodeStatus::Ok);
  CHECK(info.frameCount == samples.size());
  CHECK(info.sampleRate == 44100);
  CHECK(info.channels == 1);
  CHECK(info.bitDepth == 16);

  std::vector<float> pcm(samples.size(), 0.0f);
  SampleInfo decoded;
  REQUIRE(decodeSampleMono(bytes.data(), bytes.size(), pcm.data(), pcm.size(), decoded) ==
          SampleDecodeStatus::Ok);

  CHECK(pcm[0] == doctest::Approx(0.0f));
  CHECK(pcm[1] == doctest::Approx(0.5f).epsilon(0.001));
  CHECK(pcm[2] == doctest::Approx(-0.5f).epsilon(0.001));
  CHECK(pcm[3] == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(pcm[4] == doctest::Approx(-1.0f).epsilon(0.001));
}

TEST_CASE("SampleDecoder: WAV round-trips to mono float") {
  const std::vector<int16_t> samples = {0, 16384, -16384, 32767};
  const auto bytes = buildWav(samples);

  SampleInfo info;
  REQUIRE(probeSample(bytes.data(), bytes.size(), info) == SampleDecodeStatus::Ok);
  CHECK(info.frameCount == samples.size());
  CHECK(info.sampleRate == 44100);
  CHECK(info.channels == 1);

  std::vector<float> pcm(samples.size(), 0.0f);
  SampleInfo decoded;
  REQUIRE(decodeSampleMono(bytes.data(), bytes.size(), pcm.data(), pcm.size(), decoded) ==
          SampleDecodeStatus::Ok);

  CHECK(pcm[0] == doctest::Approx(0.0f));
  CHECK(pcm[1] == doctest::Approx(0.5f).epsilon(0.001));
  CHECK(pcm[2] == doctest::Approx(-0.5f).epsilon(0.001));
  CHECK(pcm[3] == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("SampleDecoder: stereo sources are averaged to mono") {
  // Left and right cancel exactly, so a correct downmix is silent.
  const std::vector<int16_t> interleaved = {16384, -16384, 8192, -8192};
  const auto aiffBytes = buildAiff(interleaved, 2);

  SampleInfo info;
  REQUIRE(probeSample(aiffBytes.data(), aiffBytes.size(), info) == SampleDecodeStatus::Ok);
  CHECK(info.channels == 2);
  CHECK(info.frameCount == 2);

  std::vector<float> pcm(4, 99.0f);
  SampleInfo decoded;
  REQUIRE(decodeSampleMono(aiffBytes.data(), aiffBytes.size(), pcm.data(), pcm.size(),
                           decoded) == SampleDecodeStatus::Ok);
  CHECK(pcm[0] == doctest::Approx(0.0f).epsilon(0.001));
  CHECK(pcm[1] == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("SampleDecoder: destination smaller than the sample is refused") {
  const auto bytes = buildAiff(ramp(64));

  std::vector<float> tooSmall(16, 0.0f);
  SampleInfo info;
  CHECK(decodeSampleMono(bytes.data(), bytes.size(), tooSmall.data(), tooSmall.size(), info) ==
        SampleDecodeStatus::DestinationTooSmall);
}

TEST_CASE("SampleDecoder: unknown and truncated payloads fail cleanly") {
  SampleInfo info;

  SUBCASE("not audio at all") {
    const std::vector<uint8_t> junk(64, 0xab);
    CHECK(probeSample(junk.data(), junk.size(), info) == SampleDecodeStatus::UnknownFormat);
  }

  SUBCASE("too short to hold a header") {
    const std::vector<uint8_t> tiny = {'F', 'O', 'R', 'M'};
    CHECK(probeSample(tiny.data(), tiny.size(), info) == SampleDecodeStatus::UnknownFormat);
  }

  SUBCASE("AIFF stub with no COMM or SSND — this is what minimal.rbm carries") {
    std::vector<uint8_t> stub;
    pushId(stub, "FORM");
    pushBe32(stub, 4);
    pushId(stub, "AIFF");
    CHECK(probeSample(stub.data(), stub.size(), info) == SampleDecodeStatus::Malformed);
  }

  SUBCASE("AIFF whose SSND is cut short still decodes the frames it has") {
    auto bytes = buildAiff(ramp(64));
    bytes.resize(bytes.size() - 64); // drop 32 frames of PCM
    REQUIRE(probeSample(bytes.data(), bytes.size(), info) == SampleDecodeStatus::Ok);
    CHECK(info.frameCount < 64);
    CHECK(info.frameCount > 0);
  }

  SUBCASE("null pointer") {
    CHECK(probeSample(nullptr, 128, info) == SampleDecodeStatus::UnknownFormat);
  }
}

TEST_CASE("SampleDecoder: status names are stable strings") {
  CHECK(std::string(sampleDecodeStatusName(SampleDecodeStatus::Ok)) == "ok");
  CHECK(std::string(sampleDecodeStatusName(SampleDecodeStatus::Malformed)) == "malformed");
  CHECK(std::string(sampleDecodeStatusName(SampleDecodeStatus::DestinationTooSmall)) ==
        "destination-too-small");
}
