/**
 * RbsWorklet.cpp — Emscripten Wasm Audio Worklet processor.
 *
 * This file contains the audio callback that runs on the dedicated audio
 * rendering thread. It pulls samples from RbsAudioEngine::processBlock().
 *
 * Emscripten's -sAUDIO_WORKLET flag automatically wraps this into a
 * JavaScript AudioWorkletProcessor class named "rbs-player".
 */

#include <emscripten/webaudio.h>
#include "../engine/RbsAudioEngine.h"

using namespace rb338;

// Global engine pointer — set from JS during worklet initialisation
static RbsAudioEngine* g_engine = nullptr;

/**
 * Audio callback — invoked every 128 frames.
 *
 * @param numInputs       Number of input buses
 * @param inputs          Input audio data
 * @param numOutputs      Number of output buses
 * @param outputs         Output audio data (we write here)
 * @param numParams       Number of AudioParams
 * @param params          AudioParam values
 * @param userData        Opaque pointer (unused)
 * @return true to keep the node alive
 */
bool rbsProcessCallback(int numInputs,
                        const AudioSampleFrame* inputs,
                        int numOutputs,
                        AudioSampleFrame* outputs,
                        int numParams,
                        const AudioParamFrame* params,
                        void* userData) {
  if (!g_engine || numOutputs < 1) {
    // Output silence if no engine
    for (int i = 0; i < numOutputs; ++i) {
      for (int j = 0; j < outputs[i].samplesPerChannel * outputs[i].numberOfChannels; ++j) {
        outputs[i].data[j] = 0.0f;
      }
    }
    return true;
  }

  // Render stereo interleaved output
  const uint32_t frames = outputs[0].samplesPerChannel;
  const uint32_t channels = outputs[0].numberOfChannels;

  // For now, render mono then duplicate to stereo
  // In a full implementation, RbsAudioEngine would render interleaved stereo directly
  float* buffer = outputs[0].data;
  float* channelPtrs[2] = { buffer, buffer };

  g_engine->processBlock(channelPtrs, channels, frames);

  return true;
}

// The Emscripten AudioWorklet runtime will register this processor under
// the name "rbs-player" based on the build flags.
