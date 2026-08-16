#pragma once

// Native syntax-check stub for Emscripten's val.h.

namespace emscripten {

class val {
public:
  val() = default;
  explicit val(int) {}
  static val null() { return {}; }
  static val undefined() { return {}; }
  bool isNull() const { return true; }
  bool isUndefined() const { return true; }
  template <typename... Args>
  val operator()(Args&&...) const {
    return {};
  }
};

} // namespace emscripten
