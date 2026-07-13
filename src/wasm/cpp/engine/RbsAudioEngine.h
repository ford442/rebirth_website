#pragma once

#include "../parser/RbsTypes.h"
#include "../synth/Voice.h"
#include "EngineCommands.h"
#include "Mixer.h"
#include "Sequencer.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace rb338 {

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

  /** Set master volume (0.0–1.0). */
  void setVolume(float volume);

  /** Set tempo in BPM. */
  void setTempo(float bpm);

  /** Set tempo multiplier (0.25–4.0, 1.0 = normal). */
  void setTempoMultiplier(float multiplier);

  /** Query whether the engine is currently playing. */
  bool isPlaying() const { return m_playing.load(std::memory_order_acquire); }

  /** Push a control command from the main thread. Never blocks. */
  bool pushCommand(const EngineCommand& cmd);

  /** Query current bar + step for UI display (atomics, safe from any thread). */
  void getPlaybackPosition(uint16_t& bar, uint8_t& step) const;

  /**
   * Audio callback — called by the Emscripten Wasm Audio Worklet every 128 frames.
   *
   * @param outputBuffers  Array of float* buffers, one per channel. For stereo
   *                       the first buffer is interleaved [L,R,L,R,...].
   * @param numChannels    Number of output channels (typically 2).
   * @param numFrames      Number of frames to render (typically 128).
   *
   * Must execute quickly and never allocate memory or take locks.
   */
  void processBlock(float* const* outputBuffers, uint32_t numChannels, uint32_t numFrames);

private:
  void drainCommands();
  void handleCommand(const EngineCommand& cmd);
  void resetState();

  EngineConfig m_config{};
  bool m_initialised = false;

  // Atomic state shared between main thread and audio thread.
  std::atomic<bool> m_playing{false};
  std::atomic<uint16_t> m_currentBar{1};
  std::atomic<uint8_t>  m_currentStep{0};
  std::atomic<float>    m_volume{0.8f};
  std::atomic<float>    m_tempoMultiplier{1.0f};

  // Command queue (lives in shared WASM memory).
  EngineCommandQueue m_commandQueue;

  // Cached song data used by the audio thread.
  ParsedSong m_song;

  // Sub-systems
  std::unique_ptr<Sequencer> m_sequencer;
  std::unique_ptr<Mixer> m_mixer;
  // Voices: [0]=303-A, [1]=303-B, [2]=808, [3]=909
  std::array<std::unique_ptr<Voice>, NUM_DEVICES> m_voices;

  // Per-device mono render buffers (stack-allocated pointers in processBlock).
  std::array<float*, NUM_DEVICES> m_voiceBuffers{};
};

} // namespace rb338
