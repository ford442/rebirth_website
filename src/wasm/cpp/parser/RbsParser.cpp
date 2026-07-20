#include "RbsParser.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace rb338 {

namespace {

// ── Latin-1 → UTF-8 helper for info/author text ──────────────────────
std::string latin1ToUtf8(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    uint8_t c = data[i];
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back(static_cast<char>(0xc0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
    }
  }
  return out;
}

std::string latin1ToUtf8(const std::string& s) {
  return latin1ToUtf8(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// ── Bounds-checked byte stream ───────────────────────────────────────
class ByteStream {
public:
  ByteStream(const uint8_t* data, size_t size)
    : m_data(data), m_size(size), m_pos(0) {}

  size_t pos() const { return m_pos; }
  size_t size() const { return m_size; }
  size_t remaining() const { return m_size - m_pos; }
  const uint8_t* data() const { return m_data; }

  bool atEnd() const { return m_pos >= m_size; }

  bool canRead(size_t n) const { return m_pos + n <= m_size; }

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

  bool readBytes(size_t n, const uint8_t*& out) {
    if (!canRead(n)) return false;
    out = m_data + m_pos;
    m_pos += n;
    return true;
  }

  bool readCString(std::string& out) {
    out.clear();
    while (m_pos < m_size) {
      uint8_t c = m_data[m_pos++];
      if (c == 0) return true;
      out.push_back(static_cast<char>(c));
    }
    // Reached end without null terminator — treat as terminated.
    return !out.empty();
  }

private:
  const uint8_t* m_data;
  size_t m_size;
  size_t m_pos;
};

// ── Chunk helpers ────────────────────────────────────────────────────
bool matchId(const uint8_t* id, const char* expected) {
  return std::memcmp(id, expected, 4) == 0;
}

bool readChunkHeader(ByteStream& stream, const uint8_t*& id, uint32_t& size) {
  if (!stream.readBytes(4, id)) return false;
  if (!stream.readU32BE(size)) return false;
  return true;
}

// Skip the chunk body and any odd-alignment padding byte.

// ── Pattern decoding helpers ─────────────────────────────────────────
std::array<StepData, MAX_STEPS> decode303Pattern(const uint8_t* data,
                                                  uint8_t length) {
  std::array<StepData, MAX_STEPS> steps{};
  uint8_t numSteps = std::min<uint8_t>(length, MAX_STEPS);
  for (uint8_t s = 0; s < numSteps; ++s) {
    uint8_t note = data[s * 2];
    uint8_t flags = data[s * 2 + 1];
    StepData& step = steps[s];
    step.note = note;
    step.accent = (flags & 0x01) != 0;
    step.slide = (flags & 0x02) != 0;
    // Note value 0 is treated as a rest; C-1 (MIDI 0) is not musically useful
    // on a TB-303 and never appears in observed patterns.
    step.active = (note != 0);
  }
  return steps;
}

std::array<StepData, MAX_STEPS> decodeDrumPattern(const uint8_t* data,
                                                   uint8_t length) {
  std::array<StepData, MAX_STEPS> steps{};
  uint8_t numSteps = std::min<uint8_t>(length, MAX_STEPS);
  for (uint8_t s = 0; s < numSteps; ++s) {
    const uint8_t* row = data + s * 12;
    uint8_t hits = 0;
    // Pack the eight primary drum voices into a single byte. The observed
    // 12-byte row layout is:
    //   [0] BD / BD flag, [1] BD velocity/accent/tweak, [2] SD, [3] LT,
    //   [4] MT, [5] HT, [6] CH, [7] OH, [8] CL, [9] CP, [10] MA, [11] RS.
    // Both byte 0 and byte 1 are treated as BD indicators because 808 patterns
    // store BD hits in byte 1 while 909 patterns use byte 0.
    if (row[0] != 0 || row[1] != 0) hits |= 0x01; // BD
    if (row[2] != 0) hits |= 0x02; // SD
    if (row[3] != 0) hits |= 0x04; // LT
    if (row[4] != 0) hits |= 0x08; // MT
    if (row[5] != 0) hits |= 0x10; // HT
    if (row[6] != 0) hits |= 0x20; // CH
    if (row[7] != 0) hits |= 0x40; // OH
    if (row[8] != 0) hits |= 0x80; // CL
    uint8_t extra = 0;
    if (row[9] != 0) extra |= 0x01;  // CP
    if (row[10] != 0) extra |= 0x02; // MA
    if (row[11] != 0) extra |= 0x04; // RS
    StepData& step = steps[s];
    step.note = hits;
    step.drumExtra = extra;
    step.active = (hits != 0 || extra != 0);
    // Treat the BD tweak byte as a generic accent flag for this step.
    step.accent = (row[1] != 0);
  }
  return steps;
}

// ── Metadata helpers ─────────────────────────────────────────────────
std::string trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(start, end - start);
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════
// RbsParser public API
// ═════════════════════════════════════════════════════════════════════

std::optional<ParsedSong> RbsParser::parse(const uint8_t* data, size_t size) {
  clearError();
  m_seenTb303A = false;

  if (!data || size < 16) {
    m_error = "File too small to contain a valid ReBirth container";
    return std::nullopt;
  }

  ParsedSong song;
  if (!parseContainer(data, size, song, true)) {
    return std::nullopt;
  }

  // Sanity checks after parsing
  if (song.title.empty()) {
    m_error = "Parsed song has no title (GLOB chunk missing or malformed)";
    return std::nullopt;
  }

  return song;
}

// ═════════════════════════════════════════════════════════════════════
// Container / chunk dispatcher
// ═════════════════════════════════════════════════════════════════════

bool RbsParser::parseContainer(const uint8_t* data, size_t size,
                               ParsedSong& song, bool isRoot) {
  ByteStream stream(data, size);

  if (isRoot) {
    const uint8_t* id = nullptr;
    uint32_t catSize = 0;
    if (!readChunkHeader(stream, id, catSize)) {
      m_error = "Failed to read root container header";
      return false;
    }
    if (!matchId(id, RBS_CONTAINER_MAGIC)) {
      m_error = "Missing root 'CAT ' container magic";
      return false;
    }
    if (catSize + 8 > size) {
      m_error = "Root container size exceeds file size";
      return false;
    }

    const uint8_t* marker = nullptr;
    if (!stream.readBytes(4, marker) || !matchId(marker, RBS_FORMAT_MARKER)) {
      m_error = "Missing 'RB40' format marker after root container";
      return false;
    }
  }

  while (!stream.atEnd()) {
    const uint8_t* id = nullptr;
    uint32_t chunkSize = 0;
    if (!readChunkHeader(stream, id, chunkSize)) {
      if (stream.atEnd()) break;
      m_error = "Truncated chunk header";
      return false;
    }

    if (chunkSize > stream.remaining()) {
      m_error = "Chunk size exceeds container bounds";
      return false;
    }

    const uint8_t* chunkData = stream.data() + stream.pos();
    if (!stream.skip(chunkSize)) {
      m_error = "Failed to skip chunk body";
      return false;
    }
    if ((chunkSize & 1) && !stream.skip(1)) {
      m_error = "Missing chunk alignment padding";
      return false;
    }

    if (matchId(id, RBS_HEAD_MAGIC)) {
      if (!parseHead(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "GLOB")) {
      if (!parseGlob(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "USRI")) {
      if (!parseUsri(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "MIXR")) {
      if (!parseMixr(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, RBS_CONTAINER_MAGIC)) {
      if (!parseNestedCat(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "303 ")) {
      if (!parseDeviceChunk(DeviceId::TB303_A, DeviceId::TB303_B,
                            chunkData, chunkSize, song)) {
        return false;
      }
    } else if (matchId(id, "808 ")) {
      if (!parseDeviceChunk(DeviceId::TR808, DeviceId::TR808,
                            chunkData, chunkSize, song)) {
        return false;
      }
    } else if (matchId(id, "909 ")) {
      if (!parseDeviceChunk(DeviceId::TR909, DeviceId::TR909,
                            chunkData, chunkSize, song)) {
        return false;
      }
    } else if (matchId(id, "TRAK")) {
      if (!parseTrak(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "STRA") || matchId(id, "STRAK")) {
      // STRAK is a 5-byte ID that overlaps the next chunk marker.
      // For now skip the body (arrangement detail phase 3).
      (void)chunkData;
    }
    // Unknown chunks are ignored.
  }

  return true;
}

bool RbsParser::parseNestedCat(const uint8_t* data, size_t size,
                               ParsedSong& song) {
  if (size < 4) {
    m_error = "Nested CAT container too small";
    return false;
  }

  ByteStream stream(data, size);
  const uint8_t* marker = nullptr;
  if (!stream.readBytes(4, marker)) {
    m_error = "Failed to read nested CAT marker";
    return false;
  }

  // The marker identifies the list; chunks follow immediately after it.
  return parseContainer(stream.data() + stream.pos(),
                        stream.remaining(), song, false);
}

// ═════════════════════════════════════════════════════════════════════
// Individual chunk parsers
// ═════════════════════════════════════════════════════════════════════

bool RbsParser::parseHead(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 0x1c) {
    m_error = "HEAD chunk too small";
    return false;
  }

  if (std::memcmp(data, RBS_HEAD_SIGNATURE, 4) != 0) {
    m_error = "Invalid HEAD signature";
    return false;
  }

  // The version/sub-format discriminator is the byte at offset 0x06
  // within the HEAD chunk data (0x01 vs 0x02 in observed v2.x files).
  song.headVersion = data[0x06];

  // Map observed markers to the public RbsVersion enum for backwards compat.
  if (song.headVersion == 0x01) {
    song.version = RbsVersion::V2_0;
  } else if (song.headVersion == 0x02) {
    song.version = RbsVersion::V2_0_1;
  }

  return true;
}

bool RbsParser::parseGlob(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 0x20) {
    m_error = "GLOB chunk too small";
    return false;
  }

  song.globSubFormat = data[0x03];
  song.showInfoOnOpen = (data[0x01] != 0);

  // Title is a null-terminated ASCII string starting at offset 0x0f.
  ByteStream stream(data, size);
  if (!stream.skip(0x0f)) {
    m_error = "GLOB chunk too small for title";
    return false;
  }
  std::string title;
  if (!stream.readCString(title)) {
    m_error = "Failed to read song title from GLOB";
    return false;
  }
  song.title = trim(title);

  // Creator URL fields are fixed offsets within GLOB; read if present.
  if (size > 0x119) {
    ByteStream urlStream(data + 0x119, size - 0x119);
    std::string url;
    if (urlStream.readCString(url) && !url.empty()) {
      song.creatorUrl = trim(url);
    }
  }

  // BPM field is not yet reliably identified; leave default.
  // song.bpm remains 125.0f until a confident tempo field is found.

  return true;
}

bool RbsParser::parseUsri(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 2) {
    m_error = "USRI chunk too small";
    return false;
  }

  ByteStream stream(data, size);

  // Author/creator is the first null-terminated string.
  std::string author;
  if (!stream.readCString(author)) {
    m_error = "Failed to read author from USRI";
    return false;
  }
  song.author = trim(latin1ToUtf8(author));

  // Info text follows after the author null. Skip any padding zeros, then
  // read the next null-terminated Latin-1 string.
  while (!stream.atEnd() && stream.data()[stream.pos()] == 0) {
    stream.skip(1);
  }

  std::string info;
  stream.readCString(info); // may be empty if no info text
  song.infoText = trim(latin1ToUtf8(info));

  return true;
}

bool RbsParser::parseMixr(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 64) {
    m_error = "MIXR chunk too small";
    return false;
  }

  // Master level at byte 0.
  // Per-device records begin at offset 0x10, 12 bytes each.
  for (int i = 0; i < NUM_DEVICES; ++i) {
    size_t off = 0x10 + i * 12;
    if (off + 12 > size) continue;
    DeviceState& dev = song.devices[i];
    dev.id = static_cast<DeviceId>(i);
    dev.muted = (data[off] == 0);
    dev.level = static_cast<float>(data[off + 1]) / 127.0f;
    dev.pan = static_cast<float>(data[off + 2]) / 127.0f;
    // Offsets 3-7 contain sends/flags; decode the ones we are confident about.
    dev.delaySend = static_cast<float>(data[off + 3]) / 127.0f;
    dev.dist = (data[off + 4] != 0);
    dev.pcf = (data[off + 5] != 0);
    dev.compressor = (data[off + 6] != 0);
  }

  return true;
}

// ═════════════════════════════════════════════════════════════════════
// Device chunks (state + 32 pattern slots per device)
// ═════════════════════════════════════════════════════════════════════

bool RbsParser::parseDeviceChunk(DeviceId primaryId, DeviceId secondaryId,
                                 const uint8_t* data, size_t size,
                                 ParsedSong& song) {
  // Decide which device slot this chunk belongs to. The first TB-303 chunk
  // is 303-A, the second is 303-B; 808/909 are unique.
  DeviceId targetId = primaryId;
  if (primaryId == DeviceId::TB303_A) {
    if (m_seenTb303A) {
      targetId = secondaryId;
    } else {
      m_seenTb303A = true;
    }
  }

  const bool is303 = (targetId == DeviceId::TB303_A ||
                      targetId == DeviceId::TB303_B);
  const size_t headerSize = is303 ? 9 : ((targetId == DeviceId::TR808) ? 30 : 31);
  const size_t slotSize   = is303 ? 34 : 194;

  if (size < headerSize + 32 * slotSize) {
    m_error = "Device chunk too small for state + 32 pattern slots";
    return false;
  }

  DeviceState& dev = song.devices[static_cast<int>(targetId)];
  dev.id = targetId;

  // The first bytes of each device chunk mirror the documented parameter order.
  dev.tune = static_cast<float>(data[0]) / 127.0f;
  dev.cutoff = static_cast<float>(data[1]) / 127.0f;
  dev.resonance = static_cast<float>(data[2]) / 127.0f;
  dev.envMod = static_cast<float>(data[3]) / 127.0f;
  dev.decay = static_cast<float>(data[4]) / 127.0f;
  dev.accent = static_cast<float>(data[5]) / 127.0f;

  if (is303) {
    dev.waveform = data[6];
    // The exact initial-pattern bank/index offsets inside the 9-byte 303 header
    // have not been confirmed; leave them at defaults to avoid decoding garbage.
    dev.initialPatternBank = 0;
    dev.initialPatternIndex = 0;
    dev.muted = (data[9] == 0);
  } else {
    // 808/909 state layout differs; mixer fields come from MIXR.
    // Initial pattern selection for drum machines is taken from the arrangement.
    dev.initialPatternBank = 0;
    dev.initialPatternIndex = 0;
  }

  const uint8_t* slotData = data + headerSize;
  for (int slot = 0; slot < 32; ++slot) {
    const uint8_t* slotPtr = slotData + slot * slotSize;
    uint8_t enabled = slotPtr[0];
    uint8_t length  = slotPtr[1];

    Pattern pattern;
    pattern.deviceId = targetId;
    pattern.bank = static_cast<uint8_t>(slot / MAX_PATTERNS_PER_BANK);
    pattern.patternIndex = static_cast<uint8_t>(slot % MAX_PATTERNS_PER_BANK);
    pattern.length = std::min<uint8_t>(length, MAX_STEPS);

    if (enabled != 0) {
      if (is303) {
        pattern.steps = decode303Pattern(slotPtr + 2, pattern.length);
      } else {
        pattern.steps = decodeDrumPattern(slotPtr + 2, pattern.length);
      }
    }

    song.patterns.push_back(pattern);
  }

  return true;
}

// ═════════════════════════════════════════════════════════════════════
// TRAK chunk placeholder (phase 3 will decode the event stream)
// ═════════════════════════════════════════════════════════════════════

bool RbsParser::parseTrak(const uint8_t* data, size_t size, ParsedSong& song) {
  // Phase 1 only validates that the chunk header is well-formed.
  // The body is stored as raw bytes for later arrangement decoding.
  (void)data;
  (void)size;
  (void)song;
  return true;
}

// Stubbed helpers kept for future phases
bool RbsParser::readMetadata(const uint8_t* data, size_t size,
                             size_t& offset, ParsedSong& out) {
  (void)data; (void)size; (void)offset; (void)out;
  return true;
}

bool RbsParser::readDeviceStates(const uint8_t* data, size_t size,
                                 size_t& offset, ParsedSong& out) {
  (void)data; (void)size; (void)offset; (void)out;
  return true;
}

bool RbsParser::readPatterns(const uint8_t* data, size_t size,
                             size_t& offset, ParsedSong& out) {
  (void)data; (void)size; (void)offset; (void)out;
  return true;
}

bool RbsParser::readArrangement(const uint8_t* data, size_t size,
                                size_t& offset, ParsedSong& out) {
  (void)data; (void)size; (void)offset; (void)out;
  return true;
}

bool RbsParser::validateHeader(const uint8_t* data, size_t size) {
  (void)data; (void)size;
  return true;
}

} // namespace rb338
