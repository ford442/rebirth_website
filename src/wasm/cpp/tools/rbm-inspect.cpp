/**
 * rbm-inspect — CLI dump for ReBirth .rbm files
 *
 *   cmake --build src/wasm/cpp/build --target rbm-inspect
 *   ./src/wasm/cpp/build/rbm-inspect path/to/mod.rbm
 *   ./src/wasm/cpp/build/rbm-inspect --pretty path/to/mod.rbm
 */

#include "../parser/ParsedModJson.h"
#include "../parser/RbmParser.h"
#include <fstream>
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

} // namespace

int main(int argc, char** argv) {
  bool pretty = false;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pretty") {
      pretty = true;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: rbm-inspect [--pretty] file.rbm\n";
      return 0;
    } else {
      path = arg;
    }
  }
  if (path.empty()) {
    std::cerr << "Usage: rbm-inspect [--pretty] file.rbm\n";
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
    if (pretty) {
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
