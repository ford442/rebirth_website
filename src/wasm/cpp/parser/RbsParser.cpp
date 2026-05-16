#include "RbsParser.h"
#include <cstring>

namespace rb338 {

std::optional<ParsedSong> RbsParser::parse(const uint8_t* data, size_t size) {
  clearError();

  if (!data || size < 38) {
    m_error = "File too small to contain valid header";
    return std::nullopt;
  }

  if (!validateHeader(data, size)) {
    return std::nullopt;
  }

  ParsedSong song;
  size_t offset = 38; // skip header

  if (!readMetadata(data, size, offset, song)) return std::nullopt;
  if (!readDeviceStates(data, size, offset, song)) return std::nullopt;
  if (!readPatterns(data, size, offset, song)) return std::nullopt;
  if (!readArrangement(data, size, offset, song)) return std::nullopt;

  return song;
}

bool RbsParser::validateHeader(const uint8_t* data, size_t size) {
  if (size < RBS_MAGIC_LEN) {
    m_error = "File too small";
    return false;
  }
  if (std::memcmp(data, RBS_MAGIC, RBS_MAGIC_LEN) != 0) {
    m_error = "Invalid magic bytes — not a ReBirth song file";
    return false;
  }
  // Version check
  uint8_t versionByte = data[RBS_MAGIC_LEN];
  if (versionByte != 0x15 && versionByte != 0x20 && versionByte != 0x21) {
    m_error = "Unsupported file version";
    return false;
  }
  return true;
}

bool RbsParser::readMetadata(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out) {
  // TODO: implement actual metadata parsing from offset
  out.title = "Untitled";
  out.author = "Unknown";
  out.bpm = 125.0f;
  return true;
}

bool RbsParser::readDeviceStates(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out) {
  // TODO: implement device state block parsing
  for (auto& dev : out.devices) {
    dev.tune = 0.5f;
    dev.cutoff = 0.5f;
    dev.resonance = 0.5f;
  }
  return true;
}

bool RbsParser::readPatterns(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out) {
  // TODO: implement pattern data parsing
  return true;
}

bool RbsParser::readArrangement(const uint8_t* data, size_t size, size_t& offset, ParsedSong& out) {
  // TODO: implement arrangement parsing
  return true;
}

} // namespace rb338
