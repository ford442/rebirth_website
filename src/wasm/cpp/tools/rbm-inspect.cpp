/**
 * rbm-inspect — CLI dump for ReBirth .rbm files
 *
 *   cmake --build src/wasm/cpp/build --target rbm-inspect
 *   ./src/wasm/cpp/build/rbm-inspect path/to/mod.rbm
 *   ./src/wasm/cpp/build/rbm-inspect --pretty path/to/mod.rbm
 *   ./src/wasm/cpp/build/rbm-inspect --samples path/to/mod.rbm
 *
 * `--samples` runs the payloads through the same SamplePool the engine uses,
 * so it answers "will this mod actually play?" rather than just "does it
 * parse?".
 */

#include "../parser/ParsedModJson.h"
#include "../parser/RbmParser.h"
#include "../synth/SamplePool.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace rb338;

namespace {

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

void printPretty(const ParsedMod& mod) {
  std::cout << "Title:        " << (mod.title.empty() ? "(none)" : mod.title) << "\n";
  std::cout << "Description:  " << (mod.description.empty() ? "(none)" : mod.description) << "\n";
  std::cout << "Copyright:    " << (mod.copyright.empty() ? "(none)" : mod.copyright) << "\n";
  std::cout << "Resources:    " << mod.resources.size() << "\n";
  for (const auto& res : mod.resources) {
    std::cout << "  - " << res.name << "  kind=";
    switch (res.kind) {
      case ModResourceKind::Sample: std::cout << "sample"; break;
      case ModResourceKind::Skin: std::cout << "skin"; break;
      case ModResourceKind::Song: std::cout << "song"; break;
      case ModResourceKind::Other: std::cout << "other"; break;
    }
    std::cout << "  slot=" << modSampleSlotName(res.slot) << "  bytes=" << res.bytes.size()
              << "\n";
  }
}

void printSamples(const ParsedMod& mod) {
  SamplePool pool;
  if (!pool.init()) {
    std::cerr << "Could not allocate the sample arena\n";
    return;
  }

  ModLoadReport report;
  const ModLoadStatus status = pool.loadFromParsedMod(mod, report);

  std::cout << "Title:        " << (report.title.empty() ? "(none)" : report.title) << "\n";
  std::cout << "Load status:  " << modLoadStatusName(status) << "\n";
  std::cout << "Slots loaded: " << report.loadedSlots << "\n";
  std::cout << "Skins:        " << report.skinCount << " (catalogued, never decoded)\n";
  std::cout << "Arena:        " << report.usedFrames << " / " << report.capacityFrames
            << " frames\n";
  std::cout << "Resources:    " << report.resources.size() << "\n";

  for (const auto& entry : report.resources) {
    std::cout << "  - " << std::left << std::setw(20) << entry.name << std::right
              << "  slot=" << std::setw(18) << modSampleSlotName(entry.slot)
              << "  bytes=" << std::setw(8) << entry.byteSize;
    if (entry.loaded) {
      std::cout << "  frames=" << std::setw(7) << entry.frameCount
                << "  " << entry.sampleRate << " Hz"
                << "  " << static_cast<int>(entry.bitDepth) << "-bit"
                << "  ch=" << static_cast<int>(entry.channels);
    } else if (entry.kind == ModResourceKind::Sample) {
      std::cout << "  NOT LOADED (" << sampleDecodeStatusName(entry.decodeStatus) << ")";
    } else {
      std::cout << "  (" << (entry.kind == ModResourceKind::Skin ? "skin" : "non-sample")
                << ", not decoded)";
    }
    std::cout << "\n";
  }
}

} // namespace

int main(int argc, char** argv) {
  bool pretty = false;
  bool samples = false;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pretty") {
      pretty = true;
    } else if (arg == "--samples") {
      samples = true;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: rbm-inspect [--pretty|--samples] file.rbm\n";
      return 0;
    } else {
      path = arg;
    }
  }
  if (path.empty()) {
    std::cerr << "Usage: rbm-inspect [--pretty|--samples] file.rbm\n";
    return 2;
  }

  try {
    const auto bytes = readFile(path);
    RbmParser parser;
    auto mod = parser.parse(bytes.data(), bytes.size());
    if (!mod) {
      std::cerr << "Parse error: " << parser.lastError() << "\n";
      return 1;
    }
    if (samples) {
      printSamples(*mod);
    } else if (pretty) {
      printPretty(*mod);
    } else {
      std::cout << parsedModToJson(*mod) << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
