// A small Game Boy CPU (SM83) interpreter. It exists to run LSDJ's own wave-synth generator routine out of the owner's ROM image
// (see synthgen.h): the routine is long and full of fixed-point quirks, and running it keeps the result exactly LSDJ's.
// Nothing of the ROM is stored in this repository; the image is read from the owner's SD card at run time.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gb {

class Sm83 {
public:
    struct Bus {
        virtual ~Bus() = default;
        virtual uint8_t read(uint16_t a) = 0;
        virtual void write(uint16_t a, uint8_t v) = 0;
    };
    explicit Sm83(Bus& b) : bus_(b) {}

    uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0xFFFE, pc = 0;
    // Runs until pc == stopAt (checked before each instruction) or `maxInstr` instructions; returns false on the limit.
    bool run(uint16_t stopAt, uint32_t maxInstr);
    uint32_t executed() const { return n_; }

private:
    Bus& bus_;
    uint32_t n_ = 0;
    uint8_t rd(uint16_t a) { return bus_.read(a); }
    void wr(uint16_t a, uint8_t v) { bus_.write(a, v); }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() { const uint8_t lo = fetch(); return uint16_t(lo | fetch() << 8); }
    uint16_t bc() const { return uint16_t(b << 8 | c); }
    uint16_t de() const { return uint16_t(d << 8 | e); }
    uint16_t hl() const { return uint16_t(h << 8 | l); }
    uint16_t af() const { return uint16_t(a << 8 | (f & 0xF0)); }
    void setBc(uint16_t v) { b = uint8_t(v >> 8); c = uint8_t(v); }
    void setDe(uint16_t v) { d = uint8_t(v >> 8); e = uint8_t(v); }
    void setHl(uint16_t v) { h = uint8_t(v >> 8); l = uint8_t(v); }
    void setAf(uint16_t v) { a = uint8_t(v >> 8); f = uint8_t(v & 0xF0); }
    void push(uint16_t v) { wr(--sp, uint8_t(v >> 8)); wr(--sp, uint8_t(v)); }
    uint16_t pop() { const uint8_t lo = rd(sp++); return uint16_t(lo | rd(sp++) << 8); }
    uint8_t getR(int r);                  // 0 B, 1 C, 2 D, 3 E, 4 H, 5 L, 6 (HL), 7 A
    void setR(int r, uint8_t v);
    uint16_t getRp(int p, bool af);       // 0 BC, 1 DE, 2 HL, 3 SP / AF
    void setRp(int p, uint16_t v, bool af);
    bool cond(int cc) const;
    void alu(int op, uint8_t v);
    uint8_t cb(uint8_t op, uint8_t v);
    void step();
    enum : uint8_t { kZ = 0x80, kN = 0x40, kH = 0x20, kC = 0x10 };
};

}  // namespace gb
