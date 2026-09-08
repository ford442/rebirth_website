/**
 * DrWavImpl.cpp — the single translation unit that compiles dr_wav.
 *
 * Isolated from WavDecoder.cpp so the vendored library can be built with
 * warnings suppressed (see sources.cmake) while our own wrapper code stays
 * under the project's -Wall -Wextra -Wpedantic -Werror.
 *
 * DR_WAV_NO_STDIO: there is no filesystem in the AudioWorklet; every payload
 * arrives as bytes already in the WASM heap.
 */

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../third_party/dr_wav.h"
