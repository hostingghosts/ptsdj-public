// Wave synth frame generation. LSDJ turns a synth's parameters (signal, filter, distortion, phase, start/end volume, cutoff, Q, vshift,
// limit, phase) into the 16 wave frames the Wave instrument plays. The routine that does it is long and quirky (fixed-point tables,
// 16-bit shifts with their own rounding), so this runs it exactly as LSDJ does: the owner's ROM image (read from the SD card, like the
// sample kits) in a small SM83 interpreter (sm83.h), with the song memory standing in for the cartridge RAM. No ROM bytes live in this
// repository. Without a ROM the Synth screen still edits parameters but cannot regenerate frames.
#pragma once

#include <cstddef>
#include <cstdint>

#include "gb/song.h"

namespace gb {

class SynthGen {
public:
    void setRom(const uint8_t* rom, size_t size) { rom_ = rom; size_ = size; }
    bool available() const { return rom_ != nullptr && size_ >= (1u << 19); }
    // Regenerates the 16 frames (16 * synth .. 16 * synth + 15) of synth `s` in `song` from its parameter bytes. False without a ROM.
    bool regenerate(Song& song, int s);
    uint32_t lastInstructions() const { return lastCount_; }
    // Optional CPU cycle counter (the device's DWT): the cycles the last regeneration took, shown on the Synth screen for hardware timing.
    void setCounter(uint32_t (*c)()) { counter_ = c; }
    uint32_t lastCycles() const { return lastCycles_; }

private:
    const uint8_t* rom_ = nullptr;
    size_t size_ = 0;
    uint32_t lastCount_ = 0, lastCycles_ = 0;
    uint32_t (*counter_)() = nullptr;
};

}  // namespace gb
