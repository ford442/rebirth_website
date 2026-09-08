// RbsMidiContainer — v1 / v1.5 Propellerhead MIDI-container (.rbs) support.
// Song data lives in a single SysEx message; DEVL-equivalent body starts at offset 309.

#include "RbsParser.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace rb338 {

namespace {

constexpr size_t kV1DevlOffset = 309;
constexpr size_t kMixrSize = 64;
constexpr size_t kDelySize = 8;
constexpr size_t kPcfSize = 12;
constexpr size_t kDistSize = 8;
constexpr size_t kCompSize = 8;
constexpr size_t kV1FxEnd = kMixrSize + kDelySize + kPcfSize + kDistSize + kCompSize;

constexpr size_t kV1303BlockSize = 4681;
constexpr size_t kV1303LabelSkip = 4;
constexpr size_t kV1303HeaderSize = 5;
constexpr size_t kV1303SlotSize = 146;

constexpr size_t kV1808BlockSize = 6238;
constexpr size_t kV1808LabelSkip = 5;
constexpr size_t kV1808HeaderSize = 25;
constexpr size_t kV1808SlotSize = 194;

constexpr size_t kV1909BlockSize = 6239;
constexpr size_t kV1909LabelSkip = 5;
constexpr size_t kV1909HeaderSize = 31;
constexpr size_t kV1909SlotSize = 194;

bool matchId(const uint8_t* id, const char* expected) {
  return std::memcmp(id, expected, 4) == 0;
}

bool matchPrefix(const uint8_t* data, const char* expected, size_t len) {
  return std::memcmp(data, expected, len) == 0;
}

bool containsPropellerheadMeta(const uint8_t* data, size_t size) {
  if (size < 64 || !matchId(data, "MThd")) return false;
  const char* needle = "Propellerhead Software";
  const char* end = reinterpret_cast<const char*>(data + std::min(size, size_t(512)));
  const char* start = reinterpret_cast<const char*>(data);
  return std::search(start, end, needle, needle + std::strlen(needle)) != end;
}

bool extractSysExPayload(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
  out.clear();
  size_t pos = 0;
  while (pos < size) {
    const size_t start = pos;
    if (data[pos++] != 0xF0) continue;
    while (pos < size && data[pos] != 0xF7) ++pos;
    if (pos >= size) return false;
    const size_t end = pos++;

    const uint8_t* body = data + start + 1;
    const size_t bodyLen = end - start - 1;
    if (bodyLen < 4) continue;

    size_t idx = 0;
    if (body[idx] == 0x81) {
      ++idx;
      while (idx < bodyLen && (body[idx] & 0x80)) ++idx;
      if (idx < bodyLen && body[idx] == 0x30) ++idx;
    }

    if (bodyLen > out.size()) {
      out.assign(body + idx, body + bodyLen);
    }
  }
  return !out.empty();
}

float readMidiTempoBpm(const uint8_t* data, size_t size) {
  for (size_t i = 0; i + 6 < size; ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x51 && data[i + 2] == 0x03) {
      const uint32_t usPerQuarter =
          (static_cast<uint32_t>(data[i + 3]) << 16) |
          (static_cast<uint32_t>(data[i + 4]) << 8) |
          static_cast<uint32_t>(data[i + 5]);
      if (usPerQuarter > 0) {
        return 60000000.0f / static_cast<float>(usPerQuarter);
      }
    }
  }
  return 125.0f;
}

std::string extractInfoTitle(const uint8_t* payload, size_t size) {
  std::string best;
  for (size_t i = kV1DevlOffset; i + 8 < size; ++i) {
    if (payload[i] == 0) continue;
    size_t j = i;
    while (j < size && payload[j] != 0 && j - i < 64) ++j;
    if (j <= i + 3) continue;
    bool printable = true;
    for (size_t k = i; k < j; ++k) {
      if (payload[k] < 32 || payload[k] >= 127) {
        printable = false;
        break;
      }
    }
    if (!printable) continue;
    const std::string candidate(reinterpret_cast<const char*>(payload + i), j - i);
    if (candidate == "Ti@nYd") continue;
    if (candidate.size() > best.size()) best = candidate;
  }
  return best.empty() ? "ReBirth Song" : best;
}

} // anonymous namespace

bool RbsParser::parseMidiContainer(const uint8_t* data, size_t size, ParsedSong& song) {
  if (!containsPropellerheadMeta(data, size)) {
    if (size >= 4 && matchId(data, "MThd")) {
      m_error = "Standard MIDI file, not a ReBirth song";
    } else {
      m_error = "Unrecognized ReBirth song container";
    }
    return false;
  }

  std::vector<uint8_t> payload;
  if (!extractSysExPayload(data, size, payload)) {
    m_error = "Missing Propellerhead SysEx song payload";
    return false;
  }
  if (payload.size() < kV1DevlOffset + kV1FxEnd + kV1303BlockSize) {
    m_error = "Propellerhead SysEx payload too small";
    return false;
  }

  const uint8_t subVersion = payload.size() > 9 ? payload[9] : 0;
  song.version = (subVersion == 1) ? RbsVersion::V1_0 : RbsVersion::V1_5;
  song.headVersion = subVersion;
  song.globSubFormat = subVersion;
  song.bpm = readMidiTempoBpm(data, size);
  song.title = extractInfoTitle(payload.data(), payload.size());
  song.author.clear();
  song.infoText.clear();
  song.creatorUrl.clear();

  const uint8_t* devl = payload.data() + kV1DevlOffset;
  const size_t devlSize = payload.size() - kV1DevlOffset;

  if (!parseMixr(devl, std::min(kMixrSize, devlSize), song)) return false;

  size_t off = kMixrSize;
  if (off + kDelySize <= devlSize) {
    if (!parseDely(devl + off, kDelySize, song)) return false;
    off += kDelySize;
  }
  if (off + kPcfSize <= devlSize) {
    if (!parsePcf(devl + off, kPcfSize, song)) return false;
    off += kPcfSize;
  }
  if (off + kDistSize <= devlSize) {
    if (!parseDist(devl + off, kDistSize, song)) return false;
    off += kDistSize;
  }
  if (off + kCompSize <= devlSize) {
    if (!parseComp(devl + off, kCompSize, song)) return false;
    off += kCompSize;
  }

  m_seenTb303A = false;

  struct V1DeviceSpec {
    const char* label;
    size_t labelLen;
    size_t blockSize;
    size_t labelSkip;
    size_t headerSize;
    size_t slotSize;
    DeviceId primary;
    DeviceId secondary;
    bool is303;
  };

  const std::array<V1DeviceSpec, 4> devicePlan{{
      {"303 ", 4, kV1303BlockSize, kV1303LabelSkip, kV1303HeaderSize, kV1303SlotSize,
       DeviceId::TB303_A, DeviceId::TB303_B, true},
      {"303 ", 4, kV1303BlockSize, kV1303LabelSkip, kV1303HeaderSize, kV1303SlotSize,
       DeviceId::TB303_A, DeviceId::TB303_B, true},
      {"TR0", 3, kV1808BlockSize, kV1808LabelSkip, kV1808HeaderSize, kV1808SlotSize,
       DeviceId::TR808, DeviceId::TR808, false},
      {"TR0", 3, kV1909BlockSize, kV1909LabelSkip, kV1909HeaderSize, kV1909SlotSize,
       DeviceId::TR909, DeviceId::TR909, false},
  }};

  for (const auto& spec : devicePlan) {
    if (off + spec.blockSize > devlSize) break;
    if (!matchPrefix(devl + off, spec.label, spec.labelLen)) break;
    if (spec.primary == DeviceId::TR909) {
      // v1.5 909 blocks use TR0\x009…; skip when this offset is still the 808 block.
      if (off + 4 < devlSize && devl[off + 4] == static_cast<uint8_t>('8')) {
        break;
      }
    }
    if (!parseV1DeviceChunk(spec.primary, spec.secondary, devl + off, spec.blockSize,
                            song, spec.labelSkip, spec.slotSize, spec.headerSize,
                            spec.is303, spec.is303)) {
      return false;
    }
    off += spec.blockSize;
  }

  for (int i = 0; i < NUM_DEVICES; ++i) {
    song.devices[i].id = static_cast<DeviceId>(i);
  }

  return true;
}

} // namespace rb338
