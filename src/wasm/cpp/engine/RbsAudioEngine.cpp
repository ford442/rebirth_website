#include "RbsAudioEngine.h"
#include "../parser/RbmParser.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace rb338 {

namespace {

constexpr uint32_t MAX_PROCESS_BLOCK_FRAMES = MAX_RENDER_TEST_FRAMES;

uint32_t floatBits(float f) {
  return std::bit_cast<uint32_t>(f);
}

float bitsToFloat(uint32_t bits) {
  return std::bit_cast<float>(bits);
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
  m_automation.reset();
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
  // The audio thread never sees a half-loaded voice or mixer. Any loaded mod
  // carries across so changing songs does not silently drop its samples.
  auto next = buildEngineSnapshot(song, m_config, m_samplePool);
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

void RbsAudioEngine::republishGraph() {
  // Rebuild the graph around the current song with whatever sample pool is
  // now current. Transport state lives on the engine, not the snapshot, so
  // playback position survives the swap.
  EngineSnapshot* current = m_published.load(std::memory_order_acquire);
  if (!current) return; // no song yet — the pool applies at the next loadSong()

  auto next = buildEngineSnapshot(current->song, m_config, m_samplePool);
  // A rebuilt graph starts from the song's knob values, so replay the live
  // ones — otherwise loading a mod would silently undo every knob move.
  applyParamOverrides(next.get());
  EngineSnapshot* raw = next.get();
  m_owned.push_back(std::move(next));
  m_published.store(raw, std::memory_order_release);
  reclaimRetiredSnapshots();
}

ModLoadStatus RbsAudioEngine::loadMod(const uint8_t* data, size_t size) {
  if (!m_initialised) {
    m_modReport = ModLoadReport{};
    m_modReport.status = ModLoadStatus::NotInitialised;
    return m_modReport.status;
  }
  if (!data || size == 0) {
    m_modReport = ModLoadReport{};
    m_modReport.status = ModLoadStatus::NoSamples;
    return m_modReport.status;
  }

  RbmParser parser;
  auto mod = parser.parse(data, size);
  if (!mod) {
    m_modReport = ModLoadReport{};
    m_modReport.status = ModLoadStatus::NoSamples;
    return m_modReport.status;
  }

  // Decode into a brand-new pool. Refilling the live one would race the
  // audio thread, which is reading PCM straight out of its arena.
  auto pool = std::make_shared<SamplePool>();
  if (!pool->init(SamplePool::kDefaultArenaFrames)) {
    m_modReport = ModLoadReport{};
    m_modReport.status = ModLoadStatus::ArenaExhausted;
    return m_modReport.status;
  }

  const ModLoadStatus status = pool->loadFromParsedMod(*mod, m_modReport);

  // Nothing usable decoded — keep whatever was already playing rather than
  // swapping in a silent kit.
  if (pool->loadedSlots() == 0) return status;

  m_samplePool = std::move(pool);
  republishGraph();
  return status;
}

void RbsAudioEngine::clearMod() {
  if (!m_samplePool) return;
  m_samplePool.reset();
  m_modReport = ModLoadReport{};
  republishGraph();
}

bool RbsAudioEngine::hasMod() const {
  return m_samplePool && !m_samplePool->empty();
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

  // Remember it on the main thread before queuing, so a later graph rebuild
  // can replay it. Recording inside the audio thread's command drain would
  // race the main thread reading it back.
  if (paramId < NUM_DEVICE_PARAMS) {
    m_paramOverrides[deviceId][paramId] = value;
    m_paramOverrideSet[deviceId][paramId] = true;
  }

  const uint32_t packed = static_cast<uint32_t>(deviceId) | (static_cast<uint32_t>(paramId) << 8);
  pushCommand({EngineCommandType::SetDeviceParam, packed, floatBits(value)});
}

void RbsAudioEngine::applyParamOverrides(EngineSnapshot* snap) {
  if (!snap) return;
  for (uint8_t device = 0; device < NUM_DEVICES; ++device) {
    for (uint8_t param = 0; param < NUM_DEVICE_PARAMS; ++param) {
      if (!m_paramOverrideSet[device][param]) continue;
      applyDeviceParam(snap, device, static_cast<DeviceParamId>(param),
                       m_paramOverrides[device][param]);
    }
  }
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
      m_automation.reset();
      m_currentBar.store(1, std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      resetVoices(pinSnapshot());
      break;
    case EngineCommandType::Seek: {
      const uint16_t seekBar = static_cast<uint16_t>(cmd.param1);
      m_sequencer->setPosition(seekBar, 0);
      EngineSnapshot* seekSnap = pinSnapshot();
      if (seekSnap) {
        m_automation.setPosition(&seekSnap->song, seekBar, 0);
      }
      m_currentBar.store(seekBar, std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      resetVoices(seekSnap);
      break;
    }
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

EngineSnapshot* RbsAudioEngine::prepareOfflineTransport() {
  if (!m_initialised) return nullptr;
  EngineSnapshot* snap = m_published.load(std::memory_order_acquire);
  if (!snap) return nullptr;

  // Flush anything already queued before taking the transport. loadSong()
  // enqueues a Stop, and processBlock() drains the queue at the top of every
  // block — so without this the first block would immediately stop the
  // transport we are about to start, and the whole bounce would be silent.
  drainCommands();

  // Rewind to the top of the arrangement and start the transport directly.
  // Going through the command queue would defer this into the first block,
  // which would render one block of the previous position.
  m_sequencer->setPosition(1, 0);
  m_automation.setPosition(&snap->song, 1, 0);
  m_currentBar.store(1, std::memory_order_relaxed);
  m_currentStep.store(0, std::memory_order_relaxed);
  resetVoices(snap);
  // Rewind the graph itself, not just the transport: TRAK automation has
  // been rewriting knobs and mixer levels in place, and the delay line still
  // holds the tail of whatever played last. Without both resets, bouncing
  // the same song twice produces two different files.
  applySongStateToSnapshot(*snap);
  // …then put the user's knob moves back on top, since the line above just
  // reset the graph to the song's own values.
  applyParamOverrides(snap);
  if (snap->mixer) snap->mixer->resetDspState();
  m_playing.store(true, std::memory_order_release);
  return snap;
}

uint32_t RbsAudioEngine::runOfflineRender(uint32_t frames, uint8_t deviceIndex,
                                          float* interleavedOut, WavPcm16Builder* wav) {
  EngineSnapshot* snap = prepareOfflineTransport();
  if (!snap) return 0;

  // Solo *after* the graph reset, not before: prepareOfflineTransport() calls
  // applySongStateToSnapshot(), which re-applies the song's own mute flags
  // and would silently undo an earlier solo — producing a "stem" identical
  // to the master.
  const bool stem = (deviceIndex != MASTER_BUS) && (deviceIndex < NUM_DEVICES);
  bool savedMutes[NUM_DEVICES] = {false, false, false, false};
  if (stem && snap->mixer) {
    for (int d = 0; d < NUM_DEVICES; ++d) {
      savedMutes[d] = snap->mixer->channelMuted(d);
      snap->mixer->setChannelMuted(d, d != static_cast<int>(deviceIndex));
    }
  }

  // Render in worklet-sized quanta. The engine is block-size invariant, but
  // matching the browser's quantum keeps the offline path identical to the
  // live one by construction rather than by argument.
  alignas(16) float left[AUDIO_WORKLET_FRAMES];
  alignas(16) float right[AUDIO_WORKLET_FRAMES];
  alignas(16) float interleaved[AUDIO_WORKLET_FRAMES * 2];
  float* buffers[2] = {left, right};

  uint32_t rendered = 0;
  while (rendered < frames) {
    const uint32_t count = std::min<uint32_t>(AUDIO_WORKLET_FRAMES, frames - rendered);
    processBlock(buffers, 2, count);
    for (uint32_t i = 0; i < count; ++i) {
      interleaved[i * 2] = left[i];
      interleaved[i * 2 + 1] = right[i];
    }
    if (interleavedOut) {
      ::memcpy(interleavedOut + static_cast<size_t>(rendered) * 2u, interleaved,
               static_cast<size_t>(count) * 2u * sizeof(float));
    }
    // Encoding per block means nothing accumulates as float.
    if (wav) wav->append(interleaved, count);
    rendered += count;
  }
  m_playing.store(false, std::memory_order_release);

  if (stem && snap->mixer) {
    for (int d = 0; d < NUM_DEVICES; ++d) {
      snap->mixer->setChannelMuted(d, savedMutes[d]);
    }
  }
  return rendered;
}

uint32_t RbsAudioEngine::renderOffline(float* interleavedStereo, uint32_t frames) {
  if (!interleavedStereo || frames == 0) return 0;
  return runOfflineRender(frames, MASTER_BUS, interleavedStereo, nullptr);
}

uint32_t RbsAudioEngine::renderOfflineStem(float* interleavedStereo, uint32_t frames,
                                           uint8_t deviceIndex) {
  if (!interleavedStereo || frames == 0 || deviceIndex >= NUM_DEVICES) return 0;
  return runOfflineRender(frames, deviceIndex, interleavedStereo, nullptr);
}

std::vector<uint8_t> RbsAudioEngine::renderOfflineWav(uint32_t frames, uint8_t deviceIndex) {
  const auto sampleRate = static_cast<uint32_t>(m_config.sampleRate);
  WavPcm16Builder builder(frames, 2, sampleRate);
  if (frames > 0) {
    runOfflineRender(frames, deviceIndex, nullptr, &builder);
  }
  return builder.take();
}

uint32_t RbsAudioEngine::songLengthFrames() const {
  EngineSnapshot* snap = m_published.load(std::memory_order_acquire);
  if (!snap) return 0;

  // One bar is four beats; the arrangement gives the bar count.
  const size_t bars = snap->song.arrangement.empty() ? 4u : snap->song.arrangement.size();
  const float bpm = std::clamp(m_bpm.load(std::memory_order_acquire), 40.0f, 250.0f);
  const double secondsPerBar = (60.0 / static_cast<double>(bpm)) * 4.0;
  const double seconds = secondsPerBar * static_cast<double>(bars);
  const double frames = seconds * static_cast<double>(m_config.sampleRate);

  // Keep a bounce bounded even for a pathological arrangement (30 minutes).
  const double maxFrames = 1800.0 * static_cast<double>(m_config.sampleRate);
  return static_cast<uint32_t>(std::min(frames, maxFrames));
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
      ::memset(outputBuffers[ch], 0, numFrames * sizeof(float));
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
    ::memset(m_scratchBuffers[i], 0, numFrames * sizeof(float));
  }

  alignas(16) Sequencer::Event events[128];
  alignas(16) AutomationScheduler::Event autoEvents[128];
  const float effectiveBpm =
      m_bpm.load(std::memory_order_relaxed) * m_tempoMultiplier.load(std::memory_order_relaxed);

  uint16_t bar = 0;
  uint8_t step = 0;
  m_sequencer->getPosition(bar, step);
  const double stepPhase = m_sequencer->getStepPhase();

  uint32_t eventCount = m_sequencer->generateEvents(
      &snap->song, effectiveBpm, m_config.sampleRate, numFrames, events, 128);
  uint32_t autoCount = m_automation.generateEvents(
      &snap->song, effectiveBpm, m_config.sampleRate, bar, step, stepPhase,
      numFrames, autoEvents, 128);

  const float vol = m_volume.load(std::memory_order_relaxed);
  float* left = (numChannels >= 2) ? outputBuffers[0] : nullptr;
  float* right = (numChannels >= 2) ? outputBuffers[1] : nullptr;

  Voice* voicePtrs[NUM_DEVICES];
  for (int i = 0; i < NUM_DEVICES; ++i) {
    voicePtrs[i] = snap->voices[static_cast<size_t>(i)].get();
  }

  uint32_t cursor = 0;
  uint32_t evIdx = 0;
  uint32_t autoIdx = 0;
  while (cursor < numFrames) {
    uint32_t nextStep = (evIdx < eventCount) ? events[evIdx].sampleOffset : numFrames;
    uint32_t nextAuto = (autoIdx < autoCount) ? autoEvents[autoIdx].sampleOffset : numFrames;
    uint32_t offset = std::min({nextStep, nextAuto, numFrames});
    if (offset > cursor) {
      renderSpan(*snap, cursor, offset - cursor, left, right, numChannels, effectiveBpm, vol);
    }
    while (autoIdx < autoCount && autoEvents[autoIdx].sampleOffset <= offset) {
      m_automation.applyEvent(voicePtrs, snap->mixer.get(), autoEvents[autoIdx++]);
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
      ::memset(spanVoices[i], 0, count * sizeof(float));
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
    case DeviceParamId::Level:
      if (mixer) mixer->setChannelLevel(static_cast<int>(deviceId), clamped);
      break;
    case DeviceParamId::Pan:
      if (mixer) mixer->setChannelPan(static_cast<int>(deviceId), clamped);
      break;
    case DeviceParamId::Mute:
      if (mixer) mixer->setChannelMuted(static_cast<int>(deviceId), value >= 0.5f);
      break;
    default:
      // Tune / Cutoff / Resonance / EnvMod / Decay / Accent / Waveform go
      // straight to the voice — no string lookup on the audio thread.
      if (voice) voice->setParameter(param, clamped);
      break;
  }
}

} // namespace rb338
