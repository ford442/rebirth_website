/**
 * rbs-render — render a ReBirth RB-338 .rbs to WAV natively, no browser.
 *
 * Drives the same RbsAudioEngine offline path as the browser bounce, so a
 * contributor can A/B the engine against a recording from original ReBirth
 * (see src/wasm/test-fixtures/golden/GOLDEN.md) from a shell.
 *
 * Usage:
 *   rbs-render [options] <in.rbs> <out.wav>
 *
 *   --bars N        render N bars from bar 1 (default: one pass of the song)
 *   --seconds S     render S seconds instead
 *   --rate HZ       device rate (default 44100)
 *   --bpm X         override the song's tempo
 *   --stem D        render device D alone (0 303-A, 1 303-B, 2 808, 3 909)
 *   --mode song|pattern   force the play mode
 *   --mono          write one channel (the left/right average)
 */

#include "../audio/WavWriter.h"
#include "../parser/RbsParser.h"
#include "RenderSong.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rb338;

namespace {

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
  if (!file) throw std::runtime_error("Write failed: " + path);
}

void usage() {
  std::cerr << "Usage: rbs-render [--bars N | --seconds S] [--rate HZ] [--bpm X]\n"
               "                  [--stem 0-3] [--mode song|pattern] [--mono]\n"
               "                  <in.rbs> <out.wav>\n";
}

double numberArg(int& i, int argc, char** argv) {
  if (i + 1 >= argc) throw std::runtime_error(std::string("Missing value for ") + argv[i]);
  char* end = nullptr;
  const double value = std::strtod(argv[++i], &end);
  if (!end || *end != '\0' || !std::isfinite(value) || value < 0) {
    throw std::runtime_error(std::string("Bad value: ") + argv[i]);
  }
  return value;
}

} // namespace

int main(int argc, char** argv) {
  try {
    double bars = 0.0;
    double seconds = 0.0;
    bool mono = false;
    std::string mode;
    tools::RenderOptions options;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--bars") {
        bars = numberArg(i, argc, argv);
      } else if (arg == "--seconds") {
        seconds = numberArg(i, argc, argv);
      } else if (arg == "--rate") {
        options.sampleRate = static_cast<float>(numberArg(i, argc, argv));
      } else if (arg == "--bpm") {
        options.bpm = static_cast<float>(numberArg(i, argc, argv));
      } else if (arg == "--stem") {
        const double d = numberArg(i, argc, argv);
        if (d > 3) throw std::runtime_error("--stem takes 0-3");
        options.device = static_cast<uint8_t>(d);
      } else if (arg == "--mode") {
        if (i + 1 >= argc) throw std::runtime_error("Missing value for --mode");
        mode = argv[++i];
        if (mode != "song" && mode != "pattern") throw std::runtime_error("--mode song|pattern");
      } else if (arg == "--mono") {
        mono = true;
      } else if (arg == "--help" || arg == "-h") {
        usage();
        return 0;
      } else if (arg.rfind("--", 0) == 0) {
        throw std::runtime_error("Unknown option: " + arg);
      } else {
        paths.push_back(arg);
      }
    }
    if (paths.size() != 2) {
      usage();
      return 1;
    }
    if (options.sampleRate < 8000.0f || options.sampleRate > 192000.0f) {
      throw std::runtime_error("--rate must be 8000-192000");
    }

    const auto bytes = readFile(paths[0]);
    RbsParser parser;
    auto song = parser.parse(bytes.data(), bytes.size());
    if (!song) throw std::runtime_error("Parse failed: " + parser.lastError());
    if (mode == "song") song->playMode = PlayMode::Song;
    if (mode == "pattern") song->playMode = PlayMode::Pattern;

    const float bpm = options.bpm > 0.0f ? options.bpm : song->bpm;
    if (bars > 0.0) seconds = bars * 4.0 * 60.0 / bpm;
    if (seconds > 1800.0) throw std::runtime_error("Refusing to render more than 30 minutes");
    if (seconds > 0.0) {
      options.frames = static_cast<uint32_t>(std::llround(seconds * options.sampleRate));
    }

    std::string error;
    auto stereo = tools::renderStereo(*song, options, &error);
    if (!stereo) throw std::runtime_error(error);
    const auto rate = static_cast<uint32_t>(std::lround(options.sampleRate));
    const auto frames = static_cast<uint32_t>(stereo->size() / 2u);
    const auto wav = mono ? writeWavPcm16(tools::toMono(*stereo).data(), frames, 1, rate)
                          : writeWavPcm16(stereo->data(), frames, 2, rate);
    writeFile(paths[1], wav);
    std::cerr << "Rendered " << frames << " frames (" << static_cast<double>(frames) / rate
              << " s) → " << paths[1] << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "rbs-render: " << e.what() << "\n";
    return 1;
  }
}
