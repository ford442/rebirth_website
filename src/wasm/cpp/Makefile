# Thin wrappers around CMake / emcmake. Source lists live in sources.cmake.

CXX      ?= g++
ROOT      = ../../..
BUILD_DIR = build

.PHONY: all test wasm wasm-debug inspect clean

all: test

test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX)
	cmake --build $(BUILD_DIR) --parallel
	ctest --test-dir $(BUILD_DIR) --output-on-failure

wasm:
	bash build.sh release

wasm-debug:
	bash build.sh debug

inspect: $(BUILD_DIR)/rbs-inspect

$(BUILD_DIR)/rbs-inspect:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX)
	cmake --build $(BUILD_DIR) --target rbs-inspect --parallel

clean:
	rm -rf $(BUILD_DIR) build-wasm
