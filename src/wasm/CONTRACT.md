# WASM Audio Bridge Contract

This document is the single source of truth for the data shape shared between the C++ WASM engine and the TypeScript bridge. Every C++ struct registered through Embind must match its TypeScript counterpart field-for-field, and every TypeScript interface used to call into WASM must match the Embind registration.

## Primitive mappings

| C++ type | Embind wire type | TypeScript type | Notes |
|----------|------------------|-----------------|-------|
| `bool` | boolean | `boolean` | |
| `uint8_t` | number | `number` | 0–255 |
| `uint16_t` | number | `number` | 0–65535 |
| `float` | number | `number` | |
| `std::string` | string | `string` | UTF-8 |
| `DeviceId` (enum) | number | `WasmDeviceId` | 0=TB303_A, 1=TB303_B, 2=TR808, 3=TR909 |

## Value objects

Value objects are copied across the JS/WASM boundary. They are registered with `value_object<T>(name)` in C++ and represented as plain objects in TypeScript.

### `EngineConfig`

C++ struct (`src/wasm/cpp/engine/RbsAudioEngine.h`):

```cpp
struct EngineConfig {
  float sampleRate = 44100.0f;
  uint32_t bufferSize = 128;
  bool enableTb303A = true;
  bool enableTb303B = true;
  bool enableTr808 = true;
  bool enableTr909 = true;
  bool enableDistortion = true;
  bool enableCompressor = true;
  bool enableDelay = true;
};
```

TypeScript interface (`src/wasm/types/wasm-audio.ts`):

```ts
export interface EngineConfig {
  sampleRate: number;
  bufferSize: number;
  enableTb303A: boolean;
  enableTb303B: boolean;
  enableTr808: boolean;
  enableTr909: boolean;
  enableDistortion: boolean;
  enableCompressor: boolean;
  enableDelay: boolean;
}
```

**Rule:** The bridge must flatten the `EngineFeatures` helper object from `audio-module.config.js` into these flat fields before calling `RbsAudioEngine.init()`.

### `PlaybackPosition`

C++ value object (returned by `RbsAudioEngine::getPlaybackPosition()` via wrapper):

```cpp
struct PlaybackPosition {
  uint16_t bar;
  uint8_t step;
};
```

TypeScript interface:

```ts
export interface PlaybackPosition {
  bar: number;
  step: number;
}
```

The C++ method is `void getPlaybackPosition(uint16_t& bar, uint8_t& step) const`, but Embind exposes it as a value object returning `{ bar, step }`.

### `StepData`

C++:

```cpp
struct StepData {
  bool active = false;
  uint8_t note = 0;
  bool accent = false;
  bool slide = false;
};
```

TypeScript (`WasmStepData`):

```ts
export interface WasmStepData {
  active: boolean;
  note: number;
  accent: boolean;
  slide: boolean;
}
```

### `PatternRef`

C++:

```cpp
struct PatternRef {
  uint8_t bank = 0;
  uint8_t index = 0;
};
```

TypeScript (`WasmPatternRef`):

```ts
export interface WasmPatternRef {
  bank: number;
  index: number;
}
```

### `ArrangementBar`

C++:

```cpp
struct ArrangementBar {
  uint16_t barNumber = 1;
  std::array<PatternRef, NUM_DEVICES> devicePatterns;
};
```

TypeScript (`WasmArrangementBar`):

```ts
export interface WasmArrangementBar {
  barNumber: number;
  devicePatterns: WasmPatternRef[];
}
```

### `DeviceState`

C++:

```cpp
struct DeviceState {
  DeviceId id;
  float tune = 0.5f;
  float cutoff = 0.5f;
  float resonance = 0.5f;
  float envMod = 0.5f;
  float decay = 0.5f;
  float accent = 0.5f;
  uint8_t waveform = 0;
  uint8_t initialPatternBank = 0;
  uint8_t initialPatternIndex = 0;
  bool muted = false;
  float level = 0.8f;
  float pan = 0.5f;
  bool dist = false;
  bool pcf = false;
  bool compressor = false;
  float delaySend = 0.0f;
};
```

TypeScript (`WasmDeviceState`):

```ts
export interface WasmDeviceState {
  id: WasmDeviceId;
  tune: number;
  cutoff: number;
  resonance: number;
  envMod: number;
  decay: number;
  accent: number;
  waveform: number;
  initialPatternBank: number;
  initialPatternIndex: number;
  muted: boolean;
  level: number;
  pan: number;
  dist: boolean;
  pcf: boolean;
  compressor: boolean;
  delaySend: number;
}
```

### `Pattern`

C++:

```cpp
struct Pattern {
  DeviceId deviceId;
  uint8_t bank = 0;
  uint8_t patternIndex = 0;
  uint8_t length = 16;
  std::array<StepData, MAX_STEPS> steps;
};
```

TypeScript (`WasmPattern`):

```ts
export interface WasmPattern {
  deviceId: WasmDeviceId;
  bank: number;
  patternIndex: number;
  length: number;
  steps: WasmStepData[];
}
```

### `ParsedSong`

C++:

```cpp
struct ParsedSong {
  std::string title;
  std::string author;
  std::string infoText;
  std::string creatorUrl;
  float bpm = 125.0f;
  RbsVersion version = RbsVersion::V2_0_1;
  uint8_t headVersion = 0;
  uint8_t globSubFormat = 0;
  bool showInfoOnOpen = false;
  std::array<DeviceState, NUM_DEVICES> devices;
  std::vector<Pattern> patterns;
  std::vector<ArrangementBar> arrangement;
};
```

TypeScript (`WasmParsedSong`):

```ts
export interface WasmParsedSong {
  title: string;
  author: string;
  infoText: string;
  creatorUrl: string;
  bpm: number;
  version: number;
  headVersion: number;
  globSubFormat: number;
  showInfoOnOpen: boolean;
  devices: WasmDeviceState[];
  patterns: WasmPattern[];
  arrangement: WasmArrangementBar[];
}
```

The bridge converts `WasmParsedSong` to the UI-facing `ParsedSong`, mapping numeric `WasmDeviceId` values to string labels and flattening knob fields into `DeviceState.knobs`.

## Class handles

These are C++ classes exposed by Embind and instantiated from JavaScript with `new module.rb338.ClassName()`.

### `RbsAudioEngine`

| C++ API | Embind name | TS signature |
|---------|-------------|--------------|
| `bool init(const EngineConfig&)` | `init` | `(config: EngineConfig) => boolean` |
| `bool loadSong(const ParsedSong&)` | `loadSong` | `(song: WasmParsedSong) => boolean` |
| `void play()` | `play` | `() => void` |
| `void pause()` | `pause` | `() => void` |
| `void stop()` | `stop` | `() => void` |
| `void seek(uint16_t)` | `seek` | `(bar: number) => void` |
| `void setVolume(float)` | `setVolume` | `(volume: number) => void` |
| `void setTempo(float)` | `setTempo` | `(bpm: number) => void` |
| `float getTempo() const` | `getTempo` | `() => number` |
| `void setTempoMultiplier(float)` | `setTempoMultiplier` | `(multiplier: number) => void` |
| `bool isPlaying() const` | `isPlaying` | `() => boolean` |
| `void getPlaybackPosition(...)` (wrapped) | `getPlaybackPosition` | `() => PlaybackPosition` |

### `RbsParser`

| C++ API | Embind name | TS signature |
|---------|-------------|--------------|
| `std::optional<ParsedSong> parse(const uint8_t*, size_t)` | `parse` | `(ptr: number, size: number) => WasmParsedSong \| null` |
| `const std::string& lastError() const` | `lastError` | `() => string` |

## Container registrations

Embind must register these container types before `ParsedSong` can cross the boundary:

- `register_vector<Pattern>("PatternVector")`
- `register_vector<ArrangementBar>("ArrangementBarVector")`
- `register_array<StepData, MAX_STEPS>("StepDataArray")`
- `register_array<DeviceState, NUM_DEVICES>("DeviceStateArray")`
- `register_array<PatternRef, NUM_DEVICES>("PatternRefArray")`

## Module-level symbols

The Emscripten module must export:

- `_malloc(size: number): number`
- `_free(ptr: number): void`
- `HEAPU8: Uint8Array`
- `emscriptenRegisterAudioObject(obj: AudioContext): number`
- `emscriptenGetAudioObject(handle: number): AudioObject`
- `rb338.RbsAudioEngine` (class constructor)
- `rb338.RbsParser` (class constructor)
- `rb338.initAudioWorklet` (function)

These are configured in `src/wasm/cpp/build.sh` via `-sEXPORTED_FUNCTIONS="['_malloc','_free']"` and `-sEXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue','emscriptenRegisterAudioObject','emscriptenGetAudioObject']"`. `HEAPU8` is created automatically when the module initialises.

## Change protocol

1. If you change a C++ struct, update the matching TypeScript interface in `src/wasm/types/wasm-audio.ts` and this contract.
2. If you change an Embind registration, update the `EngineModule` / instance interfaces in `src/wasm/types/wasm-audio.ts`.
3. If you add a new field, ensure it is present in both the C++ `value_object<>` registration and the TypeScript interface with the same name and compatible type.
4. Run `npx astro check` after any TypeScript change and `npm run wasm:build` after any C++ change.
