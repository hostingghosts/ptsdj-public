// LSDJ song model: defaults, accessors, .sav layout. GB_DEMO_SAV=path also writes a demo song for tools/oracle/check_demo.py.
#include <cstdlib>
#include <vector>

#include "gb/song.h"
#include "test.h"

using namespace gb;

TEST(fresh_song_has_markers_and_empty_cells) {
    Song s;
    CHECK(s.at(0x1E78) == 'r' && s.at(0x1E79) == 'b');
    CHECK(s.at(0x3E80) == 'r' && s.at(0x7FF0) == 'r');
    CHECK_EQ(int(s.chainPhrase(5, 3)), 0xFF);
    CHECK_EQ(int(s.songCell(1, 0)), 0xFF);
    CHECK_EQ(int(s.phraseInstr(9, 9)), 0xFF);
    CHECK_EQ(int(s.groove(0)[0]), 6);
    CHECK_EQ(int(s.at(off::kTempo)), 0x80);
}

TEST(accessors_hit_the_documented_offsets) {
    Song s;
    s.note(2, 3) = 9;
    s.phraseCmd(1, 0) = 4;
    s.chainTrans(3, 1) = 7;
    CHECK_EQ(int(s.at(0x0000 + 2 * 16 + 3)), 9);
    CHECK_EQ(int(s.at(0x4000 + 16)), 4);
    CHECK_EQ(int(s.at(0x2880 + 3 * 16 + 1)), 7);
    s.markPhrase(16);
    s.markChain(1);
    CHECK_EQ(int(s.at(0x3E84)), 1);
    CHECK_EQ(int(s.at(0x3EA2)), 3);   // chain 0 is allocated in a fresh song
}

TEST(sav_roundtrip_and_header) {
    Song s;
    s.note(0, 0) = 5;
    std::vector<uint8_t> sav(kSavSize);
    s.writeSav(sav.data());
    CHECK_EQ(int(sav[0x813E]), 'j');
    CHECK_EQ(int(sav[0x813F]), 'k');
    CHECK_EQ(int(sav[0x8140]), 0xFF);
    CHECK_EQ(int(sav[0x8000]), 0);
    Song t;
    CHECK(t.readSav(sav.data(), sav.size()));
    CHECK_EQ(int(t.note(0, 0)), 5);
    sav[0x1E78] = 0;
    CHECK(!t.readSav(sav.data(), sav.size()));
}

TEST(demo_song_for_the_oracle) {
    const char* path = std::getenv("GB_DEMO_SAV");
    if (!path) return;
    Song s;
    for (int ch = 0; ch < 4; ++ch) {
        s.songCell(0, ch) = uint8_t(ch);
        s.chainPhrase(ch, 0) = uint8_t(ch);
        s.markChain(ch);
        s.markPhrase(ch);
        s.markInstrument(ch);
        s.instrument(ch)[0] = uint8_t(ch);           // pulse, pulse, wave, noise
        for (int st = 0; st < 4; ++st) {
            s.note(ch, st * 4) = uint8_t(13 + st * 2 + ch);
            s.phraseInstr(ch, st * 4) = uint8_t(ch);
        }
    }
    std::vector<uint8_t> sav(kSavSize);
    s.writeSav(sav.data());
    FILE* f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    if (f) { std::fwrite(sav.data(), 1, sav.size(), f); std::fclose(f); }
}
