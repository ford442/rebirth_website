#pragma once

#include "RbsTypes.h"
#include <cstddef>
#include <optional>
#include <string>

namespace rb338 {

/**
 * RbsParser — reads a ReBirth RB-338 .rbs binary file and produces
 * a structured ParsedSong representation.
 *
 * Usage:
 *   RbsParser parser;
 *   auto song = parser.parse(buffer, bufferSize);
 *   if (!song) { /* handle error */ }
 *
 * Thread safety: NOT thread-safe. Create one parser per thread.
 */
class RbsParser {
public:
  RbsParser() = default;
  ~RbsParser() = default;

  // Non-copyable (holds parse state)
  RbsParser(const RbsParser&) = delete;
  RbsParser& operator=(const RbsParser&) = delete;

  /**
   * Parse an .rbs file from a raw byte buffer.
   *
   * @param data  Pointer to the beginning of the file bytes.
   * @param size  Total size of the buffer in bytes.
   * @return      ParsedSong on success, std::nullopt on failure.
   *              Call lastError() for details when nullopt is returned.
   */
  std::optional<ParsedSong> parse(const uint8_t* data, size_t size);

  /** Return the last error message (empty if no error occurred). */
  const std::string& lastError() const { return m_error; }

  /** Reset internal error state. */
  void clearError() { m_error.clear(); }

private:
  std::string m_error;

  // ── Internal parse helpers (stubs for future implementation) ──
  bool validateHeader(const uint8_t* data, size_t size);
  bool readMetadata(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out);
  bool readDeviceStates(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out);
  bool readPatterns(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out);
  bool readArrangement(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out);
};

} // namespace rb338
