#pragma once

#include "RbsTypes.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rb338 {

/**
 * RbsWriter — serialises a ParsedSong back to `.rbs` bytes.
 *
 * The output is always a ReBirth 2.x chunk container (`CAT ` / `RB40`),
 * whatever container the song was read from: the v1/v1.5 MIDI-SysEx
 * container is read-only (see RbsMidiContainer.cpp), so a v1 song saved
 * here comes back as a v2.x file.
 *
 * The contract with RbsParser is **semantic**, not bitwise: parse → write →
 * parse must reproduce the same ParsedSong (BPM, titles, device state, every
 * StepData, arrangement PatternRefs, FX bytes and automation events). The
 * bytes differ from the original — padding, reserved fields and chunks the
 * parser skips are not preserved, and Propellerhead's undocumented fields are
 * written as zero. tests/test_writer.cpp is the gate for that round trip.
 *
 * Thread safety: NOT thread-safe. Create one writer per thread.
 */
class RbsWriter {
public:
  RbsWriter() = default;

  // Non-copyable (holds error state)
  RbsWriter(const RbsWriter&) = delete;
  RbsWriter& operator=(const RbsWriter&) = delete;

  /**
   * Serialise `song`.
   *
   * @return  File bytes on success, std::nullopt when the song cannot be
   *          represented (see lastError()). The writer rejects the same
   *          out-of-range values the parser rejects — a pattern slot beyond
   *          the 32 the format has, or an automation track beyond the nine
   *          TRAK slots — rather than emitting a file the parser would
   *          refuse to read back.
   */
  std::optional<std::vector<uint8_t>> write(const ParsedSong& song);

  /** Return the last error message (empty if no error occurred). */
  const std::string& lastError() const { return m_error; }

private:
  std::string m_error;
};

} // namespace rb338
