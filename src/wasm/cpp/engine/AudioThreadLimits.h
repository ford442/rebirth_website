#pragma once

#include "Sequencer.h"
#include <cstddef>
#include <cstdint>

namespace rb338 {

/** AudioWorklet render quantum — fixed at 128 frames in browsers. */
constexpr uint32_t AUDIO_WORKLET_FRAMES = 128;

/** Upper bound for synchronous renderTestBlock() harness calls. */
constexpr uint32_t MAX_RENDER_TEST_FRAMES = 2048;

/**
 * Stack allocated for the Emscripten Wasm Audio Worklet pthread
 * (`g_audioThreadStack` in worklet/RbsWorklet.cpp).
 *
 * This is separate from the WASM module linear stack configured via
 * `-sSTACK_SIZE` in CMakeLists.txt (128 KiB for the main thread).
 */
constexpr size_t AUDIO_THREAD_STACK_SIZE = 65536;

constexpr size_t PROCESS_BLOCK_MAX_STACK_FRAME_BYTES =
    sizeof(Sequencer::Event) * 128 + 4096;

static_assert(
    AUDIO_THREAD_STACK_SIZE >= PROCESS_BLOCK_MAX_STACK_FRAME_BYTES,
    "AUDIO_THREAD_STACK_SIZE must cover processBlock() stack locals");

} // namespace rb338
