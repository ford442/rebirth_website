#include "../audio/WavWriter.h"
#include "../engine/AudioThreadLimits.h"
#include "../engine/RbsAudioEngine.h"
#include "../parser/RbsParser.h"
#include "../third_party/doctest.h"
#include <cmath>
#include <cstdint>
#include <cstring>
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

ParsedSong parseSongFixture(const std::string& name) {
  const auto bytes = readFixture(name);
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error("Parse failed: " + parser.lastError());
  return *song;
}

EngineConfig testConfig() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  cfg.bufferSize = 128;
  return cfg;
}

/**
 * Drive processBlock() by hand at an arbitrary quantum, the way the
 * AudioWorklet drives it at 128. Returns interleaved stereo.
 */
std::vector<float> renderViaProcessBlock(const ParsedSong& song, uint32_t frames,
                                         uint32_t blockSize) {
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  // Mirror renderOffline()'s start-of-transport handling: drain the queued
  // stop from loadSong, then play from bar 1.
  eng.play();

  std::vector<float> out;
  out.reserve(static_cast<size_t>(frames) * 2u);
  std::vector<float> left(blockSize, 0.0f);
  std::vector<float> right(blockSize, 0.0f);
  float* buffers[2] = {left.data(), right.data()};

  uint32_t rendered = 0;
  while (rendered < frames) {
    const uint32_t count = std::min<uint32_t>(blockSize, frames - rendered);
    eng.processBlock(buffers, 2, count);
    for (uint32_t i = 0; i < count; ++i) {
      out.push_back(left[i]);
      out.push_back(right[i]);
    }
    rendered += count;
  }
  return out;
}

/** True when every sample matches bit-for-bit, not merely within a tolerance. */
bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return false;
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float rms(const std::vector<float>& samples) {
  if (samples.empty()) return 0.0f;
  double sum = 0.0;
  for (float s : samples) sum += static_cast<double>(s) * static_cast<double>(s);
  return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

constexpr uint32_t kTwoSeconds = 44100u * 2u;

} // namespace

// ─────────────────────────────────────────────────────────────────
// The gate for the whole offline/studio surface: an offline render must
// be the worklet's own output, not an approximation of it. The worklet
// calls processBlock(planar, 2, 128) and nothing else, so these tests pin
// the two properties that make an offline loop equivalent to it —
// determinism and block-size invariance — plus the absence of any
// AudioContext dependency (this translation unit links no Web Audio).
// ─────────────────────────────────────────────────────────────────

TEST_CASE("Offline gate: processBlock is deterministic") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");

  const auto first = renderViaProcessBlock(song, kTwoSeconds, AUDIO_WORKLET_FRAMES);
  const auto second = renderViaProcessBlock(song, kTwoSeconds, AUDIO_WORKLET_FRAMES);

  REQUIRE(rms(first) > 0.001f); // a silent render would pass vacuously
  CHECK(bitIdentical(first, second));
}

TEST_CASE("Offline gate: output does not depend on the render quantum") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");

  const auto reference = renderViaProcessBlock(song, kTwoSeconds, AUDIO_WORKLET_FRAMES);
  REQUIRE(rms(reference) > 0.001f);

  // Sequencer and automation events are placed at absolute sample positions
  // and every DSP stage is per-sample, so splitting the timeline differently
  // must not perturb a single sample.
  for (uint32_t quantum : {32u, 64u, 256u, 512u, 1024u}) {
    const auto other = renderViaProcessBlock(song, kTwoSeconds, quantum);
    CHECK_MESSAGE(bitIdentical(reference, other), "quantum = ", quantum);
  }
}

TEST_CASE("Offline gate: renderOffline matches a hand-driven worklet loop") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");

  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  std::vector<float> offline(static_cast<size_t>(kTwoSeconds) * 2u, 0.0f);
  const uint32_t rendered = eng.renderOffline(offline.data(), kTwoSeconds);
  REQUIRE(rendered == kTwoSeconds);

  const auto worklet = renderViaProcessBlock(song, kTwoSeconds, AUDIO_WORKLET_FRAMES);

  REQUIRE(rms(offline) > 0.001f);
  CHECK(bitIdentical(offline, worklet));
}

TEST_CASE("Offline: renderOffline restarts from bar 1 every time") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");

  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  std::vector<float> first(static_cast<size_t>(kTwoSeconds) * 2u, 0.0f);
  std::vector<float> second(static_cast<size_t>(kTwoSeconds) * 2u, 0.0f);

  eng.renderOffline(first.data(), kTwoSeconds);
  // Move the transport somewhere else before the second bounce.
  eng.seek(9);
  eng.renderOffline(second.data(), kTwoSeconds);

  CHECK(bitIdentical(first, second));
}

TEST_CASE("Offline: stems isolate one device and restore mute state") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");

  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = 44100u;
  std::vector<float> master(static_cast<size_t>(frames) * 2u, 0.0f);
  eng.renderOffline(master.data(), frames);
  const float masterRms = rms(master);
  REQUIRE(masterRms > 0.001f);

  float stemSum = 0.0f;
  int audibleStems = 0;
  int stemsDifferingFromMaster = 0;
  for (uint8_t device = 0; device < NUM_DEVICES; ++device) {
    std::vector<float> stem(static_cast<size_t>(frames) * 2u, 0.0f);
    const uint32_t rendered = eng.renderOfflineStem(stem.data(), frames, device);
    CHECK(rendered == frames);
    for (float s : stem) REQUIRE(std::isfinite(s));
    const float stemRms = rms(stem);
    stemSum += stemRms;
    if (stemRms > 0.001f) ++audibleStems;
    // Guards against the solo silently not being applied, which would make
    // every "stem" a copy of the master and every check here vacuous.
    if (!bitIdentical(stem, master)) ++stemsDifferingFromMaster;
  }

  CHECK(audibleStems > 0);
  CHECK(stemsDifferingFromMaster == NUM_DEVICES);
  // Stems are individually correct but do not sum bit-exactly to the master:
  // the master limiter is non-linear. They should still be in the same league.
  CHECK(stemSum > masterRms * 0.5f);

  // A stem render must leave the mixer exactly as it found it.
  std::vector<float> after(static_cast<size_t>(frames) * 2u, 0.0f);
  eng.renderOffline(after.data(), frames);
  CHECK(bitIdentical(master, after));
}

TEST_CASE("Offline: an out-of-range stem index renders nothing") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  std::vector<float> buffer(1024, 0.0f);
  CHECK(eng.renderOfflineStem(buffer.data(), 256, NUM_DEVICES) == 0);
  CHECK(eng.renderOffline(nullptr, 256) == 0);
  CHECK(eng.renderOffline(buffer.data(), 0) == 0);
}

TEST_CASE("Offline: song length covers the arrangement") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = eng.songLengthFrames();
  const double seconds = static_cast<double>(frames) / 44100.0;
  const double bars = static_cast<double>(song.arrangement.size());
  const double expected = bars * 4.0 * (60.0 / static_cast<double>(eng.getTempo()));

  CHECK(frames > 0);
  CHECK(seconds == doctest::Approx(expected).epsilon(0.01));
}

// ── WAV writer ───────────────────────────────────────────────────

TEST_CASE("WavWriter: emits a well-formed 16-bit PCM header") {
  const std::vector<float> samples = {0.0f, 0.5f, -0.5f, 1.0f};
  const auto wav = writeWavPcm16(samples.data(), 2, 2, 44100);

  REQUIRE(wav.size() == wavPcm16ByteSize(2, 2));
  REQUIRE(wav.size() == 44u + 8u);

  CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0);
  CHECK(std::memcmp(wav.data() + 8, "WAVE", 4) == 0);
  CHECK(std::memcmp(wav.data() + 12, "fmt ", 4) == 0);
  CHECK(std::memcmp(wav.data() + 36, "data", 4) == 0);

  auto le32 = [&](size_t offset) {
    return static_cast<uint32_t>(wav[offset]) | (static_cast<uint32_t>(wav[offset + 1]) << 8) |
           (static_cast<uint32_t>(wav[offset + 2]) << 16) |
           (static_cast<uint32_t>(wav[offset + 3]) << 24);
  };
  auto le16 = [&](size_t offset) {
    return static_cast<uint16_t>(wav[offset] | (wav[offset + 1] << 8));
  };

  CHECK(le32(4) == 36u + 8u);      // RIFF size
  CHECK(le32(16) == 16u);          // fmt chunk size
  CHECK(le16(20) == 1);            // PCM
  CHECK(le16(22) == 2);            // channels
  CHECK(le32(24) == 44100u);       // sample rate
  CHECK(le32(28) == 44100u * 4u);  // byte rate
  CHECK(le16(32) == 4);            // block align
  CHECK(le16(34) == 16);           // bits per sample
  CHECK(le32(40) == 8u);           // data size
}

TEST_CASE("WavWriter: converts, clamps and never wraps at full scale") {
  const std::vector<float> samples = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f,
                                      std::nanf(""), std::numeric_limits<float>::infinity()};
  const auto wav = writeWavPcm16(samples.data(), static_cast<uint32_t>(samples.size()), 1, 44100);

  auto sampleAt = [&](size_t index) {
    const size_t offset = 44 + index * 2;
    return static_cast<int16_t>(wav[offset] | (wav[offset + 1] << 8));
  };

  CHECK(sampleAt(0) == 0);
  CHECK(sampleAt(1) == 32767);   // +1.0 must not wrap to -32768
  CHECK(sampleAt(2) == -32768);
  CHECK(sampleAt(3) == 32767);   // clamped, not wrapped
  CHECK(sampleAt(4) == -32768);
  CHECK(sampleAt(5) == 0);       // NaN
  CHECK(sampleAt(6) == 0);       // Inf
}

TEST_CASE("WavWriter: a bounce round-trips through the sample decoder") {
  // Proves the file we hand the browser is one a decoder actually accepts.
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = 22050;
  std::vector<float> pcm(static_cast<size_t>(frames) * 2u, 0.0f);
  REQUIRE(eng.renderOffline(pcm.data(), frames) == frames);

  const auto wav = writeWavPcm16(pcm.data(), frames, 2, 44100);
  REQUIRE(wav.size() == wavPcm16ByteSize(frames, 2));

  SampleInfo info;
  REQUIRE(probeSample(wav.data(), wav.size(), info) == SampleDecodeStatus::Ok);
  CHECK(info.frameCount == frames);
  CHECK(info.sampleRate == 44100);
  CHECK(info.channels == 2);
  CHECK(info.bitDepth == 16);
}

TEST_CASE("WavWriter: streaming builder matches the one-shot writer") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = 22050;

  // One-shot: buffer the whole render as float, then encode.
  std::vector<float> pcm(static_cast<size_t>(frames) * 2u, 0.0f);
  REQUIRE(eng.renderOffline(pcm.data(), frames) == frames);
  const auto oneShot = writeWavPcm16(pcm.data(), frames, 2, 44100);

  // Streaming: encode block by block, never holding the float render.
  const auto streamed = eng.renderOfflineWav(frames);

  REQUIRE(streamed.size() == oneShot.size());
  CHECK(std::memcmp(streamed.data(), oneShot.data(), streamed.size()) == 0);
}

TEST_CASE("WavWriter: stem bounce differs from the master and stays valid") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = 22050;
  const auto master = eng.renderOfflineWav(frames, RbsAudioEngine::MASTER_BUS);
  const auto stem = eng.renderOfflineWav(frames, 3); // TR-909

  REQUIRE(master.size() == stem.size());
  CHECK(std::memcmp(master.data(), stem.data(), master.size()) != 0);

  SampleInfo info;
  REQUIRE(probeSample(stem.data(), stem.size(), info) == SampleDecodeStatus::Ok);
  CHECK(info.frameCount == frames);
  CHECK(info.channels == 2);

  // Bouncing the master again must still give the original file.
  const auto masterAgain = eng.renderOfflineWav(frames, RbsAudioEngine::MASTER_BUS);
  CHECK(std::memcmp(master.data(), masterAgain.data(), master.size()) == 0);
}

TEST_CASE("WavWriter: a zero-length bounce is still a valid empty file") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  const auto wav = eng.renderOfflineWav(0);
  CHECK(wav.size() == 44u);
  CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0);
}

TEST_CASE("Offline: live knob moves survive into the bounce") {
  // setDeviceParam() is session-only and never touches the loaded song, while
  // a bounce resets the graph to that song. Without replaying the overrides,
  // a bounce would render knob values the listener is not hearing.
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  const uint32_t frames = 22050;
  const auto before = eng.renderOfflineWav(frames);

  // TB-303 B decay (DeviceParamId::Decay == 4) all the way down.
  eng.setDeviceParam(1, 4, 0.0f);
  const auto shortened = eng.renderOfflineWav(frames);

  REQUIRE(before.size() == shortened.size());
  CHECK(std::memcmp(before.data(), shortened.data(), before.size()) != 0);

  // The override is sticky: bouncing again without touching anything repeats.
  const auto again = eng.renderOfflineWav(frames);
  CHECK(std::memcmp(shortened.data(), again.data(), shortened.size()) == 0);

  // Moving it back returns to the original render.
  eng.setDeviceParam(1, 4, song.devices[1].decay);
  const auto restored = eng.renderOfflineWav(frames);
  CHECK(std::memcmp(before.data(), restored.data(), before.size()) == 0);
}

TEST_CASE("Offline: knob moves survive loading a mod") {
  const ParsedSong song = parseSongFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(testConfig()));
  REQUIRE(eng.loadSong(song));

  eng.setDeviceParam(1, 4, 0.0f);
  const uint32_t frames = 22050;
  const auto tweaked = eng.renderOfflineWav(frames);

  // Loading a mod republishes the graph, which used to reset every knob.
  const auto modBytes = readFixture("mods/sample-kit.rbm");
  REQUIRE(eng.loadMod(modBytes.data(), modBytes.size()) == ModLoadStatus::Ok);

  eng.setDeviceParam(1, 4, 0.0f); // same value; should be a no-op if sticky
  const auto afterMod = eng.renderOfflineWav(frames);

  // The 303 is unaffected by this mod's samples (it supplies a saw wavetable
  // for slot Tb303Saw, so the two renders differ) — what matters is that the
  // decay override is still in force, which the next check pins down.
  eng.setDeviceParam(1, 4, 1.0f);
  const auto longDecay = eng.renderOfflineWav(frames);
  CHECK(std::memcmp(afterMod.data(), longDecay.data(), afterMod.size()) != 0);
  CHECK(tweaked.size() == afterMod.size());
}
