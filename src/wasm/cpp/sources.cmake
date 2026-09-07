# Single source list consumed by CMake (native + emcmake) and documented for Makefile.
# Do not add .cpp files anywhere else.

set(RB338_PARSER_SOURCES
  parser/RbsParser.cpp
  parser/RbsMidiContainer.cpp
  parser/ParsedSongJson.cpp
  parser/RbmParser.cpp
  parser/ParsedModJson.cpp
)

set(RB338_ENGINE_SOURCES
  engine/RbsAudioEngine.cpp
  engine/EngineCommands.cpp
  engine/EngineSnapshot.cpp
  engine/Sequencer.cpp
  engine/AutomationScheduler.cpp
  engine/Mixer.cpp
  audio/SampleDecoder.cpp
  audio/AiffDecoder.cpp
  audio/WavDecoder.cpp
  audio/WavWriter.cpp
  synth/Voice.cpp
  synth/DrumSynth.cpp
  synth/SamplePool.cpp
  synth/Tb303Voice.cpp
  synth/Tr808Voice.cpp
  synth/Tr909Voice.cpp
)

# Vendored dr_wav (public domain / MIT-0) lives in its own translation unit
# so it can be compiled without the project's -Werror warning set.
set(RB338_VENDOR_SOURCES
  audio/DrWavImpl.cpp
)

set(RB338_WORKLET_SOURCES
  worklet/RbsWorklet.cpp
)

set(RB338_WASM_MAIN
  main.cpp
)

set(RB338_TEST_SOURCES
  tests/test_main.cpp
  tests/test_parser.cpp
  tests/test_sequencer.cpp
  tests/test_engine.cpp
  tests/test_drums.cpp
  tests/test_tb303.cpp
  tests/test_tb303_golden.cpp
  tests/test_dsp.cpp
  tests/test_offline.cpp
  tests/test_sample_decoder.cpp
  tests/test_sample_pool.cpp
  tests/test_mod_playback.cpp
  tests/test_mixer.cpp
  tests/test_rbm_parser.cpp
)

set(RB338_INSPECT_SOURCES
  tools/rbs-inspect.cpp
)

set(RB338_RBM_INSPECT_SOURCES
  tools/rbm-inspect.cpp
)
