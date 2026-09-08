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
#include "parser/RbmParser.h"
#include "parser/RbmTypes.h"
#include "engine/RbsAudioEngine.h"
#include "synth/SamplePool.h"
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

std::optional<ParsedSong> parseSongWrapper(RbsParser& self,
                                           uintptr_t dataPtr,
                                           size_t size) {
  return self.parse(reinterpret_cast<const uint8_t*>(dataPtr), size);
}

/**
 * Summarise a .rbm without decoding it.
 *
 * Deliberately returns ModLoadReport rather than ParsedMod: the report has
 * no byte field at all, so embedded sample and skin payloads structurally
 * cannot be copied onto the JS heap. Megabytes of PCM and JPEG stay in WASM
 * memory where the engine can use them directly.
 */
std::optional<ModLoadReport> parseModWrapper(RbmParser& self,
                                             uintptr_t dataPtr,
                                             size_t size) {
  auto mod = self.parse(reinterpret_cast<const uint8_t*>(dataPtr), size);
  if (!mod) return std::nullopt;
  return summariseMod(*mod);
}

/** Decode a .rbm straight from the WASM heap into the engine's sample pool. */
ModLoadStatus loadModWrapper(RbsAudioEngine& self, uintptr_t dataPtr, size_t size) {
  return self.loadMod(reinterpret_cast<const uint8_t*>(dataPtr), size);
}

ModLoadReport getModReportWrapper(const RbsAudioEngine& self) {
  return self.lastModReport();
}

/**
 * Bounce to a WAV and hand JavaScript a Uint8Array it owns.
 *
 * `new Uint8Array(view)` copies out of the WASM heap, so the returned array
 * stays valid after the C++ vector goes out of scope — a bare
 * typed_memory_view would dangle the moment this function returns.
 */
val renderOfflineWavWrapper(RbsAudioEngine& self, uint32_t frames, uint8_t deviceIndex) {
  const std::vector<uint8_t> wav = self.renderOfflineWav(frames, deviceIndex);
  return val::global("Uint8Array")
      .new_(val(typed_memory_view(wav.size(), wav.data())));
}

// Helpers to register fixed-size std::array types with value_array.
// Embind requires every index to be declared explicitly, so we use
// index_sequence to generate the .element(index<I>()) chain.

template <typename T, std::size_t N, std::size_t... Is>
void registerArrayElements(value_array<std::array<T, N>>& builder, std::index_sequence<Is...>) {
  (builder.element(emscripten::index<Is>()), ...);
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
  register_vector<ModSampleReportEntry>("ModSampleReportEntryVector");
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
    .value("V1_0",   RbsVersion::V1_0)
    .value("V1_5",   RbsVersion::V1_5)
    .value("V2_0",   RbsVersion::V2_0)
    .value("V2_0_1", RbsVersion::V2_0_1);

  // ── .rbm mod loading ──────────────────────────────────────────────

  enum_<ModResourceKind>("ModResourceKind", enum_value_type::number)
    .value("Sample", ModResourceKind::Sample)
    .value("Skin",   ModResourceKind::Skin)
    .value("Song",   ModResourceKind::Song)
    .value("Other",  ModResourceKind::Other);

  enum_<ModLoadStatus>("ModLoadStatus", enum_value_type::number)
    .value("Ok",             ModLoadStatus::Ok)
    .value("NotInitialised", ModLoadStatus::NotInitialised)
    .value("NoSamples",      ModLoadStatus::NoSamples)
    .value("ArenaExhausted", ModLoadStatus::ArenaExhausted)
    .value("Partial",        ModLoadStatus::Partial);

  enum_<SampleDecodeStatus>("SampleDecodeStatus", enum_value_type::number)
    .value("Ok",                  SampleDecodeStatus::Ok)
    .value("UnknownFormat",       SampleDecodeStatus::UnknownFormat)
    .value("Malformed",           SampleDecodeStatus::Malformed)
    .value("UnsupportedEncoding", SampleDecodeStatus::UnsupportedEncoding)
    .value("Empty",               SampleDecodeStatus::Empty)
    .value("DestinationTooSmall", SampleDecodeStatus::DestinationTooSmall);

  // Slot is exposed as a plain number; JS maps it to a label via the
  // ModSampleSlot table in types/wasm-audio.ts.
  enum_<ModSampleSlot>("ModSampleSlot", enum_value_type::number)
    .value("Unknown",        ModSampleSlot::Unknown)
    .value("Tr808Kick",      ModSampleSlot::Tr808Kick)
    .value("Tr808Snare",     ModSampleSlot::Tr808Snare)
    .value("Tr808LowTom",    ModSampleSlot::Tr808LowTom)
    .value("Tr808MidTom",    ModSampleSlot::Tr808MidTom)
    .value("Tr808HighTom",   ModSampleSlot::Tr808HighTom)
    .value("Tr808ClosedHat", ModSampleSlot::Tr808ClosedHat)
    .value("Tr808OpenHat",   ModSampleSlot::Tr808OpenHat)
    .value("Tr808Rimshot",   ModSampleSlot::Tr808Rimshot)
    .value("Tr808Clap",      ModSampleSlot::Tr808Clap)
    .value("Tr808Clave",     ModSampleSlot::Tr808Clave)
    .value("Tr808Cymbal",    ModSampleSlot::Tr808Cymbal)
    .value("Tr808Maracas",   ModSampleSlot::Tr808Maracas)
    .value("Tr909Kick",      ModSampleSlot::Tr909Kick)
    .value("Tr909Snare",     ModSampleSlot::Tr909Snare)
    .value("Tr909LowTom",    ModSampleSlot::Tr909LowTom)
    .value("Tr909MidTom",    ModSampleSlot::Tr909MidTom)
    .value("Tr909HighTom",   ModSampleSlot::Tr909HighTom)
    .value("Tr909ClosedHat", ModSampleSlot::Tr909ClosedHat)
    .value("Tr909OpenHat",   ModSampleSlot::Tr909OpenHat)
    .value("Tr909Rimshot",   ModSampleSlot::Tr909Rimshot)
    .value("Tr909Clap",      ModSampleSlot::Tr909Clap)
    .value("Tr909Crash",     ModSampleSlot::Tr909Crash)
    .value("Tr909Ride",      ModSampleSlot::Tr909Ride)
    .value("Tb303Saw",       ModSampleSlot::Tb303Saw)
    .value("Tb303Square",    ModSampleSlot::Tb303Square);

  value_object<ModSampleReportEntry>("ModSampleReportEntry")
    .field("name",         &ModSampleReportEntry::name)
    .field("kind",         &ModSampleReportEntry::kind)
    .field("slot",         &ModSampleReportEntry::slot)
    .field("byteSize",     &ModSampleReportEntry::byteSize)
    .field("frameCount",   &ModSampleReportEntry::frameCount)
    .field("sampleRate",   &ModSampleReportEntry::sampleRate)
    .field("channels",     &ModSampleReportEntry::channels)
    .field("bitDepth",     &ModSampleReportEntry::bitDepth)
    .field("decodeStatus", &ModSampleReportEntry::decodeStatus)
    .field("loaded",       &ModSampleReportEntry::loaded);

  value_object<ModLoadReport>("ModLoadReport")
    .field("title",          &ModLoadReport::title)
    .field("description",    &ModLoadReport::description)
    .field("copyright",      &ModLoadReport::copyright)
    .field("resources",      &ModLoadReport::resources)
    .field("status",         &ModLoadReport::status)
    .field("loadedSlots",    &ModLoadReport::loadedSlots)
    .field("skinCount",      &ModLoadReport::skinCount)
    .field("usedFrames",     &ModLoadReport::usedFrames)
    .field("capacityFrames", &ModLoadReport::capacityFrames);

  register_optional<ModLoadReport>();

  value_object<DelaySettings>("DelaySettings")
    .field("enabled", &DelaySettings::enabled)
    .field("time", &DelaySettings::time)
    .field("feedback", &DelaySettings::feedback)
    .field("wet", &DelaySettings::wet);

  value_object<PcfSettings>("PcfSettings")
    .field("enabled", &PcfSettings::enabled)
    .field("cutoff", &PcfSettings::cutoff)
    .field("resonance", &PcfSettings::resonance)
    .field("envAmount", &PcfSettings::envAmount);

  value_object<DistSettings>("DistSettings")
    .field("enabled", &DistSettings::enabled)
    .field("drive", &DistSettings::drive)
    .field("mix", &DistSettings::mix);

  value_object<CompSettings>("CompSettings")
    .field("enabled", &CompSettings::enabled)
    .field("threshold", &CompSettings::threshold)
    .field("ratio", &CompSettings::ratio)
    .field("attack", &CompSettings::attack);

  value_object<SongFxSettings>("SongFxSettings")
    .field("masterLevel", &SongFxSettings::masterLevel)
    .field("delay", &SongFxSettings::delay)
    .field("pcf", &SongFxSettings::pcf)
    .field("dist", &SongFxSettings::dist)
    .field("comp", &SongFxSettings::comp);

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
    .field("arrangement",  &ParsedSong::arrangement)
    .field("fx",           &ParsedSong::fx);

  register_optional<ParsedSong>();

  // RbsParser
  class_<RbsParser>("RbsParser")
    .constructor()
    .function("parse",     &parseSongWrapper)
    .function("lastError", &RbsParser::lastError);

  // RbmParser — metadata only. Returns a ModLoadReport (no byte payloads);
  // use RbsAudioEngine.loadMod() to actually put samples into the engine.
  class_<RbmParser>("RbmParser")
    .constructor()
    .function("parse",     &parseModWrapper)
    .function("lastError", &RbmParser::lastError);

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
    .function("loadMod",   &loadModWrapper)
    .function("clearMod",  &RbsAudioEngine::clearMod)
    .function("hasMod",    &RbsAudioEngine::hasMod)
    .function("getModReport", &getModReportWrapper)
    .function("play",      &RbsAudioEngine::play)
    .function("pause",     &RbsAudioEngine::pause)
    .function("stop",      &RbsAudioEngine::stop)
    .function("seek",      &RbsAudioEngine::seek)
    .function("setVolume", &RbsAudioEngine::setVolume)
    .function("setTempo",  &RbsAudioEngine::setTempo)
    .function("getTempo",  &RbsAudioEngine::getTempo)
    .function("setTempoMultiplier", &RbsAudioEngine::setTempoMultiplier)
    .function("setDeviceParam", &RbsAudioEngine::setDeviceParam)
    .function("isPlaying", &RbsAudioEngine::isPlaying)
    .function("getProcessedBlockCount", &RbsAudioEngine::getProcessedBlockCount)
    .function("renderTestBlock", &RbsAudioEngine::renderTestBlock)
    .function("renderOfflineToWav", &renderOfflineWavWrapper)
    .function("songLengthFrames", &RbsAudioEngine::songLengthFrames)
    .function("getPlaybackPosition", &getPlaybackPositionWrapper);

  // AudioWorklet wiring
  function("initAudioWorklet", &initAudioWorklet, allow_raw_pointers());
}
