#include "../parser/ParsedModJson.h"
#include "../parser/RbmParser.h"
#include "../third_party/doctest.h"
#include <fstream>
#include <string>
#include <vector>

using namespace rb338;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open fixture: " + path);
  }
  file.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  return buffer;
}

} // namespace

TEST_CASE("RBM: reject truncated and wrong magic") {
  RbmParser parser;
  const uint8_t tiny[] = {1, 2, 3};
  CHECK_FALSE(parser.parse(tiny, sizeof(tiny)));
  CHECK(parser.lastError().find("too small") != std::string::npos);

  const uint8_t zipish[] = {'P', 'K', 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK_FALSE(parser.parse(zipish, sizeof(zipish)));
  CHECK(parser.lastError().find("CAT") != std::string::npos);

  // CAT / RB40 is a song, not a mod.
  const uint8_t song[16] = {
      'C', 'A', 'T', ' ', 0, 0, 0, 8, 'R', 'B', '4', '0', 0, 0, 0, 0};
  CHECK_FALSE(parser.parse(song, sizeof(song)));
  CHECK(parser.lastError().find("PRBM") != std::string::npos);
}

TEST_CASE("RBM: filename classification") {
  CHECK(classifyResourceName("tr808bd.aif") == ModResourceKind::Sample);
  CHECK(classifyResourceName("12522.jpg") == ModResourceKind::Skin);
  CHECK(classifyResourceName("demo.rbs") == ModResourceKind::Song);
  CHECK(classifySampleSlot("tr808bd.aif") == ModSampleSlot::Tr808Kick);
  CHECK(classifySampleSlot("TR808MT.AIF") == ModSampleSlot::Tr808MidTom);
  CHECK(classifySampleSlot("tr909ccy.aif") == ModSampleSlot::Tr909Crash);
  CHECK(classifySampleSlot("tr909mt.aif") == ModSampleSlot::Tr909MidTom);
  CHECK(classifySampleSlot("12522.jpg") == ModSampleSlot::Unknown);
}

TEST_CASE("RBM: parse synthetic minimal fixture") {
  const auto bytes = readFile("src/wasm/test-fixtures/mods/minimal.rbm");
  RbmParser parser;
  auto mod = parser.parse(bytes.data(), bytes.size());
  REQUIRE(mod);
  CHECK(mod->title == "Minimal Test Mod");
  CHECK(mod->copyright.find("Propellerhead") != std::string::npos);
  REQUIRE(mod->resources.size() == 1);
  CHECK(mod->resources[0].name == "tr808bd.aif");
  CHECK(mod->resources[0].kind == ModResourceKind::Sample);
  CHECK(mod->resources[0].slot == ModSampleSlot::Tr808Kick);
  CHECK(mod->resources[0].bytes.size() >= 12);
  CHECK(mod->resources[0].bytes[0] == 'F');
  const std::string json = parsedModToJson(*mod);
  CHECK(json.find("tr808-kick") != std::string::npos);
  CHECK(json.find("\"kind\":\"sample\"") != std::string::npos);
}
