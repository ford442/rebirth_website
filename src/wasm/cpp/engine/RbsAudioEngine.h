#pragma once

#include "AudioThreadLimits.h"
#include "../parser/RbsTypes.h"
#include "../synth/SamplePool.h"
#include "EngineCommands.h"
#include "EngineSnapshot.h"
#include "Sequencer.h"
#include "AutomationScheduler.h"
#include "../audio/WavWriter.h"
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

  /**
   * Parse and decode a `.rbm` mod, replacing drum/oscillator PCM. Main
   * thread only.
   *
   * Decoding happens here, never in processBlock(): a fresh SamplePool is
   * filled, then a new snapshot is published atomically. The previously
   * live pool stays alive until its snapshot is reclaimed, so the audio
   * thread never reads freed PCM.
   *
   * Skins are catalogued in the report and never decoded.
   */
  ModLoadStatus loadMod(const uint8_t* data, size_t size);

  /** Drop mod samples and return every voice to procedural synthesis. */
  void clearMod();

  /** True when at least one slot is currently backed by mod PCM. */
  bool hasMod() const;

  /** Diagnostics for the most recent loadMod() call. Main thread only. */
  const ModLoadReport& lastModReport() const { return m_modReport; }

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

  // ── Offline rendering (bounce / stems) ────────────────────────────
  //
  // These drive the *same* processBlock() the AudioWorklet drives, over the
  // same published EngineSnapshot, with no AudioContext involved. The engine
  // is deterministic and block-size invariant (see tests/test_offline.cpp),
  // so an offline render is sample-for-sample what the worklet would have
  // produced from the same starting transport position.
  //
  // Thread safety: run these only on an engine the audio thread is NOT
  // currently pulling. They advance the sequencer on the calling thread, so
  // a concurrently running worklet would race them. The JS bridge renders on
  // a dedicated offline engine rather than the live one.

  /**
   * Render `frames` of interleaved stereo from bar 1.
   *
   * @param interleavedStereo Destination, at least `frames * 2` floats.
   * @return Frames actually written.
   */
  uint32_t renderOffline(float* interleavedStereo, uint32_t frames);

  /**
   * Render one device in isolation, for stem export.
   *
   * Every other device is muted for the duration, so the stem is that
   * device's contribution through the full mixer chain — its level, pan and
   * FX sends included. Stems therefore do not sum bit-exactly back to the
   * master bounce: the master limiter is non-linear and reacts to the summed
   * signal. They sum closely, and each is individually correct.
   */
  uint32_t renderOfflineStem(float* interleavedStereo, uint32_t frames, uint8_t deviceIndex);

  /** Frames needed to cover the loaded song's arrangement at the current tempo. */
  uint32_t songLengthFrames() const;

  /**
   * Bounce to a complete 16-bit PCM WAV file.
   *
   * Encodes block by block rather than buffering the whole render as float
   * first, so peak memory is the output file rather than ~2.5x it — which
   * matters against a fixed 64 MiB heap.
   *
   * `deviceIndex` selects a stem; pass MASTER_BUS for the full mix.
   */
  static constexpr uint8_t MASTER_BUS = 0xffu;
  std::vector<uint8_t> renderOfflineWav(uint32_t frames, uint8_t deviceIndex = MASTER_BUS);

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
  void republishGraph();
  /** Rewind transport + graph for a bounce. Returns the snapshot, or null. */
  EngineSnapshot* prepareOfflineTransport();
  /** Single offline render path; writes floats, WAV bytes, or both. */
  uint32_t runOfflineRender(uint32_t frames, uint8_t deviceIndex,
                            float* interleavedOut, WavPcm16Builder* wav);
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

  // Mod samples (main thread). The pool the audio thread reads is the one
  // referenced by the published snapshot, not this handle.
  std::shared_ptr<const SamplePool> m_samplePool;
  ModLoadReport m_modReport;

  // Live knob moves, remembered so they survive a graph rebuild.
  //
  // setDeviceParam() is session-only: it never writes back into the loaded
  // song. Anything that re-applies the song to the graph — loading a mod,
  // or rewinding for an offline bounce — would therefore silently discard
  // every knob the user has touched. Replaying these afterwards is what
  // makes a bounce sound like what is actually playing.
  static constexpr size_t NUM_DEVICE_PARAMS = 10; // DeviceParamId::Tune..Mute
  std::array<std::array<float, NUM_DEVICE_PARAMS>, NUM_DEVICES> m_paramOverrides{};
  std::array<std::array<bool, NUM_DEVICE_PARAMS>, NUM_DEVICES> m_paramOverrideSet{};
  void applyParamOverrides(EngineSnapshot* snap);

  // Per-device mono scratch buffers (member storage — not on the audio-thread stack).
  alignas(16) float m_scratchBuffers[NUM_DEVICES][MAX_RENDER_TEST_FRAMES];
  std::array<float*, NUM_DEVICES> m_voiceBuffers{};
};

} // namespace rb338
