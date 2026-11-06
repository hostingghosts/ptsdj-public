// The SM83 interpreter and the wave synth generator. The generator tests need the LSDJ ROM the owner keeps in reference/ (never
// committed); without it they are skipped.
#include <cstdio>
#include <string>
#include <vector>

#include "gb/sm83.h"
#include "gb/synthgen.h"
#include "test.h"

using namespace gb;

namespace {

struct Flat final : Sm83::Bus {
    uint8_t m[0x10000] = {};
    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
};

bool loadRom(std::vector<uint8_t>& rom) {
    FILE* f = std::fopen("../reference/oracle/run/lsdj.gb", "rb");
    if (!f) return false;
    rom.resize(1u << 20);
    rom.resize(std::fread(rom.data(), 1, rom.size(), f));
    std::fclose(f);
    return rom.size() >= (1u << 19);
}

std::string frameHex(Song& s, int f) {
    std::string out;
    const char* hx = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) { out += hx[s.wave(f)[i] >> 4]; out += hx[s.wave(f)[i] & 15]; }
    return out;
}

}  // namespace

TEST(sm83_arithmetic_flags_and_stack) {
    Flat b;
    // LD A,0x0F; ADD A,0x01 (H set); PUSH AF; POP BC; LD HL,0x1234; CALL 0x0100; JP 0x0200 ... 0x0100: INC H; RET
    const uint8_t prog[] = {0x3E, 0x0F, 0xC6, 0x01, 0xF5, 0xC1, 0x21, 0x34, 0x12, 0xCD, 0x00, 0x01, 0xC3, 0x00, 0x02};
    for (size_t i = 0; i < sizeof prog; ++i) b.m[i] = prog[i];
    b.m[0x100] = 0x24; b.m[0x101] = 0xC9;
    Sm83 c(b);
    c.sp = 0xFFF0;
    CHECK(c.run(0x0200, 100));
    CHECK_EQ(int(c.a), 0x10);
    CHECK_EQ(int(c.b), 0x10);            // A pushed with its flags: H set, Z clear
    CHECK_EQ(int(c.c & 0xF0), 0x20);
    CHECK_EQ(int(c.h), 0x13);
}

TEST(sm83_rotates_and_bit_ops) {
    Flat b;
    // LD A,0x81; RLCA (A=03, C); LD B,0x80; SRL B (B=40, C=0); BIT 6,B (Z clear); SET 0,B (41); SWAP B (14)
    const uint8_t prog[] = {0x3E, 0x81, 0x07, 0x06, 0x80, 0xCB, 0x38, 0xCB, 0x70, 0xCB, 0xC0, 0xCB, 0x30};
    for (size_t i = 0; i < sizeof prog; ++i) b.m[i] = prog[i];
    Sm83 c(b);
    CHECK(c.run(sizeof prog, 100));
    CHECK_EQ(int(c.a), 0x03);
    CHECK_EQ(int(c.b), 0x14);
}

TEST(synth_generator_matches_lsdj_frames) {
    std::vector<uint8_t> rom;
    if (!loadRom(rom)) return;           // no ROM here: nothing to compare with
    SynthGen g;
    g.setRom(rom.data(), rom.size());
    CHECK(g.available());
    Song s;
    // measured on LSDJ 9.4.2 with the default synth (cutoff FF, volume 10, clip, pinch) and only the signal changed
    s.synth(0)[0] = 1;
    CHECK(g.regenerate(s, 0));
    CHECK(frameHex(s, 0) == "74544444444444547babbbbbbbbbbbab");
    CHECK(frameHex(s, 15) == frameHex(s, 0));
    s.synth(0)[0] = 2;
    CHECK(g.regenerate(s, 0));
    CHECK(frameHex(s, 0) == "77665443334456677899abbcccbba998");
    s.synth(0)[0] = 0;
    CHECK(g.regenerate(s, 0));
    CHECK(frameHex(s, 3) == "7132334445556677788999aaabbccdce");
    // a synth only owns its own 16 frames
    s.synth(2)[0] = 1;
    CHECK(g.regenerate(s, 2));
    CHECK(frameHex(s, 32) == "74544444444444547babbbbbbbbbbbab");
    CHECK(frameHex(s, 3) == "7132334445556677788999aaabbccdce");
}

TEST(synth_generator_morphs_between_start_and_end) {
    std::vector<uint8_t> rom;
    if (!loadRom(rom)) return;
    SynthGen g;
    g.setRom(rom.data(), rom.size());
    Song s;
    uint8_t* p = s.synth(1);
    p[6] = 0x20; p[10] = 0xFF;           // cutoff opens from 20 to FF over the 16 frames
    CHECK(g.regenerate(s, 1));
    CHECK(frameHex(s, 16) != frameHex(s, 31));
}

TEST(synth_generator_without_rom_does_nothing) {
    SynthGen g;
    Song s;
    CHECK(!g.available());
    CHECK(!g.regenerate(s, 0));
}
