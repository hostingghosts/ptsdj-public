// Game Boy APU: mixing scale, duty, frequency, envelope zombie writes (values measured with tools/oracle).
#include "gb/apu.h"
#include "test.h"

using namespace gb;

namespace {
void powerOn(Apu& a) {
    a.write(0xFF26, 0x80);
    a.write(0xFF25, 0xFF);
    a.write(0xFF24, 0x77);
}
}  // namespace

TEST(silent_apu_mixes_zero_and_dac_only_is_minus_fifteen) {
    Apu a;
    powerOn(a);
    int l, r;
    a.mixNow(l, r);
    CHECK_EQ(l, 0);
    a.write(0xFF12, 0xF0);                      // DAC on, not triggered: level 0 -> (2*0-15) * 8
    a.mixNow(l, r);
    CHECK_EQ(l, -15 * 8);
    CHECK_EQ(r, -15 * 8);
}

TEST(pulse_volume_and_duty_scale) {
    Apu a;
    powerOn(a);
    a.write(0xFF11, 0x80);                      // 50 %
    a.write(0xFF12, 0x80);                      // volume 8
    a.write(0xFF13, 0x00);
    a.write(0xFF14, 0x87);                      // trigger, freq 0x700
    int hi = 0, lo = 0;
    for (int i = 0; i < 4096; ++i) {
        a.advance(2);
        int l, r;
        a.mixNow(l, r);
        if (l == (2 * 8 - 15) * 8) ++hi; else if (l == -15 * 8) ++lo; else CHECK(false);
    }
    CHECK(hi > 1500 && lo > 1500);              // half and half
}

TEST(pulse_frequency_is_131072_over_2048_minus_f) {
    Apu a;
    powerOn(a);
    a.write(0xFF11, 0x80);
    a.write(0xFF12, 0xF0);
    a.write(0xFF13, 0x00);
    a.write(0xFF14, 0x84);                      // f = 0x400 = 1024 -> 131072/1024 = 128 Hz
    int edges = 0, prev = 0;
    for (uint32_t i = 0; i < kClock / 2; ++i) {   // one second, 2 cycles at a time
        a.advance(2);
        int l, r;
        a.mixNow(l, r);
        const int hi = l > 0;
        if (hi && !prev) ++edges;
        prev = hi;
    }
    CHECK(edges >= 127 && edges <= 129);
}

TEST(zombie_volume_sequence_used_by_lsdj_ends_at_14) {
    Apu a;
    powerOn(a);
    a.write(0xFF11, 0x80);
    a.write(0xFF12, 0xF8);                      // volume 15, up, no hardware envelope
    a.write(0xFF13, 0xFF);
    a.write(0xFF14, 0x87);
    a.write(0xFF12, 0x09);
    a.write(0xFF12, 0x11);
    a.write(0xFF12, 0x18);
    CHECK_EQ(a.channelLevel(0) == 0 || a.channelLevel(0) == 14, true);
    // run to a duty-high step and read the level
    int level = -1;
    for (int i = 0; i < 20000 && level <= 0; ++i) { a.advance(2); level = a.channelLevel(0); }
    CHECK_EQ(level, 14);
}

TEST(wave_channel_follows_ram_and_volume_code) {
    Apu a;
    powerOn(a);
    for (int i = 0; i < 16; ++i) a.write(0xFF30 + i, 0xFF);   // all 15
    a.write(0xFF1A, 0x80);
    a.write(0xFF1C, 0x20);                      // 100 %
    a.write(0xFF1D, 0x00);
    a.write(0xFF1E, 0x87);
    a.advance(100);
    CHECK_EQ(a.channelLevel(2), 15);
    a.write(0xFF1C, 0x40);                      // 50 %
    CHECK_EQ(a.channelLevel(2), 7);
}

TEST(skipping_the_silent_noise_register_gives_exactly_the_same_sound_as_stepping_it) {
    Apu lazy, exact;
    exact.setLazyNoise(false);
    uint32_t rng = 12345;
    auto rnd = [&rng](uint32_t n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; };
    auto both = [&](uint16_t a, uint8_t v) { lazy.write(a, v); exact.write(a, v); };
    powerOn(lazy);
    powerOn(exact);
    int checked = 0;
    for (int i = 0; i < 6000; ++i) {
        switch (rnd(8)) {
            case 0: both(0xFF21, uint8_t(rnd(16) << 4 | (rnd(3) == 0 ? 8 : 0) | rnd(8))); break;       // envelope: volume, direction, pace (many go silent)
            case 1: both(0xFF21, uint8_t(rnd(2) ? 0x00 : 0x09)); break;                                  // the kill sequence's writes
            case 2: both(0xFF22, uint8_t(rnd(14) << 4 | (rnd(2) ? 8 : 0) | rnd(8))); break;            // pitch, long or short register
            case 3: both(0xFF23, uint8_t(0x80 | (rnd(3) == 0 ? 0x40 : 0))); break;                      // trigger
            case 4: both(0xFF20, uint8_t(rnd(64))); break;
            case 5: both(0xFF25, uint8_t(rnd(256))); break;
            default: break;
        }
        const uint32_t cycles = rnd(5) == 0 ? rnd(300000) + 1 : rnd(2000) + 1;
        lazy.advance(cycles);
        exact.advance(cycles);
        float l0, r0, l1, r1;
        lazy.takeAverage(cycles, l0, r0);
        exact.takeAverage(cycles, l1, r1);
        CHECK(l0 == l1 && r0 == r1);
        CHECK_EQ(lazy.channelLevel(3), exact.channelLevel(3));
        ++checked;
    }
    CHECK_EQ(checked, 6000);
}
