// Regression tests for the engine-correctness pass: each case here failed on
// the code it guards before the fix landed, and names the bug it pins.

#include "../engine/AutomationScheduler.h"
#include "../engine/Mixer.h"
#include "../engine/RbsAudioEngine.h"
#include "../engine/Sequencer.h"
#include "../parser/RbsParser.h"
#include "../synth/DrumBitfield.h"
#include "../synth/DrumSynth.h"
#include "../synth/Tr909Voice.h"
#include "../third_party/doctest.h"
#include "SpectralAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

const char* const kFixtures[] = {"blue-planet.rbs", "no-remorse.rbs",
                                 "standard-rebirth.rbs"};

std::vector<uint8_t> readFixtureBytes(const std::string& name) {
  const std::string path = std::string("src/wasm/test-fixtures/") + name;
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open fixture: " + path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

ParsedSong loadFixture(const std::string& name) {
  const auto bytes = readFixtureBytes(name);
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error("Parse failed: " + parser.lastError());
  return *song;
}

EngineConfig config44k() {
  EngineConfig cfg;
  cfg.sampleRate = 44100.0f;
  return cfg;
}

/**
 * Drive the sequencer and automation clocks exactly as processBlock() does
 * and count the automation events that come out over `passes` playthroughs
 * of the arrangement.
 */
size_t countFiredAutomation(const ParsedSong& song, uint32_t blockSize, int passes) {
  Sequencer seq;
  seq.reset();
  AutomationScheduler sched;
  sched.reset();
  std::vector<Sequencer::Event> events(128);
  std::vector<AutomationScheduler::Event> autoEvents(4096);

  const size_t bars = song.arrangement.size();
  size_t fired = 0;
  for (;;) {
    uint16_t bar = 0;
    uint8_t step = 0;
    seq.getPosition(bar, step);
    if (bar > bars * static_cast<size_t>(passes)) break;
    const double phase = seq.getStepPhase();
    seq.generateEvents(&song, song.bpm, 44100.0f, blockSize, events.data(), 128);
    fired += sched.generateEvents(&song, song.bpm, 44100.0f, bar, step, phase, blockSize,
                                  autoEvents.data(),
                                  static_cast<uint32_t>(autoEvents.size()));
  }
  return fired;
}

/** Interleaved stereo straight out of processBlock(). */
std::vector<float> renderBlocks(RbsAudioEngine& eng, uint32_t blocks) {
  std::vector<float> out;
  out.reserve(static_cast<size_t>(blocks) * 256u);
  float left[128];
  float right[128];
  float* buffers[2] = {left, right};
  for (uint32_t b = 0; b < blocks; ++b) {
    eng.processBlock(buffers, 2, 128);
    for (uint32_t i = 0; i < 128; ++i) {
      out.push_back(left[i]);
      out.push_back(right[i]);
    }
  }
  return out;
}

size_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) return i;
  }
  return n;
}

} // namespace

// ── §1: TRAK automation ordering ─────────────────────────────────────

TEST_CASE("Automation: parsed events are sorted by tick") {
  // parseTrak appends track by track; the scheduler's forward cursor needs
  // one time-ordered list. Unsorted, the fixtures had 8 / 6 / 7 descents.
  for (const char* name : kFixtures) {
    CAPTURE(name);
    const ParsedSong song = loadFixture(name);
    REQUIRE(!song.automation.empty());
    CHECK(std::is_sorted(song.automation.begin(), song.automation.end(),
                         [](const AutomationEvent& a, const AutomationEvent& b) {
                           return a.tickPosition < b.tickPosition;
                         }));
  }
}

TEST_CASE("Automation: every event of every fixture fires across a full render") {
  // Before the sort only 564/1664, 241/1543 and 1546/2610 events fired: once
  // one track's cursor ran ahead, every other track's events were skipped.
  for (const char* name : kFixtures) {
    CAPTURE(name);
    const ParsedSong song = loadFixture(name);
    for (uint32_t block : {128u, 97u}) {
      CAPTURE(block);
      CHECK(countFiredAutomation(song, block, 1) == song.automation.size());
    }
  }
}

TEST_CASE("Automation: loops with the arrangement") {
  // The sequencer loops the arrangement; the automation used to stop after
  // the first pass, leaving every later pass unautomated.
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  CHECK(countFiredAutomation(song, 128, 2) == 2 * song.automation.size());
}

TEST_CASE("Automation: a delay controller other than 0 does not switch the delay on") {
  Mixer mixer;
  mixer.init(44100.0f);
  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].level = 1.0f;
  devices[0].delaySend = 1.0f;
  mixer.setDeviceStates(devices);
  SongFxSettings fx;
  fx.delay.enabled = false;
  fx.delay.wet = 127;
  fx.delay.feedback = 64;
  mixer.setSongFx(fx);

  // Tick-0 snapshot of the delay's feedback knob — present in every song.
  AutomationScheduler::Event ev;
  ev.trackIndex = 5;
  ev.controller = 0x03;
  ev.value = 90;
  AutomationScheduler::applyEvent(nullptr, &mixer, ev);

  // One impulse, then silence: with the delay off nothing may come back.
  std::vector<float> dev(44100, 0.0f);
  dev[0] = 0.5f;
  const float* bufs[NUM_DEVICES] = {dev.data(), nullptr, nullptr, nullptr};
  std::vector<float> l(dev.size()), r(dev.size());
  mixer.process(bufs, l.data(), r.data(), static_cast<uint32_t>(dev.size()), 120.0f);
  float tail = 0.0f;
  for (size_t i = 1; i < l.size(); ++i) tail = std::max(tail, std::fabs(l[i]));
  CHECK(tail == 0.0f);
}

// ── §1: drum noise ──────────────────────────────────────────────────

TEST_CASE("DrumSynth: noise source is zero-mean, bounded and white") {
  DrumVoiceChannel ch;
  ch.init(44100.0f);
  constexpr size_t kN = 1u << 16;
  std::vector<float> x(kN);
  double sum = 0.0;
  float peak = 0.0f;
  for (auto& v : x) {
    v = ch.nextNoise();
    sum += v;
    peak = std::max(peak, std::fabs(v));
  }
  const double mean = sum / kN;
  // The old fmod map sat around -2 with no excursion above -0.87.
  CHECK(std::fabs(mean) < 0.02);
  CHECK(peak <= 1.0f);
  CHECK(peak > 0.9f);

  // Flat-ish spectrum: low, mid and high thirds of the band carry similar
  // energy (a DC offset or a lowpassed map puts nearly all of it low).
  std::vector<float> re(x.begin(), x.end());
  std::vector<float> im(kN, 0.0f);
  rb338::test::fftRadix2(re, im);
  double bands[3] = {0, 0, 0};
  const size_t half = kN / 2;
  for (size_t k = 1; k < half; ++k) {
    const double p = double(re[k]) * re[k] + double(im[k]) * im[k];
    bands[std::min<size_t>(2, k * 3 / half)] += p;
  }
  const double total = bands[0] + bands[1] + bands[2];
  for (double b : bands) {
    CHECK(b / total > 0.28);
    CHECK(b / total < 0.39);
  }
}

TEST_CASE("DrumSynth: a reset channel replays the same noise") {
  DrumVoiceChannel a;
  a.init(44100.0f);
  const float first = a.nextNoise();
  a.nextNoise();
  a.reset();
  CHECK(a.nextNoise() == first);
}

// ── §1: TR-909 hand clap ─────────────────────────────────────────────

TEST_CASE("Tr909: the CL bit plays the hand clap") {
  // Tr909Voice ignored DrumHit::CL, so channel 4 / Tr909Clap never sounded.
  Tr909Voice voice;
  voice.init(44100.0f);
  DeviceState state;
  state.id = DeviceId::TR909;
  voice.load(state);

  StepData step;
  step.active = true;
  step.note = DrumHit::CL;
  voice.triggerStep(0, step);
  std::vector<float> out(4410, 0.0f);
  voice.render(out.data(), static_cast<uint32_t>(out.size()));
  float peak = 0.0f;
  for (float s : out) peak = std::max(peak, std::fabs(s));
  CHECK(peak > 0.05f);

  // A clap is a short burst: well under the crash's length, which proves the
  // CL hit did not just get routed to the crash channel instead.
  voice.reset();
  voice.triggerStep(0, step);
  std::vector<float> longOut(44100, 0.0f);
  voice.render(longOut.data(), static_cast<uint32_t>(longOut.size()));
  size_t last = 0;
  for (size_t i = 0; i < longOut.size(); ++i) {
    if (std::fabs(longOut[i]) > 1e-4f) last = i;
  }
  CHECK(last < 44100u / 10u); // < 100 ms; the 909 crash rings for 350+ ms
}

// ── §1: master limiter ───────────────────────────────────────────────

namespace {

struct HotMix {
  std::vector<float> l, r;
};

HotMix runHotMix(bool compressorOn, uint32_t frames) {
  Mixer mixer;
  mixer.init(44100.0f);
  std::array<DeviceState, NUM_DEVICES> devices{};
  for (auto& d : devices) {
    d.level = 1.0f;
    d.pan = 0.5f;
  }
  mixer.setDeviceStates(devices);
  SongFxSettings fx;
  fx.masterLevel = 127;
  fx.comp.enabled = compressorOn;
  mixer.setSongFx(fx);

  // Four full-scale square waves in phase, with a hard transient every
  // 100 ms: about 2.8× full scale at the centre-panned master.
  std::vector<float> dev(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    dev[i] = ((i / 50) % 2 ? 1.0f : -1.0f) * ((i % 4410) < 64 ? 1.0f : 0.25f);
  }
  const float* bufs[NUM_DEVICES] = {dev.data(), dev.data(), dev.data(), dev.data()};
  HotMix out{std::vector<float>(frames), std::vector<float>(frames)};
  for (uint32_t pos = 0; pos < frames; pos += 128) {
    const uint32_t n = std::min<uint32_t>(128, frames - pos);
    const float* span[NUM_DEVICES] = {bufs[0] + pos, bufs[1] + pos, bufs[2] + pos,
                                      bufs[3] + pos};
    mixer.process(span, out.l.data() + pos, out.r.data() + pos, n, 120.0f);
  }
  return out;
}

} // namespace

TEST_CASE("Mixer: limiter holds every sample under full scale, COMP on or off") {
  for (bool comp : {true, false}) {
    CAPTURE(comp);
    const HotMix mix = runHotMix(comp, 44100);
    float peak = 0.0f;
    for (size_t i = 0; i < mix.l.size(); ++i) {
      peak = std::max({peak, std::fabs(mix.l[i]), std::fabs(mix.r[i])});
    }
    // Instant attack: not one sample over the 0.92 ceiling. The shared 20 ms
    // attack let transients through, and with COMP off there was no limiter.
    CHECK(peak <= 0.92f + 1e-6f);
    CHECK(peak > 0.5f);
  }
}

TEST_CASE("Mixer: the limiter does not fade in the first samples") {
  // m_limiterEnvelope started at 0, so the first ~50 ms of every bounce and
  // every rebuilt snapshot faded in from silence.
  Mixer mixer;
  mixer.init(44100.0f);
  std::array<DeviceState, NUM_DEVICES> devices{};
  devices[0].level = 1.0f;
  devices[0].pan = 0.5f;
  mixer.setDeviceStates(devices);
  SongFxSettings fx;
  fx.comp.enabled = true;
  mixer.setSongFx(fx);

  std::vector<float> dev(128, 0.25f);
  const float* bufs[NUM_DEVICES] = {dev.data(), nullptr, nullptr, nullptr};
  std::vector<float> l(128), r(128);
  mixer.process(bufs, l.data(), r.data(), 128, 120.0f);
  const float expected = 0.25f * std::sqrt(0.5f); // constant-power centre pan
  CHECK(l[0] == doctest::Approx(expected).epsilon(1e-4));
  CHECK(l[127] == doctest::Approx(expected).epsilon(1e-4));
}

// ── §1: nested CAT recursion ─────────────────────────────────────────

TEST_CASE("Parser: deeply nested CAT containers fail cleanly") {
  // ~500k levels segfaulted rbs-inspect; in WASM a far shallower file could
  // run the 128 KiB stack into the heap.
  constexpr size_t kLevels = 500000;
  std::vector<uint8_t> inner;
  const auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
  };
  // Build inside-out into one buffer: each level is "CAT " size "LIST" body.
  std::vector<uint8_t> buf(kLevels * 12 + 12);
  size_t bodySize = 0;
  size_t pos = buf.size();
  for (size_t level = 0; level < kLevels; ++level) {
    pos -= 12;
    std::vector<uint8_t> header;
    header.insert(header.end(), {'C', 'A', 'T', ' '});
    be32(header, static_cast<uint32_t>(bodySize + 4));
    header.insert(header.end(), {'L', 'I', 'S', 'T'});
    std::memcpy(buf.data() + pos, header.data(), 12);
    bodySize += 12;
  }
  // Root header: "CAT " size "RB40".
  pos -= 12;
  std::vector<uint8_t> root;
  root.insert(root.end(), {'C', 'A', 'T', ' '});
  be32(root, static_cast<uint32_t>(bodySize + 4));
  root.insert(root.end(), {'R', 'B', '4', '0'});
  std::memcpy(buf.data() + pos, root.data(), 12);
  REQUIRE(pos == 0);

  RbsParser parser;
  CHECK_FALSE(parser.parse(buf.data(), buf.size()).has_value());
  CHECK(parser.lastError().find("depth") != std::string::npos);
}

TEST_CASE("Parser: real files nest well inside the CAT depth cap") {
  for (const char* name : kFixtures) {
    CAPTURE(name);
    CHECK_NOTHROW(loadFixture(name));
  }
}

// ── §3: edits must not reset the sound ──────────────────────────────

TEST_CASE("Engine: a step edit during playback leaves the audio untouched until it plays") {
  // setStep used to rebuild every voice and the mixer, cutting held 303
  // notes, filter state and the delay tail on every click.
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  RbsAudioEngine edited;
  RbsAudioEngine reference;
  for (RbsAudioEngine* eng : {&edited, &reference}) {
    REQUIRE(eng->init(config44k()));
    REQUIRE(eng->loadSong(song));
    eng->play();
  }

  constexpr uint32_t kLead = 200; // ~0.58 s in
  const auto leadA = renderBlocks(edited, kLead);
  const auto leadB = renderBlocks(reference, kLead);
  REQUIRE(leadA == leadB);

  // Edit a step of a pattern that is not playing in the next few bars.
  const PatternRef playing = song.arrangement[0].devicePatterns[0];
  const Pattern* idle = nullptr;
  for (const Pattern& p : song.patterns) {
    if (p.deviceId != DeviceId::TB303_A) continue;
    if (p.bank == playing.bank && p.patternIndex == playing.index) continue;
    bool usedSoon = false;
    for (size_t bar = 0; bar < std::min<size_t>(8, song.arrangement.size()); ++bar) {
      const PatternRef ref = song.arrangement[bar].devicePatterns[0];
      usedSoon = usedSoon || (ref.bank == p.bank && ref.index == p.patternIndex);
    }
    if (!usedSoon) {
      idle = &p;
      break;
    }
  }
  REQUIRE(idle != nullptr);
  StepData note;
  note.active = true;
  note.note = 48;
  REQUIRE(edited.setStep(0, idle->bank, idle->patternIndex, 3, note));

  const auto afterA = renderBlocks(edited, 400);
  const auto afterB = renderBlocks(reference, 400);
  // Bit-identical: no voice, filter, delay or automation state was touched.
  CHECK(firstDifference(afterA, afterB) == afterA.size());
  CHECK(edited.getStep(0, idle->bank, idle->patternIndex, 3).note == 48);
}

TEST_CASE("Engine: a step edit to the playing pattern is heard at that step, not before") {
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  const PatternRef playing = song.arrangement[0].devicePatterns[1]; // 303-B
  RbsAudioEngine edited;
  RbsAudioEngine reference;
  for (RbsAudioEngine* eng : {&edited, &reference}) {
    REQUIRE(eng->init(config44k()));
    REQUIRE(eng->loadSong(song));
    eng->play();
  }
  // One block in: step 0 is playing.
  renderBlocks(edited, 1);
  renderBlocks(reference, 1);

  constexpr uint8_t kStep = 4;
  StepData step = edited.getStep(1, playing.bank, playing.index, kStep);
  step.active = true;
  step.note = static_cast<uint8_t>(step.note == 30 ? 31 : 30);
  step.accent = !step.accent;
  REQUIRE(edited.setStep(1, playing.bank, playing.index, kStep, step));

  const auto a = renderBlocks(edited, 200);
  const auto b = renderBlocks(reference, 200);
  const size_t diff = firstDifference(a, b);
  REQUIRE(diff < a.size()); // the edit was applied

  // Step 4 of bar 1 starts 4 steps in, minus the block already rendered.
  const double samplesPerStep = 60.0 / song.bpm * 0.25 * 44100.0;
  const auto stepStart = static_cast<size_t>(kStep * samplesPerStep) - 128u;
  CHECK(diff / 2 >= stepStart);
  CHECK(diff / 2 < stepStart + 128u);
}

TEST_CASE("Engine: a pattern length edit takes effect without a rebuild") {
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  const PatternRef playing = song.arrangement[0].devicePatterns[1];
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(song));
  eng.play();
  renderBlocks(eng, 4);
  REQUIRE(eng.setPatternLength(1, playing.bank, playing.index, 3));
  renderBlocks(eng, 1);
  CHECK(eng.getPatternLength(1, playing.bank, playing.index) == 3);
}

// ── §4: control path ─────────────────────────────────────────────────

TEST_CASE("Engine: knob moves coalesce and never crowd out a Stop") {
  // With the AudioContext suspended no block drains the queue; 64 knob moves
  // used to fill it and the Stop after them was dropped silently.
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(loadFixture("standard-rebirth.rbs")));
  for (int i = 0; i < 500; ++i) {
    eng.setDeviceParam(0, 1, static_cast<float>(i % 100) / 100.0f);
    eng.setVolume(0.5f);
    eng.setTempo(120.0f + static_cast<float>(i % 10));
  }
  CHECK(eng.play());
  CHECK(eng.stop());

  // Discrete commands still report overflow instead of vanishing.
  bool rejected = false;
  for (int i = 0; i < 200 && !rejected; ++i) rejected = !eng.play();
  CHECK(rejected);
}

namespace {

/** A one-pattern song whose 303-A cutoff automation slams shut at bar 2. */
ParsedSong cutoffAutomatedSong() {
  ParsedSong song;
  song.title = "automation";
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) song.devices[i].id = static_cast<DeviceId>(i);
  song.devices[0].cutoff = 1.0f;
  song.devices[0].resonance = 0.0f;
  song.devices[0].envMod = 0.0f; // the cutoff knob alone sets the brightness
  Pattern p;
  p.deviceId = DeviceId::TB303_A;
  p.length = 16;
  for (int s = 0; s < MAX_STEPS; ++s) {
    p.steps[s].active = true;
    p.steps[s].note = 36;
  }
  song.patterns.push_back(p);
  for (uint16_t bar = 1; bar <= 4; ++bar) {
    ArrangementBar ab;
    ab.barNumber = bar;
    song.arrangement.push_back(ab);
  }
  AutomationEvent close;
  close.trackIndex = 1;
  close.tickPosition = AutomationScheduler::TICKS_PER_BAR; // start of bar 2
  close.controller = 0x03; // cutoff
  close.value = 0;
  song.automation.push_back(close);
  return song;
}

float rmsOf(const std::vector<float>& v) {
  double acc = 0.0;
  for (float s : v) acc += double(s) * s;
  return static_cast<float>(std::sqrt(acc / std::max<size_t>(1, v.size())));
}

} // namespace

TEST_CASE("Engine: seek lands on the automated knob values") {
  // Seek skipped earlier events without replaying them, so jumping past the
  // cutoff automation left the filter wide open.
  ParsedSong automated = cutoffAutomatedSong();
  ParsedSong closed = automated;
  closed.automation.clear();
  closed.devices[0].cutoff = 0.0f;

  RbsAudioEngine a;
  RbsAudioEngine b;
  REQUIRE(a.init(config44k()));
  REQUIRE(b.init(config44k()));
  REQUIRE(a.loadSong(automated));
  REQUIRE(b.loadSong(closed));
  for (RbsAudioEngine* eng : {&a, &b}) {
    eng->seek(3);
    eng->play();
  }
  ParsedSong open = automated;
  open.automation.clear();
  RbsAudioEngine c;
  REQUIRE(c.init(config44k()));
  REQUIRE(c.loadSong(open));
  c.seek(3);
  c.play();

  const float seeked = rmsOf(renderBlocks(a, 300));
  const float closedRms = rmsOf(renderBlocks(b, 300));
  const float openRms = rmsOf(renderBlocks(c, 300));
  REQUIRE(openRms > closedRms * 1.5f); // the knob is audible at all
  CHECK(seeked == doctest::Approx(closedRms).epsilon(0.01));
}

TEST_CASE("Engine: stop puts automated knobs back to the song's values") {
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(cutoffAutomatedSong()));
  eng.play();
  const auto first = renderBlocks(eng, 300); // bar 1: filter open
  renderBlocks(eng, 400);                    // into bar 2: closed
  eng.stop();
  eng.play();
  const auto again = renderBlocks(eng, 300);
  // Same bar-1 audio again (bit-identical apart from the delay tail, which
  // this song does not use).
  CHECK(firstDifference(first, again) == first.size());
}

TEST_CASE("Engine: tempo multiplier keeps automation locked to the notes") {
  // The sequencer ran unclamped while automation clamped, so a multiplied
  // tempo outside the range drifted them apart. Both now share one clamp.
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(song));
  eng.setTempo(MAX_BPM);
  eng.setTempoMultiplier(4.0f);
  eng.play();
  const auto out = renderBlocks(eng, 500);
  for (float s : out) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::fabs(s) <= 1.0f);
  }
}

// ── §2: chunked bounce ───────────────────────────────────────────────

TEST_CASE("Offline: the chunked bounce matches the whole-file WAV byte for byte") {
  // The browser bounce now renders in fixed chunks and assembles the WAV in
  // JavaScript, so the WASM heap holds one chunk instead of the whole file.
  // It must still be the same render.
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(song));
  eng.setDeviceParam(1, 1, 0.2f); // a live knob move must carry into both

  const uint32_t frames = RbsAudioEngine::OFFLINE_CHUNK_FRAMES * 2u + 12345u;
  for (uint8_t device : {RbsAudioEngine::MASTER_BUS, uint8_t{1}, uint8_t{3}}) {
    CAPTURE(int(device));
    const auto wav = eng.renderOfflineWav(frames, device);
    REQUIRE(wav.size() == 44u + size_t(frames) * 4u);

    REQUIRE(eng.beginOfflineRender(device));
    std::vector<uint8_t> body;
    uint32_t done = 0;
    while (done < frames) {
      // Uneven chunk sizes on purpose: the chunk boundary must not show.
      const uint32_t ask = std::min<uint32_t>(frames - done, done == 0 ? 1000u : 70000u);
      const uint32_t got = eng.renderOfflineChunk(ask);
      REQUIRE(got == std::min(ask, RbsAudioEngine::OFFLINE_CHUNK_FRAMES));
      body.insert(body.end(), eng.offlineChunkData(), eng.offlineChunkData() + size_t(got) * 4u);
      done += got;
    }
    eng.endOfflineRender();
    REQUIRE(body.size() == wav.size() - 44u);
    CHECK(std::memcmp(body.data(), wav.data() + 44, body.size()) == 0);
  }
  // Outside a begin/end pair nothing renders.
  CHECK(eng.renderOfflineChunk(128) == 0);
}

// ── §7: slim Embind surface ──────────────────────────────────────────

TEST_CASE("SongSummary: carries metadata and the slot map, patterns come on demand") {
  const ParsedSong song = loadFixture("standard-rebirth.rbs");
  const SongSummary summary = summariseSong(song);
  CHECK(summary.title == song.title);
  CHECK(summary.bpm == song.bpm);
  CHECK(summary.barCount == song.arrangement.size());
  size_t slots = 0;
  for (uint32_t mask : summary.patternSlots) slots += static_cast<size_t>(__builtin_popcount(mask));
  CHECK(slots == song.patterns.size());

  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(song));
  const Pattern& first = song.patterns.front();
  auto fetched = eng.getPattern(static_cast<uint8_t>(first.deviceId), first.bank,
                                first.patternIndex);
  REQUIRE(fetched.has_value());
  CHECK(fetched->length == first.length);
  CHECK(fetched->steps[0].note == first.steps[0].note);
  // Edits show up in what the UI fetches.
  StepData edit;
  edit.active = true;
  edit.note = 77;
  REQUIRE(eng.setStep(static_cast<uint8_t>(first.deviceId), first.bank, first.patternIndex, 0,
                      edit));
  CHECK(eng.getPattern(static_cast<uint8_t>(first.deviceId), first.bank, first.patternIndex)
            ->steps[0].note == 77);
  CHECK(eng.arrangement().size() == song.arrangement.size());
  CHECK_FALSE(eng.getPattern(9, 0, 0).has_value());
}

TEST_CASE("Mods: the sample arena is sized to the mod, not the 8 MiB budget") {
  // loadMod builds the new pool while the old one is still live, so two
  // fixed 8 MiB arenas used to sit in the 64 MiB heap at once.
  const auto bytes = readFixtureBytes("mods/sample-kit.rbm");
  RbsAudioEngine eng;
  REQUIRE(eng.init(config44k()));
  REQUIRE(eng.loadSong(loadFixture("standard-rebirth.rbs")));
  REQUIRE(eng.loadMod(bytes.data(), bytes.size()) == ModLoadStatus::Ok);
  const ModLoadReport& report = eng.lastModReport();
  CHECK(report.usedFrames > 0);
  CHECK(report.capacityFrames == report.usedFrames);
  CHECK(report.capacityFrames < SamplePool::kDefaultArenaFrames / 64);
}

// ── §5: DSP hygiene ──────────────────────────────────────────────────

#include "../synth/Tb303Voice.h"
#include <cfloat>
#include <chrono>

TEST_CASE("Tb303: the tail after note-off flushes to zero instead of going subnormal") {
  // WASM has no FTZ/DAZ; a decaying envelope or filter state that drifts
  // into the subnormal range makes every following operation slow.
  Tb303Voice voice;
  voice.init(44100.0f);
  DeviceState state;
  state.id = DeviceId::TB303_A;
  state.resonance = 0.9f;
  state.decay = 1.0f;
  voice.load(state);
  StepData on;
  on.active = true;
  on.note = 36;
  on.accent = true;
  voice.triggerStep(0, on);
  std::vector<float> note(4410);
  voice.render(note.data(), static_cast<uint32_t>(note.size()));
  voice.triggerStep(1, StepData{}); // note off

  std::vector<float> tail(44100 * 10);
  const auto start = std::chrono::steady_clock::now();
  voice.render(tail.data(), static_cast<uint32_t>(tail.size()));
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  MESSAGE("TB-303 10 s release tail: " << us << " us");

  size_t subnormal = 0;
  for (float s : tail) {
    if (s != 0.0f && std::fabs(s) < FLT_MIN) ++subnormal;
  }
  CHECK(subnormal == 0);
  // And the voice is genuinely silent by the end, not ringing at -300 dB.
  for (size_t i = tail.size() - 4410; i < tail.size(); ++i) REQUIRE(tail[i] == 0.0f);
}

// ── §8: inputs the fuzzers found ─────────────────────────────────────

#include "../audio/SampleDecoder.h"
#include "../parser/RbsCompare.h"
#include "../parser/RbsWriter.h"

TEST_CASE("Fuzz regression: a knob byte past 127 survives a save unchanged") {
  // fuzz_rbs_roundtrip: MIXR level 0xff parsed as 2.0, the writer clamped it
  // to 127, so the song changed on save. Knob bytes now clamp on read.
  const ParsedSong song = loadFixture("fuzz/roundtrip-mixr-level-255.rbs");
  for (const DeviceState& dev : song.devices) {
    CHECK(dev.level <= 1.0f);
    CHECK(dev.pan <= 1.0f);
  }
  RbsWriter writer;
  auto bytes = writer.write(song);
  REQUIRE(bytes.has_value());
  RbsParser parser;
  auto reloaded = parser.parse(bytes->data(), bytes->size());
  REQUIRE(reloaded.has_value());
  CHECK(semanticDifference(song, *reloaded) == "");
}

TEST_CASE("Fuzz regression: a hostile MS-ADPCM WAV decodes without overflow") {
  // fuzz_sample_decoder: dr_wav's MS-ADPCM step overflowed int32 (signed
  // overflow is UB; the asan-ubsan leg now fails on it). Patched to 64-bit.
  const auto bytes = readFixtureBytes("fuzz/msadpcm-delta-overflow.wav");
  std::vector<float> dest(4096);
  SampleInfo info;
  const SampleDecodeStatus status =
      decodeSampleMono(bytes.data(), bytes.size(), dest.data(), dest.size(), info);
  if (status == SampleDecodeStatus::Ok) {
    for (uint32_t i = 0; i < info.frameCount; ++i) REQUIRE(std::isfinite(dest[i]));
  }
  CHECK(info.frameCount <= dest.size());
}

TEST_CASE("Fuzz regression: a whitespace-only title is rejected before a save, not after") {
  // fuzz_rbs_roundtrip: a v1 (MIDI container) song whose title was all
  // spaces parsed — the v1 path keeps padding — but its v2 save did not,
  // because the GLOB reader trims. The title check now applies to the
  // trimmed title, so the original is refused up front.
  const auto bytes = readFixtureBytes("fuzz/roundtrip-blank-v1-title.rbs");
  RbsParser parser;
  CHECK_FALSE(parser.parse(bytes.data(), bytes.size()).has_value());
  CHECK(parser.lastError().find("no title") != std::string::npos);
}

TEST_CASE("Parser: GLOB strings are read only within their fixed-width fields") {
  // An unterminated 65-byte title used to run on into the FTP URL field —
  // a title the writer cannot store, so it changed on save.
  auto bytes = readFixtureBytes("standard-rebirth.rbs");
  const std::string glob = "GLOB";
  auto it = std::search(bytes.begin(), bytes.end(), glob.begin(), glob.end());
  REQUIRE(it != bytes.end());
  const size_t body = static_cast<size_t>(it - bytes.begin()) + 8;
  for (size_t i = 0; i < 65; ++i) bytes[body + 0x0f + i] = 'T';  // no terminator
  for (size_t i = 0; i < 8; ++i) bytes[body + 0x50 + i] = 'U';   // FTP URL text

  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  REQUIRE(song.has_value());
  CHECK(song->title == std::string(64, 'T'));
  RbsWriter writer;
  auto saved = writer.write(*song);
  REQUIRE(saved.has_value());
  RbsParser reparser;
  auto reloaded = reparser.parse(saved->data(), saved->size());
  REQUIRE(reloaded.has_value());
  CHECK(semanticDifference(*song, *reloaded) == "");
}
