/**
 * rbs-inspect — CLI dump tool for ReBirth RB-338 .rbs files
 *
 * Build (CMake):
 *   cmake -S src/wasm/cpp -B src/wasm/cpp/build
 *   cmake --build src/wasm/cpp/build --target rbs-inspect
 *
 * Usage:
 *   ./src/wasm/cpp/build/rbs-inspect path/to/song.rbs          # JSON (default)
 *   ./src/wasm/cpp/build/rbs-inspect --pretty path/to/song.rbs # human-readable
 */

#include "../parser/ParsedSongJson.h"
#include "../parser/RbsParser.h"
#include "../parser/RbsTypes.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace rb338;

namespace {

const char* deviceName(DeviceId id) {
  switch (id) {
    case DeviceId::TB303_A: return "TB-303 A";
    case DeviceId::TB303_B: return "TB-303 B";
    case DeviceId::TR808:   return "TR-808";
    case DeviceId::TR909:   return "TR-909";
  }
  return "?";
}

const char* bankLetter(uint8_t bank) {
  switch (bank) {
    case 0: return "A";
    case 1: return "B";
    case 2: return "C";
    case 3: return "D";
  }
  return "?";
}

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open: " + path);
  }
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

void printStep303(const StepData& step) {
  if (!step.active) {
    std::cout << "  ---  ";
    return;
  }
  std::cout << std::setw(3) << static_cast<int>(step.note);
  std::cout << (step.accent ? "A" : " ") << (step.slide ? "S" : " ") << " ";
}

void printStepDrum(const StepData& step) {
  if (!step.active) {
    std::cout << " . ";
    return;
  }
  const char labels[] = {'B', 'S', 'L', 'M', 'H', 'C', 'O', 'R'};
  for (int i = 0; i < 8; ++i) {
    std::cout << ((step.note & (1 << i)) ? labels[i] : '.');
  }
  std::cout << " ";
}

void printPretty(const ParsedSong& song) {
  std::cout << "═══════════════════════════════════════════════════════════════\n";
  std::cout << "  " << song.title << "\n";
  std::cout << "═══════════════════════════════════════════════════════════════\n";
  std::cout << "Author:       " << song.author << "\n";
  std::cout << "Info text:    " << (song.infoText.empty() ? "(none)" : song.infoText) << "\n";
  std::cout << "Creator URL:  " << (song.creatorUrl.empty() ? "(none)" : song.creatorUrl) << "\n";
  std::cout << "BPM:          " << song.bpm << "\n";
  std::cout << "HEAD marker:  0x" << std::hex << static_cast<int>(song.headVersion) << std::dec << "\n";
  std::cout << "GLOB marker:  0x" << std::hex << static_cast<int>(song.globSubFormat) << std::dec << "\n";
  std::cout << "Show info:    " << (song.showInfoOnOpen ? "yes" : "no") << "\n";

  std::cout << "\n── Devices ───────────────────────────────────────────────────\n";
  for (const auto& dev : song.devices) {
    std::cout << deviceName(dev.id) << "\n";
    std::cout << "  tune=" << static_cast<int>(dev.tune * 127)
              << " cutoff=" << static_cast<int>(dev.cutoff * 127)
              << " res=" << static_cast<int>(dev.resonance * 127)
              << " env=" << static_cast<int>(dev.envMod * 127)
              << " decay=" << static_cast<int>(dev.decay * 127)
              << " accent=" << static_cast<int>(dev.accent * 127);
    if (dev.id == DeviceId::TB303_A || dev.id == DeviceId::TB303_B) {
      std::cout << " wave=" << static_cast<int>(dev.waveform);
    }
    std::cout << "\n  pattern=" << bankLetter(dev.initialPatternBank)
              << (dev.initialPatternIndex + 1)
              << " mute=" << (dev.muted ? "yes" : "no")
              << " level=" << static_cast<int>(dev.level * 127)
              << " pan=" << static_cast<int>(dev.pan * 127)
              << " dist=" << (dev.dist ? "yes" : "no")
              << " pcf=" << (dev.pcf ? "yes" : "no")
              << " comp=" << (dev.compressor ? "yes" : "no")
              << "\n";
  }

  std::cout << "\n── Patterns ──────────────────────────────────────────────────\n";
  for (const auto& dev : song.devices) {
    size_t active = 0;
    size_t total = 0;
    for (const auto& p : song.patterns) {
      if (p.deviceId != dev.id) continue;
      ++total;
      for (const auto& s : p.steps) {
        if (s.active) { ++active; break; }
      }
    }
    std::cout << deviceName(dev.id) << ": " << active << "/" << total << " slots used\n";
  }

  std::cout << "\n── First active pattern per device ───────────────────────────\n";
  for (const auto& dev : song.devices) {
    const Pattern* first = nullptr;
    for (const auto& p : song.patterns) {
      if (p.deviceId != dev.id) continue;
      bool used = false;
      for (const auto& s : p.steps) {
        if (s.active) { used = true; break; }
      }
      if (used) { first = &p; break; }
    }
    if (!first) continue;

    std::cout << deviceName(dev.id) << " bank " << bankLetter(first->bank)
              << " pattern " << (first->patternIndex + 1)
              << " (length " << static_cast<int>(first->length) << ")\n  ";
    for (int i = 0; i < first->length; ++i) {
      if (dev.id == DeviceId::TB303_A || dev.id == DeviceId::TB303_B) {
        printStep303(first->steps[i]);
      } else {
        printStepDrum(first->steps[i]);
      }
    }
    std::cout << "\n";
  }

  std::cout << "\n── Arrangement ───────────────────────────────────────────────\n";
  if (song.arrangement.empty()) {
    std::cout << "(not yet parsed)\n";
  } else {
    std::cout << "Bars: " << song.arrangement.size() << "\n";
  }
}

void printUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [--pretty] <path/to/song.rbs>\n"
            << "\n"
            << "  (default)  JSON dump of ParsedSong (WasmParsedSong-compatible)\n"
            << "  --pretty   Human-readable summary instead of JSON\n";
}

} // anonymous namespace

int main(int argc, char** argv) {
  bool pretty = false;
  std::string path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pretty" || arg == "-p") {
      pretty = true;
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else if (arg.rfind('-', 0) == 0) {
      std::cerr << "Unknown option: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    } else {
      path = arg;
    }
  }

  if (path.empty()) {
    printUsage(argv[0]);
    return 1;
  }

  try {
    auto buffer = readFile(path);

    RbsParser parser;
    auto songOpt = parser.parse(buffer.data(), buffer.size());
    if (!songOpt) {
      std::cerr << "Parse error: " << parser.lastError() << "\n";
      return 1;
    }
    const ParsedSong& song = *songOpt;

    if (pretty) {
      printPretty(song);
    } else {
      std::cout << parsedSongToJson(song) << '\n';
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
