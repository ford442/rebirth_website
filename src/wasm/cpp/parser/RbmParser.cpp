#include "RbmParser.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <span>

namespace rb338 {

namespace {

constexpr char kCatMagic[4] = {'C', 'A', 'T', ' '};
constexpr char kPrbmMagic[4] = {'P', 'R', 'B', 'M'};

bool matchId(const uint8_t* id, const char* expect) {
  return std::memcmp(id, expect, 4) == 0;
}

uint32_t readU32BE(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

std::string asciiLower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string stemLower(const std::string& name) {
  std::string lower = asciiLower(name);
  const auto slash = lower.find_last_of("/\\");
  if (slash != std::string::npos) {
    lower = lower.substr(slash + 1);
  }
  const auto dot = lower.find_last_of('.');
  if (dot != std::string::npos) {
    lower = lower.substr(0, dot);
  }
  return lower;
}

std::string extensionLower(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return {};
  return asciiLower(name.substr(dot + 1));
}

bool containsToken(const std::string& stem, const char* token) {
  return stem.find(token) != std::string::npos;
}

} // namespace

ModResourceKind classifyResourceName(const std::string& name) {
  const std::string ext = extensionLower(name);
  if (ext == "aif" || ext == "aiff" || ext == "wav") return ModResourceKind::Sample;
  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "pict" ||
      ext == "pct") {
    return ModResourceKind::Skin;
  }
  if (ext == "rbs") return ModResourceKind::Song;
  return ModResourceKind::Other;
}

ModSampleSlot classifySampleSlot(const std::string& name) {
  if (classifyResourceName(name) != ModResourceKind::Sample) {
    return ModSampleSlot::Unknown;
  }
  const std::string stem = stemLower(name);
  const bool is909 = containsToken(stem, "909");
  const bool is303 = containsToken(stem, "303") || containsToken(stem, "saw") ||
                     containsToken(stem, "sqr") || containsToken(stem, "square");

  if (is303 && (containsToken(stem, "saw"))) return ModSampleSlot::Tb303Saw;
  if (is303 && (containsToken(stem, "sqr") || containsToken(stem, "square"))) {
    return ModSampleSlot::Tb303Square;
  }

  auto slot808 = [&](ModSampleSlot s808, ModSampleSlot s909) {
    return is909 ? s909 : s808;
  };

  if (containsToken(stem, "bd")) return slot808(ModSampleSlot::Tr808Kick, ModSampleSlot::Tr909Kick);
  if (containsToken(stem, "sd")) return slot808(ModSampleSlot::Tr808Snare, ModSampleSlot::Tr909Snare);
  if (containsToken(stem, "lt")) return slot808(ModSampleSlot::Tr808LowTom, ModSampleSlot::Tr909LowTom);
  if (containsToken(stem, "mt")) return slot808(ModSampleSlot::Tr808MidTom, ModSampleSlot::Tr909MidTom);
  if (containsToken(stem, "ht")) return slot808(ModSampleSlot::Tr808HighTom, ModSampleSlot::Tr909HighTom);
  if (containsToken(stem, "oh")) return slot808(ModSampleSlot::Tr808OpenHat, ModSampleSlot::Tr909OpenHat);
  if (containsToken(stem, "ch") || containsToken(stem, "hh")) {
    return slot808(ModSampleSlot::Tr808ClosedHat, ModSampleSlot::Tr909ClosedHat);
  }
  if (containsToken(stem, "rs")) {
    return slot808(ModSampleSlot::Tr808Rimshot, ModSampleSlot::Tr909Rimshot);
  }
  if (containsToken(stem, "cp")) return slot808(ModSampleSlot::Tr808Clap, ModSampleSlot::Tr909Clap);
  if (containsToken(stem, "ccy") || containsToken(stem, "cc") || containsToken(stem, "crash")) {
    return ModSampleSlot::Tr909Crash;
  }
  if (containsToken(stem, "rc") || containsToken(stem, "ride")) return ModSampleSlot::Tr909Ride;
  if (containsToken(stem, "cy")) return ModSampleSlot::Tr808Cymbal;
  if (containsToken(stem, "cb") || (containsToken(stem, "cl") && !containsToken(stem, "clap"))) {
    return ModSampleSlot::Tr808Clave;
  }
  if (containsToken(stem, "ma")) return ModSampleSlot::Tr808Maracas;
  return ModSampleSlot::Unknown;
}

bool RbmParser::parseHead(const uint8_t* data, size_t size, ParsedMod& mod) {
  if (size < 4) {
    m_error = "HEAD chunk too small";
    return false;
  }
  // Copyright C-string starts at offset 9 in every studied file.
  if (size > 9) {
    const size_t maxLen = size - 9;
    const uint8_t* start = data + 9;
    size_t n = 0;
    while (n < maxLen && start[n] != 0) ++n;
    if (n > 0) {
      mod.copyright.assign(reinterpret_cast<const char*>(start), n);
    }
  }
  return true;
}

bool RbmParser::parseEmbf(const uint8_t* data, size_t size, ParsedMod& mod) {
  if (size < 2) {
    m_error = "EMBF chunk too small";
    return false;
  }
  size_t n = 0;
  while (n < size && data[n] != 0) ++n;
  if (n == 0 || n >= size) {
    m_error = "EMBF missing filename";
    return false;
  }
  EmbeddedResource res;
  res.name.assign(reinterpret_cast<const char*>(data), n);
  res.kind = classifyResourceName(res.name);
  res.slot = classifySampleSlot(res.name);
  const size_t payloadOff = n + 1;
  if (payloadOff < size) {
    res.bytes.assign(data + payloadOff, data + size);
  }
  mod.resources.push_back(std::move(res));
  return true;
}

bool RbmParser::parseInfo(const uint8_t* data, size_t size, ParsedMod& mod) {
  std::string longest;
  std::string first;
  size_t i = 0;
  while (i < size) {
    while (i < size && data[i] == 0) ++i;
    if (i >= size) break;
    const size_t start = i;
    while (i < size && data[i] != 0) ++i;
    const size_t len = i - start;
    if (len < 3) continue;
    bool printable = true;
    for (size_t k = start; k < i; ++k) {
      const uint8_t c = data[k];
      if (c < 9 || (c > 13 && c < 32)) {
        printable = false;
        break;
      }
    }
    if (!printable) continue;
    std::string s(reinterpret_cast<const char*>(data + start), len);
    if (first.empty()) first = s;
    if (s.size() > longest.size()) longest = s;
  }
  if (longest.size() >= 20) {
    mod.description = longest;
    if (first.size() < 20) mod.title = first;
    if (mod.title.empty()) {
      const auto nl = longest.find('\r');
      mod.title = longest.substr(0, std::min(nl, size_t{48}));
    }
  } else if (!first.empty()) {
    mod.title = first;
  }
  return true;
}

bool RbmParser::parseList(const uint8_t* data, size_t size, ParsedMod& mod) {
  size_t offset = 0;
  while (offset + 8 <= size) {
    const uint8_t* id = data + offset;
    const uint32_t chunkSize = readU32BE(data + offset + 4);
    offset += 8;
    if (chunkSize > size - offset) {
      m_error = "Chunk size exceeds container bounds";
      return false;
    }
    const uint8_t* body = data + offset;
    if (matchId(id, "HEAD")) {
      if (!parseHead(body, chunkSize, mod)) return false;
    } else if (matchId(id, "EMBF")) {
      if (!parseEmbf(body, chunkSize, mod)) return false;
    } else if (matchId(id, "INFO")) {
      if (!parseInfo(body, chunkSize, mod)) return false;
    }
    offset += chunkSize;
    if ((chunkSize & 1u) && offset < size) ++offset;
  }
  return true;
}

std::optional<ParsedMod> RbmParser::parse(const uint8_t* data, size_t size) {
  if (!data && size != 0) {
    return std::nullopt;
  }
  return parse(std::span<const uint8_t>(data, size));
}

std::optional<ParsedMod> RbmParser::parse(std::span<const uint8_t> buffer) {
  m_error.clear();
  if (buffer.size() < 12) {
    m_error = "Buffer too small for PRBM container";
    return std::nullopt;
  }
  const uint8_t* data = buffer.data();
  const size_t size = buffer.size();
  if (!matchId(data, kCatMagic)) {
    m_error = "Missing root 'CAT ' container magic";
    return std::nullopt;
  }
  const uint32_t bodySize = readU32BE(data + 4);
  if (bodySize < 4 || bodySize > size - 8) {
    m_error = "Root container size exceeds file size";
    return std::nullopt;
  }
  if (!matchId(data + 8, kPrbmMagic)) {
    m_error = "Missing 'PRBM' format marker after root container";
    return std::nullopt;
  }

  ParsedMod mod;
  if (!parseList(data + 12, bodySize - 4, mod)) {
    return std::nullopt;
  }
  if (mod.resources.empty()) {
    m_error = "PRBM contains no EMBF resources";
    return std::nullopt;
  }
  return mod;
}

} // namespace rb338
