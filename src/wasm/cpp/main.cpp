/**
 * main.cpp — Emscripten entry point for the ReBirth RB-338 WASM audio engine.
 *
 * This file is compiled by Emscripten and produces:
 *   - rbsParser.js   (JS glue)
 *   - rbsParser.wasm (WASM binary)
 *
 * The actual audio worklet processor is generated automatically by Emscripten's
 * -sAUDIO_WORKLET flag; we only need to export our C++ classes via embind here.
 */

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <array>
#include <utility>
#include "parser/RbsTypes.h"
#include "parser/RbsParser.h"
#include "engine/RbsAudioEngine.h"
#include "worklet/RbsWorklet.h"

using namespace emscripten;
using namespace rb338;

// ── Embind-friendly wrappers ─────────────────────────────────────

struct PlaybackPosition {
  uint16_t bar = 1;
  uint8_t step = 0;
};

PlaybackPosition getPlaybackPositionWrapper(const RbsAudioEngine& self) {
  uint16_t bar = 0;
  uint8_t step = 0;
  self.getPlaybackPosition(bar, step);
  return PlaybackPosition{bar, step};
}

// Helpers to register fixed-size std::array types with value_array.
// Embind requires every index to be declared explicitly, so we use
// index_sequence to generate the .element(index<I>()) chain.

template <typename T, std::size_t N, std::size_t... Is>
void registerArrayElements(value_array<std::array<T, N>>& builder, std::index_sequence<Is...>) {
  (builder.element(index<Is>()), ...);
}

template <typename T, std::size_t N>
void registerFixedArray(const char* name) {
  auto builder = value_array<std::array<T, N>>(name);
  registerArrayElements<T, N>(builder, std::make_index_sequence<N>{});
}

// ── Embind exports ───────────────────────────────────────────────
// These make C++ classes callable from JavaScript/TypeScript.

EMSCRIPTEN_BINDINGS(rb338_audio) {
  // Container registrations must appear before any value_object that uses them.
  register_vector<Pattern>("PatternVector");
  register_vector<ArrangementBar>("ArrangementBarVector");
  registerFixedArray<StepData, MAX_STEPS>("StepDataArray");
  registerFixedArray<DeviceState, NUM_DEVICES>("DeviceStateArray");
  registerFixedArray<PatternRef, NUM_DEVICES>("PatternRefArray");

  // enums — expose as plain numbers so JS/TS can use them directly.
  enum_<DeviceId>("DeviceId", enum_value_type::number)
    .value("TB303_A", DeviceId::TB303_A)
    .value("TB303_B", DeviceId::TB303_B)
    .value("TR808",   DeviceId::TR808)
    .value("TR909",   DeviceId::TR909);

  enum_<RbsVersion>("RbsVersion", enum_value_type::number)
    .value("V1_5",   RbsVersion::V1_5)
    .value("V2_0",   RbsVersion::V2_0)
    .value("V2_0_1", RbsVersion::V2_0_1);

  // PlaybackPosition
  value_object<PlaybackPosition>("PlaybackPosition")
    .field("bar",  &PlaybackPosition::bar)
    .field("step", &PlaybackPosition::step);

  // StepData
  value_object<StepData>("StepData")
    .field("active", &StepData::active)
    .field("note",   &StepData::note)
    .field("drumExtra", &StepData::drumExtra)
    .field("accent", &StepData::accent)
    .field("slide",  &StepData::slide);

  // PatternRef
  value_object<PatternRef>("PatternRef")
    .field("bank",  &PatternRef::bank)
    .field("index", &PatternRef::index);

  // ArrangementBar
  value_object<ArrangementBar>("ArrangementBar")
    .field("barNumber",       &ArrangementBar::barNumber)
    .field("devicePatterns",  &ArrangementBar::devicePatterns);

  // DeviceState
  value_object<DeviceState>("DeviceState")
    .field("id",       &DeviceState::id)
    .field("tune",     &DeviceState::tune)
    .field("cutoff",   &DeviceState::cutoff)
    .field("resonance",&DeviceState::resonance)
    .field("envMod",   &DeviceState::envMod)
    .field("decay",    &DeviceState::decay)
    .field("accent",   &DeviceState::accent)
    .field("waveform", &DeviceState::waveform)
    .field("initialPatternBank",  &DeviceState::initialPatternBank)
    .field("initialPatternIndex", &DeviceState::initialPatternIndex)
    .field("muted",    &DeviceState::muted)
    .field("level",    &DeviceState::level)
    .field("pan",      &DeviceState::pan)
    .field("dist",     &DeviceState::dist)
    .field("pcf",      &DeviceState::pcf)
    .field("compressor", &DeviceState::compressor)
    .field("delaySend", &DeviceState::delaySend);

  // Pattern
  value_object<Pattern>("Pattern")
    .field("deviceId",     &Pattern::deviceId)
    .field("bank",         &Pattern::bank)
    .field("patternIndex", &Pattern::patternIndex)
    .field("length",       &Pattern::length)
    .field("steps",        &Pattern::steps);

  // ParsedSong
  value_object<ParsedSong>("ParsedSong")
    .field("title",        &ParsedSong::title)
    .field("author",       &ParsedSong::author)
    .field("infoText",     &ParsedSong::infoText)
    .field("creatorUrl",   &ParsedSong::creatorUrl)
    .field("bpm",          &ParsedSong::bpm)
    .field("version",      &ParsedSong::version)
    .field("headVersion",  &ParsedSong::headVersion)
    .field("globSubFormat",&ParsedSong::globSubFormat)
    .field("showInfoOnOpen", &ParsedSong::showInfoOnOpen)
    .field("devices",      &ParsedSong::devices)
    .field("patterns",     &ParsedSong::patterns)
    .field("arrangement",  &ParsedSong::arrangement);

  register_optional<ParsedSong>();

  // RbsParser
  class_<RbsParser>("RbsParser")
    .constructor()
    .function("parse",     &RbsParser::parse, allow_raw_pointer<arg<1>>())
    .function("lastError", &RbsParser::lastError);

  // EngineConfig
  value_object<EngineConfig>("EngineConfig")
    .field("sampleRate",        &EngineConfig::sampleRate)
    .field("bufferSize",        &EngineConfig::bufferSize)
    .field("enableTb303A",      &EngineConfig::enableTb303A)
    .field("enableTb303B",      &EngineConfig::enableTb303B)
    .field("enableTr808",       &EngineConfig::enableTr808)
    .field("enableTr909",       &EngineConfig::enableTr909)
    .field("enableDistortion",  &EngineConfig::enableDistortion)
    .field("enableCompressor",  &EngineConfig::enableCompressor)
    .field("enableDelay",       &EngineConfig::enableDelay);

  // RbsAudioEngine
  class_<RbsAudioEngine>("RbsAudioEngine")
    .constructor()
    .function("init",      &RbsAudioEngine::init)
    .function("loadSong",  &RbsAudioEngine::loadSong)
    .function("play",      &RbsAudioEngine::play)
    .function("pause",     &RbsAudioEngine::pause)
    .function("stop",      &RbsAudioEngine::stop)
    .function("seek",      &RbsAudioEngine::seek)
    .function("setVolume", &RbsAudioEngine::setVolume)
    .function("setTempo",  &RbsAudioEngine::setTempo)
    .function("getTempo",  &RbsAudioEngine::getTempo)
    .function("setTempoMultiplier", &RbsAudioEngine::setTempoMultiplier)
    .function("isPlaying", &RbsAudioEngine::isPlaying)
    .function("getPlaybackPosition", &getPlaybackPositionWrapper);

  // AudioWorklet wiring
  function("initAudioWorklet", &initAudioWorklet, allow_raw_pointers());
}
