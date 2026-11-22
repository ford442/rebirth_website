#pragma once

// RenderSong — the one native offline-render path shared by rbs-render,
// rbs-golden and the golden comparison tests, so a reference rendered by a
// tool and a render checked by a test can never drift apart.

#include "../engine/RbsAudioEngine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rb338::tools {

struct RenderOptions {
  float sampleRate = 44100.0f;
  /** Frames to render; 0 = one pass of the song (songLengthFrames()). */
  uint32_t frames = 0;
  /** Device 0–3 for a stem, or RbsAudioEngine::MASTER_BUS. */
  uint8_t device = RbsAudioEngine::MASTER_BUS;
  /** Override the song's tempo (0 = the song's own). */
  float bpm = 0.0f;
};

/** Interleaved stereo from bar 1, exactly as the browser bounce renders it. */
inline std::optional<std::vector<float>> renderStereo(const ParsedSong& song,
                                                      const RenderOptions& options,
                                                      std::string* error = nullptr) {
  RbsAudioEngine engine;
  EngineConfig config;
  config.sampleRate = options.sampleRate;
  if (!engine.init(config) || !engine.loadSong(song)) {
    if (error) *error = "engine init / load failed";
    return std::nullopt;
  }
  if (options.bpm > 0.0f) engine.setTempo(options.bpm);
  const uint32_t frames = options.frames ? options.frames : engine.songLengthFrames();
  std::vector<float> stereo(static_cast<size_t>(frames) * 2u, 0.0f);
  const uint32_t rendered =
      options.device == RbsAudioEngine::MASTER_BUS
          ? engine.renderOffline(stereo.data(), frames)
          : engine.renderOfflineStem(stereo.data(), frames, options.device);
  if (rendered != frames) {
    if (error) *error = "render stopped early";
    return std::nullopt;
  }
  return stereo;
}

/** The left/right average of an interleaved stereo buffer. */
inline std::vector<float> toMono(const std::vector<float>& stereo) {
  std::vector<float> mono(stereo.size() / 2u);
  for (size_t i = 0; i < mono.size(); ++i) {
    mono[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
  }
  return mono;
}

} // namespace rb338::tools
