#pragma once

#include "RbsTypes.h"
#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rb338 {

/**
 * RbsParser — reads a ReBirth RB-338 .rbs binary file and produces
 * a structured ParsedSong representation.
 *
 * The parser understands the real Propellerhead chunk container used by
 * archive .rbs files ("CAT ", "RB40", "HEAD", "GLOB", "USRI", device chunks,
 * etc.), not the plain header/offset layout described in older drafts.
 *
 * Usage:
 *   RbsParser parser;
 *   auto song = parser.parse(buffer, bufferSize);
 *   if (!song) { // handle error }
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
  bool m_seenTb303A = false;
  size_t m_trakIndex = 0;
  uint32_t m_maxTrakPosition = 0;
  std::array<std::vector<std::pair<uint32_t, uint8_t>>, NUM_DEVICES>
    m_patternChanges;

  // ── Container dispatch ──
  bool parseContainer(const uint8_t* data, size_t size, ParsedSong& song,
                      bool isRoot);
  bool parseNestedCat(const uint8_t* data, size_t size, ParsedSong& song);

  // ── Chunk parsers ──
  bool parseHead(const uint8_t* data, size_t size, ParsedSong& song);
  bool parseGlob(const uint8_t* data, size_t size, ParsedSong& song);
  bool parseUsri(const uint8_t* data, size_t size, ParsedSong& song);
  bool parseMixr(const uint8_t* data, size_t size, ParsedSong& song);
  bool parseDeviceChunk(DeviceId primaryId, DeviceId secondaryId,
                        const uint8_t* data, size_t size, ParsedSong& song);
  bool parseTrak(const uint8_t* data, size_t size, ParsedSong& song);
  bool buildArrangement(ParsedSong& song);

  // ── Legacy stubs (kept so existing call sites compile) ──
  bool validateHeader(const uint8_t* data, size_t size);
  bool readMetadata(const uint8_t* data, size_t size, size_t& offset,
                    ParsedSong& out);
  bool readDeviceStates(const uint8_t* data, size_t size, size_t& offset,
                        ParsedSong& out);
  bool readPatterns(const uint8_t* data, size_t size, size_t& offset,
                    ParsedSong& out);
  bool readArrangement(const uint8_t* data, size_t size, size_t& offset,
                       ParsedSong& out);
};

} // namespace rb338
