#include "RbsAudioEngine.h"
#include "../synth/Tb303Voice.h"
#include "../synth/Tr808Voice.h"
#include "../synth/Tr909Voice.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

namespace {

constexpr uint32_t MAX_PROCESS_BLOCK_FRAMES = 2048;

uint32_t floatBits(float f) {
  uint32_t bits;
  static_assert(sizeof(bits) == sizeof(f), "float size mismatch");
  std::memcpy(&bits, &f, sizeof(f));
  return bits;
}

float bitsToFloat(uint32_t bits) {
  float f;
  static_assert(sizeof(f) == sizeof(bits), "float size mismatch");
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

} // anonymous namespace

RbsAudioEngine::RbsAudioEngine() {
  m_voices[0] = std::make_unique<Tb303Voice>();
  m_voices[1] = std::make_unique<Tb303Voice>();
  m_voices[2] = std::make_unique<Tr808Voice>();
  m_voices[3] = std::make_unique<Tr909Voice>();
  m_mixer = std::make_unique<Mixer>();
  m_sequencer = std::make_unique<Sequencer>();
}

RbsAudioEngine::~RbsAudioEngine() = default;

bool RbsAudioEngine::init(const EngineConfig& config) {
  m_config = config;

  for (size_t i = 0; i < m_voices.size(); ++i) {
    if (m_voices[i]) {
      m_voices[i]->init(config.sampleRate);
    }
  }

  m_mixer->init(config.sampleRate);
  m_mixer->setDistortionEnabled(config.enableDistortion);
  m_mixer->setCompressorEnabled(config.enableCompressor);
  m_mixer->setDelayEnabled(config.enableDelay);

  m_sequencer->reset();
  m_currentBar.store(1, std::memory_order_relaxed);
  m_currentStep.store(0, std::memory_order_relaxed);
  m_playing.store(false, std::memory_order_relaxed);
  m_volume.store(0.8f, std::memory_order_relaxed);
  m_bpm.store(125.0f, std::memory_order_relaxed);
  m_processedBlocks.store(0, std::memory_order_relaxed);
  m_initialised = true;
  return true;
}

bool RbsAudioEngine::loadSong(const ParsedSong& song) {
  if (!m_initialised) return false;

  // Loading is a main-thread operation. Stop first so the audio thread resets
  // its transport (via the command queue) and stops rendering old voices.
  stop();

  // Load per-device voice state on the main thread.
  for (size_t i = 0; i < m_voices.size(); ++i) {
    if (m_voices[i]) {
      DeviceId dev = static_cast<DeviceId>(i);
      std::vector<Pattern> voicePatterns;
      for (const auto& p : song.patterns) {
        if (p.deviceId == dev) voicePatterns.push_back(p);
      }
      m_voices[i]->load(song.devices[i], voicePatterns);
    }
  }

  m_mixer->setDeviceStates(song.devices);

  // Publish an immutable snapshot for the audio thread. The sequencer detects
  // the pointer change on its next block and re-resolves patterns safely.
  auto snapshot = std::make_shared<const ParsedSong>(song);
  std::atomic_store(&m_activeSong, std::shared_ptr<const ParsedSong>(snapshot));

  m_bpm.store(std::clamp(song.bpm, 40.0f, 250.0f), std::memory_order_release);
  m_currentBar.store(1, std::memory_order_relaxed);
  m_currentStep.store(0, std::memory_order_relaxed);
  return true;
}

void RbsAudioEngine::play() {
  pushCommand({EngineCommandType::Play, 0, 0});
}

void RbsAudioEngine::pause() {
  pushCommand({EngineCommandType::Pause, 0, 0});
}

void RbsAudioEngine::stop() {
  pushCommand({EngineCommandType::Stop, 0, 0});
}

void RbsAudioEngine::seek(uint16_t bar) {
  pushCommand({EngineCommandType::Seek, std::max<uint16_t>(1, bar), 0});
}

void RbsAudioEngine::setVolume(float volume) {
  pushCommand({EngineCommandType::SetVolume, floatBits(std::clamp(volume, 0.0f, 1.0f)), 0});
}

void RbsAudioEngine::setTempo(float bpm) {
  pushCommand({EngineCommandType::SetTempo, floatBits(std::clamp(bpm, 40.0f, 250.0f)), 0});
}

void RbsAudioEngine::setTempoMultiplier(float multiplier) {
  pushCommand({EngineCommandType::SetTempoMultiplier, floatBits(std::clamp(multiplier, 0.25f, 4.0f)), 0});
}

bool RbsAudioEngine::pushCommand(const EngineCommand& cmd) {
  return m_commandQueue.push(cmd);
}

void RbsAudioEngine::drainCommands() {
  EngineCommand cmd;
  while (m_commandQueue.pop(cmd)) {
    handleCommand(cmd);
  }
}

void RbsAudioEngine::handleCommand(const EngineCommand& cmd) {
  switch (cmd.type) {
    case EngineCommandType::Play:
      m_playing.store(true, std::memory_order_relaxed);
      break;
    case EngineCommandType::Pause:
      m_playing.store(false, std::memory_order_relaxed);
      break;
    case EngineCommandType::Stop:
      m_playing.store(false, std::memory_order_relaxed);
      m_sequencer->setPosition(1, 0);
      m_currentBar.store(1, std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      for (auto& voice : m_voices) voice->reset();
      break;
    case EngineCommandType::Seek:
      m_sequencer->setPosition(static_cast<uint16_t>(cmd.param1), 0);
      m_currentBar.store(static_cast<uint16_t>(cmd.param1), std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      for (auto& voice : m_voices) voice->reset();
      break;
    case EngineCommandType::SetVolume:
      m_volume.store(bitsToFloat(cmd.param1), std::memory_order_relaxed);
      break;
    case EngineCommandType::SetTempo:
      m_bpm.store(bitsToFloat(cmd.param1), std::memory_order_release);
      break;
    case EngineCommandType::SetTempoMultiplier:
      m_tempoMultiplier.store(bitsToFloat(cmd.param1), std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

void RbsAudioEngine::getPlaybackPosition(uint16_t& bar, uint8_t& step) const {
  bar = m_currentBar.load(std::memory_order_acquire);
  step = m_currentStep.load(std::memory_order_acquire);
}

float RbsAudioEngine::renderTestBlock(uint32_t numFrames) {
  if (numFrames == 0 || numFrames > MAX_PROCESS_BLOCK_FRAMES) return 0.0f;
  alignas(16) float output[MAX_PROCESS_BLOCK_FRAMES * 2]{};
  float* buffers[1] = {output};
  processBlock(buffers, 2, numFrames);
  float peak = 0.0f;
  for (uint32_t i = 0; i < numFrames * 2; ++i) {
    peak = std::max(peak, std::abs(output[i]));
  }
  return peak;
}

void RbsAudioEngine::processBlock(float* const* outputBuffers,
                                   uint32_t numChannels,
                                   uint32_t numFrames) {
  // Defensive: output must exist and not exceed our fixed scratch size.
  if (!outputBuffers || numChannels == 0 || numFrames == 0 ||
      numFrames > MAX_PROCESS_BLOCK_FRAMES) {
    return;
  }
  m_processedBlocks.fetch_add(1, std::memory_order_relaxed);

  // Always drain control commands at the start of the callback.
  drainCommands();

  // Clear the output buffer.
  float* stereoOut = outputBuffers[0];
  if (stereoOut) {
    std::memset(stereoOut, 0, numFrames * numChannels * sizeof(float));
  }

  if (!m_playing.load(std::memory_order_relaxed)) {
    return;
  }

  // Allocate scratch mono buffers on the stack (fixed size, no malloc).
  alignas(16) float scratchBuffers[NUM_DEVICES][MAX_PROCESS_BLOCK_FRAMES];
  for (int i = 0; i < NUM_DEVICES; ++i) {
    m_voiceBuffers[i] = scratchBuffers[i];
    std::memset(scratchBuffers[i], 0, numFrames * sizeof(float));
  }

  // Read the current immutable song snapshot. Holding a local shared_ptr for
  // the duration of the callback keeps it alive even if the main thread swaps
  // in a new song mid-block.
  std::shared_ptr<const ParsedSong> song = std::atomic_load(&m_activeSong);

  // Generate sequencer events for this block. Fixed-size stack buffer — no
  // heap allocation inside the audio callback.
  alignas(16) Sequencer::Event events[128];
  const float effectiveBpm =
      m_bpm.load(std::memory_order_relaxed) * m_tempoMultiplier.load(std::memory_order_relaxed);
  uint32_t eventCount = m_sequencer->generateEvents(
      song.get(), effectiveBpm, m_config.sampleRate, numFrames, events, 128);

  // Deliver events to voices at their sample offsets.
  // Sample-offset scheduling is intentionally simple for this pass: voices are
  // triggered at the start of the block containing the step boundary.
  for (uint32_t i = 0; i < eventCount; ++i) {
    const auto& ev = events[i];
    (void)ev.sampleOffset; // reserved for sample-accurate triggering later
    m_voices[static_cast<int>(ev.device)]->triggerStep(ev.stepIndex, ev.step);
  }

  // Render each voice into its mono scratch buffer.
  for (int i = 0; i < NUM_DEVICES; ++i) {
    const bool enabled = [&]() {
      switch (static_cast<DeviceId>(i)) {
        case DeviceId::TB303_A: return m_config.enableTb303A;
        case DeviceId::TB303_B: return m_config.enableTb303B;
        case DeviceId::TR808: return m_config.enableTr808;
        case DeviceId::TR909: return m_config.enableTr909;
        default: return true;
      }
    }();
    if (!enabled) {
      std::memset(m_voiceBuffers[i], 0, numFrames * sizeof(float));
      continue;
    }
    m_voices[i]->render(m_voiceBuffers[i], numFrames);
  }

  // Mix into final stereo interleaved output.
  if (stereoOut && numChannels >= 2) {
    m_mixer->process(m_voiceBuffers.data(), stereoOut, numFrames, effectiveBpm);

    // Master volume — sole user-facing gain stage (JS GainNode stays at unity).
    float vol = m_volume.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < numFrames * 2; ++i) {
      stereoOut[i] *= vol;
    }
  }

  // Publish position for UI thread.
  uint16_t bar = 0;
  uint8_t step = 0;
  m_sequencer->getPosition(bar, step);
  m_currentBar.store(bar, std::memory_order_relaxed);
  m_currentStep.store(step, std::memory_order_relaxed);
}

} // namespace rb338
