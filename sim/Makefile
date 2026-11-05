# SIDTracker desktop simulator: the portable app + the host HALs in sim/src.
#   make -C sim          -> build/sim/sidtracker-sim
#   make -C sim check    -> headless smoke run: screenshots and a WAV in build/sim/
CXX      ?= g++
ROOT     := ..
OUT      := $(ROOT)/build/sim
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
ATARI_SRC := $(ROOT)/atari_core/atari
CXXFLAGS := -DATARI_BUILD -I$(ROOT)/atari_core -std=c++17 -O2 -g -Wall -Wextra -Wno-format-truncation -MMD -I$(ROOT)/src -I$(ROOT)/src/platform \
            -I$(ROOT)/src/platform/core -Isrc $(SDL_CFLAGS)
LDLIBS   := $(SDL_LIBS) -lpthread

PORTABLE := $(shell find $(ROOT)/src/atw $(ROOT)/src/platform/core -name '*.cpp' ! -name 'device_*' 2>/dev/null)
CORE_OBJS := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/obj/%.o,$(PORTABLE))
SIM_SRCS  := $(filter-out src/host_midi.cpp src/sim_nvstore.cpp,$(wildcard src/*.cpp))
SIM_OBJS  := $(patsubst src/%.cpp,$(OUT)/obj/sim/%.o,$(SIM_SRCS))
ATARI_OBJS := $(patsubst $(ATARI_SRC)/%.cpp,$(OUT)/obj/atari/%.o,$(addprefix $(ATARI_SRC)/,engine.cpp fastpokey.cpp voice.cpp bank.cpp presets.cpp instrument.cpp instedit.cpp insfile.cpp romdata.cpp seqmap.cpp songwalk.cpp tia.cpp json.cpp macsession.cpp))
BIN       := $(OUT)/sidtracker-sim

all: $(BIN)

$(OUT)/obj/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/atari/%.o: $(ATARI_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/sim/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(CORE_OBJS) $(SIM_OBJS) $(ATARI_OBJS)
	$(CXX) $^ $(LDLIBS) -o $@

# Headless smoke test on a scratch SD folder: screenshots of every screen,
# a WAV of the played song, and the saved project.
check: $(BIN)
	rm -rf $(OUT)/check-sd
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $(BIN) --sd $(OUT)/check-sd \
	    --script scripts/atari_smoke.txt --wav $(OUT)/smoke.wav --screenshot $(OUT)/smoke-end.png
	python3 -I scripts/check_wav.py $(OUT)/smoke.wav
	test -f $(OUT)/check-sd/AtariTracker/Projects/*.atr

clean:
	rm -rf $(OUT)

-include $(CORE_OBJS:.o=.d) $(SIM_OBJS:.o=.d) $(ATARI_OBJS:.o=.d)
.PHONY: all clean check
