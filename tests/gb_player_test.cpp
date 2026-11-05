// Player rules measured against LSDJ 9.4.2 (see docs/LSDJ_FORMAT.md): the random generator, B gate and Z.
#include "gb/player.h"
#include "test.h"

#include <array>
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

namespace {
// Pulse 1 state per tick for instrument bytes `in` (16) and table 0 rows (trans, cmd1, val1, cmd2, val2), notes at the given phrase steps.
struct Sample { int vol, freq, pan; };
std::vector<Sample> playPulse(const uint8_t* in16, const std::vector<std::array<uint8_t, 5>>& rows, const std::vector<int>& noteSteps, int ticks) {
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 0) = 0;
    for (int st : noteSteps) { s.note(0, st) = 25; s.phraseInstr(0, st) = 0; }
    for (int i = 0; i < 16; ++i) s.instrument(0)[i] = in16[i];
    for (size_t i = 0; i < rows.size(); ++i) {
        s.at(uint16_t(off::kTblTrans + i)) = rows[i][0];
        s.at(uint16_t(off::kTblCmd1 + i)) = rows[i][1]; s.at(uint16_t(off::kTblVal1 + i)) = rows[i][2];
        s.at(uint16_t(off::kTblCmd2 + i)) = rows[i][3]; s.at(uint16_t(off::kTblVal2 + i)) = rows[i][4];
    }
    s.at(off::kTempo) = 128;
    Player p(s);
    p.start(0);
    std::vector<Sample> out;
    long long done = 0;
    for (int t = 0; t < ticks; ++t) {
        const long long until = (long long)((t + 0.9) * 10485760.0 / 128);
        p.run(uint32_t(until - done));
        done = until;
        const auto st = p.apu().state(0);
        out.push_back({st.vol, st.freq, p.apu().pan()});
    }
    return out;
}
}  // namespace

TEST(envelope_has_three_stages) {
    // measured: 0x77 then byte 9 = 5D, byte 10 = 2F decays 7 -> 5 fast, 5 -> 2 slowly (rate D), 2 -> 0 very slowly (rate F)
    const uint8_t in[16] = {0, 0x77, 12, 0, 0x7F, 1, 0x20, 3, 0, 0x5D, 0x2F, 0, 0, 0, 0, 0};
    const auto a = playPulse(in, {{0, 0, 0, 0, 0}}, {0}, 60);
    CHECK_EQ(a[0].vol, 7);
    CHECK_EQ(a[3].vol, 5);
    CHECK_EQ(a[10].vol, 5);      // holds at 5 for 9 ticks
    CHECK_EQ(a[12].vol, 4);
    CHECK_EQ(a[30].vol, 2);
    CHECK_EQ(a[44].vol, 2);
}

TEST(table_runs_one_row_per_note_when_byte5_bit3_is_set) {
    // measured: rows (0, +5, +7, +3); with byte 5 = 9 each note advances one row and the position carries over
    const uint8_t in[16] = {0, 0xF0, 0, 0, 0x7F, 9, 0x20, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto a = playPulse(in, {{0, 0, 0, 0, 0}, {5, 0, 0, 0, 0}, {7, 0, 0, 0, 0}}, {0, 4}, 40);
    CHECK_EQ(a[0].freq, a[23].freq);
    CHECK(a[24].freq != a[0].freq);
    CHECK_EQ(a[24].freq, a[39].freq);
}

TEST(z_in_table_column_two_ignores_column_one) {
    // measured: O1 in column 2 and Z03 in column 1 leaves the pan alone (Z only repeats its own column's command)
    const uint8_t in[16] = {0, 0xF0, 0, 0, 0x7F, 1, 0x20, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto a = playPulse(in, {{0, 0, 0, kCmdO, 1}, {0, kCmdZ, 3, 0, 0}, {0, kCmdH, 1, 0, 0}}, {0}, 30);
    for (int t = 0; t < 30; ++t) CHECK_EQ(a[t].pan & 0x11, 0x10);   // left only on channel 1
}

TEST(r_first_digit_is_a_signed_volume_step) {
    // measured: start volume F, R93 -> 8, then 1 on the next retrigger (3 ticks later), then 0
    const uint8_t in[16] = {0, 0xF0, 0, 0, 0x7F, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 0) = 0;
    s.note(0, 0) = 25; s.phraseInstr(0, 0) = 0; s.phraseCmd(0, 0) = kCmdR; s.phraseVal(0, 0) = 0x93;
    for (int i = 0; i < 16; ++i) s.instrument(0)[i] = in[i];
    s.at(off::kTempo) = 128;
    Player p(s);
    p.start(0);
    int vol[8];
    long long done = 0;
    for (int t = 0; t < 8; ++t) {
        const long long until = (long long)((t + 0.9) * 10485760.0 / 128);
        p.run(uint32_t(until - done)); done = until;
        vol[t] = p.apu().state(0).vol;
    }
    CHECK_EQ(vol[0], 8); CHECK_EQ(vol[2], 8); CHECK_EQ(vol[3], 1); CHECK_EQ(vol[5], 1); CHECK_EQ(vol[6], 0);
}
