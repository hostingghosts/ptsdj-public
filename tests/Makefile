# Host unit tests: every tests/*_test.cpp is its own binary, linked against the
# portable sources (everything except the device HAL and device entry point).
CXX      ?= g++
ROOT     := ..
OUT      := $(ROOT)/build/host-tests
INCS     := -I$(ROOT)/atari_core -I$(ROOT)/src -I$(ROOT)/src/platform -I$(ROOT)/src/platform/core -I.
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined $(INCS) -MMD
LDFLAGS  := -fsanitize=address,undefined

PORTABLE := $(shell find $(ROOT)/src/atw $(ROOT)/src/platform/core -name '*.cpp' \
              ! -name 'device_*' 2>/dev/null)
ATARI_CORE := engine fastpokey voice bank presets instrument instedit insfile romdata seqmap songwalk export tia song json macsession macsession_read divcompute
OBJS     := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/obj/%.o,$(PORTABLE)) $(patsubst %,$(OUT)/obj/atari_core/%.o,$(ATARI_CORE))
TESTS    := $(wildcard *_test.cpp)
BINS     := $(patsubst %.cpp,$(OUT)/%,$(TESTS))

all: run

$(OUT)/obj/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/atari_core/%.o: $(ROOT)/atari_core/atari/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(subst -Werror,,$(CXXFLAGS)) -c $< -o $@

$(OUT)/libsidt.a: $(OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/%_test: %_test.cpp $(OUT)/libsidt.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(OUT)/libsidt.a $(LDFLAGS) -o $@

build: $(BINS)

run: $(BINS)
	@set -e; for t in $(BINS); do SIDT_ROOT=$(abspath $(ROOT)) $$t; done

clean:
	rm -rf $(OUT)

-include $(OBJS:.o=.d) $(BINS:=.d)
.PHONY: all build run clean
