// The two demo songs (src/gb/demo.cpp): one piece in A minor across both consoles. These checks keep it that way:
// same tempo, length and SYNC, every pitch the chips play inside the scale, the melody on the chord on beats 1 and 3,
// a balanced mix that does not squash, and a valid .lsdsng that needs no ROM.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "gb/demo.h"
#include "gb/dual.h"
#include "gb/lsdsng.h"
#include "gb/player.h"
#include "test.h"

using namespace gb;

namespace {

// A natural minor as pitch classes (C = 0): A B C D E F G.
bool inScale(int pc) { return pc == 9 || pc == 11 || pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7; }

// The pitch classes of the tonal channels over one pass, read from the chip registers once per tick; also the ticks of the pass.
struct Heard { bool pc[3][12] = {}; int outOfTune = 0, ticks = 0; };

Heard listen(Song& s) {
    Heard h;
    Player p(s);
    p.start(0);
    const double tickCycles = 10485760.0 / p.tempo();
    long long done = 0;
    while (p.running() && !p.songLooped() && h.ticks < 20000) {
        const long long until = (long long)((h.ticks + 0.9) * tickCycles);
        p.run(uint32_t(until - done));
        done = until;
        ++h.ticks;
        for (int c = 0; c < 3; ++c) {
            const Apu::ChState st = p.apu().state(c);
            if (!st.enabled || !st.dac || st.vol == 0) continue;
            const double hz = (c == 2 ? 65536.0 : 131072.0) / (2048 - st.freq);
            const double semis = 69 + 12 * std::log2(hz / 440.0);
            const int n = int(std::lround(semis));
            if (std::fabs(semis - n) > 0.4) { ++h.outOfTune; continue; }
            h.pc[c][((n % 12) + 12) % 12] = true;
        }
    }
    return h;
}

struct Mix { double peakA = 0, rmsA = 0, rmsB = 0, hotPct = 0; };

double rmsOf(const std::vector<int16_t>& p) {
    double s = 0;
    for (int16_t v : p) s += double(v) * v;
    return p.empty() ? 0 : std::sqrt(s / double(p.size())) / 32768.0;
}

std::vector<int16_t> renderPass(Dual& d, int rate) {
    std::vector<int16_t> pcm;
    int16_t buf[2 * 1024];
    while (d.running() && !d.looped() && pcm.size() < size_t(rate) * 2 * 60) {
        d.render(buf, 1024, rate);
        pcm.insert(pcm.end(), buf, buf + 2048);
    }
    return pcm;
}

// The chord of each bar of the progression Am - F - C - G as pitch classes.
const int kChord[4][3] = {{9, 0, 4}, {5, 9, 0}, {0, 4, 7}, {7, 11, 2}};

}  // namespace

TEST(both_songs_share_tempo_sync_and_length) {
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    CHECK_EQ(int(a.at(off::kTempo)), kDemoTempo);
    CHECK_EQ(int(b.at(off::kTempo)), kDemoTempo);
    CHECK_EQ(int(a.at(off::kSync)), 1);                   // SYNC on in both: Play on either console starts both
    CHECK_EQ(int(b.at(off::kSync)), 1);
    CHECK(a.currentFormat() && b.currentFormat());
    int rowsA = 0, rowsB = 0;
    for (int r = 0; r < kSongRows; ++r)
        for (int ch = 0; ch < 4; ++ch) {
            if (a.songCell(r, ch) != 0xFF) rowsA = r + 1;
            if (b.songCell(r, ch) != 0xFF) rowsB = r + 1;
        }
    CHECK_EQ(rowsA, 4);
    CHECK_EQ(rowsB, 4);
    // every chain in the song rows is 4 phrases (4 bars): the rows line up bar for bar on all eight tracks
    for (Song* s : {&a, &b})
        for (int r = 0; r < 4; ++r)
            for (int ch = 0; ch < 4; ++ch) {
                const int c = s->songCell(r, ch);
                CHECK(c != 0xFF);
                if (c == 0xFF) continue;
                for (int k = 0; k < 16; ++k) CHECK_EQ(int(s->chainPhrase(c, k) != 0xFF), int(k < 4));
            }
    const Heard ha = listen(a), hb = listen(b);
    CHECK_EQ(ha.ticks, hb.ticks);                           // they come round to the top together, so the pair loops as one piece
    CHECK(std::abs(ha.ticks - 1536) <= 8);                  // 16 bars x 16 steps x 6 ticks
}

TEST(every_pitch_is_in_a_minor) {
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    for (Song* s : {&a, &b}) {
        const Heard h = listen(*s);
        CHECK_EQ(h.outOfTune, 0);
        bool any = false;
        for (int c = 0; c < 3; ++c)
            for (int pc = 0; pc < 12; ++pc)
                if (h.pc[c][pc]) { any = true; CHECK(inScale(pc)); }
        CHECK(any);
    }
}

TEST(the_melody_sits_on_the_chord_on_beats_one_and_three) {
    static Song a;
    buildDemoA(a);
    for (int row = 1; row <= 3; ++row) {                      // the rows with the lead
        const int chain = a.songCell(row, 0);
        for (int bar = 0; bar < 4; ++bar) {
            const int phrase = a.chainPhrase(chain, bar);
            for (int step : {0, 8}) {
                const int n = a.note(phrase, step);
                CHECK(n != 0);
                const int pc = ((n - 1) % 12 + 0) % 12;       // note 1 = C2
                bool onChord = false;
                for (int t : kChord[bar]) onChord = onChord || t == pc;
                CHECK(onChord);
            }
        }
    }
}

TEST(the_pad_and_stabs_follow_the_progression_by_transpose) {
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    // Am - F - C - G is A moved by 0, -4, +3, -2; the chord command is minor on the first bar and major after it
    for (Song* s : {&a, &b}) {
        const int chain = s->songCell(2, 1);                // PU2: the pad in song A, the stabs in song B
        const int want[4] = {0, -4, 3, -2};
        for (int bar = 0; bar < 4; ++bar) {
            CHECK_EQ(int(int8_t(s->chainTrans(chain, bar))), want[bar]);
            const int p = s->chainPhrase(chain, bar);
            bool found = false;
            for (int st = 0; st < 16; ++st)
                if (s->phraseCmd(p, st) == kCmdC) { found = true; CHECK_EQ(int(s->phraseVal(p, st)), bar == 0 ? 0x37 : 0x47); }
            CHECK(found);
        }
    }
}

TEST(the_mix_is_balanced_and_does_not_squash) {
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    constexpr int kRate = 44100;
    Player pa(a), pb(b);
    Dual both(pa, pb);
    both.start(0, 0);
    const std::vector<int16_t> mix = renderPass(both, kRate);
    CHECK(mix.size() > size_t(kRate) * 2 * 30);
    int peak = 0;
    size_t hot = 0;
    for (int16_t v : mix) { peak = std::max(peak, std::abs(int(v))); if (std::abs(int(v)) > 19660) ++hot; }
    CHECK(peak < 32000);                                      // no flat clipping
    CHECK(double(hot) / double(mix.size()) < 0.02);           // under 2 % of samples reach the mixer's soft knee
    CHECK(rmsOf(mix) > 0.1);                                  // and it is not quiet either
    double r[2];
    for (int c = 0; c < 2; ++c) {
        Song s;
        if (c == 0) buildDemoA(s); else buildDemoB(s);
        Player x(s), y(s);
        Dual one(x, y);
        one.startOne(0, 0);
        r[c] = rmsOf(renderPass(one, kRate));
    }
    CHECK(r[0] > r[1] * 0.8 && r[0] < r[1] * 1.6);            // the melody leads a little, neither part buries the other
}

TEST(the_songs_are_valid_lsdsng_files_and_draw_their_own_waves) {
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    for (Song* s : {&a, &b}) {
        std::vector<uint8_t> out(9 + 64 * 512);
        const size_t n = saveLsdsng(*s, "DEMO", 1, out.data());
        CHECK(n > 9 && (n - 9) % 512 == 0);
        static Song back;
        CHECK(loadLsdsng(back, out.data(), n));
        CHECK(std::memcmp(back.mem(), s->mem(), kMemSize) == 0);
    }
    // wave instruments point at synths whose frames are drawn into the song (marked hand drawn), so no ROM is needed to hear them
    CHECK(a.at(off::kHandDrawn + 1) & 1);
    CHECK(b.at(off::kHandDrawn + 1) & 2);
    bool drawn = false;
    for (int i = 0; i < 16; ++i) drawn = drawn || b.wave(16)[i] != 0;
    CHECK(drawn);
}
