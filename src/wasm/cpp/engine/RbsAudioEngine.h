#pragma once

#include "AudioThreadLimits.h"
#include "../parser/RbsTypes.h"
#include "EngineCommands.h"
#include "EngineSnapshot.h"
#include "Sequencer.h"
#include "AutomationScheduler.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

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

  /** Set absolute tempo in BPM (clamped to 40–250). */
  void setTempo(float bpm);

  /** Current absolute tempo in BPM. Safe to call from any thread. */
  float getTempo() const { return m_bpm.load(std::memory_order_acquire); }

  /** Set tempo multiplier (0.25–4.0, 1.0 = normal). Used for UI scrubbing. */
  void setTempoMultiplier(float multiplier);

  /**
   * Live device/mixer parameter. Main thread only — enqueued for the audio
   * callback. Session-only; does not rewrite the loaded song file.
   */
  void setDeviceParam(uint8_t deviceId, uint8_t paramId, float value);

  /** Query whether the engine is currently playing. */
  bool isPlaying() const { return m_playing.load(std::memory_order_acquire); }

  /** Number of render quanta received from the AudioWorklet (diagnostics/tests). */
  uint32_t getProcessedBlockCount() const {
    return m_processedBlocks.load(std::memory_order_acquire);
  }

  /** Render one block synchronously and return its absolute peak (test hook). */
  float renderTestBlock(uint32_t numFrames);

  /** Push a control command from the main thread. Never blocks. */
  bool pushCommand(const EngineCommand& cmd);

  /** Query current bar + step for UI display (atomics, safe from any thread). */
  void getPlaybackPosition(uint16_t& bar, uint8_t& step) const;

  /**
   * Audio callback — called by the Emscripten Wasm Audio Worklet every 128 frames.
   *
   * @param outputBuffers  Planar output: one float* per channel, each numFrames long.
   * @param numChannels    Number of output channels (typically 2).
   * @param numFrames      Number of frames to render (typically 128).
   *
   * Must execute quickly and never allocate memory or take locks.
   */
  void processBlock(float* const* outputBuffers, uint32_t numChannels, uint32_t numFrames);

private:
  void drainCommands();
  void handleCommand(const EngineCommand& cmd);
  EngineSnapshot* pinSnapshot();
  void reclaimRetiredSnapshots();
  void resetVoices(EngineSnapshot* snap);
  void applyDeviceParam(EngineSnapshot* snap, uint8_t deviceId, DeviceParamId param,
                        float value);
  bool deviceEnabled(int deviceIndex) const;
  void renderSpan(EngineSnapshot& snap, uint32_t start, uint32_t count,
                  float* left, float* right, uint32_t numChannels, float bpm, float volume);

  EngineConfig m_config{};
  bool m_initialised = false;

  // Atomic state shared between main thread and audio thread.
  std::atomic<bool> m_playing{false};
  std::atomic<uint16_t> m_currentBar{1};
  std::atomic<uint8_t>  m_currentStep{0};
  std::atomic<float>    m_volume{0.8f};
  std::atomic<float>    m_bpm{125.0f};
  std::atomic<float>    m_tempoMultiplier{1.0f};
  std::atomic<uint32_t> m_processedBlocks{0};

  // Command queue (lives in shared WASM memory).
  EngineCommandQueue m_commandQueue;

  // Lock-free snapshot handoff. Audio loads a raw pointer and publishes it
  // as the in-use hazard; main retires unique_ptrs once the epoch has moved on.
  static_assert(std::atomic<EngineSnapshot*>::is_always_lock_free,
                "EngineSnapshot* atomics must be lock-free");
  std::atomic<EngineSnapshot*> m_published{nullptr};
  std::atomic<EngineSnapshot*> m_inUse{nullptr};
  std::vector<std::unique_ptr<EngineSnapshot>> m_owned; // main thread only

  // Sequencer transport lives on the engine (audio-thread owned after init).
  std::unique_ptr<Sequencer> m_sequencer;
  AutomationScheduler m_automation;

  // Per-device mono scratch buffers (member storage — not on the audio-thread stack).
  alignas(16) float m_scratchBuffers[NUM_DEVICES][MAX_RENDER_TEST_FRAMES];
  std::array<float*, NUM_DEVICES> m_voiceBuffers{};
};

} // namespace rb338
