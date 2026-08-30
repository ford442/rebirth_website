#pragma once

#include "RbmTypes.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace rb338 {

/**
 * RbmParser — reads a ReBirth .rbm (PRBM chunk container).
 *
 * Thread safety: not thread-safe. Create one parser per thread.
 */
class RbmParser {
public:
  std::optional<ParsedMod> parse(std::span<const uint8_t> data);
  std::optional<ParsedMod> parse(const uint8_t* data, size_t size);
  const std::string& lastError() const { return m_error; }
  void clearError() { m_error.clear(); }

private:
  std::string m_error;

  bool parseList(const uint8_t* data, size_t size, ParsedMod& mod);
  bool parseHead(const uint8_t* data, size_t size, ParsedMod& mod);
  bool parseEmbf(const uint8_t* data, size_t size, ParsedMod& mod);
  bool parseInfo(const uint8_t* data, size_t size, ParsedMod& mod);
};

ModResourceKind classifyResourceName(const std::string& name);
ModSampleSlot classifySampleSlot(const std::string& name);

} // namespace rb338
