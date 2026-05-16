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
#include "parser/RbsParser.h"
#include "engine/RbsAudioEngine.h"

using namespace emscripten;
using namespace rb338;

// ── Embind exports ───────────────────────────────────────────────
// These make C++ classes callable from JavaScript/TypeScript.

EMSCRIPTEN_BINDINGS(rb338_audio) {
  // enums
  enum_<DeviceId>("DeviceId")
    .value("TB303_A", DeviceId::TB303_A)
    .value("TB303_B", DeviceId::TB303_B)
    .value("TR808",   DeviceId::TR808)
    .value("TR909",   DeviceId::TR909);

  // StepData
  value_object<StepData>("StepData")
    .field("active", &StepData::active)
    .field("note",   &StepData::note)
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
    .field("muted",    &DeviceState::muted)
    .field("level",    &DeviceState::level)
    .field("pan",      &DeviceState::pan);

  // ParsedSong
  value_object<ParsedSong>("ParsedSong")
    .field("title",        &ParsedSong::title)
    .field("author",       &ParsedSong::author)
    .field("infoText",     &ParsedSong::infoText)
    .field("creatorUrl",   &ParsedSong::creatorUrl)
    .field("bpm",          &ParsedSong::bpm)
    .field("devices",      &ParsedSong::devices)
    .field("patterns",     &ParsedSong::patterns)
    .field("arrangement",  &ParsedSong::arrangement);

  // RbsParser
  class_<RbsParser>("RbsParser")
    .constructor()
    .function("parse",     &RbsParser::parse)
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
    .function("isPlaying", &RbsAudioEngine::isPlaying)
    .function("getPlaybackPosition", &RbsAudioEngine::getPlaybackPosition);
}
