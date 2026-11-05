# Atari tracker firmware for the original Polyend Tracker hardware (MK66FX1M0).
#   make            -> build/sidtracker.hex and build/PolyendTracAtariv<ver>.ptf (24 chars)
#   make SELFTEST=1 -> build/selftest/PolyendTracker_STst<N>.ptf: hardware selftest only
#   make host-test  -> host unit tests (ASan/UBSan)
#   make sim        -> desktop simulator (SDL2)
#   make check      -> host tests + simulator smoke run + device image
VERSION   ?= 0.1.0
BUILD     := build
DIAG_N    ?= 1
VERNUM    := $(subst .,,$(VERSION))
ifeq ($(SELFTEST),1)
BUILD     := build/selftest
DEFS_EXTRA := -DOT_FORCE_SELFTEST=1
PTF_NAME  := PolyendTracker_STst$(DIAG_N).ptf
else
PTF_NAME  := PolyendTracAtariv$(VERNUM).ptf
endif
PTF       := $(BUILD)/$(PTF_NAME)
# The stock update bootloader ignores image names that are not exactly 24
# characters (and then installs a random file); it lists only
# Firmware/PolyendTrac*.ptf (docs/firmware-map.md).
ifneq ($(shell printf '%s' '$(PTF_NAME)' | wc -c | tr -d ' '),24)
$(error image name '$(PTF_NAME)' must be exactly 24 characters (docs/firmware-map.md))
endif
# USB identity (DECISIONS.md 4): pid.codes test PID during development,
# product string "SIDTracker" (src/platform/PROVENANCE.md local change 3).
# RELEASE BLOCKER: register a dedicated pid.codes PID before a public release.
USB_ID    := -DOT_USB_VID=0x1209 -DOT_USB_PID=0x0001 -DSIDT_USB_STRINGS
CORE      := third_party/teensy3
CROSS     ?= arm-none-eabi-
CC        := $(CROSS)gcc
CXX       := $(CROSS)g++
OBJCOPY   := $(CROSS)objcopy
SIZE      := $(CROSS)size

CPUFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS      := -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI_SERIAL -DLAYOUT_US_ENGLISH \
             -DF_BUS=90000000 -DSERIAL5_TX_BUFFER_SIZE=512 -DSERIAL5_RX_BUFFER_SIZE=512 \
             -DTEENSYDUINO=159 -DARDUINO=10819 -DOT_VERSION=\"$(VERSION)\" -DOT_TARGET_DEVICE=1 \
             -DSIDT_VERSION=\"$(VERSION)\" -DATARI_BUILD $(USB_ID) $(DEFS_EXTRA)
INCS      := -Iatari_core -I$(CORE) -Isrc -Isrc/platform -Isrc/platform/core -Isrc/platform/display -Isrc/platform/input \
             -Isrc/platform/audio -Isrc/platform/storage -Isrc/platform/timer -Ithird_party/fatfs
COMMON    := $(CPUFLAGS) $(DEFS) $(INCS) -O2 -g -ffunction-sections -fdata-sections -Wall -MMD
CFLAGS    := $(COMMON) -std=gnu11
CXXFLAGS  := $(COMMON) -std=gnu++17 -fno-exceptions -fno-rtti -felide-constructors -Wno-psabi
LDFLAGS   := $(CPUFLAGS) -O2 -Wl,--gc-sections,--defsym=__rtc_localtime=0 -T$(CORE)/mk66fx1m0.ld \
             --specs=nano.specs
LIBS      := -lm -lstdc++

CORE_C    := $(wildcard $(CORE)/*.c)
CORE_CXX  := $(filter-out $(CORE)/main.cpp,$(wildcard $(CORE)/*.cpp))
APP_C     := third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
APP_CXX   := $(shell find src -name '*.cpp') $(addprefix atari_core/atari/,engine.cpp fastpokey.cpp voice.cpp bank.cpp presets.cpp instrument.cpp instedit.cpp insfile.cpp romdata.cpp seqmap.cpp songwalk.cpp tia.cpp json.cpp macsession.cpp)
OBJS      := $(patsubst %,$(BUILD)/%.o,$(CORE_C) $(CORE_CXX) $(APP_C) $(APP_CXX))

all: $(BUILD)/sidtracker.hex $(PTF)

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sidtracker.elf: $(OBJS)
	$(CXX) $(LDFLAGS) $^ -L$(CORE) $(LIBS) -o $@ -Wl,-Map=$(BUILD)/sidtracker.map
	$(SIZE) $@

$(BUILD)/sidtracker.hex: $(BUILD)/sidtracker.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Every image passes the bootloader checks (tools/check_image.py) before it
# gets its .ptf name.
$(PTF): $(BUILD)/sidtracker.hex
	python3 -I tools/check_image.py $<
	cp $< $@

host-test:
	$(MAKE) -C tests

sim:
	$(MAKE) -C sim

check: host-test all
	$(MAKE) -C sim check
	tools/sync_hal.sh

clean:
	rm -rf build

-include $(OBJS:.o=.d)

.PHONY: all clean host-test sim check
