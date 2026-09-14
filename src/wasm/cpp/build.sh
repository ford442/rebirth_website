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

# ── Build via emcmake (single source list in sources.cmake) ──────────
if ! command -v emcmake &>/dev/null; then
  echo "❌ Error: 'emcmake' not found. Activate the Emscripten SDK." >&2
  exit 1
fi

CMAKE_BUILD_TYPE="Release"
if [[ "$MODE" == "debug" ]]; then
  CMAKE_BUILD_TYPE="Debug"
fi

WASM_BUILD_DIR="$SRC_DIR/build-wasm"
echo ""
echo "🔧 Configuring emcmake (${MODE})..."
emcmake cmake \
  -S "$SRC_DIR" \
  -B "$WASM_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
  -DRB338_WASM_OUT="$OUT_DIR"

echo "🔧 Compiling (${MODE})..."
cmake --build "$WASM_BUILD_DIR" --target rbsParser --parallel

# CMake emits rbsParser.js into public/wasm; keep the historical glue name.
if [[ ! -f "$GLUE_FILE" ]]; then
  echo "❌ emcmake did not produce $GLUE_FILE" >&2
  exit 1
fi

# ── Materialise the stable AudioWorklet bootstrap ───────────────────
# Checked-in template — never string-replace generated glue. locateFile in
# WasmAudioBridge maps worklet-scope glue requests to this file.
WORKLET_BOOTSTRAP="$ROOT_DIR/src/wasm/js/rbs-worklet-bootstrap.js"
if [[ ! -f "$WORKLET_BOOTSTRAP" ]]; then
  echo "❌ Missing worklet bootstrap: $WORKLET_BOOTSTRAP" >&2
  exit 1
fi
cp "$WORKLET_BOOTSTRAP" "$WORKLET_DST"
# Older Emscripten emitted a dedicated .aw.js sidecar; Emscripten 6 folds
# that into the ES-module glue. Drop the sidecar so public/wasm stays tidy.
rm -f "$WORKLET_SRC"

# The generic Wasm Worker bootstrap is emitted alongside the AudioWorklet file.
# Keep it: a Dedicated Worker bounce instantiates the same pthread glue and
# may request rbsParser.ww.js via locateFile.
# (Previously deleted; bounce-worker maps .ww.js if the file exists.)

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

HEAP_FLAGS_FILE="$WASM_BUILD_DIR/wasm-heap-flags.txt"
INITIAL_MEMORY=0
MAXIMUM_MEMORY=0
ALLOW_MEMORY_GROWTH=0
if [[ -f "$HEAP_FLAGS_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$HEAP_FLAGS_FILE"
fi

node -e "
const fs = require('fs');
const glue = fs.readFileSync('$GLUE_FILE', 'utf8');
if (!glue.includes('locateFile(')) {
  console.error('Generated glue no longer contains locateFile(; refuse to ship a worklet bootstrap that cannot remap assets');
  process.exit(1);
}
const pthreadWorkerPath = fs.existsSync('$OUT_DIR/${GLUE_BASENAME}.ww.js')
  ? '${GLUE_BASENAME}.ww.js'
  : '';
const pthreadWorkerBytes = pthreadWorkerPath
  ? $(file_size "$OUT_DIR/${GLUE_BASENAME}.ww.js")
  : 0;
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
    initialMemory: $INITIAL_MEMORY,
    maximumMemory: $MAXIMUM_MEMORY,
    allowMemoryGrowth: $ALLOW_MEMORY_GROWTH,
  },
  files: {
    glue:   { path: 'rbsParser.js',  bytes: $(file_size "$GLUE_FILE") },
    wasm:   { path: 'rbsParser.wasm', bytes: $(file_size "$WASM_FILE") },
    worklet:{ path: 'rbsWorklet.js',  bytes: $(file_size "$WORKLET_DST") },
  },
};
if (pthreadWorkerPath) {
  manifest.files.pthreadWorker = { path: pthreadWorkerPath, bytes: pthreadWorkerBytes };
}
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
