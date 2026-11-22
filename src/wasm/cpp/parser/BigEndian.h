#pragma once

// Raw big-endian reads and four-character-code comparison, shared by every
// IFF-style reader in the tree: the .rbs parser (RbsByteStream.h), the v1
// MIDI container, the .rbm parser and the AIFF decoder. Callers bounds-check
// before calling; these never do.

#include <cstdint>
#include <cstring>

namespace rb338::detail {

/** True when the 4 bytes at `id` equal the first 4 chars of `expected`. */
inline bool matchId(const uint8_t* id, const char* expected) {
  return std::memcmp(id, expected, 4) == 0;
}

inline uint16_t loadU16BE(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t loadU32BE(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

} // namespace rb338::detail
