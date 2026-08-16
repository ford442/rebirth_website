#pragma once

// Native syntax-check stubs for Emscripten's Wasm Audio Worklet API.

#include <cstddef>
#include <cstdint>

using EM_BOOL = int;
using EMSCRIPTEN_WEBAUDIO_T = int;

struct AudioSampleFrame {
  uint32_t numberOfChannels = 0;
  uint32_t samplesPerChannel = 0;
  float* data = nullptr;
};

struct AudioParamFrame {
  uint32_t count = 0;
  float* data = nullptr;
};

struct EmscriptenAudioWorkletNodeCreateOptions {
  int numberOfInputs = 0;
  int numberOfOutputs = 0;
  int* outputChannelCounts = nullptr;
};

struct WebAudioWorkletProcessorCreateOptions {
  const char* name = nullptr;
};

using EmscriptenWorkletProcessorCallback = bool (*)(
    int, const AudioSampleFrame*, int, AudioSampleFrame*, int, const AudioParamFrame*, void*);

using EmscriptenWorkletReadyCallback = void (*)(EMSCRIPTEN_WEBAUDIO_T, EM_BOOL, void*);

inline EMSCRIPTEN_WEBAUDIO_T emscripten_create_wasm_audio_worklet_node(
    EMSCRIPTEN_WEBAUDIO_T,
    const char*,
    const EmscriptenAudioWorkletNodeCreateOptions*,
    EmscriptenWorkletProcessorCallback,
    void*) {
  return 0;
}

inline void emscripten_create_wasm_audio_worklet_processor_async(
    EMSCRIPTEN_WEBAUDIO_T,
    const WebAudioWorkletProcessorCreateOptions*,
    EmscriptenWorkletReadyCallback,
    void*) {}

inline void emscripten_start_wasm_audio_worklet_thread_async(
    EMSCRIPTEN_WEBAUDIO_T,
    void*,
    size_t,
    EmscriptenWorkletReadyCallback,
    void*) {}
