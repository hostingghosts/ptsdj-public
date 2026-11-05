// Player rules measured against LSDJ 9.4.2 (see docs/LSDJ_FORMAT.md): the random generator, B gate and Z.
#include "gb/player.h"
#include "test.h"

#include <utility>
#include <vector>

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

namespace {
// Pulse 1 frequency at the end of each tick for a note with `cmds` (step -> C value) in phrase 0, groove 6 ticks.
std::vector<int> chordFreqs(const std::vector<std::pair<int, uint8_t>>& cmds, int ticks) {
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 0) = 0;
    s.note(0, 0) = 25;
    s.phraseInstr(0, 0) = 0;
    for (auto& c : cmds) { s.phraseCmd(0, c.first) = kCmdC; s.phraseVal(0, c.first) = c.second; }
    uint8_t* in = s.instrument(0);
    in[0] = 0; in[1] = 0xF0; in[4] = 0x7F; in[5] = 1; in[7] = 3;
    s.at(off::kTempo) = 128;
    Player p(s);
    p.start(0);
    std::vector<int> out;
    long long done = 0;
    for (int t = 0; t < ticks; ++t) {   // sampled late in each tick, as tools/oracle/playdump does
        const long long until = (long long)((t + 0.9) * 10485760.0 / 128);
        p.run(uint32_t(until - done));
        done = until;
        out.push_back(p.apu().state(0).freq);
    }
    return out;
}
}  // namespace

TEST(chord_phase_survives_running_c_again) {
    // measured on LSDJ: C04 gives base, base, +4 repeating; a second C04 or C05 one step later does not restart the cycle
    const auto a = chordFreqs({{0, 0x04}}, 18);
    const auto b = chordFreqs({{0, 0x04}, {1, 0x04}}, 18);
    CHECK(a[0] == a[1] && a[2] != a[1] && a[3] == a[0] && a[5] == a[2]);
    CHECK(a == b);
}

TEST(chord_off_returns_to_the_note) {
    const auto a = chordFreqs({{0, 0x04}, {1, 0x00}}, 18);
    CHECK(a[17] == a[0]);
}
