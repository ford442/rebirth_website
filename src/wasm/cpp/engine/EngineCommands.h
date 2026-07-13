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
};

struct EngineCommand {
  EngineCommandType type = EngineCommandType::None;
  uint32_t param1 = 0;  // bar, tempo, or raw float bits
  uint32_t param2 = 0;  // reserved for future use
};

class EngineCommandQueue {
public:
  static constexpr size_t Capacity = 64;

  bool push(const EngineCommand& cmd);
  bool pop(EngineCommand& cmd);

  bool empty() const {
    return m_writeIndex.load(std::memory_order_acquire) ==
           m_readIndex.load(std::memory_order_acquire);
  }

private:
  std::atomic<uint32_t> m_writeIndex{0};
  std::atomic<uint32_t> m_readIndex{0};
  EngineCommand m_commands[Capacity];
};

inline bool EngineCommandQueue::push(const EngineCommand& cmd) {
  const uint32_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
  const uint32_t nextWrite = (writeIdx + 1) % Capacity;
  // Single-producer: only the main thread writes, so we don't need to sync
  // against other writers. We do need to ensure the consumer sees the new data
  // after the index update.
  if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
    return false; // queue full
  }
  m_commands[writeIdx] = cmd;
  m_writeIndex.store(nextWrite, std::memory_order_release);
  return true;
}

inline bool EngineCommandQueue::pop(EngineCommand& cmd) {
  const uint32_t readIdx = m_readIndex.load(std::memory_order_relaxed);
  if (readIdx == m_writeIndex.load(std::memory_order_acquire)) {
    return false; // queue empty
  }
  cmd = m_commands[readIdx];
  m_readIndex.store((readIdx + 1) % Capacity, std::memory_order_release);
  return true;
}

} // namespace rb338
