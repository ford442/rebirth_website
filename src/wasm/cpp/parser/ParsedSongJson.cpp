#include "ParsedSongJson.h"

#include <iomanip>
#include <sstream>

namespace rb338 {

namespace {

void appendEscapedJsonString(std::ostringstream& out, const std::string& value) {
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':  out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec << std::setfill('0');
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  out << '"';
}

void appendStep(std::ostringstream& out, const StepData& step) {
  out << '{'
      << "\"active\":" << (step.active ? "true" : "false") << ','
      << "\"note\":" << static_cast<int>(step.note) << ','
      << "\"drumExtra\":" << static_cast<int>(step.drumExtra) << ','
      << "\"accent\":" << (step.accent ? "true" : "false") << ','
      << "\"slide\":" << (step.slide ? "true" : "false")
      << '}';
}

void appendDevice(std::ostringstream& out, const DeviceState& dev) {
  out << '{'
      << "\"id\":" << static_cast<int>(dev.id) << ','
      << "\"tune\":" << dev.tune << ','
      << "\"cutoff\":" << dev.cutoff << ','
      << "\"resonance\":" << dev.resonance << ','
      << "\"envMod\":" << dev.envMod << ','
      << "\"decay\":" << dev.decay << ','
      << "\"accent\":" << dev.accent << ','
      << "\"waveform\":" << static_cast<int>(dev.waveform) << ','
      << "\"initialPatternBank\":" << static_cast<int>(dev.initialPatternBank) << ','
      << "\"initialPatternIndex\":" << static_cast<int>(dev.initialPatternIndex) << ','
      << "\"muted\":" << (dev.muted ? "true" : "false") << ','
      << "\"level\":" << dev.level << ','
      << "\"pan\":" << dev.pan << ','
      << "\"dist\":" << (dev.dist ? "true" : "false") << ','
      << "\"pcf\":" << (dev.pcf ? "true" : "false") << ','
      << "\"compressor\":" << (dev.compressor ? "true" : "false") << ','
      << "\"delaySend\":" << dev.delaySend
      << '}';
}

void appendPattern(std::ostringstream& out, const Pattern& pattern) {
  out << '{'
      << "\"deviceId\":" << static_cast<int>(pattern.deviceId) << ','
      << "\"bank\":" << static_cast<int>(pattern.bank) << ','
      << "\"patternIndex\":" << static_cast<int>(pattern.patternIndex) << ','
      << "\"length\":" << static_cast<int>(pattern.length) << ','
      << "\"steps\":[";
  for (int i = 0; i < MAX_STEPS; ++i) {
    if (i > 0) out << ',';
    appendStep(out, pattern.steps[i]);
  }
  out << "]}";
}

void appendArrangementBar(std::ostringstream& out, const ArrangementBar& bar) {
  out << '{'
      << "\"barNumber\":" << bar.barNumber << ','
      << "\"devicePatterns\":[";
  for (int i = 0; i < NUM_DEVICES; ++i) {
    if (i > 0) out << ',';
    const PatternRef& ref = bar.devicePatterns[i];
    out << '{'
        << "\"bank\":" << static_cast<int>(ref.bank) << ','
        << "\"index\":" << static_cast<int>(ref.index)
        << '}';
  }
  out << "]}";
}

} // anonymous namespace

std::string parsedSongToJson(const ParsedSong& song) {
  std::ostringstream out;
  out << std::setprecision(9);

  out << '{'
      << "\"title\":";
  appendEscapedJsonString(out, song.title);
  out << ",\"author\":";
  appendEscapedJsonString(out, song.author);
  out << ",\"infoText\":";
  appendEscapedJsonString(out, song.infoText);
  out << ",\"creatorUrl\":";
  appendEscapedJsonString(out, song.creatorUrl);
  out << ",\"bpm\":" << song.bpm
      << ",\"version\":" << static_cast<int>(song.version)
      << ",\"headVersion\":" << static_cast<int>(song.headVersion)
      << ",\"globSubFormat\":" << static_cast<int>(song.globSubFormat)
      << ",\"showInfoOnOpen\":" << (song.showInfoOnOpen ? "true" : "false")
      << ",\"devices\":[";

  for (int i = 0; i < NUM_DEVICES; ++i) {
    if (i > 0) out << ',';
    appendDevice(out, song.devices[i]);
  }

  out << "],\"patterns\":[";
  for (size_t i = 0; i < song.patterns.size(); ++i) {
    if (i > 0) out << ',';
    appendPattern(out, song.patterns[i]);
  }

  out << "],\"arrangement\":[";
  for (size_t i = 0; i < song.arrangement.size(); ++i) {
    if (i > 0) out << ',';
    appendArrangementBar(out, song.arrangement[i]);
  }

  out << "]}";
  return out.str();
}

} // namespace rb338
