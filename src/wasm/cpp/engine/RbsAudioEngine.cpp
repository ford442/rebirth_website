#include "RbsAudioEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace rb338 {

namespace {

constexpr uint32_t MAX_PROCESS_BLOCK_FRAMES = MAX_RENDER_TEST_FRAMES;

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

void applyMasterVolume(float* left, float* right, uint32_t numFrames, float volume) {
#ifdef __wasm_simd128__
  const v128_t gain = wasm_f32x4_splat(volume);
  uint32_t i = 0;
  for (; i + 4 <= numFrames; i += 4) {
    wasm_v128_store(left + i, wasm_f32x4_mul(wasm_v128_load(left + i), gain));
    wasm_v128_store(right + i, wasm_f32x4_mul(wasm_v128_load(right + i), gain));
  }
  for (; i < numFrames; ++i) {
    left[i] *= volume;
    right[i] *= volume;
  }
#else
  for (uint32_t i = 0; i < numFrames; ++i) {
    left[i] *= volume;
    right[i] *= volume;
  }
#endif
}

} // anonymous namespace

RbsAudioEngine::RbsAudioEngine() {
  m_sequencer = std::make_unique<Sequencer>();
}

RbsAudioEngine::~RbsAudioEngine() {
  m_published.store(nullptr, std::memory_order_release);
  m_inUse.store(nullptr, std::memory_order_release);
  m_owned.clear();
}

bool RbsAudioEngine::init(const EngineConfig& config) {
  m_config = config;
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

  // Build the entire render graph on the main thread, then publish one pointer.
  // The audio thread never sees a half-loaded voice or mixer.
  auto next = buildEngineSnapshot(song, m_config);
  EngineSnapshot* raw = next.get();
  m_owned.push_back(std::move(next));
  m_published.store(raw, std::memory_order_release);

  m_bpm.store(std::clamp(song.bpm, 40.0f, 250.0f), std::memory_order_release);
  m_currentBar.store(1, std::memory_order_relaxed);
  m_currentStep.store(0, std::memory_order_relaxed);

  // Reset transport via the command queue; do not touch the live graph here.
  stop();
  reclaimRetiredSnapshots();
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

void RbsAudioEngine::setDeviceParam(uint8_t deviceId, uint8_t paramId, float value) {
  if (deviceId >= NUM_DEVICES) return;
  const uint32_t packed = static_cast<uint32_t>(deviceId) | (static_cast<uint32_t>(paramId) << 8);
  pushCommand({EngineCommandType::SetDeviceParam, packed, floatBits(value)});
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
      resetVoices(pinSnapshot());
      break;
    case EngineCommandType::Seek:
      m_sequencer->setPosition(static_cast<uint16_t>(cmd.param1), 0);
      m_currentBar.store(static_cast<uint16_t>(cmd.param1), std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      resetVoices(pinSnapshot());
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
    case EngineCommandType::SetDeviceParam: {
      const uint8_t deviceId = static_cast<uint8_t>(cmd.param1 & 0xffu);
      const auto param = static_cast<DeviceParamId>((cmd.param1 >> 8) & 0xffu);
      const float value = bitsToFloat(cmd.param2);
      applyDeviceParam(pinSnapshot(), deviceId, param, value);
      break;
    }
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
  alignas(16) float left[MAX_PROCESS_BLOCK_FRAMES]{};
  alignas(16) float right[MAX_PROCESS_BLOCK_FRAMES]{};
  float* buffers[2] = {left, right};
  processBlock(buffers, 2, numFrames);
  float peak = 0.0f;
  for (uint32_t i = 0; i < numFrames; ++i) {
    peak = std::max(peak, std::abs(left[i]));
    peak = std::max(peak, std::abs(right[i]));
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

  // Clear planar output channels.
  for (uint32_t ch = 0; ch < numChannels; ++ch) {
    if (outputBuffers[ch]) {
      std::memset(outputBuffers[ch], 0, numFrames * sizeof(float));
    }
  }

  if (!m_playing.load(std::memory_order_relaxed)) {
    return;
  }

  EngineSnapshot* snap = pinSnapshot();
  if (!snap || !snap->mixer) {
    return;
  }

  // Scratch mono buffers live in engine members (not on the audio-thread stack).
  for (int i = 0; i < NUM_DEVICES; ++i) {
    m_voiceBuffers[i] = m_scratchBuffers[i];
    std::memset(m_scratchBuffers[i], 0, numFrames * sizeof(float));
  }

  alignas(16) Sequencer::Event events[128];
  const float effectiveBpm =
      m_bpm.load(std::memory_order_relaxed) * m_tempoMultiplier.load(std::memory_order_relaxed);
  uint32_t eventCount = m_sequencer->generateEvents(
      &snap->song, effectiveBpm, m_config.sampleRate, numFrames, events, 128);

  const float vol = m_volume.load(std::memory_order_relaxed);
  float* left = (numChannels >= 2) ? outputBuffers[0] : nullptr;
  float* right = (numChannels >= 2) ? outputBuffers[1] : nullptr;

  uint32_t cursor = 0;
  uint32_t evIdx = 0;
  while (evIdx < eventCount) {
    uint32_t offset = events[evIdx].sampleOffset;
    if (offset > numFrames) offset = numFrames;
    if (offset > cursor) {
      renderSpan(*snap, cursor, offset - cursor, left, right, numChannels, effectiveBpm, vol);
    }
    while (evIdx < eventCount && events[evIdx].sampleOffset <= offset) {
      const auto& ev = events[evIdx++];
      const int di = static_cast<int>(ev.device);
      if (di >= 0 && di < NUM_DEVICES && snap->voices[static_cast<size_t>(di)]) {
        snap->voices[static_cast<size_t>(di)]->triggerStep(ev.stepIndex, ev.step);
      }
    }
    cursor = offset;
  }
  if (cursor < numFrames) {
    renderSpan(*snap, cursor, numFrames - cursor, left, right, numChannels, effectiveBpm, vol);
  }

  uint16_t bar = 0;
  uint8_t step = 0;
  m_sequencer->getPosition(bar, step);
  m_currentBar.store(bar, std::memory_order_relaxed);
  m_currentStep.store(step, std::memory_order_relaxed);
}

EngineSnapshot* RbsAudioEngine::pinSnapshot() {
  EngineSnapshot* snap = nullptr;
  do {
    snap = m_published.load(std::memory_order_acquire);
    m_inUse.store(snap, std::memory_order_release);
  } while (snap != m_published.load(std::memory_order_acquire));
  return snap;
}

void RbsAudioEngine::reclaimRetiredSnapshots() {
  EngineSnapshot* live = m_published.load(std::memory_order_acquire);
  EngineSnapshot* used = m_inUse.load(std::memory_order_acquire);
  auto it = std::remove_if(
      m_owned.begin(), m_owned.end(),
      [&](const std::unique_ptr<EngineSnapshot>& graph) {
        return graph.get() != live && graph.get() != used;
      });
  m_owned.erase(it, m_owned.end());
}

void RbsAudioEngine::resetVoices(EngineSnapshot* snap) {
  if (!snap) return;
  for (auto& voice : snap->voices) {
    if (voice) voice->reset();
  }
}

bool RbsAudioEngine::deviceEnabled(int deviceIndex) const {
  switch (static_cast<DeviceId>(deviceIndex)) {
    case DeviceId::TB303_A: return m_config.enableTb303A;
    case DeviceId::TB303_B: return m_config.enableTb303B;
    case DeviceId::TR808: return m_config.enableTr808;
    case DeviceId::TR909: return m_config.enableTr909;
    default: return true;
  }
}

void RbsAudioEngine::renderSpan(EngineSnapshot& snap, uint32_t start, uint32_t count,
                                float* left, float* right, uint32_t numChannels,
                                float bpm, float volume) {
  if (count == 0) return;

  float* spanVoices[NUM_DEVICES];
  for (int i = 0; i < NUM_DEVICES; ++i) {
    spanVoices[i] = m_voiceBuffers[i] + start;
    if (!deviceEnabled(i) || !snap.voices[static_cast<size_t>(i)]) {
      std::memset(spanVoices[i], 0, count * sizeof(float));
      continue;
    }
    snap.voices[static_cast<size_t>(i)]->render(spanVoices[i], count);
  }

  if (numChannels >= 2 && left && right && snap.mixer) {
    snap.mixer->process(spanVoices, left + start, right + start, count, bpm);
    applyMasterVolume(left + start, right + start, count, volume);
  }
}

void RbsAudioEngine::applyDeviceParam(EngineSnapshot* snap, uint8_t deviceId,
                                      DeviceParamId param, float value) {
  if (!snap || deviceId >= NUM_DEVICES) return;
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  Voice* voice = snap->voices[deviceId].get();
  Mixer* mixer = snap->mixer.get();

  switch (param) {
    case DeviceParamId::Tune:
      if (voice) voice->setParameter("tune", clamped);
      break;
    case DeviceParamId::Cutoff:
      if (voice) voice->setParameter("cutoff", clamped);
      break;
    case DeviceParamId::Resonance:
      if (voice) voice->setParameter("resonance", clamped);
      break;
    case DeviceParamId::EnvMod:
      if (voice) voice->setParameter("envMod", clamped);
      break;
    case DeviceParamId::Decay:
      if (voice) voice->setParameter("decay", clamped);
      break;
    case DeviceParamId::Accent:
      if (voice) voice->setParameter("accent", clamped);
      break;
    case DeviceParamId::Waveform:
      if (voice) voice->setParameter("waveform", clamped);
      break;
    case DeviceParamId::Level:
      if (mixer) mixer->setChannelLevel(static_cast<int>(deviceId), clamped);
      break;
    case DeviceParamId::Pan:
      if (mixer) mixer->setChannelPan(static_cast<int>(deviceId), clamped);
      break;
    case DeviceParamId::Mute:
      if (mixer) mixer->setChannelMuted(static_cast<int>(deviceId), value >= 0.5f);
      break;
  }
}

} // namespace rb338
