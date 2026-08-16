#include "EngineSnapshot.h"
#include "RbsAudioEngine.h"
#include "../synth/Tb303Voice.h"
#include "../synth/Tr808Voice.h"
#include "../synth/Tr909Voice.h"
#include <vector>

namespace rb338 {

std::unique_ptr<EngineSnapshot> buildEngineSnapshot(const ParsedSong& song,
                                                    const EngineConfig& config) {
  auto snap = std::make_unique<EngineSnapshot>();
  snap->song = song;
  snap->voices[0] = std::make_unique<Tb303Voice>();
  snap->voices[1] = std::make_unique<Tb303Voice>();
  snap->voices[2] = std::make_unique<Tr808Voice>();
  snap->voices[3] = std::make_unique<Tr909Voice>();
  snap->mixer = std::make_unique<Mixer>();

  snap->mixer->init(config.sampleRate);
  snap->mixer->setDistortionEnabled(config.enableDistortion);
  snap->mixer->setCompressorEnabled(config.enableCompressor);
  snap->mixer->setDelayEnabled(config.enableDelay);
  snap->mixer->setDeviceStates(song.devices);

  for (size_t i = 0; i < snap->voices.size(); ++i) {
    if (!snap->voices[i]) continue;
    snap->voices[i]->init(config.sampleRate);
    const DeviceId dev = static_cast<DeviceId>(i);
    std::vector<Pattern> voicePatterns;
    voicePatterns.reserve(song.patterns.size());
    for (const auto& pattern : song.patterns) {
      if (pattern.deviceId == dev) voicePatterns.push_back(pattern);
    }
    snap->voices[i]->load(song.devices[i], voicePatterns);
  }

  return snap;
}

} // namespace rb338
