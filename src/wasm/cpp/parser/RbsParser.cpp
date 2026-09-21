#include "RbsParser.h"
#include "RbsByteStream.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <span>

namespace rb338 {

namespace {

using detail::ByteStream;
using detail::matchId;
using detail::readChunkHeader;

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
  if (!data && size != 0) {
    return std::nullopt;
  }
  return parse(std::span<const uint8_t>(data, size));
}

std::optional<ParsedSong> RbsParser::parse(std::span<const uint8_t> buffer) {
  clearError();
  m_seenTb303A = false;
  m_trakIndex = 0;
  m_maxTrakPosition = 0;
  for (auto& changes : m_patternChanges) changes.clear();

  if (buffer.size() < 16) {
    m_error = "File too small to contain a valid ReBirth container";
    return std::nullopt;
  }

  ParsedSong song;
  const uint8_t* data = buffer.data();
  const size_t size = buffer.size();

  if (size >= 4 && matchId(data, RBS_CONTAINER_MAGIC)) {
    if (!parseContainer(data, size, song, true)) {
      return std::nullopt;
    }
  } else if (size >= 4 && matchId(data, "MThd")) {
    if (!parseMidiContainer(data, size, song)) {
      return std::nullopt;
    }
  } else {
    m_error = "Unrecognized ReBirth song container";
    return std::nullopt;
  }

  if (!buildArrangement(song)) {
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
    bool fiveByteId = false;
    if (!readChunkHeader(stream, id, catSize, fiveByteId)) {
      m_error = "Failed to read root container header";
      return false;
    }
    if (!matchId(id, RBS_CONTAINER_MAGIC)) {
      m_error = "Missing root 'CAT ' container magic";
      return false;
    }
    (void)fiveByteId;
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
    bool fiveByteId = false;
    if (!readChunkHeader(stream, id, chunkSize, fiveByteId)) {
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
    // IFF pads to an even byte length from the chunk start. A 4-byte id is
    // 8 header bytes (pad when size is odd); STRAK's 5-byte id is 9 (pad
    // when size is even).
    const uint32_t headerBytes = fiveByteId ? 9u : 8u;
    if (((headerBytes + chunkSize) & 1u) && !stream.skip(1)) {
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
    } else if (matchId(id, "DELY")) {
      if (!parseDely(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "PCF ")) {
      if (!parsePcf(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "DIST")) {
      if (!parseDist(chunkData, chunkSize, song)) return false;
    } else if (matchId(id, "COMP")) {
      if (!parseComp(chunkData, chunkSize, song)) return false;
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
    }
    // Unknown chunks (including a 4-byte STRA that is not STRAK) are ignored.
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

  // Propellerhead's RBS 4.2 specification stores tempo as a big-endian
  // unsigned integer at offset 2, scaled by 1000.
  ByteStream tempoStream(data + 2, size - 2);
  uint32_t tempoTimes1000 = 0;
  if (!tempoStream.readU32BE(tempoTimes1000)) {
    m_error = "Truncated GLOB tempo field";
    return false;
  }
  const float bpm = static_cast<float>(tempoTimes1000) / 1000.0f;
  if (bpm < 20.0f || bpm > 500.0f) {
    m_error = "GLOB tempo is outside ReBirth's 20-500 BPM range";
    return false;
  }
  song.bpm = bpm;

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

  song.fx.masterLevel = data[0];

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

bool RbsParser::parseDely(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 5) {
    m_error = "DELY chunk too small";
    return false;
  }
  auto& fx = song.fx.delay;
  fx.enabled = (data[0] != 0);
  fx.time = data[1];
  fx.feedback = data[3];
  fx.wet = data[4];
  return true;
}

bool RbsParser::parsePcf(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 7) {
    m_error = "PCF  chunk too small";
    return false;
  }
  auto& fx = song.fx.pcf;
  fx.enabled = (data[0] != 0);
  fx.cutoff = data[1];
  fx.resonance = data[2];
  fx.envAmount = data[3];
  return true;
}

bool RbsParser::parseDist(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 3) {
    m_error = "DIST chunk too small";
    return false;
  }
  auto& fx = song.fx.dist;
  fx.enabled = (data[0] != 0);
  fx.drive = data[1];
  fx.mix = data[2];
  return true;
}

bool RbsParser::parseComp(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 3) {
    m_error = "COMP chunk too small";
    return false;
  }
  auto& fx = song.fx.comp;
  fx.enabled = (data[0] != 0);
  fx.ratio = data[1];
  fx.threshold = data[2];
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

  // Every device header starts with enabled + selected-pattern. This is also
  // the correct fallback when a pattern-mode file has no recorded TRAK data.
  dev.muted = (data[0] == 0);
  const uint8_t selectedPattern = data[1];
  if (selectedPattern >= 32) {
    m_error = "Device selected-pattern value exceeds the 32 pattern slots";
    return false;
  }
  dev.initialPatternBank = static_cast<uint8_t>(selectedPattern / 8);
  dev.initialPatternIndex = static_cast<uint8_t>(selectedPattern % 8);

  if (is303) {
    dev.tune = static_cast<float>(data[2]) / 127.0f;
    dev.cutoff = static_cast<float>(data[3]) / 127.0f;
    dev.resonance = static_cast<float>(data[4]) / 127.0f;
    dev.envMod = static_cast<float>(data[5]) / 127.0f;
    dev.decay = static_cast<float>(data[6]) / 127.0f;
    dev.accent = static_cast<float>(data[7]) / 127.0f;
    dev.waveform = data[8];
  }

  const uint8_t* slotData = data + headerSize;
  for (int slot = 0; slot < 32; ++slot) {
    const uint8_t* slotPtr = slotData + slot * slotSize;
    // Byte 0 is the per-pattern shuffle flag, not an enabled marker. ReBirth
    // stores every one of the 32 slots; an unused slot is represented by empty
    // steps and still has a valid length field.
    uint8_t length  = slotPtr[1];

    Pattern pattern;
    pattern.deviceId = targetId;
    pattern.bank = static_cast<uint8_t>(slot / MAX_PATTERNS_PER_BANK);
    pattern.patternIndex = static_cast<uint8_t>(slot % MAX_PATTERNS_PER_BANK);
    pattern.length = std::min<uint8_t>(length, MAX_STEPS);

    if (is303) {
      pattern.steps = decode303Pattern(slotPtr + 2, pattern.length);
    } else {
      pattern.steps = decodeDrumPattern(slotPtr + 2, pattern.length);
    }

    song.patterns.push_back(pattern);
  }

  return true;
}

bool RbsParser::parseV1DeviceChunk(DeviceId primaryId, DeviceId secondaryId,
                                   const uint8_t* data, size_t size,
                                   ParsedSong& song, size_t labelSkip,
                                   size_t slotSize, size_t headerSize,
                                   bool is303, bool lengthAtZero) {
  DeviceId targetId = primaryId;
  if (primaryId == DeviceId::TB303_A) {
    if (m_seenTb303A) {
      targetId = secondaryId;
    } else {
      m_seenTb303A = true;
    }
  }

  if (size < labelSkip + headerSize + 32 * slotSize) {
    m_error = "v1 device block too small for state + 32 pattern slots";
    return false;
  }

  const uint8_t* hdr = data + labelSkip;
  DeviceState& dev = song.devices[static_cast<int>(targetId)];
  dev.id = targetId;

  uint8_t selectedPattern = 0;
  if (is303) {
    if (hdr[0] <= 1 && hdr[1] > 31) {
      dev.muted = (hdr[0] == 0);
      dev.tune = static_cast<float>(hdr[1]) / 127.0f;
      selectedPattern = hdr[2];
      dev.cutoff = static_cast<float>(hdr[3]) / 127.0f;
      if (headerSize >= 5) {
        dev.resonance = static_cast<float>(hdr[4]) / 127.0f;
      }
    } else {
      dev.muted = false;
      dev.tune = static_cast<float>(hdr[0]) / 127.0f;
      selectedPattern = hdr[1];
      dev.cutoff = static_cast<float>(hdr[2]) / 127.0f;
      if (headerSize >= 5) {
        dev.resonance = static_cast<float>(hdr[3]) / 127.0f;
        dev.waveform = hdr[4] <= 1 ? hdr[4] : 0;
      }
    }
  } else {
    if (hdr[0] <= 1 && hdr[1] <= 31) {
      dev.muted = (hdr[0] == 0);
      selectedPattern = hdr[1];
    } else if (hdr[0] <= 1) {
      dev.muted = (hdr[0] == 0);
      selectedPattern = hdr[2] <= 31 ? hdr[2] : 0;
    } else {
      dev.muted = false;
      selectedPattern = hdr[1] <= 31 ? hdr[1] : 0;
    }
  }

  if (selectedPattern >= 32) {
    m_error = "v1 device selected-pattern value exceeds the 32 pattern slots (device=" +
              std::to_string(static_cast<int>(targetId)) +
              ", pattern=" + std::to_string(selectedPattern) + ")";
    return false;
  }
  dev.initialPatternBank = static_cast<uint8_t>(selectedPattern / 8);
  dev.initialPatternIndex = static_cast<uint8_t>(selectedPattern % 8);

  const uint8_t* slotData = hdr + headerSize;
  for (int slot = 0; slot < 32; ++slot) {
    const uint8_t* slotPtr = slotData + static_cast<size_t>(slot) * slotSize;
    const uint8_t length =
        lengthAtZero ? slotPtr[0] : slotPtr[1];
    const uint8_t* stepData = lengthAtZero ? slotPtr + 1 : slotPtr + 2;

    Pattern pattern;
    pattern.deviceId = targetId;
    pattern.bank = static_cast<uint8_t>(slot / MAX_PATTERNS_PER_BANK);
    pattern.patternIndex = static_cast<uint8_t>(slot % MAX_PATTERNS_PER_BANK);
    pattern.length = std::min<uint8_t>(length, MAX_STEPS);

    if (is303) {
      pattern.steps = decode303Pattern(stepData, pattern.length);
    } else {
      pattern.steps = decodeDrumPattern(stepData, pattern.length);
    }

    song.patterns.push_back(pattern);
  }

  return true;
}

// TRAK / STRAK decoding and arrangement projection live in RbsTrak.cpp.

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
