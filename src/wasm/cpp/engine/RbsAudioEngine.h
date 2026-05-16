#pragma once

#include "../parser/RbsTypes.h"
#include <cstdint>
#include <memory>

namespace rb338 {

// Forward declarations
class Sequencer;
class Mixer;
class Voice;

/**
 * EngineConfig — runtime parameters passed from JavaScript before initialisation.
 */
struct EngineConfig {
  float sampleRate = 44100.0f;
  uint32_t bufferSize = 128;
  bool enableTb303A = true;
  bool enableTb303B = true;
  bool enableTr808 = true;
  bool enableTr909 = true;
  bool enableDistortion = true;
  bool enableCompressor = true;
  bool enableDelay = true;
};

/**
 * RbsAudioEngine — top-level audio engine.
 *
 * Owns the parser, sequencer, mixer, and all synthesis voices.
 * This class lives inside WASM and is called from two contexts:
 *
 *   1. Main thread (via Embind): loadSong(), play(), stop(), seek()
 *   2. AudioWorklet callback (processBlock()): real-time audio generation
 *
 * All processBlock() code must be lock-free and non-blocking.
 */
class RbsAudioEngine {
public:
  RbsAudioEngine();
  ~RbsAudioEngine();

  // Non-copyable
  RbsAudioEngine(const RbsAudioEngine&) = delete;
  RbsAudioEngine& operator=(const RbsAudioEngine&) = delete;

  /** Initialise the engine with host parameters. Must be called once before use. */
  bool init(const EngineConfig& config);

  /** Load a parsed song. Safe to call from main thread only. */
  bool loadSong(const ParsedSong& song);

  /** Start playback from the current position. */
  void play();

  /** Pause playback ( retains position ). */
  void pause();

  /** Stop playback and reset to bar 1. */
  void stop();

  /** Seek to a specific bar (1-based). */
  void seek(uint16_t bar);

  /** Query whether the engine is currently playing. */
  bool isPlaying() const;

  /** Query current bar + step for UI display (atomics, safe from any thread). */
  void getPlaybackPosition(uint16_t& bar, uint8_t& step) const;

  /**
   * Audio callback — called by the Emscripten Wasm Audio Worklet every 128 frames.
   *
   * @param outputBuffers  Array of float* buffers, one per channel (interleaved stereo).
   * @param numChannels    Number of output channels (typically 2).
   * @param numFrames      Number of frames to render (typically 128).
   *
   * Must execute quickly and never allocate memory or take locks.
   */
  void processBlock(float* const* outputBuffers, uint32_t numChannels, uint32_t numFrames);

private:
  EngineConfig m_config{};
  bool m_initialised = false;
  bool m_playing = false;

  // Playback position (accessed from both threads — use atomics in real impl)
  uint16_t m_currentBar = 1;
  uint8_t  m_currentStep = 0;

  // Sub-systems
  std::unique_ptr<Sequencer> m_sequencer;
  std::unique_ptr<Mixer> m_mixer;
  // Voices: [0]=303-A, [1]=303-B, [2]=808, [3]=909
  std::array<std::unique_ptr<Voice>, NUM_DEVICES> m_voices;
};

} // namespace rb338
