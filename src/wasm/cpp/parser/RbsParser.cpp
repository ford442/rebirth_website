#include "RbsParser.h"
#include "DrumKit.h"
#include "RbsByteStream.h"
#include "RbsDrumRow.h"
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

// ── GLOB fixed-width strings ─────────────────────────────────────────
constexpr size_t GLOB_TITLE_OFFSET = 0x0f;
constexpr size_t GLOB_TITLE_WIDTH = 65;
constexpr size_t GLOB_FTP_URL_OFFSET = 0x50;
constexpr size_t GLOB_WEB_URL_OFFSET = 0x119;
constexpr size_t GLOB_URL_WIDTH = 201;
constexpr size_t GLOB_VINTAGE_OFFSET = 0x1e2;

// ── USRI fixed-width fields (712-byte chunk in every archive file) ───
constexpr size_t USRI_AUTHOR_WIDTH = 41;
constexpr size_t USRI_INFO_OFFSET = 41;
constexpr size_t USRI_INFO_WIDTH = 401;
constexpr size_t USRI_SHOW_INFO_OFFSET = 0x21f;

/**
 * The null-terminated string in the `width`-byte field at `offset`, never
 * reading past the field. The last byte is the terminator slot — the writer
 * stores at most width - 1 characters — so it is never part of the string.
 */
std::string fixedCString(const uint8_t* data, size_t size, size_t offset, size_t width) {
  if (offset >= size) return std::string();
  const size_t limit = std::min(width - 1, size - offset);
  const auto* begin = reinterpret_cast<const char*>(data + offset);
  size_t len = 0;
  while (len < limit && begin[len] != '\0') ++len;
  return std::string(begin, len);
}

// ── Knob decoding ────────────────────────────────────────────────────
/**
 * A 0–127 knob byte as 0–1. Bytes past 127 are clamped: every knob in the
 * format is a MIDI-range value, the writer can only store 0–127, and an
 * unclamped 255 became a 2.0 level that changed on save (found by
 * fuzz_rbs_roundtrip).
 */
float knobFromByte(uint8_t byte) {
  return static_cast<float>(byte > 127 ? 127 : byte) / 127.0f;
}

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

/**
 * One 808 / 909 pattern. Rows are accent first (RbsDrumRow.h): byte 0 is the
 * AC (total accent) row, bytes 1–11 one instrument each. The 808 stores 0/1;
 * the 909 stores 1 = hit, 2 = accented hit, 3 = flam.
 */
std::array<StepData, MAX_STEPS> decodeDrumPattern(const uint8_t* data, uint8_t length,
                                                   bool is909) {
  using namespace detail;
  const auto& rowMap = is909 ? kDrumRow909 : kDrumRow808;
  std::array<StepData, MAX_STEPS> steps{};
  uint8_t numSteps = std::min<uint8_t>(length, MAX_STEPS);
  for (uint8_t s = 0; s < numSteps; ++s) {
    const uint8_t* row = data + s * DRUM_ROW_BYTES;
    uint32_t hits = 0;
    uint16_t strong = 0;
    uint16_t flam = 0;
    for (size_t hit = 0; hit < DRUM_ROW_HITS; ++hit) {
      const uint8_t value = row[rowMap[hit]];
      if (value == DRUM_ROW_OFF) continue;
      hits |= hitBit(hit);
      if (!is909) continue;
      if (value == DRUM_ROW_ACCENTED) strong |= static_cast<uint16_t>(hitBit(hit));
      if (value == DRUM_ROW_FLAM) flam |= static_cast<uint16_t>(hitBit(hit));
    }
    StepData& step = steps[s];
    step.note = static_cast<uint8_t>(hits & 0xffu);
    step.drumExtra = static_cast<uint8_t>(hits >> 8);
    step.active = hits != 0;
    // An AC-only step (no instrument) is kept as an inactive accent: it
    // sounds like nothing, and a save writes the accent back.
    step.accent = row[DRUM_ROW_ACCENT] != 0;
    step.drumAccent = strong;
    step.drumFlam = flam;
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
  m_catDepth = 0;
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
  // Judged on the trimmed title: the v1 container keeps padding, the v2 GLOB
  // reader trims it, so a whitespace-only v1 title used to be accepted here
  // and then rejected once saved as v2 (found by fuzz_rbs_roundtrip).
  if (trim(song.title).empty()) {
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

  if (m_catDepth >= MAX_CAT_DEPTH) {
    m_error = "Nested CAT containers exceed the supported depth";
    return false;
  }

  // The marker identifies the list; chunks follow immediately after it.
  ++m_catDepth;
  const bool ok = parseContainer(stream.data() + stream.pos(),
                                 stream.remaining(), song, false);
  --m_catDepth;
  return ok;
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
  // Bytes 0x00/0x01 are the play mode and the loop switch (RBS 4.2). Byte
  // 0x01 used to be read as "show info on open", which lives in USRI.
  song.playMode = data[0x00] == 0 ? PlayMode::Pattern : PlayMode::Song;
  song.loopEnabled = data[0x01] != 0;

  // Propellerhead's RBS 4.2 specification stores tempo as a big-endian
  // unsigned integer at offset 2, scaled by 1000.
  ByteStream tempoStream(data + 2, size - 2);
  uint32_t tempoTimes1000 = 0;
  if (!tempoStream.readU32BE(tempoTimes1000)) {
    m_error = "Truncated GLOB tempo field";
    return false;
  }
  const float bpm = static_cast<float>(tempoTimes1000) / 1000.0f;
  if (bpm < MIN_BPM || bpm > MAX_BPM) {
    m_error = "GLOB tempo is outside ReBirth's 20-500 BPM range";
    return false;
  }
  song.bpm = bpm;

  ByteStream loopStream(data + 0x06, size - 0x06);
  if (!loopStream.readU32BE(song.loopStart) || !loopStream.readU32BE(song.loopEnd)) {
    m_error = "Truncated GLOB loop locators";
    return false;
  }
  song.shuffle = data[0x0e];

  // Title is a null-terminated string in the 65-byte field at 0x0f. It is
  // read only within that field: an unterminated title used to run on into
  // the FTP URL field, which the writer (correctly) truncates on save — so a
  // title could change, or vanish, in a round trip (found by
  // fuzz_rbs_roundtrip).
  if (size <= GLOB_TITLE_OFFSET) {
    m_error = "GLOB chunk too small for title";
    return false;
  }
  const std::string title = fixedCString(data, size, GLOB_TITLE_OFFSET, GLOB_TITLE_WIDTH);
  if (title.empty()) {
    m_error = "Failed to read song title from GLOB";
    return false;
  }
  song.title = trim(title);

  // Creator URL fields are fixed offsets within GLOB; read if present, and
  // only within the 201-byte field (see the title above).
  if (size > GLOB_FTP_URL_OFFSET) {
    song.modFtpUrl = trim(fixedCString(data, size, GLOB_FTP_URL_OFFSET, GLOB_URL_WIDTH));
  }
  if (size > GLOB_WEB_URL_OFFSET) {
    const std::string url = fixedCString(data, size, GLOB_WEB_URL_OFFSET, GLOB_URL_WIDTH);
    if (!url.empty()) song.creatorUrl = trim(url);
  }
  // Bytes past the documented fields hold uninitialised memory in many
  // files, so only the vintage byte itself is read.
  song.vintage = size > GLOB_VINTAGE_OFFSET && data[GLOB_VINTAGE_OFFSET] != 0;

  return true;
}

bool RbsParser::parseUsri(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 2) {
    m_error = "USRI chunk too small";
    return false;
  }

  // Fixed fields: author at 0 (41 bytes), info text at 41 (401 bytes), a
  // web page at 442 (101 bytes, not decoded) and the show-on-open flag at
  // 0x21f. Each string is read only within its field, as the writer bounds
  // it. A short chunk (an older save) keeps the sequential layout: author,
  // padding zeros, info text.
  song.author = trim(latin1ToUtf8(fixedCString(data, size, 0, USRI_AUTHOR_WIDTH)));

  std::string info;
  if (size >= USRI_INFO_OFFSET + USRI_INFO_WIDTH) {
    info = fixedCString(data, size, USRI_INFO_OFFSET, USRI_INFO_WIDTH);
  } else {
    // Past the author's terminator, then past any padding.
    size_t pos = 0;
    while (pos < size && data[pos] != 0) ++pos;
    while (pos < size && data[pos] == 0) ++pos;
    info = fixedCString(data, size, pos, USRI_INFO_WIDTH);
  }
  song.infoText = trim(latin1ToUtf8(info));
  song.showInfoOnOpen = size > USRI_SHOW_INFO_OFFSET && data[USRI_SHOW_INFO_OFFSET] != 0;

  return true;
}

bool RbsParser::parseMixr(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 64) {
    m_error = "MIXR chunk too small";
    return false;
  }

  song.fx.masterLevel = data[0];
  // The compressor and the PCF are single inserts: bytes 1 and 2 name the
  // device each one sits on (0 none, 1 master, 2–5 the four devices).
  song.fx.comp.target = data[1];
  song.fx.pcf.target = data[2];

  // Per-device records begin at offset 0x10, 12 bytes each.
  for (int i = 0; i < NUM_DEVICES; ++i) {
    size_t off = 0x10 + i * 12;
    if (off + 12 > size) continue;
    DeviceState& dev = song.devices[i];
    dev.id = static_cast<DeviceId>(i);
    dev.muted = (data[off] == 0);
    dev.level = knobFromByte(data[off + 1]);
    dev.pan = knobFromByte(data[off + 2]);
    dev.delaySend = knobFromByte(data[off + 3]);
    dev.dist = (data[off + 4] != 0);
    // Bytes 5–11 are reserved. 5 and 6 used to be read as PCF / compressor
    // sends, but they are zero in all but 2 of 772 records in the corpus;
    // the inserts are chosen by bytes 1 and 2 above.
  }

  return true;
}

bool RbsParser::parseDely(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 5) {
    m_error = "DELY chunk too small";
    return false;
  }
  // enabled, steps (1–32), step unit (0 = triplet, 1 = straight), feedback,
  // pan. Bytes 1 and 4 used to be read as a subdivision index and a wet
  // level.
  auto& fx = song.fx.delay;
  fx.enabled = (data[0] != 0);
  fx.steps = data[1];
  fx.triplet = (data[2] == 0);
  fx.feedback = data[3];
  fx.pan = data[4];
  return true;
}

bool RbsParser::parsePcf(const uint8_t* data, size_t size, ParsedSong& song) {
  if (size < 7) {
    m_error = "PCF  chunk too small";
    return false;
  }
  // enabled, frequency, Q, amount, trigger pattern (0–54), decay, filter
  // type (0 = band-pass, 1 = low-pass).
  auto& fx = song.fx.pcf;
  fx.enabled = (data[0] != 0);
  fx.cutoff = data[1];
  fx.resonance = data[2];
  fx.envAmount = data[3];
  fx.pattern = data[4];
  fx.decay = data[5];
  fx.lowpass = (data[6] != 0);
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
  if (size < 4) {
    m_error = "COMP chunk too small";
    return false;
  }
  auto& fx = song.fx.comp;
  fx.enabled = (data[0] != 0);
  fx.ratio = data[1];
  fx.threshold = data[2];
  fx.attack = data[3];
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
  const bool is909 = targetId == DeviceId::TR909;
  const size_t headerSize =
      is303 ? 9 : (is909 ? Kit909::kHeaderBytes : Kit808::kHeaderBytes);
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
    dev.tune = knobFromByte(data[2]);
    dev.cutoff = knobFromByte(data[3]);
    dev.resonance = knobFromByte(data[4]);
    dev.envMod = knobFromByte(data[5]);
    dev.decay = knobFromByte(data[6]);
    dev.accent = knobFromByte(data[7]);
    dev.waveform = data[8];
  } else {
    // Drum machines: byte 2 is the ACCENT knob, the rest the per-instrument
    // kit (DrumKit.h). Bytes are kept raw so a save writes them back as is.
    dev.accent = knobFromByte(data[KIT_ACCENT_CONTROLLER]);
    dev.kit = defaultDrumKit(targetId);
    const size_t kitBytes = kitBytesFor(targetId);
    for (size_t i = 0; i < kitBytes; ++i) {
      dev.kit[i] = data[KIT_FIRST_CONTROLLER + i];
    }
    dev.hasKit = true;
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
    pattern.shuffle = slotPtr[0] != 0;

    if (is303) {
      pattern.steps = decode303Pattern(slotPtr + 2, pattern.length);
    } else {
      pattern.steps = decodeDrumPattern(slotPtr + 2, pattern.length, is909);
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
      dev.tune = knobFromByte(hdr[1]);
      selectedPattern = hdr[2];
      dev.cutoff = knobFromByte(hdr[3]);
      if (headerSize >= 5) {
        dev.resonance = knobFromByte(hdr[4]);
      }
    } else {
      dev.muted = false;
      dev.tune = knobFromByte(hdr[0]);
      selectedPattern = hdr[1];
      dev.cutoff = knobFromByte(hdr[2]);
      if (headerSize >= 5) {
        dev.resonance = knobFromByte(hdr[3]);
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
      pattern.steps = decodeDrumPattern(stepData, pattern.length,
                                        targetId == DeviceId::TR909);
    }

    song.patterns.push_back(pattern);
  }

  return true;
}

// TRAK / STRAK decoding and arrangement projection live in RbsTrak.cpp.

} // namespace rb338
