cmake_minimum_required(VERSION 3.16)

# Ubuntu images often symlink `c++` to Clang without libstdc++ dev libs.
# Prefer g++ unless the caller already set CMAKE_CXX_COMPILER / CXX.
if(NOT DEFINED ENV{CXX} AND NOT CMAKE_CXX_COMPILER)
  find_program(RB338_GXX NAMES g++)
  if(RB338_GXX)
    set(CMAKE_CXX_COMPILER "${RB338_GXX}" CACHE PATH "C++ compiler" FORCE)
  endif()
endif()

project(rb338_native LANGUAGES CXX)

option(RB338_ASAN "Build native tests with AddressSanitizer" OFF)
if(RB338_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Repository root — native tests load fixtures from src/wasm/test-fixtures/.
get_filename_component(RB338_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# Shared translation units (also compiled by build.sh for Emscripten).
set(RB338_PARSER_SOURCES
  parser/RbsParser.cpp
  parser/ParsedSongJson.cpp
)

set(RB338_ENGINE_SOURCES
  engine/RbsAudioEngine.cpp
  engine/Sequencer.cpp
  engine/Mixer.cpp
  synth/Voice.cpp
  synth/DrumSynth.cpp
  synth/Tb303Voice.cpp
  synth/Tr808Voice.cpp
  synth/Tr909Voice.cpp
)

set(RB338_TEST_SOURCES
  tests/test_main.cpp
  tests/test_parser.cpp
  tests/test_sequencer.cpp
  tests/test_engine.cpp
  tests/test_drums.cpp
  tests/test_tb303.cpp
  tests/test_mixer.cpp
)

add_library(rb338_parser STATIC ${RB338_PARSER_SOURCES})
target_include_directories(rb338_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(rb338_engine STATIC ${RB338_ENGINE_SOURCES})
target_link_libraries(rb338_engine PUBLIC rb338_parser)
target_include_directories(rb338_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(rbs_native_tests ${RB338_TEST_SOURCES})
target_link_libraries(rbs_native_tests PRIVATE rb338_engine)

add_executable(rbs-inspect tools/rbs-inspect.cpp)
target_link_libraries(rbs-inspect PRIVATE rb338_parser)

enable_testing()
add_test(
  NAME rbs_native_tests
  COMMAND rbs_native_tests
  WORKING_DIRECTORY "${RB338_REPO_ROOT}"
)
set_tests_properties(rbs_native_tests PROPERTIES LABELS "native")
