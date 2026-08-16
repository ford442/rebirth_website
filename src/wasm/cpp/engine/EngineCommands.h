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

enum class EngineCommandType : uint8_t {
  None = 0,
  Play,
  Pause,
  Stop,
  Seek,
  SetVolume,
  SetTempo,
  SetTempoMultiplier,
  LoadSong,
  SetDeviceParam,
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

struct EngineCommand {
  EngineCommandType type = EngineCommandType::None;
  uint32_t param1 = 0;  // bar, tempo, or raw float bits
  uint32_t param2 = 0;  // reserved for future use
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

