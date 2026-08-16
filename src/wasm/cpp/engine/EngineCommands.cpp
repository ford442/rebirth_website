#include "EngineCommands.h"

namespace rb338 {

bool EngineCommandQueue::push(const EngineCommand& cmd) {
  const uint32_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
  const uint32_t readIdx = m_readIndex.load(std::memory_order_acquire);
  if (writeIdx - readIdx >= Capacity) {
    return false;
  }
  const uint32_t slot = writeIdx & kIndexMask;
  if (slot >= Capacity) {
    return false;
  }
  m_commands[slot] = cmd;
  m_writeIndex.store(writeIdx + 1u, std::memory_order_release);
  return true;
}

bool EngineCommandQueue::pop(EngineCommand& cmd) {
  const uint32_t readIdx = m_readIndex.load(std::memory_order_relaxed);
  const uint32_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
  if (readIdx == writeIdx) {
    return false;
  }
  const uint32_t slot = readIdx & kIndexMask;
  if (slot >= Capacity) {
    return false;
  }
  cmd = m_commands[slot];
  m_readIndex.store(readIdx + 1u, std::memory_order_release);
  return true;
}

} // namespace rb338
