// Internal byte helpers shared by the RbsParser and RbsWriter translation
// units (RbsParser.cpp chunk decoding, RbsTrak.cpp TRAK/STRAK decoding,
// RbsWriter.cpp chunk encoding).
//
// ByteStream reads; ByteSink writes the same primitives back. The two are
// deliberately adjacent: a change to the VLQ or big-endian encoding has to
// be made on both sides in one edit.
//
// Implementation detail of cpp/parser/ — not part of the public parser API.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace rb338::detail {

class ByteStream {
public:
  explicit ByteStream(std::span<const uint8_t> data)
    : m_data(data), m_pos(0) {}

  ByteStream(const uint8_t* data, size_t size)
    : ByteStream(std::span<const uint8_t>(data, size)) {}

  size_t pos() const { return m_pos; }
  size_t size() const { return m_data.size(); }
  size_t remaining() const { return m_data.size() - m_pos; }
  const uint8_t* data() const { return m_data.data(); }

  bool atEnd() const { return m_pos >= m_data.size(); }

  bool canRead(size_t n) const { return n <= m_data.size() - m_pos; }

  bool skip(size_t n) {
    if (!canRead(n)) return false;
    m_pos += n;
    return true;
  }

  bool readU8(uint8_t& out) {
    if (!canRead(1)) return false;
    out = m_data[m_pos++];
    return true;
  }

  bool readU16BE(uint16_t& out) {
    if (!canRead(2)) return false;
    out = static_cast<uint16_t>(m_data[m_pos]) << 8 |
          static_cast<uint16_t>(m_data[m_pos + 1]);
    m_pos += 2;
    return true;
  }

  bool readU32BE(uint32_t& out) {
    if (!canRead(4)) return false;
    out = static_cast<uint32_t>(m_data[m_pos]) << 24 |
          static_cast<uint32_t>(m_data[m_pos + 1]) << 16 |
          static_cast<uint32_t>(m_data[m_pos + 2]) << 8 |
          static_cast<uint32_t>(m_data[m_pos + 3]);
    m_pos += 4;
    return true;
  }

  bool peekU8(uint8_t& out) const {
    if (!canRead(1)) return false;
    out = m_data[m_pos];
    return true;
  }

  bool readBytes(size_t n, const uint8_t*& out) {
    if (!canRead(n)) return false;
    out = m_data.data() + m_pos;
    m_pos += n;
    return true;
  }

  bool readCString(std::string& out) {
    out.clear();
    while (m_pos < m_data.size()) {
      uint8_t c = m_data[m_pos++];
      if (c == 0) return true;
      out.push_back(static_cast<char>(c));
    }
    // Reached end without null terminator — treat as terminated.
    return !out.empty();
  }

private:
  std::span<const uint8_t> m_data;
  size_t m_pos;
};

// ── ByteSink — the write side of ByteStream ──────────────────────────
class ByteSink {
public:
  ByteSink() = default;

  size_t size() const { return m_bytes.size(); }
  const std::vector<uint8_t>& bytes() const { return m_bytes; }
  std::vector<uint8_t> take() { return std::move(m_bytes); }

  void u8(uint8_t value) { m_bytes.push_back(value); }

  void u16BE(uint16_t value) {
    u8(static_cast<uint8_t>(value >> 8));
    u8(static_cast<uint8_t>(value));
  }

  void u32BE(uint32_t value) {
    u8(static_cast<uint8_t>(value >> 24));
    u8(static_cast<uint8_t>(value >> 16));
    u8(static_cast<uint8_t>(value >> 8));
    u8(static_cast<uint8_t>(value));
  }

  void raw(const uint8_t* data, size_t n) { m_bytes.insert(m_bytes.end(), data, data + n); }

  void raw(const std::vector<uint8_t>& data) { raw(data.data(), data.size()); }

  /** Four-character chunk id (no terminator). */
  void id(const char (&chunkId)[5]) {
    raw(reinterpret_cast<const uint8_t*>(chunkId), 4);
  }

  void zeros(size_t n) { m_bytes.insert(m_bytes.end(), n, uint8_t{0}); }

  /** Zero-fill until the sink is exactly `n` bytes long. */
  void padTo(size_t n) {
    if (m_bytes.size() < n) zeros(n - m_bytes.size());
  }

  /** IFF alignment: chunks start on even offsets. */
  void padToEven() {
    if (m_bytes.size() & 1u) u8(0);
  }

  /**
   * Null-terminated string written into a fixed-width field. Longer strings
   * are truncated so the terminator always fits.
   */
  void cStringField(const std::string& text, size_t fieldWidth) {
    const size_t start = m_bytes.size();
    const size_t copied = std::min(text.size(), fieldWidth - 1);
    raw(reinterpret_cast<const uint8_t*>(text.data()), copied);
    padTo(start + fieldWidth);
  }

  /** Null-terminated string with no fixed width. */
  void cString(const std::string& text) {
    raw(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    u8(0);
  }

  /** MIDI-style big-endian variable-length quantity (the readVlq inverse). */
  void vlq(uint32_t value) {
    uint8_t buffer[5];
    int count = 0;
    buffer[count++] = static_cast<uint8_t>(value & 0x7f);
    while (value >= 0x80) {
      value >>= 7;
      buffer[count++] = static_cast<uint8_t>((value & 0x7f) | 0x80);
    }
    while (count > 0) u8(buffer[--count]);
  }

private:
  std::vector<uint8_t> m_bytes;
};

// ── Chunk helpers ────────────────────────────────────────────────────
inline bool matchId(const uint8_t* id, const char* expected) {
  return std::memcmp(id, expected, 4) == 0;
}

inline bool readChunkHeader(ByteStream& stream, const uint8_t*& id, uint32_t& size,
                     bool& fiveByteId) {
  fiveByteId = false;
  if (!stream.readBytes(4, id)) return false;
  // Some files use a 5-byte "STRAK" id (STRA + K) as an alias of TRAK.
  // Consume the extra K before the BE size so the body is a normal TRAK stream.
  uint8_t extra = 0;
  if (matchId(id, "STRA") && stream.peekU8(extra) && extra == 'K') {
    if (!stream.skip(1)) return false;
    static constexpr uint8_t kTrakId[4] = {'T', 'R', 'A', 'K'};
    id = kTrakId;
    fiveByteId = true;
  }
  if (!stream.readU32BE(size)) return false;
  return true;
}

// TRAK event positions are stored as MIDI-style big-endian variable-length
// quantities. The encoded unit is 1/24 of the 192 PPQN position documented by
// Propellerhead, so one 4/4 bar occupies 768 / 24 = 32 encoded ticks.
constexpr uint32_t TRAK_TICKS_PER_BAR = 32;

inline bool readVlq(ByteStream& stream, uint32_t& out) {
  out = 0;
  for (int i = 0; i < 5; ++i) {
    uint8_t byte = 0;
    if (!stream.readU8(byte)) return false;
    if (out > (std::numeric_limits<uint32_t>::max() >> 7)) return false;
    out = (out << 7) | static_cast<uint32_t>(byte & 0x7f);
    if ((byte & 0x80) == 0) return true;
  }
  return false;
}

}  // namespace rb338::detail
