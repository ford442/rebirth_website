/**
 * EngineCommands — lock-free command queue between main thread and audio thread.
 *
 * The queue lives inside the WASM heap (as a member of RbsAudioEngine) so it is
 * visible to both the main thread and the Emscripten Wasm Audio Worklet thread,
 * which share the same linear memory.
 *
 * Single-producer / single-consumer:
 *   - Main thread calls pushCommand().
 *   - Audio thread calls drainCommands() at the top of each processBlock().
 *
 * No locks, no dynamic allocation, no blocking — safe for a real-time callback.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace rb338 {

/**
 * Discrete transport and edit commands. Continuous values (volume, tempo,
 * device knobs) do not travel through the queue: they are atomic
 * "latest value" slots on RbsAudioEngine, so a burst of knob moves coalesces
 * instead of filling the queue and crowding out a Stop.
 */
enum class EngineCommandType : uint8_t {
  None = 0,
  Play,
  Pause,
  Stop,
  Seek,
  /** param1 = pattern index | stepIndex << 16, param2 = packStep(step). */
  PatchStep,
  /** param1 = pattern index, param2 = new length (1–16). */
  PatchLength,
};

/** Live knob / mixer params pushed from the UI (session-only). */
enum class DeviceParamId : uint8_t {
  Tune = 0,
  Cutoff,
  Resonance,
  EnvMod,
  Decay,
  Accent,
  Waveform,
  Level,
  Pan,
  Mute,
};

/** Number of DeviceParamId values (Tune..Mute). */
constexpr uint8_t NUM_DEVICE_PARAMS = 10;

struct EngineCommand {
  EngineCommandType type = EngineCommandType::None;
  uint32_t param1 = 0;  // bar, or pattern index (+ step index)
  uint32_t param2 = 0;  // packed step, or pattern length
};

class EngineCommandQueue {
public:
  // Power-of-two so slot = index & (Capacity - 1) is a visible bound.
  static constexpr uint32_t Capacity = 64;

  static_assert((Capacity & (Capacity - 1u)) == 0u, "Capacity must be a power of two");

  bool push(const EngineCommand& cmd);
  bool pop(EngineCommand& cmd);

  bool empty() const {
    return m_writeIndex.load(std::memory_order_acquire) ==
           m_readIndex.load(std::memory_order_acquire);
  }

private:
  static constexpr uint32_t kIndexMask = Capacity - 1u;

  std::atomic<uint32_t> m_writeIndex{0};
  std::atomic<uint32_t> m_readIndex{0};
  EngineCommand m_commands[Capacity];
};

} // namespace rb338

