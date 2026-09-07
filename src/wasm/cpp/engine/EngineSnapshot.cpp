#include "EngineSnapshot.h"
#include "RbsAudioEngine.h"
#include "../synth/Tb303Voice.h"
#include "../synth/Tr808Voice.h"
#include "../synth/Tr909Voice.h"
#include <vector>

namespace rb338 {

std::unique_ptr<EngineSnapshot> buildEngineSnapshot(
    const ParsedSong& song, const EngineConfig& config,
    std::shared_ptr<const SamplePool> samplePool) {
  auto snap = std::make_unique<EngineSnapshot>();
  snap->song = song;
  snap->samplePool = std::move(samplePool);
  snap->voices[0] = std::make_unique<Tb303Voice>();
  snap->voices[1] = std::make_unique<Tb303Voice>();
  snap->voices[2] = std::make_unique<Tr808Voice>();
  snap->voices[3] = std::make_unique<Tr909Voice>();
  snap->mixer = std::make_unique<Mixer>();

  snap->mixer->init(config.sampleRate);
  snap->mixer->setDistortionEnabled(config.enableDistortion);
  snap->mixer->setCompressorEnabled(config.enableCompressor);
  snap->mixer->setDelayEnabled(config.enableDelay);
  for (size_t i = 0; i < snap->voices.size(); ++i) {
    if (!snap->voices[i]) continue;
    snap->voices[i]->init(config.sampleRate);
    // Attach before load() so voices can resolve slots against final knobs.
    snap->voices[i]->setSamplePool(snap->samplePool.get());
  }

  applySongStateToSnapshot(*snap);

  return snap;
}

void applySongStateToSnapshot(EngineSnapshot& snap) {
  if (snap.mixer) {
    snap.mixer->setDeviceStates(snap.song.devices);
    snap.mixer->setSongFx(snap.song.fx);
  }

  for (size_t i = 0; i < snap.voices.size(); ++i) {
    if (!snap.voices[i]) continue;
    const DeviceId dev = static_cast<DeviceId>(i);
    std::vector<Pattern> voicePatterns;
    voicePatterns.reserve(snap.song.patterns.size());
    for (const auto& pattern : snap.song.patterns) {
      if (pattern.deviceId == dev) voicePatterns.push_back(pattern);
    }
    snap.voices[i]->load(snap.song.devices[i], voicePatterns);
  }
}

} // namespace rb338
