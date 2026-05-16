#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
#  ReBirth RB-338 WASM Audio Engine — Emscripten Build Script
# ═══════════════════════════════════════════════════════════════════
#
#  Prerequisites:
#    git clone https://github.com/emscripten-core/emsdk.git
#    cd emsdk
#    ./emsdk install latest
#    ./emsdk activate latest
#    source ./emsdk_env.sh
#
#  Usage:
#    cd src/wasm/cpp
#    ./build.sh
#
#  Output:
#    ../../../public/wasm/rbsParser.js   (glue)
#    ../../../public/wasm/rbsParser.wasm (binary)
#    ../../../public/wasm/rbsWorklet.js  (worklet, auto-generated)
# ═══════════════════════════════════════════════════════════════════

set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$(cd "$SRC_DIR/../../../public/wasm" && pwd)"

# Compiler settings
EMCC=${EMCC:-emcc}
CXXFLAGS=(
  -std=c++17
  -O3
  -sAUDIO_WORKLET=1
  -sWASM_WORKERS=1
  -sALLOW_MEMORY_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue']"
  -sEXPORT_NAME="RbsAudioModule"
  -sMODULARIZE=1
  -lembind
  -I"$SRC_DIR"
)

# Source files
SOURCES=(
  "$SRC_DIR/main.cpp"
  "$SRC_DIR/parser/RbsParser.cpp"
  "$SRC_DIR/engine/RbsAudioEngine.cpp"
  "$SRC_DIR/engine/Sequencer.cpp"
  "$SRC_DIR/engine/Mixer.cpp"
  "$SRC_DIR/synth/Voice.cpp"
  "$SRC_DIR/synth/Tb303Voice.cpp"
  "$SRC_DIR/synth/Tr808Voice.cpp"
  "$SRC_DIR/synth/Tr909Voice.cpp"
  "$SRC_DIR/worklet/RbsWorklet.cpp"
)

# Output
OUTPUT="$OUT_DIR/rbsParser.js"

echo "═══════════════════════════════════════════════════════════════"
echo "  Building ReBirth RB-338 WASM Audio Engine"
echo "═══════════════════════════════════════════════════════════════"
echo "  Source : $SRC_DIR"
echo "  Output : $OUT_DIR"
echo "  Compiler: $EMCC"
echo ""

mkdir -p "$OUT_DIR"

# Check emcc is available
if ! command -v "$EMCC" &>/dev/null; then
  echo "❌ Error: emcc not found. Did you source emsdk_env.sh?"
  exit 1
fi

# Build
"$EMCC" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$OUTPUT"

echo ""
echo "✅ Build complete."
echo "   Glue : $OUTPUT"
echo "   WASM : ${OUTPUT%.js}.wasm"
echo "   Worklet: $OUT_DIR/rbsWorklet.js"
