#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
#  ReBirth RB-338 WASM Audio Engine — Emscripten Build Script
# ═══════════════════════════════════════════════════════════════════
#
#  Prerequisites:
#    git clone https://github.com/emscripten-core/emsdk.git
#    cd emsdk
#    ./emsdk install 6.0.3
#    ./emsdk activate 6.0.3
#    source ./emsdk_env.sh
#
#  Usage:
#    cd src/wasm/cpp
#    ./build.sh [debug|release]
#
#  Output (under ../../../public/wasm/):
#    rbsParser.js   (ES-module glue)
#    rbsParser.wasm (WASM binary)
#    rbsWorklet.js  (AudioWorklet bootstrap, renamed from rbsParser.aw.js)
#    wasm-build.json (build manifest / version stamp)
# ═══════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Paths ──────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC_DIR="$SCRIPT_DIR"
OUT_DIR="$ROOT_DIR/public/wasm"

PINNED_VERSION_FILE="$SRC_DIR/.emscripten-version"
PACKAGE_JSON="$ROOT_DIR/package.json"

# ── Defaults ─────────────────────────────────────────────────────────
MODE="${WASM_BUILD_MODE:-release}"
EMCC="${EMCC:-emcc}"

# ── Argument parsing ───────────────────────────────────────────────
if [[ $# -gt 0 ]]; then
  case "$1" in
    debug|release)
      MODE="$1"
      ;;
    -h|--help)
      echo "Usage: $0 [debug|release]"
      echo "Environment: WASM_BUILD_MODE=release|debug"
      exit 0
      ;;
    *)
      echo "❌ Unknown build mode: $1" >&2
      echo "   Use 'debug' or 'release'" >&2
      exit 1
      ;;
  esac
fi

# ── Version pin ──────────────────────────────────────────────────────
if [[ ! -f "$PINNED_VERSION_FILE" ]]; then
  echo "❌ Missing version pin file: $PINNED_VERSION_FILE" >&2
  exit 1
fi
PINNED_VERSION="$(<"$PINNED_VERSION_FILE" tr -d '[:space:]')"

# ── Emscripten availability ──────────────────────────────────────────
if ! command -v "$EMCC" &>/dev/null; then
  echo "❌ Error: '$EMCC' not found." >&2
  echo "   Install/activate Emscripten ${PINNED_VERSION}:" >&2
  echo "     emsdk install ${PINNED_VERSION}" >&2
  echo "     emsdk activate ${PINNED_VERSION}" >&2
  echo "     source emsdk_env.sh" >&2
  exit 1
fi

INSTALLED_VERSION="$($EMCC --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1 || true)"
if [[ -z "$INSTALLED_VERSION" ]]; then
  echo "❌ Could not detect installed Emscripten version" >&2
  exit 1
elif [[ "$INSTALLED_VERSION" != "$PINNED_VERSION" ]]; then
  echo "❌ Emscripten version mismatch: installed=${INSTALLED_VERSION}, pinned=${PINNED_VERSION}" >&2
  echo "   Activate the pinned toolchain before building." >&2
  exit 1
fi

# ── Project version ──────────────────────────────────────────────────
PROJECT_VERSION="unknown"
if [[ -f "$PACKAGE_JSON" ]] && command -v node &>/dev/null; then
  PROJECT_VERSION="$(node -p "require('$PACKAGE_JSON').version || 'unknown'")"
fi

GIT_HASH="unknown"
if command -v git &>/dev/null && git -C "$ROOT_DIR" rev-parse --is-inside-work-tree &>/dev/null; then
  GIT_HASH="$(git -C "$ROOT_DIR" rev-parse --short HEAD)"
  if ! git -C "$ROOT_DIR" diff --quiet; then
    GIT_HASH="${GIT_HASH}-dirty"
  fi
fi

BUILD_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# ── Source files ─────────────────────────────────────────────────────
SOURCES=(
  "$SRC_DIR/main.cpp"
  "$SRC_DIR/parser/RbsParser.cpp"
  "$SRC_DIR/engine/RbsAudioEngine.cpp"
  "$SRC_DIR/engine/Sequencer.cpp"
  "$SRC_DIR/engine/Mixer.cpp"
  "$SRC_DIR/synth/Voice.cpp"
  "$SRC_DIR/synth/DrumSynth.cpp"
  "$SRC_DIR/synth/Tb303Voice.cpp"
  "$SRC_DIR/synth/Tr808Voice.cpp"
  "$SRC_DIR/synth/Tr909Voice.cpp"
  "$SRC_DIR/worklet/RbsWorklet.cpp"
)

# ── Common flags ─────────────────────────────────────────────────────
COMMON_FLAGS=(
  -std=c++17
  -pthread
  -sAUDIO_WORKLET=1
  -sWASM_WORKERS=1
  -sEXPORT_ES6=1
  -sMODULARIZE=1
  -sEXPORT_NAME="RbsAudioModule"
  -sEXPORTED_FUNCTIONS="['_malloc','_free']"
  -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue','HEAPU8','emscriptenRegisterAudioObject','emscriptenGetAudioObject']"
  -sENVIRONMENT="web,worker"
  -sALLOW_TABLE_GROWTH=1
  -sSTACK_SIZE=131072
  --no-entry
  -lembind
  -I"$SRC_DIR"
)

# ── Mode-specific flags ──────────────────────────────────────────────
if [[ "$MODE" == "debug" ]]; then
  MODE_FLAGS=(
    -O0
    -g3
    -sASSERTIONS=1
    -sSAFE_HEAP=1
    -sSTACK_OVERFLOW_CHECK=2
    -sWEBAUDIO_DEBUG=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=134217728
  )
else
  MODE_FLAGS=(
    -O3
    -flto
    -sALLOW_MEMORY_GROWTH=0
    -sINITIAL_MEMORY=67108864
  )
fi

# ── Output artifacts ─────────────────────────────────────────────────
GLUE_BASENAME="rbsParser"
GLUE_FILE="$OUT_DIR/${GLUE_BASENAME}.js"
WASM_FILE="$OUT_DIR/${GLUE_BASENAME}.wasm"
WORKLET_SRC="$OUT_DIR/${GLUE_BASENAME}.aw.js"
WORKLET_DST="$OUT_DIR/rbsWorklet.js"
MANIFEST_FILE="$OUT_DIR/wasm-build.json"

# ── Header ───────────────────────────────────────────────────────────
echo "═══════════════════════════════════════════════════════════════"
echo "  ReBirth RB-338 WASM Audio Engine — Emscripten Build"
echo "  Mode   : $MODE"
echo "  Emcc   : $INSTALLED_VERSION (pinned: $PINNED_VERSION)"
echo "  Output : $OUT_DIR"
echo "═══════════════════════════════════════════════════════════════"

# ── Clean old artifacts ──────────────────────────────────────────────
mkdir -p "$OUT_DIR"
rm -f \
  "$GLUE_FILE" \
  "$WASM_FILE" \
  "$WORKLET_SRC" \
  "$WORKLET_DST" \
  "$OUT_DIR/${GLUE_BASENAME}.ww.js" \
  "$MANIFEST_FILE"

# ── Build ────────────────────────────────────────────────────────────
echo ""
echo "🔧 Compiling (${MODE})..."
"$EMCC" \
  "${MODE_FLAGS[@]}" \
  "${COMMON_FLAGS[@]}" \
  "${SOURCES[@]}" \
  -o "$GLUE_FILE"

# ── Materialise the stable AudioWorklet bootstrap ───────────────────
if [[ -f "$WORKLET_SRC" ]]; then
  # Older Emscripten releases emitted a dedicated .aw.js sidecar.
  mv "$WORKLET_SRC" "$WORKLET_DST"
else
  # Emscripten 6 folds the AudioWorklet bootstrap into the ES-module glue but
  # no longer emits a dedicated .aw.js sidecar. The generated module detects
  # AudioWorkletGlobalScope and starts itself, so the stable wrapper imports it
  # only for side effects. Invoking its factory here registers processors twice.
  node -e "
const fs = require('fs');
const file = '$GLUE_FILE';
const needle = 'locateFile(\"${GLUE_BASENAME}.js\")';
const replacement = 'locateFile(\"rbsWorklet.js\")';
const source = fs.readFileSync(file, 'utf8');
if (!source.includes(needle)) {
  console.error('AudioWorklet module locator was not found in generated glue');
  process.exit(1);
}
fs.writeFileSync(file, source.replace(needle, replacement));
fs.writeFileSync(
  '$WORKLET_DST',
  \"import './${GLUE_BASENAME}.js';\\n\"
);
"
fi

# The generic Wasm Worker bootstrap is emitted alongside the AudioWorklet file,
# but this project only uses AudioWorklets. Remove the unused .ww.js to keep
# public/wasm tidy.
rm -f "$OUT_DIR/${GLUE_BASENAME}.ww.js"

# ── Manifest / version stamp ─────────────────────────────────────────
echo ""
echo "📝 Writing build manifest..."

file_size() {
  local f="$1"
  if [[ -f "$f" ]]; then
    wc -c < "$f" | tr -d ' '
  else
    echo 0
  fi
}

node -e "
const fs = require('fs');
const manifest = {
  project: {
    version: '$PROJECT_VERSION',
    gitHash: '$GIT_HASH',
    buildTime: '$BUILD_TIME',
  },
  emscripten: {
    pinned: '$PINNED_VERSION',
    installed: '$INSTALLED_VERSION',
  },
  build: {
    mode: '$MODE',
    sourceDir: '$SRC_DIR',
    outputDir: '$OUT_DIR',
  },
  files: {
    // Paths are relative to this manifest's own directory (public/wasm/),
    // so they stay valid regardless of the site's Astro base path.
    glue:   { path: 'rbsParser.js',  bytes: $(file_size "$GLUE_FILE") },
    wasm:   { path: 'rbsParser.wasm', bytes: $(file_size "$WASM_FILE") },
    worklet:{ path: 'rbsWorklet.js',  bytes: $(file_size "$WORKLET_DST") },
  },
};
fs.writeFileSync('$MANIFEST_FILE', JSON.stringify(manifest, null, 2) + '\n');
" || {
  echo "❌ Failed to write manifest" >&2
  exit 1
}

# ── Verify ───────────────────────────────────────────────────────────
echo ""
echo "🔍 Verifying artifacts..."
ALL_OK=true
for f in "$GLUE_FILE" "$WASM_FILE" "$WORKLET_DST" "$MANIFEST_FILE"; do
  if [[ -s "$f" ]]; then
    size="$(file_size "$f")"
    printf "   ✅ %-28s %8s bytes\n" "$(basename "$f")" "$size"
  else
    printf "   ❌ %-28s missing or empty\n" "$(basename "$f")" >&2
    ALL_OK=false
  fi
done

if [[ "$ALL_OK" != true ]]; then
  echo "❌ Build verification failed" >&2
  exit 1
fi

echo ""
echo "✅ Build complete."
echo "   Glue    : $GLUE_FILE"
echo "   WASM    : $WASM_FILE"
echo "   Worklet : $WORKLET_DST"
echo "   Manifest: $MANIFEST_FILE"
