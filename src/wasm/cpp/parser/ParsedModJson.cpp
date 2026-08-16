#include "ParsedModJson.h"
#include <sstream>

namespace rb338 {

namespace {

void appendEscaped(std::ostringstream& out, const std::string& s) {
  out << '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u00";
          const char* hex = "0123456789abcdef";
          out << hex[c >> 4] << hex[c & 0xf];
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  out << '"';
}

const char* kindName(ModResourceKind kind) {
  switch (kind) {
    case ModResourceKind::Sample: return "sample";
    case ModResourceKind::Skin: return "skin";
    case ModResourceKind::Song: return "song";
    case ModResourceKind::Other: return "other";
  }
  return "other";
}

} // namespace

std::string parsedModToJson(const ParsedMod& mod) {
  std::ostringstream out;
  out << "{\"title\":";
  appendEscaped(out, mod.title);
  out << ",\"description\":";
  appendEscaped(out, mod.description);
  out << ",\"copyright\":";
  appendEscaped(out, mod.copyright);
  out << ",\"resources\":[";
  for (size_t i = 0; i < mod.resources.size(); ++i) {
    if (i) out << ',';
    const auto& r = mod.resources[i];
    out << "{\"name\":";
    appendEscaped(out, r.name);
    out << ",\"kind\":\"" << kindName(r.kind) << "\"";
    out << ",\"slot\":\"" << modSampleSlotName(r.slot) << "\"";
    out << ",\"bytes\":" << r.bytes.size() << '}';
  }
  out << "]}";
  return out.str();
}

} // namespace rb338
