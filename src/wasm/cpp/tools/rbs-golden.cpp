/**
 * rbs-golden — regenerate the golden reference songs and engine renders in
 * src/wasm/test-fixtures/golden/ (GOLDEN.md).
 *
 *   rbs-golden songs  <dir>   write <id>.rbs for every case (GoldenCases.h)
 *   rbs-golden render <dir>   render each <id>.rbs to <id>.wav (engine source)
 *   rbs-golden list           print the cases
 *
 * `render` overwrites only cases whose source is the engine: a reference
 * recorded from original ReBirth is never replaced by an engine render.
 * Re-render after an intended sound change, listen, and commit the .wav
 * files with the change that caused them.
 */

#include "../audio/WavWriter.h"
#include "../parser/RbsParser.h"
#include "../parser/RbsWriter.h"
#include "GoldenCases.h"
#include "RenderSong.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

constexpr float kRate = 44100.0f;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open: " + path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not write: " + path);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char** argv) {
  try {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "list") {
      for (const auto& c : golden::cases()) {
        std::cout << c.id << "  " << c.seconds << " s  "
                  << (c.source == golden::Source::Engine ? "engine" : "rebirth-2.0.1") << "  "
                  << c.description << "\n";
      }
      return 0;
    }
    if ((command != "songs" && command != "render") || argc != 3) {
      std::cerr << "Usage: rbs-golden songs|render <dir> | rbs-golden list\n";
      return 1;
    }
    const std::string dir = argv[2];
    for (const auto& c : golden::cases()) {
      const std::string rbsPath = dir + "/" + c.id + ".rbs";
      if (command == "songs") {
        RbsWriter writer;
        auto bytes = writer.write(golden::buildSong(c.id));
        if (!bytes) throw std::runtime_error(std::string(c.id) + ": " + writer.lastError());
        writeFile(rbsPath, *bytes);
        std::cerr << "wrote " << rbsPath << "\n";
        continue;
      }
      if (c.source != golden::Source::Engine) {
        std::cerr << "skip  " << c.id << " (a ReBirth recording)\n";
        continue;
      }
      const auto bytes = readFile(rbsPath);
      RbsParser parser;
      auto song = parser.parse(bytes.data(), bytes.size());
      if (!song) throw std::runtime_error(rbsPath + ": " + parser.lastError());
      tools::RenderOptions options;
      options.sampleRate = kRate;
      options.frames = static_cast<uint32_t>(std::lround(c.seconds * kRate));
      std::string error;
      auto stereo = tools::renderStereo(*song, options, &error);
      if (!stereo) throw std::runtime_error(std::string(c.id) + ": " + error);
      const auto mono = tools::toMono(*stereo);
      writeFile(dir + "/" + c.id + ".wav",
                writeWavPcm16(mono.data(), static_cast<uint32_t>(mono.size()), 1,
                              static_cast<uint32_t>(kRate)));
      std::cerr << "rendered " << c.id << "\n";
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "rbs-golden: " << e.what() << "\n";
    return 1;
  }
}
