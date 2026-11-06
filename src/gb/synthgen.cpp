#include "gb/synthgen.h"

#include <cstring>

#include "gb/sm83.h"

namespace gb {

namespace {

constexpr uint16_t kEntry = 0x68F9;      // bank 29: regenerate synth number (WRAM C441)
constexpr uint8_t kBank = 29;
constexpr uint16_t kSynthIndexAddr = 0xC441;
constexpr uint16_t kBankMirror = 0xFF8E; // HRAM byte LSDJ keeps the current bank in
constexpr uint16_t kFrame = 0x6035;      // bank 29: generate and store the frame numbered WRAM C9F0
constexpr uint16_t kFrameIndex = 0xC9F0;
constexpr uint16_t kJobFlag = 0xC9F1;    // non-zero while frames remain to be generated
constexpr uint16_t kReturn = 0x3FF0;     // never executed: the routine returns here

// The memory map of the cartridge as the routine sees it: banked ROM from the owner's image, cartridge RAM = the song memory.
struct Bus final : Sm83::Bus {
    const uint8_t* rom;
    size_t romSize;
    uint8_t* sram;                       // 32 KB: the song memory
    uint8_t wram[0x2000] = {};
    uint8_t hram[0x80] = {};
    uint8_t io[0x80] = {};
    uint8_t romBank = 1, ramBank = 0;

    uint8_t read(uint16_t a) override {
        if (a < 0x4000) return a < romSize ? rom[a] : 0xFF;
        if (a < 0x8000) { const size_t o = size_t(romBank) * 0x4000 + (a - 0x4000); return o < romSize ? rom[o] : 0xFF; }
        if (a >= 0xA000 && a < 0xC000) return sram[size_t(ramBank & 3) * 0x2000 + (a - 0xA000)];
        if (a >= 0xC000 && a < 0xE000) return wram[a - 0xC000];
        if (a >= 0xE000 && a < 0xFE00) return wram[a - 0xE000];
        if (a >= 0xFF80) return hram[a - 0xFF80];
        if (a >= 0xFF00) return io[a - 0xFF00];
        return 0xFF;
    }
    void write(uint16_t a, uint8_t v) override {
        if (a >= 0x2000 && a < 0x4000) { romBank = v; return; }
        if (a >= 0x4000 && a < 0x6000) { ramBank = v; return; }
        if (a >= 0xA000 && a < 0xC000) { sram[size_t(ramBank & 3) * 0x2000 + (a - 0xA000)] = v; return; }
        if (a >= 0xC000 && a < 0xE000) { wram[a - 0xC000] = v; return; }
        if (a >= 0xE000 && a < 0xFE00) { wram[a - 0xE000] = v; return; }
        if (a >= 0xFF80) { hram[a - 0xFF80] = v; return; }
        if (a >= 0xFF00) { io[a - 0xFF00] = v; return; }
    }
};

}  // namespace

bool SynthGen::regenerate(Song& song, int s) {
    if (!available()) return false;
    static Bus bus;                      // ~8 KB of WRAM image: kept off the stack
    bus.rom = rom_;
    bus.romSize = size_;
    bus.sram = song.mem();
    std::memset(bus.wram, 0, sizeof bus.wram);
    std::memset(bus.hram, 0, sizeof bus.hram);
    std::memset(bus.io, 0, sizeof bus.io);
    bus.romBank = kBank;
    bus.ramBank = 0;
    bus.wram[kSynthIndexAddr - 0xC000] = uint8_t(s & 15);
    bus.hram[kBankMirror - 0xFF80] = kBank;
    Sm83 cpu(bus);
    auto call = [&](uint16_t entry, uint32_t limit) {
        cpu.sp = 0xDFF0;
        cpu.sp = uint16_t(cpu.sp - 2);   // return address
        bus.write(cpu.sp, uint8_t(kReturn));
        bus.write(uint16_t(cpu.sp + 1), uint8_t(kReturn >> 8));
        cpu.pc = entry;
        return cpu.run(kReturn, limit);
    };
    bool ok = call(kEntry, 10u * 1000u * 1000u);      // sets up the 16 frames' parameters and starts the job
    // LSDJ's main loop then produces one frame per pass while the job flag is set: genFrame, frame counter + 1, job ends at 16.
    for (int guard = 0; ok && bus.wram[kJobFlag - 0xC000] && guard < 32; ++guard) {
        bus.romBank = kBank;
        ok = call(kFrame, 10u * 1000u * 1000u);
        if (++bus.wram[kFrameIndex - 0xC000] == 16) bus.wram[kJobFlag - 0xC000] = 0;
    }
    lastCount_ = cpu.executed();
    return ok && !bus.wram[kJobFlag - 0xC000];
}

}  // namespace gb
