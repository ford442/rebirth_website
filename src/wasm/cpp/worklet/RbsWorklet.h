/**
 * RbsWorklet.h — AudioWorklet registration interface.
 *
 * Exposes a single embind-callable helper that wires the WASM audio engine
 * into a host Web Audio AudioContext using Emscripten's Wasm Audio Worklet API.
 */

#pragma once

#include <cstdint>
#include <emscripten/val.h>

namespace rb338 {

class RbsAudioEngine;

/**
 * Initialise the AudioWorklet for the given AudioContext and engine.
 *
 * This function:
 *   1. Starts the Emscripten Wasm Audio Worklet thread on `contextHandle`.
 *   2. Registers an AudioWorkletProcessor named "rbs-player".
 *   3. Creates a stereo AudioWorkletNode that calls `rbsProcessCallback`.
 *   4. Invokes `onReady(nodeHandle)` with the Emscripten node handle, or
 *      `onReady(null)` on failure.
 *
 * @param contextHandle  Handle obtained from `emscriptenRegisterAudioObject()`.
 * @param enginePtr      Raw pointer to the RbsAudioEngine instance.
 * @param onReady        JS callback function(nodeHandle).
 */
void initAudioWorklet(int contextHandle, uintptr_t enginePtr, emscripten::val onReady);

} // namespace rb338
