# GB tracker desktop simulator: the portable app + the host HALs in sim/src.
#   make -C sim          -> build/sim/gb-sim
#   make -C sim check    -> headless smoke run: screenshots and a WAV in build/sim/
CXX      ?= g++
VERSION  ?= 0.1.0
ROOT     := ..
OUT      := $(ROOT)/build/sim
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
CXXFLAGS := -DGB_BUILD -DGB_VERSION=\"$(VERSION)\" -std=c++17 -O2 -g -Wall -Wextra -Wno-format-truncation -MMD -I$(ROOT)/src -I$(ROOT)/src/platform \
            -I$(ROOT)/src/platform/core -Isrc $(SDL_CFLAGS)
LDLIBS   := $(SDL_LIBS) -lpthread

PORTABLE := $(shell find $(ROOT)/src/atw $(ROOT)/src/gb $(ROOT)/src/platform/core -name '*.cpp' ! -name 'device_*' 2>/dev/null)
CORE_OBJS := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/obj/%.o,$(PORTABLE))
SIM_SRCS  := $(filter-out src/host_midi.cpp src/sim_nvstore.cpp,$(wildcard src/*.cpp))
SIM_OBJS  := $(patsubst src/%.cpp,$(OUT)/obj/sim/%.o,$(SIM_SRCS))
BIN       := $(OUT)/gb-sim

all: $(BIN)

$(OUT)/obj/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/sim/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(CORE_OBJS) $(SIM_OBJS)
	$(CXX) $^ $(LDLIBS) -o $@

# Headless smoke test on a scratch SD folder: the first screen must be blank (every pixel the background colour).
check: $(BIN)
	rm -rf $(OUT)/check-sd
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $(BIN) --sd $(OUT)/check-sd \
	    --script scripts/blank_smoke.txt --wav $(OUT)/smoke.wav --screenshot $(OUT)/smoke-end.png
	python3 -I scripts/check_blank.py $(OUT)/blank.png 0x0F380F
	python3 -I scripts/check_wav.py $(OUT)/smoke.wav

clean:
	rm -rf $(OUT)

-include $(CORE_OBJS:.o=.d) $(SIM_OBJS:.o=.d)
.PHONY: all clean check
