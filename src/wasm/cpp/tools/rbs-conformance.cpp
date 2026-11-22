/**
 * rbs-conformance — full-corpus conformance sweep for the parser, writer and
 * engine.
 *
 * For every `.rbs` it is given (files, or directories searched recursively):
 *
 *   1. parse                            → `parse-rejected` if it refuses
 *   2. RbsWriter → parse again          → `write-failed` / `reparse-failed`
 *   3. compare the two semantically     → `roundtrip-diff` (RbsCompare.h)
 *   4. render the first 4 bars offline  → `render-nonfinite` / `render-clip`
 *
 * and prints a JSON report: a pass/fail histogram, the semantic round-trip
 * rate over the songs that parsed, and every failure with its first
 * difference. Exit status is 1 when the round-trip rate is below
 * `--min-roundtrip` (default 0.99) or any render produced a NaN or a sample
 * past full scale.
 *
 * Usage:
 *   rbs-conformance [--min-roundtrip 0.99] [--out report.json] <path>...
 *
 * archive-health.yml runs it nightly over the `.mirror/` content-addressed
 * corpus (ADR 0001).
 */

#include "../engine/RbsAudioEngine.h"
#include "../parser/RbsCompare.h"
#include "../parser/RbsParser.h"
#include "../parser/RbsWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace rb338;
namespace fs = std::filesystem;

namespace {

struct Failure {
  std::string path;
  std::string category;
  std::string detail;
};

std::string jsonEscape(const std::string& s) {
  std::ostringstream out;
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
        } else {
          out << c;
        }
    }
  }
  return out.str();
}

std::vector<uint8_t> readFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

void collect(const fs::path& root, std::vector<fs::path>& out) {
  std::error_code ec;
  if (fs::is_regular_file(root, ec)) {
    out.push_back(root);
    return;
  }
  for (auto it = fs::recursive_directory_iterator(
           root, fs::directory_options::follow_directory_symlink |
                     fs::directory_options::skip_permission_denied,
           ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    if (it->path().filename().string().rfind('.', 0) == 0) continue; // .gitkeep etc.
    std::string ext = it->path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    // The content-addressed blob store has no extensions; accept those too.
    if (ext == ".rbs" || ext.empty()) out.push_back(it->path());
  }
}

/** Render 4 bars; returns an empty string or the failure category. */
std::string renderCheck(const ParsedSong& song, std::string& detail) {
  EngineConfig config;
  config.sampleRate = 44100.0f;
  RbsAudioEngine engine;
  engine.init(config);
  engine.loadSong(song);
  const double secondsPerBar = 60.0 / std::clamp(song.bpm, MIN_BPM, MAX_BPM) * 4.0;
  const auto frames = static_cast<uint32_t>(secondsPerBar * 4.0 * 44100.0);
  std::vector<float> pcm(static_cast<size_t>(frames) * 2u);
  engine.renderOffline(pcm.data(), frames);
  float peak = 0.0f;
  for (size_t i = 0; i < pcm.size(); ++i) {
    if (!std::isfinite(pcm[i])) {
      detail = "sample " + std::to_string(i / 2) + " is not finite";
      return "render-nonfinite";
    }
    peak = std::max(peak, std::fabs(pcm[i]));
  }
  if (peak > 1.0f) {
    detail = "peak " + std::to_string(peak);
    return "render-clip";
  }
  return std::string();
}

} // namespace

int main(int argc, char** argv) {
  double minRoundtrip = 0.99;
  std::string outPath;
  std::vector<fs::path> files;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--min-roundtrip" && i + 1 < argc) {
      minRoundtrip = std::atof(argv[++i]);
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cerr << "Usage: rbs-conformance [--min-roundtrip 0.99] [--out report.json] <path>...\n";
      return 0;
    } else {
      collect(arg, files);
    }
  }
  if (files.empty()) {
    std::cerr << "rbs-conformance: no input files\n";
    return 2;
  }
  std::sort(files.begin(), files.end());

  std::map<std::string, size_t> histogram;
  std::vector<Failure> failures;
  size_t parsed = 0;
  size_t roundtripped = 0;
  bool renderFailed = false;

  for (const fs::path& path : files) {
    const std::vector<uint8_t> bytes = readFile(path);
    RbsParser parser;
    auto song = parser.parse(bytes.data(), bytes.size());
    if (!song) {
      ++histogram["parse-rejected"];
      failures.push_back({path.string(), "parse-rejected", parser.lastError()});
      continue;
    }
    ++parsed;

    std::string category;
    std::string detail;
    RbsWriter writer;
    auto written = writer.write(*song);
    if (!written) {
      category = "write-failed";
      detail = writer.lastError();
    } else {
      RbsParser reparser;
      auto reloaded = reparser.parse(written->data(), written->size());
      if (!reloaded) {
        category = "reparse-failed";
        detail = reparser.lastError();
      } else {
        detail = semanticDifference(*song, *reloaded);
        if (!detail.empty()) category = "roundtrip-diff";
      }
    }
    if (category.empty()) ++roundtripped;

    if (category.empty()) {
      category = renderCheck(*song, detail);
      if (!category.empty()) renderFailed = true;
    }
    if (category.empty()) category = "pass";
    ++histogram[category];
    if (category != "pass") failures.push_back({path.string(), category, detail});
  }

  const double rate = parsed == 0 ? 0.0 : static_cast<double>(roundtripped) / parsed;
  std::ostringstream json;
  json << "{\n  \"files\": " << files.size() << ",\n  \"parsed\": " << parsed
       << ",\n  \"roundtripped\": " << roundtripped << ",\n  \"roundtripRate\": " << rate
       << ",\n  \"minRoundtrip\": " << minRoundtrip << ",\n  \"histogram\": {";
  bool first = true;
  for (const auto& [name, count] : histogram) {
    json << (first ? "\n" : ",\n") << "    \"" << name << "\": " << count;
    first = false;
  }
  json << "\n  },\n  \"failures\": [";
  for (size_t i = 0; i < failures.size(); ++i) {
    json << (i ? ",\n" : "\n") << "    {\"path\": \"" << jsonEscape(failures[i].path)
         << "\", \"category\": \"" << failures[i].category << "\", \"detail\": \""
         << jsonEscape(failures[i].detail) << "\"}";
  }
  json << "\n  ]\n}\n";

  if (!outPath.empty()) {
    std::ofstream(outPath) << json.str();
  }
  std::cout << json.str();
  std::cerr << "rbs-conformance: " << roundtripped << "/" << parsed
            << " parsed songs round-trip (" << rate * 100.0 << "%), " << files.size()
            << " files\n";
  return (rate < minRoundtrip || renderFailed) ? 1 : 0;
}
