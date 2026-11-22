#include "RbsAudioEngine.h"
#include "../parser/RbsWriter.h"
#include "../parser/RbmParser.h"
#include "../parser/RbsParser.h"
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

// StepData ↔ one 32-bit command word: note, drumExtra, then three flags.
uint32_t packStep(const StepData& step) {
  return static_cast<uint32_t>(step.note) |
         (static_cast<uint32_t>(step.drumExtra) << 8) |
         (step.active ? (1u << 16) : 0u) |
         (step.accent ? (1u << 17) : 0u) |
         (step.slide ? (1u << 18) : 0u);
}

StepData unpackStep(uint32_t word) {
  StepData step;
  step.note = static_cast<uint8_t>(word & 0xffu);
  step.drumExtra = static_cast<uint8_t>((word >> 8) & 0xffu);
  step.active = (word >> 16) & 1u;
  step.accent = (word >> 17) & 1u;
  step.slide = (word >> 18) & 1u;
  return step;
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
  m_song = song;
  m_hasSong = true;

  auto next = buildEngineSnapshot(m_song, m_config, m_samplePool);
  EngineSnapshot* raw = next.get();
  m_owned.push_back(std::move(next));
  m_published.store(raw, std::memory_order_seq_cst);

  m_bpm.store(std::clamp(song.bpm, MIN_BPM, MAX_BPM), std::memory_order_release);
  m_currentBar.store(1, std::memory_order_relaxed);
  m_currentStep.store(0, std::memory_order_relaxed);

  // Reset transport via the command queue; do not touch the live graph here.
  // The fresh graph is already at the song's own values, so the Stop's
  // rewind just re-applies the session overrides on the audio thread.
  pushCommand({EngineCommandType::Stop, 0, 0});
  reclaimRetiredSnapshots();
  return true;
}

std::optional<ParsedSong> RbsAudioEngine::loadSongFromBytes(const uint8_t* data,
                                                            size_t size) {
  RbsParser parser;
  auto song = parser.parse(data, size);
  if (!song) {
    m_lastParseError = parser.lastError();
    return std::nullopt;
  }
  m_lastParseError.clear();
  if (!loadSong(*song)) {
    m_lastParseError = "Engine not initialised";
    return std::nullopt;
  }
  return song;
}

void RbsAudioEngine::republishGraph() {
  // Rebuild the graph around the working copy with whatever sample pool is
  // now current. Transport state lives on the engine, not the snapshot, so
  // playback position survives the swap.
  //
  // The rebuild starts from m_song, not from the live snapshot's copy: the
  // working copy is the editable master, so a pattern edit made earlier is
  // still there after a later mod load.
  if (!m_hasSong) return; // no song yet — this applies at the next loadSong()

  auto next = buildEngineSnapshot(m_song, m_config, m_samplePool);
  // A rebuilt graph starts from the song's knob values. Bring it to where
  // playback is — automation up to the current bar, then the live knob
  // moves — or loading a mod mid-song would snap every knob back to bar 1.
  // The snapshot is not published yet, so the main thread still owns it.
  rewindGraph(next.get(), m_currentBar.load(std::memory_order_acquire));
  EngineSnapshot* raw = next.get();
  m_owned.push_back(std::move(next));
  m_published.store(raw, std::memory_order_seq_cst);
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

bool RbsAudioEngine::play() {
  return pushCommand({EngineCommandType::Play, 0, 0});
}

bool RbsAudioEngine::pause() {
  return pushCommand({EngineCommandType::Pause, 0, 0});
}

bool RbsAudioEngine::stop() {
  return pushCommand({EngineCommandType::Stop, 0, 0});
}

bool RbsAudioEngine::seek(uint16_t bar) {
  return pushCommand({EngineCommandType::Seek, std::max<uint16_t>(1, bar), 0});
}

// Volume and tempo are continuous: the audio thread reads each atomic once
// per block, so storing straight into it coalesces and needs no queue slot.
void RbsAudioEngine::setVolume(float volume) {
  m_volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

void RbsAudioEngine::setTempo(float bpm) {
  m_bpm.store(std::clamp(bpm, MIN_BPM, MAX_BPM), std::memory_order_release);
}

void RbsAudioEngine::setTempoMultiplier(float multiplier) {
  m_tempoMultiplier.store(std::clamp(multiplier, 0.25f, 4.0f), std::memory_order_relaxed);
}

void RbsAudioEngine::setDeviceParam(uint8_t deviceId, uint8_t paramId, float value) {
  if (deviceId >= NUM_DEVICES || paramId >= NUM_DEVICE_PARAMS) return;
  const uint64_t bit = paramBit(deviceId, paramId);
  // Value first, then the bits with release: the audio thread's acquire swap
  // of the dirty mask is then guaranteed to see this value (or a later one).
  m_paramSlots[size_t(deviceId) * NUM_DEVICE_PARAMS + paramId].store(
      floatBits(value), std::memory_order_relaxed);
  m_paramOverridden.fetch_or(bit, std::memory_order_release);
  m_paramDirty.fetch_or(bit, std::memory_order_release);
}

float RbsAudioEngine::paramSlotValue(size_t device, size_t param) const {
  return bitsToFloat(
      m_paramSlots[device * NUM_DEVICE_PARAMS + param].load(std::memory_order_acquire));
}

void RbsAudioEngine::applyParamOverrides(EngineSnapshot* snap) {
  if (!snap) return;
  const uint64_t mask = overrideMask();
  if (mask == 0) return;
  for (uint8_t device = 0; device < NUM_DEVICES; ++device) {
    for (uint8_t param = 0; param < NUM_DEVICE_PARAMS; ++param) {
      if (!(mask & paramBit(device, param))) continue;
      applyDeviceParam(snap, device, static_cast<DeviceParamId>(param),
                       paramSlotValue(device, param));
    }
  }
}

void RbsAudioEngine::drainParamSlots(EngineSnapshot* snap) {
  const uint64_t dirty = m_paramDirty.exchange(0, std::memory_order_acquire);
  if (dirty == 0 || !snap) return;
  for (uint8_t device = 0; device < NUM_DEVICES; ++device) {
    for (uint8_t param = 0; param < NUM_DEVICE_PARAMS; ++param) {
      if (!(dirty & paramBit(device, param))) continue;
      applyDeviceParam(snap, device, static_cast<DeviceParamId>(param),
                       paramSlotValue(device, param));
    }
  }
}

void RbsAudioEngine::rewindGraph(EngineSnapshot* snap, uint16_t bar) {
  if (!snap) return;
  applySongStateToSnapshot(*snap);
  Voice* voices[NUM_DEVICES];
  for (int i = 0; i < NUM_DEVICES; ++i) {
    voices[i] = snap->voices[static_cast<size_t>(i)].get();
  }
  const uint64_t mask = overrideMask();
  AutomationScheduler::foldUntil(snap->song, bar, 0, voices, snap->mixer.get(), mask);
  applyParamOverrides(snap);
}

bool RbsAudioEngine::pushCommand(const EngineCommand& cmd) {
  return m_commandQueue.push(cmd);
}

void RbsAudioEngine::drainCommands() {
  EngineCommand cmd;
  while (m_commandQueue.pop(cmd)) {
    handleCommand(cmd);
  }
  // Knob moves after the transport commands: a Stop/Seek rewind re-applies
  // the overrides anyway, and a move made after it must win.
  drainParamSlots(pinSnapshot());
}

void RbsAudioEngine::handleCommand(const EngineCommand& cmd) {
  switch (cmd.type) {
    case EngineCommandType::Play:
      m_playing.store(true, std::memory_order_relaxed);
      break;
    case EngineCommandType::Pause:
      m_playing.store(false, std::memory_order_relaxed);
      break;
    case EngineCommandType::Stop: {
      m_playing.store(false, std::memory_order_relaxed);
      m_sequencer->setPosition(1, 0);
      m_automation.reset();
      m_currentBar.store(1, std::memory_order_relaxed);
      m_currentStep.store(0, std::memory_order_relaxed);
      EngineSnapshot* stopSnap = pinSnapshot();
      resetVoices(stopSnap);
      // Automation moved the knobs as the song played; put them back.
      rewindGraph(stopSnap, 1);
      break;
    }
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
      // Land on the knob values playback would have reached at seekBar.
      rewindGraph(seekSnap, seekBar);
      break;
    }
    case EngineCommandType::PatchStep:
    case EngineCommandType::PatchLength: {
      // Rewrite the live snapshot's copy of the pattern in place. The
      // sequencer holds pointers into this vector, so a step edit is heard
      // the next time the step plays — with no voice, filter or delay state
      // touched. The vector never resizes here: the main thread only sends a
      // patch for an index the published snapshot already has.
      EngineSnapshot* patchSnap = pinSnapshot();
      if (!patchSnap) break;
      const size_t index = cmd.param1 & 0xffffu;
      if (index >= patchSnap->song.patterns.size()) break;
      Pattern& pattern = patchSnap->song.patterns[index];
      if (cmd.type == EngineCommandType::PatchStep) {
        const uint32_t stepIndex = (cmd.param1 >> 16) & 0xffu;
        if (stepIndex < MAX_STEPS) pattern.steps[stepIndex] = unpackStep(cmd.param2);
      } else {
        pattern.length = static_cast<uint8_t>(std::clamp<uint32_t>(cmd.param2, 1, MAX_STEPS));
        m_sequencer->invalidatePatterns(); // cached lengths must re-resolve
      }
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
  // the same song twice produces two different files. rewindGraph() also
  // puts the user's knob moves back on top of the song's values.
  rewindGraph(snap, 1);
  // Knob slots are folded in by rewindGraph; nothing is left to drain.
  m_paramDirty.store(0, std::memory_order_relaxed);
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
  const float bpm = std::clamp(m_bpm.load(std::memory_order_acquire), MIN_BPM, MAX_BPM);
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
  // One clamp, shared by the sequencer, the automation clock and the delay:
  // the multiplier can push the product outside the tempo range, and the
  // sequencer used to run unclamped while the other two clamped — so at
  // 2× a 140 BPM song, automation drifted away from the notes.
  const float effectiveBpm = std::clamp(
      m_bpm.load(std::memory_order_relaxed) * m_tempoMultiplier.load(std::memory_order_relaxed),
      MIN_BPM, MAX_BPM);

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
  const uint64_t overridden = overrideMask();

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
      AutomationScheduler::applyEvent(voicePtrs, snap->mixer.get(), autoEvents[autoIdx++],
                                      overridden);
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
  // Hazard pointer. Sequential consistency is load-bearing here, not
  // decoration: with plain release/acquire the store to m_inUse may be
  // reordered after the validating load of m_published, and then a reclaim
  // running in between reads a stale hazard and frees the snapshot this
  // thread just pinned.
  //
  // Under seq_cst the total order gives the guarantee the protocol needs: if
  // this thread's validating load saw snapshot S published, its hazard store
  // precedes that load, so any reclaim that publishes *after* S and then
  // reads m_inUse observes S and keeps it alive.
  EngineSnapshot* snap = nullptr;
  do {
    snap = m_published.load(std::memory_order_seq_cst);
    m_inUse.store(snap, std::memory_order_seq_cst);
  } while (snap != m_published.load(std::memory_order_seq_cst));
  return snap;
}

void RbsAudioEngine::reclaimRetiredSnapshots() {
  // Must run *after* the new snapshot is published (see pinSnapshot), and
  // must read the hazard with the same ordering.
  EngineSnapshot* live = m_published.load(std::memory_order_seq_cst);
  EngineSnapshot* used = m_inUse.load(std::memory_order_seq_cst);
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

bool RbsAudioEngine::validSlot(uint8_t deviceId, uint8_t bank, uint8_t patternIndex) {
  return deviceId < NUM_DEVICES && bank < MAX_BANKS && patternIndex < MAX_PATTERNS_PER_BANK;
}

const Pattern* RbsAudioEngine::findPattern(uint8_t deviceId, uint8_t bank,
                                           uint8_t patternIndex) const {
  if (!m_hasSong || !validSlot(deviceId, bank, patternIndex)) return nullptr;
  const auto device = static_cast<DeviceId>(deviceId);
  for (const auto& pattern : m_song.patterns) {
    if (pattern.deviceId == device && pattern.bank == bank &&
        pattern.patternIndex == patternIndex) {
      return &pattern;
    }
  }
  return nullptr;
}

Pattern* RbsAudioEngine::findOrCreatePattern(uint8_t deviceId, uint8_t bank,
                                             uint8_t patternIndex) {
  if (!m_hasSong || !validSlot(deviceId, bank, patternIndex)) return nullptr;
  const auto device = static_cast<DeviceId>(deviceId);
  for (auto& pattern : m_song.patterns) {
    if (pattern.deviceId == device && pattern.bank == bank &&
        pattern.patternIndex == patternIndex) {
      return &pattern;
    }
  }
  // The parser only emits slots the file actually stores, so editing an
  // untouched pattern has to materialise it first.
  Pattern fresh;
  fresh.deviceId = device;
  fresh.bank = bank;
  fresh.patternIndex = patternIndex;
  fresh.length = MAX_STEPS;
  m_song.patterns.push_back(fresh);
  return &m_song.patterns.back();
}

void RbsAudioEngine::publishPatternEdit(const Pattern& pattern, const EngineCommand& patch) {
  // The live snapshot holds a copy of m_song taken when it was built, and
  // m_song.patterns only ever grows by appending — so an index that exists
  // in the snapshot names the same pattern there.
  const EngineSnapshot* live = m_published.load(std::memory_order_acquire);
  const size_t index = static_cast<size_t>(&pattern - m_song.patterns.data());
  const bool inLive = live && index < live->song.patterns.size() && index <= 0xffffu;
  if (inLive) {
    EngineCommand cmd = patch;
    cmd.param1 |= static_cast<uint32_t>(index);
    if (pushCommand(cmd)) return;
  }
  // A brand-new pattern, or a full queue: rebuild. Correct, just not seamless.
  republishGraph();
}

bool RbsAudioEngine::setStep(uint8_t deviceId, uint8_t bank, uint8_t patternIndex,
                             uint8_t stepIndex, const StepData& step) {
  if (!m_initialised || stepIndex >= MAX_STEPS) return false;
  Pattern* pattern = findOrCreatePattern(deviceId, bank, patternIndex);
  if (!pattern) return false;

  pattern->steps[stepIndex] = step;
  // Slide is a TB-303 concept; a drum voice would just ignore it, but keeping
  // the working copy honest means an eventual .rbs writer has nothing to undo.
  if (deviceId != static_cast<uint8_t>(DeviceId::TB303_A) &&
      deviceId != static_cast<uint8_t>(DeviceId::TB303_B)) {
    pattern->steps[stepIndex].slide = false;
  }
  publishPatternEdit(*pattern,
                     EngineCommand{EngineCommandType::PatchStep,
                                   static_cast<uint32_t>(stepIndex) << 16,
                                   packStep(pattern->steps[stepIndex])});
  return true;
}

StepData RbsAudioEngine::getStep(uint8_t deviceId, uint8_t bank, uint8_t patternIndex,
                                 uint8_t stepIndex) const {
  if (stepIndex >= MAX_STEPS) return StepData{};
  const Pattern* pattern = findPattern(deviceId, bank, patternIndex);
  if (!pattern) return StepData{};
  return pattern->steps[stepIndex];
}

bool RbsAudioEngine::setPatternLength(uint8_t deviceId, uint8_t bank,
                                      uint8_t patternIndex, uint8_t length) {
  if (!m_initialised || length == 0 || length > MAX_STEPS) return false;
  Pattern* pattern = findOrCreatePattern(deviceId, bank, patternIndex);
  if (!pattern) return false;
  pattern->length = length;
  publishPatternEdit(*pattern, EngineCommand{EngineCommandType::PatchLength, 0, length});
  return true;
}

uint8_t RbsAudioEngine::getPatternLength(uint8_t deviceId, uint8_t bank,
                                         uint8_t patternIndex) const {
  const Pattern* pattern = findPattern(deviceId, bank, patternIndex);
  return pattern ? pattern->length : 0;
}

std::vector<uint8_t> RbsAudioEngine::saveRbs() {
  m_lastSaveError.clear();
  if (!m_hasSong) {
    m_lastSaveError = "No song loaded";
    return {};
  }

  // Save what is playing, not what was on disk: the working copy already
  // carries setStep() edits, so only the two pieces of session state held
  // outside it — the transport tempo and the live knob moves — have to be
  // folded in here.
  ParsedSong song = m_song;
  song.bpm = m_bpm.load(std::memory_order_acquire);
  const uint64_t overridden = overrideMask();
  for (int device = 0; device < NUM_DEVICES; ++device) {
    DeviceState& dev = song.devices[device];
    for (size_t param = 0; param < NUM_DEVICE_PARAMS; ++param) {
      if (!(overridden & paramBit(static_cast<size_t>(device), param))) continue;
      const float value = paramSlotValue(static_cast<size_t>(device), param);
      switch (static_cast<DeviceParamId>(param)) {
        case DeviceParamId::Tune:      dev.tune = value; break;
        case DeviceParamId::Cutoff:    dev.cutoff = value; break;
        case DeviceParamId::Resonance: dev.resonance = value; break;
        case DeviceParamId::EnvMod:    dev.envMod = value; break;
        case DeviceParamId::Decay:     dev.decay = value; break;
        case DeviceParamId::Accent:    dev.accent = value; break;
        case DeviceParamId::Waveform:  dev.waveform = value >= 0.5f ? 1 : 0; break;
        case DeviceParamId::Level:     dev.level = value; break;
        case DeviceParamId::Pan:       dev.pan = value; break;
        case DeviceParamId::Mute:      dev.muted = value >= 0.5f; break;
      }
    }
  }

  RbsWriter writer;
  auto bytes = writer.write(song);
  if (!bytes) {
    m_lastSaveError = writer.lastError();
    return {};
  }
  return std::move(*bytes);
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
