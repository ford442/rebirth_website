// Groove and sound fidelity (issue: "Groove & sound fidelity"): shuffle,
// transport semantics, TB-303 envelopes / gate / accent / slide, drum kit
// parameters, flam and accent, and the oversampled distortion.

#include "../engine/AutomationScheduler.h"
#include "../engine/Mixer.h"
#include "../engine/RbsAudioEngine.h"
#include "../engine/Sequencer.h"
#include "../parser/DrumKit.h"
#include "../parser/RbsParser.h"
#include "../synth/DrumBitfield.h"
#include "../synth/Tb303Voice.h"
#include "../synth/Tr808Voice.h"
#include "../synth/Tr909Voice.h"
#include "../synth/dsp/OversampledShaper.h"
#include "../third_party/doctest.h"
#include "SpectralAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

using namespace rb338;

namespace {

constexpr float kRate = 44100.0f;

double samplesPerStep(float bpm, float rate = kRate) {
  return 60.0 / bpm * 0.25 * rate;
}

/** A song whose `device` plays `pattern` (bank A, pattern 1) from bar 1. */
ParsedSong songWith(const Pattern& pattern, float bpm = 120.0f) {
  ParsedSong song;
  song.title = "Fidelity";
  song.bpm = bpm;
  for (int i = 0; i < NUM_DEVICES; ++i) song.devices[i].id = static_cast<DeviceId>(i);
  song.patterns.push_back(pattern);
  return song;
}

Pattern drumPattern(DeviceId device, uint8_t hit, bool shuffle, uint8_t extra = 0) {
  Pattern p;
  p.deviceId = device;
  p.length = 16;
  p.shuffle = shuffle;
  for (auto& s : p.steps) {
    s.active = true;
    s.note = hit;
    s.drumExtra = extra;
  }
  return p;
}

/** Every Trigger event of an active step, as (absolute frame, device). */
std::vector<std::pair<uint64_t, int>> noteOnsets(const ParsedSong& song, uint64_t frames,
                                                 uint32_t block = 128) {
  Sequencer seq;
  seq.reset();
  std::vector<std::pair<uint64_t, int>> out;
  Sequencer::Event events[128];
  for (uint64_t at = 0; at < frames && !seq.ended(); at += block) {
    const uint32_t n = seq.generateEvents(&song, song.bpm, kRate, block, events, 128);
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t frame = at + events[i].sampleOffset;
      // The last block can overhang `frames`; what lies past it is not asked for.
      if (frame < frames && events[i].kind == Sequencer::EventKind::Trigger &&
          events[i].step.active) {
        out.emplace_back(frame, static_cast<int>(events[i].device));
      }
    }
  }
  return out;
}

/** Render the engine's mono mix of `song` for `frames` from bar 1. */
std::vector<float> renderMono(const ParsedSong& song, uint32_t frames) {
  RbsAudioEngine eng;
  EngineConfig cfg;
  cfg.sampleRate = kRate;
  REQUIRE(eng.init(cfg));
  REQUIRE(eng.loadSong(song));
  std::vector<float> stereo(static_cast<size_t>(frames) * 2u);
  REQUIRE(eng.renderOffline(stereo.data(), frames) == frames);
  std::vector<float> mono(frames);
  for (uint32_t i = 0; i < frames; ++i) mono[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
  return mono;
}

/** First sample of each window of `window` frames that rises above `threshold`. */
std::vector<int64_t> audioOnsets(const std::vector<float>& mono, double window, size_t count,
                                 float threshold) {
  std::vector<int64_t> onsets;
  for (size_t w = 0; w < count; ++w) {
    const auto begin = static_cast<size_t>(std::llround(w * window));
    const auto end = std::min(mono.size(), static_cast<size_t>(std::llround((w + 1) * window)));
    int64_t found = -1;
    for (size_t i = begin; i < end; ++i) {
      if (std::fabs(mono[i]) > threshold) {
        found = static_cast<int64_t>(i);
        break;
      }
    }
    onsets.push_back(found);
  }
  return onsets;
}

template <typename V>
std::vector<float> renderVoice(V& voice, uint32_t frames) {
  std::vector<float> out(frames, 0.0f);
  for (uint32_t done = 0; done < frames; done += 128) {
    voice.render(out.data() + done, std::min<uint32_t>(128, frames - done));
  }
  return out;
}

float peakOf(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX) {
  float peak = 0.0f;
  for (size_t i = from; i < std::min(to, v.size()); ++i) peak = std::max(peak, std::fabs(v[i]));
  return peak;
}

StepData note(uint8_t midi, bool accent = false, bool slide = false) {
  StepData s;
  s.active = true;
  s.note = midi;
  s.accent = accent;
  s.slide = slide;
  return s;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════
// Shuffle
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("Shuffle: off-beat sixteenths move by shuffle/127 x 1/3 step, on-beats stay") {
  const float bpm = 120.0f;
  const double step = samplesPerStep(bpm);
  for (uint8_t amount : {uint8_t{0}, uint8_t{64}, uint8_t{127}}) {
    CAPTURE(static_cast<int>(amount));
    ParsedSong song = songWith(drumPattern(DeviceId::TR909, DrumHit::BD, true), bpm);
    song.shuffle = amount;
    const auto onsets = noteOnsets(song, static_cast<uint64_t>(step * 16));
    REQUIRE(onsets.size() == 16);
    const double swing = amount / 127.0 * Sequencer::MAX_SHUFFLE_STEPS * step;
    for (size_t s = 0; s < 16; ++s) {
      const double expected = s * step + ((s & 1u) ? swing : 0.0);
      CAPTURE(s);
      // First whole sample at or after the exact time.
      CHECK(static_cast<double>(onsets[s].first) >= expected - 1e-6);
      CHECK(static_cast<double>(onsets[s].first) < expected + 1.0);
    }
  }
}

TEST_CASE("Shuffle: a pattern without its shuffle flag plays straight") {
  ParsedSong song = songWith(drumPattern(DeviceId::TR909, DrumHit::BD, false));
  song.shuffle = 127;
  const double step = samplesPerStep(song.bpm);
  const auto onsets = noteOnsets(song, static_cast<uint64_t>(step * 16));
  REQUIRE(onsets.size() == 16);
  for (size_t s = 0; s < 16; ++s) {
    CHECK(static_cast<double>(onsets[s].first) < s * step + 1.0);
  }
}

TEST_CASE("Shuffle: onsets are block-size invariant") {
  ParsedSong song = songWith(drumPattern(DeviceId::TR909, DrumHit::BD, true));
  song.shuffle = 100;
  const auto frames = static_cast<uint64_t>(samplesPerStep(song.bpm) * 32);
  CHECK(noteOnsets(song, frames, 128) == noteOnsets(song, frames, 97));
  CHECK(noteOnsets(song, frames, 128) == noteOnsets(song, frames, 1));
}

TEST_CASE("Shuffle: shuffle 0 vs 127 renders show the predicted onset offsets") {
  // Acceptance check through the whole engine: a 909 rim shot on every
  // sixteenth, rendered twice, onsets found in the audio.
  const float bpm = 120.0f;
  const double step = samplesPerStep(bpm);
  auto render = [&](uint8_t amount) {
    ParsedSong song = songWith(drumPattern(DeviceId::TR909, 0, true, DrumExtra::RS), bpm);
    song.shuffle = amount;
    song.fx.delay.enabled = false;
    song.fx.comp.enabled = false;
    song.fx.dist.enabled = false;
    song.fx.pcf.enabled = false;
    song.devices[3].level = 1.0f;
    return audioOnsets(renderMono(song, static_cast<uint32_t>(step * 16 + 256)), step, 16,
                       0.01f);
  };
  const auto straight = render(0);
  const auto swung = render(127);
  const double expectedShift = Sequencer::MAX_SHUFFLE_STEPS * step; // 1837.5 samples
  for (size_t s = 0; s < 16; ++s) {
    CAPTURE(s);
    REQUIRE(straight[s] >= 0);
    REQUIRE(swung[s] >= 0);
    const double shift = static_cast<double>(swung[s] - straight[s]);
    if (s & 1u) {
      CHECK(shift == doctest::Approx(expectedShift).epsilon(0.002));
    } else {
      CHECK(shift == 0.0);
    }
  }
}

// ═════════════════════════════════════════════════════════════════════
// Transport: song end, loop, pattern mode
// ═════════════════════════════════════════════════════════════════════

namespace {

ParsedSong threeBarSong() {
  // Bar n plays pattern n, whose only note is on step n — so an onset's
  // step says which bar it came from.
  ParsedSong song;
  song.title = "Three bars";
  song.bpm = 120.0f;
  for (int i = 0; i < NUM_DEVICES; ++i) song.devices[i].id = static_cast<DeviceId>(i);
  for (uint8_t bar = 0; bar < 3; ++bar) {
    Pattern p;
    p.deviceId = DeviceId::TR909;
    p.patternIndex = bar;
    p.steps[bar].active = true;
    p.steps[bar].note = DrumHit::BD;
    song.patterns.push_back(p);
    ArrangementBar ab;
    ab.barNumber = static_cast<uint16_t>(bar + 1);
    ab.devicePatterns[3] = PatternRef{0, bar};
    song.arrangement.push_back(ab);
  }
  return song;
}

/** (bar index, step) of each 909 onset. */
std::vector<std::pair<int, int>> barSteps(const ParsedSong& song, uint64_t frames) {
  const double bar = samplesPerStep(song.bpm) * 16;
  std::vector<std::pair<int, int>> out;
  for (const auto& [at, device] : noteOnsets(song, frames)) {
    if (device != 3) continue;
    const auto b = static_cast<int>(static_cast<double>(at) / bar);
    const auto s = static_cast<int>(std::lround((static_cast<double>(at) - b * bar) /
                                                samplesPerStep(song.bpm)));
    out.emplace_back(b, s);
  }
  return out;
}

} // namespace

TEST_CASE("Transport: song mode stops after the last bar") {
  const ParsedSong song = threeBarSong();
  const double bar = samplesPerStep(song.bpm) * 16;
  const auto onsets = barSteps(song, static_cast<uint64_t>(bar * 8));
  REQUIRE(onsets.size() == 3);
  CHECK(onsets[0] == std::make_pair(0, 0));
  CHECK(onsets[1] == std::make_pair(1, 1));
  CHECK(onsets[2] == std::make_pair(2, 2));
  CHECK(Sequencer::songLengthBars(song) == 3);
}

TEST_CASE("Transport: the loop region jumps from its end back to its start") {
  ParsedSong song = threeBarSong();
  song.loopEnabled = true;
  song.loopStart = 1 * GLOB_TICKS_PER_BAR;
  song.loopEnd = 2 * GLOB_TICKS_PER_BAR; // loop bar 2 (0-based 1) forever
  const double bar = samplesPerStep(song.bpm) * 16;
  const auto onsets = barSteps(song, static_cast<uint64_t>(bar * 5));
  REQUIRE(onsets.size() == 5);
  CHECK(onsets[0].second == 0);
  for (size_t i = 1; i < onsets.size(); ++i) CHECK(onsets[i].second == 1);
  CHECK(Sequencer::songLengthBars(song) == 2);
}

TEST_CASE("Transport: a loop that starts past the song's end is ignored") {
  ParsedSong song = threeBarSong();
  song.loopEnabled = true;
  song.loopStart = 10 * GLOB_TICKS_PER_BAR;
  song.loopEnd = 11 * GLOB_TICKS_PER_BAR;
  uint32_t start = 0;
  uint32_t end = 0;
  CHECK_FALSE(Sequencer::loopRegion(song, start, end));
  CHECK(barSteps(song, static_cast<uint64_t>(samplesPerStep(song.bpm) * 16 * 6)).size() == 3);
}

TEST_CASE("Transport: pattern mode ignores the arrangement and queues changes to the bar") {
  ParsedSong song = threeBarSong();
  song.playMode = PlayMode::Pattern;
  song.devices[3].initialPatternIndex = 2; // selected: pattern 3 (note on step 2)
  Sequencer seq;
  seq.reset();
  const double step = samplesPerStep(song.bpm);
  Sequencer::Event events[128];
  std::vector<std::pair<uint64_t, uint8_t>> hits; // (frame, step index of the note)
  uint64_t at = 0;
  bool queued = false;
  while (at < static_cast<uint64_t>(step * 16 * 4)) {
    // Half way through bar 2, queue pattern 2 (note on step 1).
    if (!queued && at >= static_cast<uint64_t>(step * 24)) {
      seq.queuePattern(3, 1);
      queued = true;
    }
    const uint32_t n = seq.generateEvents(&song, song.bpm, kRate, 128, events, 128);
    for (uint32_t i = 0; i < n; ++i) {
      if (events[i].device == DeviceId::TR909 && events[i].step.active) {
        hits.emplace_back(at + events[i].sampleOffset, events[i].stepIndex);
      }
    }
    at += 128;
  }
  CHECK_FALSE(seq.ended());
  REQUIRE(hits.size() == 4);
  CHECK(hits[0].second == 2); // bars 1 and 2: the selected pattern
  CHECK(hits[1].second == 2);
  CHECK(hits[2].second == 1); // bar 3 on: the queued one, from its downbeat
  CHECK(hits[3].second == 1);
  CHECK(static_cast<double>(hits[2].first) == doctest::Approx(step * (32 + 1)).epsilon(1e-4));
  CHECK(Sequencer::songLengthBars(song) == 0); // never ends
}

TEST_CASE("Engine: setPlayMode / selectPattern drive the live transport and the save") {
  ParsedSong song = threeBarSong();
  RbsAudioEngine eng;
  EngineConfig cfg;
  REQUIRE(eng.init(cfg));
  REQUIRE(eng.loadSong(song));
  CHECK(eng.playMode() == 1);
  REQUIRE(eng.setPlayMode(0));
  REQUIRE(eng.selectPattern(3, 0, 2));
  CHECK(eng.playMode() == 0);
  CHECK_FALSE(eng.setPlayMode(2));
  CHECK_FALSE(eng.selectPattern(3, 4, 0));

  // Pattern mode never ends: render well past the 3-bar arrangement.
  eng.play();
  float left[128];
  float right[128];
  float* buffers[2] = {left, right};
  for (int i = 0; i < 2000; ++i) eng.processBlock(buffers, 2, 128);
  CHECK(eng.isPlaying());

  RbsParser parser;
  const auto saved = eng.saveRbs();
  auto reparsed = parser.parse(saved.data(), saved.size());
  REQUIRE(reparsed);
  CHECK(reparsed->playMode == PlayMode::Pattern);
  CHECK(reparsed->devices[3].initialPatternIndex == 2);
}

TEST_CASE("Engine: Play after the song ran out starts again from bar 1") {
  ParsedSong song = threeBarSong();
  RbsAudioEngine eng;
  EngineConfig cfg;
  REQUIRE(eng.init(cfg));
  REQUIRE(eng.loadSong(song));
  eng.play();
  float left[128];
  float right[128];
  float* buffers[2] = {left, right};
  const int barBlocks = static_cast<int>(samplesPerStep(song.bpm) * 16 / 128) + 1;
  for (int i = 0; i < barBlocks * 4; ++i) eng.processBlock(buffers, 2, 128);
  CHECK_FALSE(eng.isPlaying());
  eng.play();
  eng.processBlock(buffers, 2, 128);
  CHECK(eng.isPlaying());
  uint16_t bar = 0;
  uint8_t step = 0;
  eng.getPlaybackPosition(bar, step);
  CHECK(bar == 1);
}

// ═════════════════════════════════════════════════════════════════════
// TB-303
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("Sequencer: the 303 gate closes half a step after a note unless it slides") {
  Pattern p;
  p.deviceId = DeviceId::TB303_A;
  p.steps[0] = note(48);
  p.steps[1] = note(50, false, true); // slides into step 2
  p.steps[2] = note(52);
  ParsedSong song = songWith(p);
  Sequencer seq;
  seq.reset();
  const double step = samplesPerStep(song.bpm);
  Sequencer::Event events[128];
  std::vector<uint64_t> releases;
  for (uint64_t at = 0; at < static_cast<uint64_t>(step * 4); at += 128) {
    const uint32_t n = seq.generateEvents(&song, song.bpm, kRate, 128, events, 128);
    for (uint32_t i = 0; i < n; ++i) {
      if (events[i].device == DeviceId::TB303_A &&
          events[i].kind == Sequencer::EventKind::Release) {
        releases.push_back(at + events[i].sampleOffset);
      }
    }
  }
  REQUIRE(releases.size() == 2); // steps 0 and 2; step 1 holds the gate
  CHECK(static_cast<double>(releases[0]) == doctest::Approx(0.5 * step).epsilon(1e-3));
  CHECK(static_cast<double>(releases[1]) == doctest::Approx(2.5 * step).epsilon(1e-3));
}

TEST_CASE("Tb303Voice: the VCA and VCF envelopes are separate") {
  // DECAY moves the filter envelope only; the VCA's decay is fixed.
  auto envelopesAfter = [](float decayKnob) {
    Tb303Voice voice;
    voice.init(kRate);
    DeviceState state;
    state.decay = decayKnob;
    voice.load(state);
    voice.triggerStep(0, note(48));
    renderVoice(voice, 4410); // 100 ms
    return std::make_pair(voice.vcaEnvelope(), voice.vcfEnvelope());
  };
  const auto shortDecay = envelopesAfter(0.0f);
  const auto longDecay = envelopesAfter(1.0f);
  CHECK(shortDecay.first == doctest::Approx(longDecay.first).epsilon(1e-6));
  CHECK(longDecay.second > shortDecay.second * 1.3f);
  // At DECAY 0 the VCF envelope is the 200 ms minimum: e^(-0.1/0.2).
  CHECK(shortDecay.second == doctest::Approx(std::exp(-0.5f)).epsilon(0.01));
  // The VCA's 1 s decay has barely moved after 100 ms.
  CHECK(shortDecay.first == doctest::Approx(std::exp(-0.1f)).epsilon(0.01));
}

TEST_CASE("Tb303Voice: closing the gate releases the VCA within milliseconds") {
  Tb303Voice voice;
  voice.init(kRate);
  voice.load(DeviceState{});
  voice.triggerStep(0, note(48));
  renderVoice(voice, 2000);
  REQUIRE(voice.gateOpen());
  voice.releaseGate();
  renderVoice(voice, static_cast<uint32_t>(0.04f * kRate)); // five release time constants
  CHECK_FALSE(voice.gateOpen());
  CHECK(voice.vcaEnvelope() < 0.01f);
}

TEST_CASE("Tb303Voice: consecutive accents charge the accent capacitor higher") {
  Tb303Voice voice;
  voice.init(kRate);
  DeviceState state;
  state.accent = 1.0f;
  voice.load(state);
  const auto step = static_cast<uint32_t>(samplesPerStep(125.0f));
  std::vector<float> peaks;
  for (int n = 0; n < 4; ++n) {
    voice.triggerStep(0, note(48, true));
    float peak = 0.0f;
    for (uint32_t i = 0; i < step; i += 16) {
      renderVoice(voice, 16);
      peak = std::max(peak, voice.accentCharge());
    }
    peaks.push_back(peak);
  }
  CHECK(peaks[1] > peaks[0] * 1.05f);
  CHECK(peaks[2] > peaks[1]);
  CHECK(peaks[3] > peaks[2]);
  CHECK(peaks[3] <= 1.0f);

  // An unaccented note lets it drain.
  voice.triggerStep(0, note(48, false));
  renderVoice(voice, static_cast<uint32_t>(0.5f * kRate));
  CHECK(voice.accentCharge() < 0.1f * peaks[3]);
}

TEST_CASE("Tb303Voice: slide reaches 63% of the interval in ~60 ms at 44.1 and 48 kHz") {
  for (float rate : {44100.0f, 48000.0f}) {
    CAPTURE(rate);
    Tb303Voice voice;
    voice.init(rate);
    voice.load(DeviceState{});
    voice.triggerStep(0, note(48, false, true));
    renderVoice(voice, 256);
    const float from = std::log2(voice.currentPitchHz());
    voice.triggerStep(1, note(60)); // slid into: an octave up
    const float to = from + 1.0f;
    const auto tau = static_cast<uint32_t>(Tb303Voice::kSlideTimeSeconds * rate);
    renderVoice(voice, tau);
    const float progress = (std::log2(voice.currentPitchHz()) - from) / (to - from);
    CHECK(progress == doctest::Approx(1.0f - std::exp(-1.0f)).epsilon(0.02));
  }
}

// ═════════════════════════════════════════════════════════════════════
// Drum machines
// ═════════════════════════════════════════════════════════════════════

namespace {

template <typename V>
float hitPeak(DeviceId device, const DeviceState& state, const StepData& step) {
  V voice;
  voice.init(kRate);
  DeviceState s = state;
  s.id = device;
  voice.load(s);
  voice.triggerStep(0, step);
  return peakOf(renderVoice(voice, 8192));
}

StepData drumStep(uint8_t hits, uint8_t extra = 0) {
  StepData s;
  s.active = true;
  s.note = hits;
  s.drumExtra = extra;
  return s;
}

DeviceState withKit(DeviceId device, uint8_t controller, uint8_t value) {
  DeviceState state;
  state.kit = defaultDrumKit(device);
  state.kit[kitIndex(controller)] = value;
  state.hasKit = true;
  return state;
}

} // namespace

TEST_CASE("Drums: per-instrument LEVEL scales only that instrument") {
  const StepData kick = drumStep(DrumHit::BD);
  const StepData snare = drumStep(DrumHit::SD);
  const DeviceState quietKick = withKit(DeviceId::TR909, Kit909::BassLevel, 50);
  const float kickDefault = hitPeak<Tr909Voice>(DeviceId::TR909, DeviceState{}, kick);
  const float kickQuiet = hitPeak<Tr909Voice>(DeviceId::TR909, quietKick, kick);
  CHECK(kickQuiet == doctest::Approx(kickDefault * 0.5f).epsilon(0.02));
  CHECK(hitPeak<Tr909Voice>(DeviceId::TR909, quietKick, snare) ==
        doctest::Approx(hitPeak<Tr909Voice>(DeviceId::TR909, DeviceState{}, snare)));
  CHECK(hitPeak<Tr808Voice>(DeviceId::TR808, withKit(DeviceId::TR808, Kit808::BassLevel, 0),
                            kick) == 0.0f);
}

TEST_CASE("Drums: tune and decay kit knobs change the sound") {
  auto render = [](const DeviceState& state) {
    Tr909Voice voice;
    voice.init(kRate);
    voice.load(state);
    voice.triggerStep(0, drumStep(DrumHit::LT));
    return renderVoice(voice, 16384);
  };
  // Pitch from zero crossings over the first 50 ms (a tom is a swept sine).
  auto crossings = [](const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < static_cast<size_t>(0.05f * kRate); ++i) {
      if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) ++n;
    }
    return n;
  };
  const auto base = render(DeviceState{});
  const auto high = render(withKit(DeviceId::TR909, Kit909::LowTomTune, 127));
  const auto longer = render(withKit(DeviceId::TR909, Kit909::LowTomDecay, 127));
  // TUNE 127 is ~+6 semitones: 1.41× the crossings.
  CHECK(crossings(high) > crossings(base) * 1.3);
  CHECK(peakOf(longer, 8000) > peakOf(base, 8000) * 1.5f);
}

TEST_CASE("Tr808: the selector switches pick congas, claves and maracas") {
  auto render = [](uint8_t selector, uint8_t value, const StepData& step) {
    Tr808Voice voice;
    voice.init(kRate);
    voice.load(withKit(DeviceId::TR808, selector, value));
    voice.triggerStep(0, step);
    return renderVoice(voice, 8192);
  };
  auto crossings = [](const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < static_cast<size_t>(0.05f * kRate); ++i) {
      if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) ++n;
    }
    return n;
  };
  const auto tom = render(Kit808::LowTomSelector, 0, drumStep(DrumHit::LT));
  const auto conga = render(Kit808::LowTomSelector, 1, drumStep(DrumHit::LT));
  CHECK(crossings(conga) > crossings(tom) * 1.3);
  const auto rim = render(Kit808::RimShotSelector, 1, drumStep(0, DrumExtra::RS));
  const auto claves = render(Kit808::RimShotSelector, 0, drumStep(0, DrumExtra::RS));
  CHECK(rim != claves);
  const auto clap = render(Kit808::ClapSelector, 1, drumStep(0, DrumExtra::CP));
  const auto maracas = render(Kit808::ClapSelector, 0, drumStep(0, DrumExtra::CP));
  CHECK(clap != maracas);
  // The 808's own rows: cowbell and cymbal sound.
  CHECK(peakOf(render(Kit808::RimShotSelector, 1, drumStep(DrumHit::CB))) > 0.02f);
  CHECK(peakOf(render(Kit808::RimShotSelector, 1, drumStep(0, DrumExtra::CY))) > 0.02f);
}

TEST_CASE("Drums: the closed hat chokes the open hat") {
  Tr909Voice voice;
  voice.init(kRate);
  voice.load(DeviceState{});
  voice.triggerStep(0, drumStep(DrumHit::OH));
  renderVoice(voice, 1000);
  voice.triggerStep(1, drumStep(DrumHit::CH));
  const auto out = renderVoice(voice, 8000);
  // The closed hat is ~55 ms at most; the open hat would ring for 280 ms.
  CHECK(peakOf(out, static_cast<size_t>(0.08f * kRate)) < 1e-4f);
}

TEST_CASE("Tr909: a flam is a quieter grace hit, then the hit FLAM INTERVAL later") {
  const uint8_t interval = 64;
  const DeviceState state = withKit(DeviceId::TR909, Kit909::FlamInterval, interval);
  StepData step = drumStep(0, DrumExtra::RS); // rim shot: short and percussive
  step.drumFlam = 1u << 10;                   // kHitRS
  Tr909Voice voice;
  voice.init(kRate);
  voice.load(state);
  voice.triggerStep(0, step);
  const auto out = renderVoice(voice, 4096);

  const float flamSeconds = Tr909Kit::flamSeconds(state.kit);
  const auto offset = static_cast<size_t>(std::lround(flamSeconds * kRate));
  const float grace = peakOf(out, 0, offset);
  const float main = peakOf(out, offset, offset + 400);
  CHECK(grace > 0.01f);
  CHECK(main > grace * 1.5f);

  // Without the flam the hit is one onset at the full level.
  StepData plain = drumStep(0, DrumExtra::RS);
  Tr909Voice reference;
  reference.init(kRate);
  reference.load(state);
  reference.triggerStep(0, plain);
  CHECK(peakOf(renderVoice(reference, 4096), 0, 400) == doctest::Approx(main).epsilon(0.05));
}

TEST_CASE("Drums: AC row x ACCENT knob, and the 909's own instrument accent") {
  StepData plain = drumStep(DrumHit::SD);
  StepData accented = plain;
  accented.accent = true;
  DeviceState low;
  low.accent = 0.0f;
  DeviceState high;
  high.accent = 1.0f;
  const float p808 = hitPeak<Tr808Voice>(DeviceId::TR808, low, plain);
  CHECK(hitPeak<Tr808Voice>(DeviceId::TR808, low, accented) > p808 * 1.05f);
  CHECK(hitPeak<Tr808Voice>(DeviceId::TR808, high, accented) >
        hitPeak<Tr808Voice>(DeviceId::TR808, low, accented) * 1.2f);

  StepData strong = plain;
  strong.drumAccent = 1u << 1; // kHitSD
  CHECK(hitPeak<Tr909Voice>(DeviceId::TR909, low, strong) >
        hitPeak<Tr909Voice>(DeviceId::TR909, low, plain) * 1.2f);
  // The 808 has no per-instrument accent.
  CHECK(hitPeak<Tr808Voice>(DeviceId::TR808, low, strong) == doctest::Approx(p808));
}

TEST_CASE("Automation: drum-track controllers drive the kit, not 303 knobs") {
  Tr909Voice voice;
  voice.init(kRate);
  voice.load(DeviceState{});
  Voice* voices[NUM_DEVICES] = {nullptr, nullptr, nullptr, &voice};
  AutomationScheduler::Event ev;
  ev.trackIndex = 4; // TR-909
  ev.controller = Kit909::SnareSnappy;
  ev.value = 17;
  AutomationScheduler::applyEvent(voices, nullptr, ev);
  CHECK(voice.kit()[kitIndex(Kit909::SnareSnappy)] == 17);

  // A user override of that kit knob masks its automation.
  ParamMask mask;
  mask.bits[3] = uint64_t{1} << static_cast<unsigned>(kitParam(kitIndex(Kit909::SnareSnappy)));
  ev.value = 99;
  AutomationScheduler::applyEvent(voices, nullptr, ev, mask);
  CHECK(voice.kit()[kitIndex(Kit909::SnareSnappy)] == 17);
}

TEST_CASE("Engine: a kit knob move is heard and saved") {
  ParsedSong song = songWith(drumPattern(DeviceId::TR909, DrumHit::BD, false));
  RbsAudioEngine eng;
  EngineConfig cfg;
  REQUIRE(eng.init(cfg));
  REQUIRE(eng.loadSong(song));
  const uint8_t param = static_cast<uint8_t>(kitParam(kitIndex(Kit909::BassLevel)));
  eng.setDeviceParam(3, param, 0.0f);
  std::vector<float> out(4096u * 2u);
  eng.renderOffline(out.data(), 4096);
  CHECK(peakOf(out) == 0.0f);

  RbsParser parser;
  const auto saved = eng.saveRbs();
  auto reparsed = parser.parse(saved.data(), saved.size());
  REQUIRE(reparsed);
  CHECK(reparsed->devices[3].kit[kitIndex(Kit909::BassLevel)] == 0);
  CHECK(reparsed->devices[3].kit[kitIndex(Kit909::SnareLevel)] ==
        defaultDrumKit(DeviceId::TR909)[kitIndex(Kit909::SnareLevel)]);
}

TEST_CASE("Engine: setStep keeps 909 dynamics, strips them elsewhere") {
  ParsedSong song = songWith(drumPattern(DeviceId::TR909, DrumHit::BD, false));
  RbsAudioEngine eng;
  EngineConfig cfg;
  REQUIRE(eng.init(cfg));
  REQUIRE(eng.loadSong(song));
  StepData step = drumStep(DrumHit::BD | DrumHit::SD);
  step.drumAccent = 0x01 | 0x400; // BD, plus RS which the step does not hit
  step.drumFlam = 0x02;
  REQUIRE(eng.setStep(3, 0, 0, 0, step));
  const StepData stored = eng.getStep(3, 0, 0, 0);
  CHECK(stored.drumAccent == 0x01);
  CHECK(stored.drumFlam == 0x02);
  REQUIRE(eng.setStep(2, 0, 0, 0, step));
  CHECK(eng.getStep(2, 0, 0, 0).drumAccent == 0);
  CHECK(eng.getStep(2, 0, 0, 0).drumFlam == 0);
}

// ═════════════════════════════════════════════════════════════════════
// Distortion aliasing
// ═════════════════════════════════════════════════════════════════════

namespace {

/**
 * Aliased energy in 10–20 kHz relative to the total, in dB: everything in
 * the band that is not a harmonic of the (bin-centred) test tone.
 */
double aliasDb(const std::vector<float>& y, size_t toneBin) {
  std::vector<float> re(y.begin(), y.end());
  std::vector<float> im(y.size(), 0.0f);
  test::fftRadix2(re, im);
  const size_t n = y.size();
  const auto lo = static_cast<size_t>(10000.0 * n / kRate);
  const auto hi = static_cast<size_t>(20000.0 * n / kRate);
  double total = 0.0;
  double alias = 0.0;
  for (size_t k = 1; k < n / 2; ++k) {
    const double power = static_cast<double>(re[k]) * re[k] + static_cast<double>(im[k]) * im[k];
    total += power;
    if (k >= lo && k < hi && k % toneBin != 0) alias += power;
  }
  return 10.0 * std::log10(alias / total + 1e-30);
}

std::vector<float> shapeTone(size_t bin, float drive, bool oversampled) {
  // Coherent sampling: the tone sits exactly on `bin`, so harmonics land on
  // multiples of it and an aliased component lands anywhere else. A lead-in
  // lets the oversampler's filters settle before the measured window.
  constexpr size_t kN = 16384;
  constexpr size_t kLead = 1024;
  dsp::OversampledShaper shaper;
  shaper.prepare();
  std::vector<float> out(kN);
  for (size_t i = 0; i < kN + kLead; ++i) {
    const float x = 0.6f * static_cast<float>(std::sin(2.0 * 3.141592653589793 *
                                                       static_cast<double>(bin * i) / kN));
    const float y = oversampled ? shaper.process(x, drive)
                                : dsp::OversampledShaper::shape(x, drive);
    if (i >= kLead) out[i - kLead] = y;
  }
  return out;
}

} // namespace

TEST_CASE("Distortion: oversampling cuts aliasing above 10 kHz by at least 30 dB") {
  // Tones from 2.5 to 7 kHz, drive across the DIST byte range (the mixer maps
  // 0–127 to 1–10). Below ~2 kHz the plain shaper's aliasing is already
  // under −50 dB, so there is little left to remove.
  constexpr size_t kN = 16384;
  for (double hz : {2500.0, 4000.0, 7000.0}) {
    // Odd, so no folded harmonic lands back on a harmonic bin.
    const size_t bin = static_cast<size_t>(hz * kN / kRate) | 1u;
    for (float drive : {3.3f, 6.7f, 10.0f}) {
      CAPTURE(hz);
      CAPTURE(drive);
      const double plain = aliasDb(shapeTone(bin, drive, false), bin);
      const double oversampled = aliasDb(shapeTone(bin, drive, true), bin);
      CHECK(plain - oversampled >= 30.0);
    }
  }
}

TEST_CASE("Distortion: the oversampled shaper keeps the plain shaper's level and tone") {
  // Same curve, just without the aliases: the harmonic it makes most of
  // (the fundamental) matches to within a fraction of a dB.
  constexpr size_t kN = 16384;
  const size_t bin = 37; // ~100 Hz
  for (float drive : {1.0f, 5.0f, 10.0f}) {
    auto a = shapeTone(bin, drive, false);
    auto b = shapeTone(bin, drive, true);
    std::vector<float> reA(a.begin(), a.end()), imA(kN, 0.0f);
    std::vector<float> reB(b.begin(), b.end()), imB(kN, 0.0f);
    test::fftRadix2(reA, imA);
    test::fftRadix2(reB, imB);
    const double magA = std::hypot(reA[bin], imA[bin]);
    const double magB = std::hypot(reB[bin], imB[bin]);
    CHECK(std::fabs(20.0 * std::log10(magB / magA)) < 0.1);
  }
}
