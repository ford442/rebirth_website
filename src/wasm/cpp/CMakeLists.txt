cmake_minimum_required(VERSION 3.16)

# Ubuntu images often symlink `c++` to Clang without libstdc++ dev libs.
# Prefer g++ unless the caller already set CMAKE_CXX_COMPILER / CXX.
if(NOT EMSCRIPTEN AND NOT DEFINED ENV{CXX} AND NOT CMAKE_CXX_COMPILER)
  find_program(RB338_GXX NAMES g++)
  if(RB338_GXX)
    set(CMAKE_CXX_COMPILER "${RB338_GXX}" CACHE PATH "C++ compiler" FORCE)
  endif()
endif()

project(rb338_native LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RB338_ASAN "Build native tests with AddressSanitizer" OFF)
option(RB338_UBSAN "Build native tests with UndefinedBehaviorSanitizer" OFF)
option(RB338_TSAN "Build native tests with ThreadSanitizer" OFF)
option(RB338_NO_EXCEPTIONS "Compile rb338_engine with -fno-exceptions (spike only)" OFF)

if(RB338_TSAN AND (RB338_ASAN OR RB338_UBSAN))
  message(FATAL_ERROR "RB338_TSAN cannot be combined with ASan/UBSan")
endif()

if(RB338_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
if(RB338_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()
if(RB338_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(EMSCRIPTEN)
  add_compile_options(-pthread -msimd128)
endif()

# Repository root — native tests load fixtures from src/wasm/test-fixtures/.
get_filename_component(RB338_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

include("${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake")

add_library(rb338_parser STATIC ${RB338_PARSER_SOURCES})
target_include_directories(rb338_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(rb338_engine STATIC ${RB338_ENGINE_SOURCES})
target_link_libraries(rb338_engine PUBLIC rb338_parser)
target_include_directories(rb338_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(RB338_NO_EXCEPTIONS)
  target_compile_options(rb338_engine PRIVATE -fno-exceptions)
endif()

# Type-check the worklet translation unit on every CMake build.
# Native builds use header stubs; emcmake uses the real Emscripten SDK.
add_library(rb338_worklet OBJECT ${RB338_WORKLET_SOURCES})
target_link_libraries(rb338_worklet PUBLIC rb338_engine)
if(NOT EMSCRIPTEN)
  target_include_directories(rb338_worklet SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native_stubs")
endif()

if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  add_executable(rbs_native_tests ${RB338_TEST_SOURCES})
  target_link_libraries(rbs_native_tests PRIVATE rb338_engine Threads::Threads)
  # doctest is third-party — do not fail the tree on its pedantic warnings.
  target_include_directories(rbs_native_tests SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

  add_executable(rbs-inspect ${RB338_INSPECT_SOURCES})
  target_link_libraries(rbs-inspect PRIVATE rb338_parser)

  add_executable(rbm-inspect tools/rbm-inspect.cpp)
  target_link_libraries(rbm-inspect PRIVATE rb338_parser)

  enable_testing()
  add_test(
    NAME rbs_native_tests
    COMMAND rbs_native_tests
    WORKING_DIRECTORY "${RB338_REPO_ROOT}"
  )
  set_tests_properties(rbs_native_tests PROPERTIES LABELS "native")
endif()

if(EMSCRIPTEN)
  set(RB338_WASM_OUT "${RB338_REPO_ROOT}/public/wasm" CACHE PATH "WASM output directory")
  add_executable(rbsParser ${RB338_WASM_MAIN} ${RB338_WORKLET_SOURCES})
  target_link_libraries(rbsParser PRIVATE rb338_engine)
  target_compile_options(rbsParser PRIVATE -msimd128)
  target_link_options(rbsParser PRIVATE
    -pthread
    -sAUDIO_WORKLET=1
    -sWASM_WORKERS=1
    -sEXPORT_ES6=1
    -sMODULARIZE=1
    -sEXPORT_NAME=RbsAudioModule
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','HEAPU8','emscriptenRegisterAudioObject','emscriptenGetAudioObject']"
    -sENVIRONMENT=web,worker
    -sALLOW_TABLE_GROWTH=1
    # WASM module linear stack (main thread + synchronous WASM calls).
    # Not the AudioWorklet pthread stack — see engine/AudioThreadLimits.h.
    -sSTACK_SIZE=131072
    --no-entry
    -lembind
  )
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(rbsParser PRIVATE -O0 -g3)
    target_link_options(rbsParser PRIVATE
      -sASSERTIONS=1
      -sSAFE_HEAP=1
      -sSTACK_OVERFLOW_CHECK=2
      -sWEBAUDIO_DEBUG=1
      -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=33554432
      -sMAXIMUM_MEMORY=134217728
    )
  else()
    target_compile_options(rbsParser PRIVATE -O3 -flto)
    target_link_options(rbsParser PRIVATE
      -O3
      -flto
      -sASSERTIONS=0
      -sALLOW_MEMORY_GROWTH=0
      -sINITIAL_MEMORY=67108864
    )
  endif()
  set_target_properties(rbsParser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${RB338_WASM_OUT}"
    SUFFIX ".js"
  )
endif()
