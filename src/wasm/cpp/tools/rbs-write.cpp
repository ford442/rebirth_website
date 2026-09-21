/**
 * rbs-write — re-serialise a ReBirth RB-338 .rbs file through RbsWriter.
 *
 * The round-trip check the `.rbs` writer is graded on:
 *
 *   rbs-inspect song.rbs            # semantic JSON
 *   rbs-write song.rbs out.rbs
 *   rbs-inspect out.rbs             # must match on every semantic field
 *
 * Build (CMake):
 *   cmake -S src/wasm/cpp -B src/wasm/cpp/build
 *   cmake --build src/wasm/cpp/build --target rbs-write
 *
 * Usage:
 *   ./src/wasm/cpp/build/rbs-write in.rbs [out.rbs]   # writes in.rbs → out.rbs
 *   ./src/wasm/cpp/build/rbs-write --check in.rbs     # round-trip, no output file
 */

#include "../parser/RbsParser.h"
#include "../parser/RbsWriter.h"
#include "../parser/RbsTypes.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open: " + path);
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not write: " + path);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file) throw std::runtime_error("Write failed: " + path);
}

ParsedSong parseOrThrow(const std::vector<uint8_t>& bytes, const std::string& label) {
  RbsParser parser;
  auto song = parser.parse(bytes.data(), bytes.size());
  if (!song) throw std::runtime_error("Parse failed (" + label + "): " + parser.lastError());
  return *song;
}

void usage() {
  std::cerr << "Usage: rbs-write <input.rbs> [output.rbs]\n"
               "       rbs-write --check <input.rbs>\n";
}

} // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  bool checkOnly = false;
  std::vector<std::string> paths;
  for (const std::string& arg : args) {
    if (arg == "--check") {
      checkOnly = true;
    } else if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    } else {
      paths.push_back(arg);
    }
  }

  if (paths.empty() || paths.size() > 2 || (checkOnly && paths.size() != 1)) {
    usage();
    return 2;
  }

  try {
    const ParsedSong song = parseOrThrow(readFile(paths[0]), paths[0]);

    RbsWriter writer;
    const auto bytes = writer.write(song);
    if (!bytes) {
      std::cerr << "Write failed: " << writer.lastError() << "\n";
      return 1;
    }

    // Always prove the output reloads before handing it to anyone.
    const ParsedSong reloaded = parseOrThrow(*bytes, "written output");

    if (checkOnly) {
      std::cerr << "OK: " << paths[0] << " → " << bytes->size() << " bytes, reloads as \""
                << reloaded.title << "\" (" << reloaded.bpm << " BPM, "
                << reloaded.arrangement.size() << " bars, " << reloaded.patterns.size()
                << " patterns, " << reloaded.automation.size() << " automation events)\n";
      return 0;
    }

    const std::string outPath = paths.size() == 2 ? paths[1] : "out.rbs";
    writeFile(outPath, *bytes);
    std::cerr << "Wrote " << outPath << " (" << bytes->size() << " bytes)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
