// Older-song upgrade: the rules that were measured against LSDJ's own conversion.
#include "gb/upgrade.h"
#include "test.h"

using namespace gb;

TEST(current_format_is_left_alone) {
    Song s;
    s.note(0, 0) = 25;
    s.phraseCmd(0, 0) = 4;
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.phraseCmd(0, 0)), 4);
}

TEST(unknown_versions_are_refused) {
    Song s;
    s.mem()[0x7FFF] = 3;
    CHECK(!upgradeSong(s));
    s.mem()[0x7FFF] = 0x40;
    CHECK(!upgradeSong(s));
}

TEST(version_7_commands_move_up_one_letter) {
    Song s;
    s.mem()[0x7FFF] = 7;
    s.phraseCmd(0, 0) = 1;      // A stays
    s.phraseCmd(0, 1) = 4;      // old D -> new D (code 5 in the new list... measured: 4 -> 5)
    s.phraseCmd(0, 2) = 17;
    s.mem()[off::kTblCmd1] = 8;
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.phraseCmd(0, 0)), 1);
    CHECK_EQ(int(s.phraseCmd(0, 1)), 5);
    CHECK_EQ(int(s.phraseCmd(0, 2)), 18);
    CHECK_EQ(int(s.mem()[off::kTblCmd1]), 9);
    CHECK_EQ(int(s.mem()[0x7FFF]), 0x16);
}

TEST(envelope_release_digit_and_wave_fields_convert) {
    Song s;
    s.mem()[0x7FFF] = 9;
    uint8_t* p = s.instrument(1);
    p[0] = 0; p[1] = 0x68; p[2] = 0x20; p[4] = 0x11;            // pulse, release 8 (none) -> 0
    uint8_t* w = s.instrument(2);
    w[0] = 1; w[1] = 0xA8; w[2] = 0x30; w[9] = 1;                // wave: synth 3, play mode 1 -> 2
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.instrument(1)[1]), 0x60);
    CHECK_EQ(int(s.instrument(2)[3]), 0x30);
    CHECK_EQ(int(s.instrument(2)[9]), 2);
}

TEST(old_default_pulse_instrument_becomes_the_new_default) {
    Song s;
    s.mem()[0x7FFF] = 9;
    uint8_t* p = s.instrument(5);
    const uint8_t old[16] = {0, 0, 0x88, 0, 0, 0x7F, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(p, old, 16);
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.instrument(5)[1]), 0xA0);
    CHECK_EQ(int(s.instrument(5)[4]), 0xFF);
    CHECK_EQ(int(s.instrument(5)[10]), 0xD0);
}

TEST(noise_notes_follow_the_old_shape_byte) {
    Song s;
    s.mem()[0x7FFF] = 9;
    uint8_t* n = s.instrument(3);
    n[0] = 3; n[4] = 0x6F;                                  // old noise instrument, shape byte 0x6F
    s.note(0, 0) = 37; s.phraseInstr(0, 0) = 3;             // octave 3: the shape byte is used as is -> new note 32 (measured)
    s.note(0, 1) = 1;  s.phraseInstr(0, 1) = 3;             // three octaves down: high nibble 6 -> 3 -> new note 20
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.note(0, 0)), 32);
    CHECK_EQ(int(s.note(0, 1)), 20);
}

TEST(retrigger_period_counts_from_one_now) {
    Song s;
    s.mem()[0x7FFF] = 9;
    s.phraseCmd(0, 0) = 14; s.phraseVal(0, 0) = 0x51;
    s.phraseCmd(0, 1) = 14; s.phraseVal(0, 1) = 0x8A;       // R8y stays
    s.phraseCmd(0, 2) = 14; s.phraseVal(0, 2) = 0x2F;       // once stays
    CHECK(upgradeSong(s));
    CHECK_EQ(int(s.phraseVal(0, 0)), 0x52);
    CHECK_EQ(int(s.phraseVal(0, 1)), 0x8A);
    CHECK_EQ(int(s.phraseVal(0, 2)), 0x2F);
}
