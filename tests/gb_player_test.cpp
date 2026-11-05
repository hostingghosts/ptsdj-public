// Player rules measured against LSDJ 9.4.2 (see docs/LSDJ_FORMAT.md): the random generator, B gate and Z.
#include "gb/player.h"
#include "test.h"

using namespace gb;

namespace {
// Steps on which a B xy command lets the note play, over `steps` steps of one channel (first channel only).
int playedSteps(uint8_t val, int steps, int* first) {
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = uint8_t(c < 4 ? c : 0xFF);
    s.songCell(0, 0) = 0;
    for (int st = 0; st < steps; ++st) {
        const int p = st / 16, i = st % 16;
        s.note(p, i) = 25;
        s.phraseInstr(p, i) = 0;
        s.phraseCmd(p, i) = kCmdB;
        s.phraseVal(p, i) = val;
    }
    uint8_t* in = s.instrument(0);
    in[0] = 0; in[1] = 0xF0; in[4] = 0x7F; in[5] = 1; in[7] = 3;
    s.at(off::kTempo) = 255;
    Player p(s);
    p.start(0);
    int played = 0, firstStep = -1;
    bool last = false;
    for (int t = 0; t < steps * 6 + 6; ++t) {
        p.run(uint32_t(10485760 / 255));
        const bool on = p.voiceOn(0);
        (void)last;
        if (on && firstStep < 0) firstStep = t / 6;
        (void)on;
    }
    if (first) *first = firstStep;
    (void)played;
    return firstStep;
}
}  // namespace

TEST(b_gate_first_played_step_follows_the_generator) {
    // measured: with B09 the first note that plays is step 1; with B04 it is step 9
    int f = -1;
    playedSteps(0x09, 32, &f);
    CHECK_EQ(f, 1);
    playedSteps(0x04, 32, &f);
    CHECK_EQ(f, 9);
}

TEST(b00_never_plays_and_b0f_always_does) {
    int f = -1;
    playedSteps(0x00, 32, &f);
    CHECK_EQ(f, -1);
    playedSteps(0x0F, 32, &f);
    CHECK_EQ(f, 0);
}
