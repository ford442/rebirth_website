# Single source list consumed by CMake (native + emcmake) and documented for Makefile.
# Do not add .cpp files anywhere else.

set(RB338_PARSER_SOURCES
  parser/RbsParser.cpp
  parser/ParsedSongJson.cpp
  parser/RbmParser.cpp
  parser/ParsedModJson.cpp
)

set(RB338_ENGINE_SOURCES
  engine/RbsAudioEngine.cpp
  engine/EngineCommands.cpp
  engine/EngineSnapshot.cpp
  engine/Sequencer.cpp
  engine/Mixer.cpp
  synth/Voice.cpp
  synth/DrumSynth.cpp
  synth/Tb303Voice.cpp
  synth/Tr808Voice.cpp
  synth/Tr909Voice.cpp
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
  tests/test_mixer.cpp
  tests/test_rbm_parser.cpp
)

set(RB338_INSPECT_SOURCES
  tools/rbs-inspect.cpp
)
