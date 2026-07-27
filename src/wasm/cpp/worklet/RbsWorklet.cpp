/**
 * RbsWorklet.cpp — Emscripten Wasm Audio Worklet processor.
 *
 * Registers a processor named "rbs-player" and forwards each 128-frame render
 * quantum to RbsAudioEngine::processBlock().
 *
 * Architecture note: there is no global engine pointer. The engine instance
 * pointer is passed as userData when the AudioWorkletNode is created, and the
 * callback casts it back. This keeps the audio thread's state explicit and
 * avoids main-thread/audio-thread races on a shared global.
 */

#include "RbsWorklet.h"
#include "../engine/RbsAudioEngine.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/webaudio.h>

using namespace emscripten;
using namespace rb338;

namespace {

constexpr const char* PROCESSOR_NAME = "rbs-player";
constexpr size_t AUDIO_THREAD_STACK_SIZE = 4096;

// Stack for the Emscripten audio worklet thread. Must be visible to both the
// main thread and the worklet thread, so it lives in shared WASM memory.
static uint8_t g_audioThreadStack[AUDIO_THREAD_STACK_SIZE];

// Opaque state carried through the async AudioWorklet initialisation chain.
struct WorkletInitContext {
  RbsAudioEngine* engine;
  val onReady;
  EMSCRIPTEN_WEBAUDIO_T context;
};

// Forward declarations for the async initialisation chain.
void rbsWorkletThreadInitialized(EMSCRIPTEN_WEBAUDIO_T context,
                                 EM_BOOL success,
                                 void* userData);
void rbsWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T context,
                                EM_BOOL success,
                                void* userData);

/**
 * Audio callback — invoked every 128 frames on the dedicated audio thread.
 *
 * @param userData  The RbsAudioEngine pointer passed to
 *                  emscripten_create_wasm_audio_worklet_node().
 * @return true to keep the node alive.
 */
bool rbsProcessCallback(int numInputs,
                        const AudioSampleFrame* inputs,
                        int numOutputs,
                        AudioSampleFrame* outputs,
                        int numParams,
                        const AudioParamFrame* params,
                        void* userData) {
  RbsAudioEngine* engine = static_cast<RbsAudioEngine*>(userData);

  if (!engine || numOutputs < 1) {
    for (int i = 0; i < numOutputs; ++i) {
      const uint32_t samples = outputs[i].samplesPerChannel * outputs[i].numberOfChannels;
      for (uint32_t j = 0; j < samples; ++j) {
        outputs[i].data[j] = 0.0f;
      }
    }
    return true;
  }

  const uint32_t frames = outputs[0].samplesPerChannel;
  const uint32_t channels = outputs[0].numberOfChannels;

  // The engine renders into the first interleaved output buffer.
  float* buffer = outputs[0].data;
  float* channelPtrs[2] = { buffer, buffer };

  engine->processBlock(channelPtrs, channels, frames);

  return true;
}

/**
 * Called once the AudioWorkletProcessor has been registered. Creates the actual
 * AudioWorkletNode and reports its handle back to JavaScript.
 */
void rbsWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T context,
                                EM_BOOL success,
                                void* userData) {
  WorkletInitContext* ctx = static_cast<WorkletInitContext*>(userData);

  if (!success) {
    ctx->onReady(val::null());
    delete ctx;
    return;
  }

  int outputChannelCounts[1] = { 2 };
  EmscriptenAudioWorkletNodeCreateOptions options = {
    .numberOfInputs = 0,
    .numberOfOutputs = 1,
    .outputChannelCounts = outputChannelCounts,
  };

  // Pass the engine pointer as userData so the audio callback can use it
  // without relying on a global.
  EMSCRIPTEN_WEBAUDIO_T node = emscripten_create_wasm_audio_worklet_node(
    context,
    PROCESSOR_NAME,
    &options,
    &rbsProcessCallback,
    ctx->engine);

  ctx->onReady(val(static_cast<int>(node)));
  delete ctx;
}

/**
 * Called once the Wasm Audio Worklet thread is running. Registers the
 * "rbs-player" processor type.
 */
void rbsWorkletThreadInitialized(EMSCRIPTEN_WEBAUDIO_T context,
                                 EM_BOOL success,
                                 void* userData) {
  WorkletInitContext* ctx = static_cast<WorkletInitContext*>(userData);

  if (!success) {
    ctx->onReady(val::null());
    delete ctx;
    return;
  }

  WebAudioWorkletProcessorCreateOptions opts = {
    .name = PROCESSOR_NAME,
  };

  emscripten_create_wasm_audio_worklet_processor_async(
    context,
    &opts,
    &rbsWorkletProcessorCreated,
    ctx);
}

} // anonymous namespace

namespace rb338 {

void initAudioWorklet(int contextHandle, RbsAudioEngine* engine, val onReady) {
  WorkletInitContext* ctx = new WorkletInitContext{
    engine,
    onReady,
    static_cast<EMSCRIPTEN_WEBAUDIO_T>(contextHandle),
  };

  emscripten_start_wasm_audio_worklet_thread_async(
    ctx->context,
    g_audioThreadStack,
    sizeof(g_audioThreadStack),
    &rbsWorkletThreadInitialized,
    ctx);
}

} // namespace rb338
