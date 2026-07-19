# ReBirth RB-338 WASM / native C++ build helpers
# Run from src/wasm/cpp/

CXX      ?= g++
CXXFLAGS  = -std=c++17 -Wall -Wextra -O2 -I.
ROOT      = ../../..
BUILD_DIR = build

PARSER_SRC = parser/RbsParser.cpp
ENGINE_SRCS = engine/RbsAudioEngine.cpp engine/Sequencer.cpp engine/Mixer.cpp \
              synth/Voice.cpp synth/Tb303Voice.cpp synth/Tr808Voice.cpp synth/Tr909Voice.cpp
TEST_SRCS  = tests/test_main.cpp tests/test_parser.cpp tests/test_sequencer.cpp tests/test_engine.cpp
TEST_BIN   = $(BUILD_DIR)/test_rbs_parser

.PHONY: all test wasm clean inspect

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(PARSER_SRC) $(ENGINE_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PARSER_SRC) $(ENGINE_SRCS) $(TEST_SRCS) -o $@

test: $(TEST_BIN)
	cd $(ROOT) && ./src/wasm/cpp/$(TEST_BIN)

wasm:
	bash build.sh release

wasm-debug:
	bash build.sh debug

# Optional CLI dump tool (built only on demand)
inspect: $(BUILD_DIR)/rbs-inspect

$(BUILD_DIR)/rbs-inspect: tools/rbs-inspect.cpp $(PARSER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PARSER_SRC) tools/rbs-inspect.cpp -o $@

clean:
	rm -rf $(BUILD_DIR)
