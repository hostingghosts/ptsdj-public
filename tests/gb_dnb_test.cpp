// The drum and bass pair (src/gb/dnb.cpp): one piece in A minor at 170 BPM over two consoles. These checks keep it a piece: same tempo, length and SYNC, every tonal
// pitch inside A minor, the break on the grid with the kits, the tables doing what they are written to do, no note ringing into the next part, a mix that does not squash.
// The two Amen kits are read from ../demos/kits; the checks that need them are skipped when the files are not there.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gb/dnb.h"
#include "gb/dual.h"
#include "gb/kitbank.h"
#include "gb/kits.h"
#include "gb/lsdsng.h"
#include "gb/player.h"
#include "test.h"

using namespace gb;

namespace {

std::vector<uint8_t> slurp(const char* path) {
    std::vector<uint8_t> v;
    if (std::FILE* f = std::fopen(path, "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF) v.push_back(uint8_t(c));
        std::fclose(f);
    }
    return v;
}

struct Kits {
    std::vector<uint8_t> a1 = slurp("../demos/kits/DM-AMEN1.kit"), a2 = slurp("../demos/kits/DM-AMEN2.kit");
    RomKits src;
    bool ok() const { return a1.size() == kKitBankSize && a2.size() == kKitBankSize; }
    Kits() {
        if (ok()) { src.setOverride(kDnbKit1, a1.data()); src.setOverride(kDnbKit2, a2.data()); }
    }
};

// The state of all four channels of one console at the end of every tick (the step phase of the oracle), `ticks` of them.
struct Tick { bool on[4]; int vol[4], freq[4]; };
std::vector<Tick> record(Song& s, const KitSource* kits, int ticks) {
    Player p(s);
    p.setKits(kits);
    p.start(0);
    const double tc = 10485760.0 / p.tempo();
    long long done = 0;
    std::vector<Tick> v;
    for (int t = 0; t < ticks; ++t) {
        const long long until = (long long)((t + 0.9) * tc);
        p.run(uint32_t(until - done)); done = until;
        Tick k{};
        for (int c = 0; c < 4; ++c) {
            const Apu::ChState st = p.apu().state(c);
            k.on[c] = st.enabled && st.dac && (c == 2 || st.vol > 0);
            k.vol[c] = st.vol; k.freq[c] = st.freq;
        }
        v.push_back(k);
    }
    return v;
}

bool inScale(int pc) { return pc == 9 || pc == 11 || pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7; }
int pitchClass(int ch, int freq) {
    const double hz = (ch == 2 ? 65536.0 : 131072.0) / (2048 - freq);
    return ((int(std::lround(69 + 12 * std::log2(hz / 440.0))) % 12) + 12) % 12;
}

double rmsOf(const std::vector<int16_t>& p) {
    double s = 0;
    for (int16_t v : p) s += double(v) * v;
    return p.empty() ? 0 : std::sqrt(s / double(p.size())) / 32768.0;
}

}  // namespace

// Song A's pulse 2 is a drum (a synth kick and snare tone, a pitch falling through every semitone) in the first build and both drops.
bool synthDrumBar(int bar) { return (bar >= 8 && bar < 32) || (bar >= 44 && bar < 60); }

TEST(both_songs_share_tempo_sync_length_and_the_shape_of_every_part) {
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    CHECK_EQ(int(a.at(off::kTempo)), kDnbTempo);
    CHECK_EQ(int(b.at(off::kTempo)), kDnbTempo);
    CHECK_EQ(int(a.at(off::kSync)), 1);
    CHECK_EQ(int(b.at(off::kSync)), 1);
    CHECK(a.currentFormat() && b.currentFormat());
    int rowsA = 0, rowsB = 0, barsA = 0, barsB = 0;
    for (int r = 0; r < kSongRows; ++r)
        for (int ch = 0; ch < 4; ++ch) {
            if (a.songCell(r, ch) != 0xFF) rowsA = r + 1;
            if (b.songCell(r, ch) != 0xFF) rowsB = r + 1;
        }
    CHECK_EQ(rowsA, 9);
    CHECK_EQ(rowsB, 9);
    for (int r = 0; r < 9; ++r) {
        int len[8];
        for (int ch = 0; ch < 4; ++ch)
            for (int c = 0; c < 2; ++c) {
                Song& s = c ? b : a;
                const int chain = s.songCell(r, ch);
                CHECK(chain != 0xFF);
                int n = 0;
                while (n < 16 && s.chainPhrase(chain, n) != 0xFF) ++n;
                len[c * 4 + ch] = n;
            }
        for (int i = 1; i < 8; ++i) CHECK_EQ(len[i], len[0]);       // every track of both consoles has the same number of bars in a part
        barsA += len[0];
        barsB += len[4];
    }
    CHECK_EQ(barsA, kDnbBars);
    CHECK_EQ(barsB, kDnbBars);
    // many chains, phrases and tables, as asked: this is not a loop
    for (Song* s : {&a, &b}) {
        int phrases = 0, chains = 0, instruments = 0, tablesUsed = 0;
        for (int p = 0; p < kPhrases; ++p) { bool any = false; for (int i = 0; i < 16; ++i) any = any || s->note(p, i) || s->phraseCmd(p, i); phrases += any; }
        for (int c = 0; c < kChains; ++c) chains += s->chainPhrase(c, 0) != 0xFF;
        for (int i = 0; i < kInstruments; ++i) instruments += s->at(uint16_t(off::kInstrAlloc + i)) != 0;
        for (int t = 0; t < kTables; ++t) { bool any = false; for (int r = 0; r < 16; ++r) any = any || s->at(uint16_t(off::kTblTrans + t * 16 + r)) || s->at(uint16_t(off::kTblCmd1 + t * 16 + r)) || s->at(uint16_t(off::kTblEnv + t * 16 + r)); tablesUsed += any; }
        CHECK(phrases >= 18 && chains >= 18 && instruments >= 8 && tablesUsed >= 3);
    }
    CHECK_EQ(a.at(off::kHandDrawn + 1), 0);              // song A draws no waves: its wave channel plays the kits
    CHECK(b.at(off::kHandDrawn + 1) & 7);                // song B draws its own three
}

TEST(every_tonal_note_is_in_a_minor_and_nothing_clashes) {
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    Kits k;
    const int total = kDnbBars * 96;
    const KitSource* ks = k.ok() ? &k.src : nullptr;
    const std::vector<Tick> va = record(a, ks, total + 2), vb = record(b, ks, total + 2);
    int waveTicks = 0, waveOut = 0;
    for (int t = 0; t < total; ++t)
        for (int c = 0; c < 3; ++c)
            for (int con = 0; con < 2; ++con) {
                const Tick& tk = (con ? vb : va)[size_t(t)];
                if (!tk.on[c] || (c == 2 && con == 0)) continue;     // the kits play their samples at a fixed pitch
                if (con == 0 && c == 1 && synthDrumBar(t / 96)) continue;     // song A's pulse 2 is the synth kick and snare tone there
                const int pc = pitchClass(c, tk.freq[c]);
                if (c < 2) CHECK(inScale(pc));                        // the pulse channels never leave the scale, vibrato and all
                else { ++waveTicks; if (!inScale(pc)) ++waveOut; }
            }
    CHECK(waveTicks > 2500 && waveOut * 50 < waveTicks);              // the sub's slow vibrato may round to a neighbour at a turning point
    // Clean means no semitone or tritone sounding between two tonal voices (the pulses of both consoles and song B's wave channel at a level that can be heard).
    int multi = 0, clash = 0, clashInDrops = 0;
    for (int t = 0; t < total; ++t) {
        int pcs[5], n = 0;
        for (int c = 0; c < 2; ++c) if (!(c == 1 && synthDrumBar(t / 96)) && va[size_t(t)].on[c] && va[size_t(t)].vol[c] >= 2) pcs[n++] = pitchClass(c, va[size_t(t)].freq[c]);
        for (int c = 0; c < 3; ++c) if (vb[size_t(t)].on[c] && (c == 2 || vb[size_t(t)].vol[c] >= 2)) pcs[n++] = pitchClass(c, vb[size_t(t)].freq[c]);
        if (n < 2) continue;
        ++multi;
        bool bad = false;
        for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) { const int d = std::abs(pcs[i] - pcs[j]) % 12, ic = std::min(d, 12 - d); bad = bad || ic == 1 || ic == 6; }
        clash += bad;
        const int bar = t / 96;
        if (bad && ((bar >= 16 && bar < 32) || (bar >= 44 && bar < 60))) ++clashInDrops;
    }
    CHECK(multi > 5000);
    CHECK(clash * 200 <= multi);                                      // under half a percent of the ticks
    CHECK_EQ(clashInDrops, 0);                                        // and none at all in the drops
    Player pa(a), pb(b);
    Dual d(pa, pb);
    d.start(0, 0);
    int16_t buf[2 * 1024];
    long long frames = 0;
    while (d.running() && !d.looped() && frames < 22050LL * 200) { d.render(buf, 1024, 22050); frames += 1024; }
    CHECK(d.looped());
    CHECK(std::fabs(double(frames) / 22050 - kDnbBars * 96 / (kDnbTempo * 0.4)) < 0.2);     // 96 s: the two songs come round together
}

TEST(the_break_is_sparse_and_sits_on_the_grid_kick_and_snare_only) {
    Kits k;
    if (!k.ok()) { std::printf("  (no Amen kits in ../demos/kits, skipped)\n"); return; }
    static Song a;
    buildDnbA(a);
    Player p(a);
    p.setKits(&k.src);
    static std::vector<int> starts;
    starts.clear();
    p.setWaveHook([](int pos, int tick) { if (pos == 16) starts.push_back(tick); });     // the first chunk of a kit sample is at position 16
    p.start(0);
    const double tc = 10485760.0 / p.tempo();
    long long done = 0;
    for (int t = 0; t < 24 * 96; ++t) {
        const long long until = (long long)((t + 0.9) * tc);
        p.run(uint32_t(until - done)); done = until;
    }
    // drop 1a is bars 16-23: the first seven bars have 7 to 9 hits each (kicks, snares and the ghost snares between), the eighth a fill; every hit begins on a step (6 ticks) or a half step
    int inDrop = 0, onGrid = 0, inBars = 0;
    for (int t : starts) if (t >= 16 * 96 && t < 23 * 96) { ++inDrop; if (std::abs(t % 6) <= 1 || std::abs(t % 6) >= 5) ++onGrid; }
    for (int t : starts) if (t >= 16 * 96 && t < 17 * 96) ++inBars;
    CHECK(inDrop >= 45 && inDrop <= 66);                      // 7 bars of 7 to 9 hits
    CHECK(inBars >= 7 && inBars <= 9);
    CHECK_EQ(onGrid, inDrop);
    // the kit holds only kicks and snares here (no break hats), so the notes between the hits are the sample tails and the half-level ghost snares
    const std::vector<Tick> v = record(a, &k.src, 24 * 96);
    int on = 0;
    for (int t = 16 * 96; t < 23 * 96; ++t) on += v[size_t(t)].on[2];
    CHECK(on > 7 * 96 * 6 / 10);                                  // a chopped break: the wave channel is sounding most of the time
}

TEST(the_tables_do_what_they_are_written_to_do) {
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    Kits k;
    const std::vector<Tick> va = record(a, k.ok() ? &k.src : nullptr, 46 * 96);
    const std::vector<Tick> vb = record(b, nullptr, 46 * 96);
    const int drop = 16 * 96;
    // the slow arpeggio (intro, bar 0, Am): a note a step, up A C E A and back E C; on the F bar (bar 2) it is major: F A C F C A
    {
        const int want[6] = {9, 0, 4, 9, 4, 0};
        for (int i = 0; i < 6; ++i) CHECK_EQ(pitchClass(1, va[size_t(2 + i * 6)].freq[1]), want[i]);
        const int f[6] = {5, 9, 0, 5, 0, 9};
        for (int i = 0; i < 6; ++i) CHECK_EQ(pitchClass(1, va[size_t(2 * 96 + 2 + i * 6)].freq[1]), f[i]);
    }
    // the lead's delayed vibrato: a held note is steady for the first step and a half and then moves
    {
        const int t0 = 35 * 96;                                   // the break's first long note (bar 35, B4), held for 5 steps
        std::vector<int> fr;
        for (int t = 0; t < 80; ++t) fr.push_back(va[size_t(t0 + t)].freq[0]);
        int early = 0, late = 0;
        for (int t = 1; t < 8; ++t) early = std::max(early, std::abs(fr[size_t(t)] - fr[0]));
        for (int t = 20; t < 80; ++t) late = std::max(late, std::abs(fr[size_t(t)] - fr[0]));
        CHECK(early <= 1);
        CHECK(late >= 3);
    }
    // the pump: the pad root (B pulse 1) starts a drop bar at volume 1, is back at 3 twelve ticks later, and ducks again on step 10; the fifth (pulse 2) is held at 2
    CHECK_EQ(vb[size_t(drop)].vol[0], 1);
    CHECK_EQ(vb[size_t(drop + 12)].vol[0], 3);
    CHECK(vb[size_t(drop + 10 * 6)].vol[0] <= 2);
    CHECK_EQ(vb[size_t(drop + 10 * 6 + 12)].vol[0], 3);
    CHECK_EQ(vb[size_t(drop)].vol[1], 1);
    CHECK_EQ(vb[size_t(drop + 12)].vol[1], 2);
    // the reese opens up: a new wave frame every six ticks while a note lasts, and the sweep starts again on each note (three notes in this bar: about ten frames)
    {
        Player p(b);
        static int loads;
        loads = 0;
        p.setWaveHook([](int, int tick) { if (tick >= 16 * 96 && tick < 17 * 96) ++loads; });
        p.start(0);
        const double tc = 10485760.0 / p.tempo();
        long long done = 0;
        for (int t = 0; t < 17 * 96; ++t) { const long long until = (long long)((t + 0.9) * tc); p.run(uint32_t(until - done)); done = until; }
        CHECK(loads >= 8 && loads <= 16);
    }
    // the sub holds one note and its vibrato comes in after four steps
    {
        std::vector<int> fr;
        for (int t = 0; t < 90; ++t) fr.push_back(vb[size_t(t)].freq[2]);
        int early = 0, late = 0;
        for (int t = 1; t < 20; ++t) early = std::max(early, std::abs(fr[size_t(t)] - fr[0]));
        for (int t = 30; t < 90; ++t) late = std::max(late, std::abs(fr[size_t(t)] - fr[0]));
        CHECK(early <= 1);
        CHECK(late >= 4);
    }
    // the riser (build 2, bar 40): A C D G climbing in steps, louder through the bar
    {
        const int t0 = 40 * 96;
        CHECK_EQ(pitchClass(1, vb[size_t(t0 + 2)].freq[1]), 9);
        CHECK_EQ(pitchClass(1, vb[size_t(t0 + 14)].freq[1]), 0);
        CHECK_EQ(pitchClass(1, vb[size_t(t0 + 26)].freq[1]), 2);
        CHECK_EQ(pitchClass(1, vb[size_t(t0 + 38)].freq[1]), 7);
        CHECK(vb[size_t(t0 + 90)].vol[1] > vb[size_t(t0 + 2)].vol[1] + 4);
    }
    // no noise roll: the last bar of every block has hats at the usual level on song A, and song B's snare layer and break riser stay under level 12
    {
        int topA = 0;
        for (int bar : {15, 23, 31, 43, 51, 59})
            for (int t = bar * 96; t < (bar + 1) * 96; ++t) topA = std::max(topA, int(va[size_t(t)].vol[3]));
        CHECK(topA <= 9);                                          // the open hat (level 9) is the loudest noise
        int topB = 0;
        for (int t = 31 * 96; t < 32 * 96; ++t) topB = std::max(topB, int(vb[size_t(t)].vol[3]));
        for (int t = 38 * 96; t < 40 * 96; ++t) topB = std::max(topB, int(vb[size_t(t)].vol[3]));
        CHECK(topB <= 11);
        // the break: the pads stay (bars 32-37); the long vibrato notes of song A's lead are out for its first three bars (about 0:45 to 0:49) and back on bar 35, at a lower level than the lead
        for (int tk = 32 * 96; tk < 35 * 96; ++tk) CHECK(!va[size_t(tk)].on[0]);
        bool longBack = false;
        int longTop = 0;
        for (int tk = 35 * 96; tk < 36 * 96; ++tk) { longBack = longBack || va[size_t(tk)].on[0]; longTop = std::max(longTop, int(va[size_t(tk)].vol[0])); }
        CHECK(longBack);
        CHECK(longTop <= 6);
        CHECK(vb[size_t(32 * 96 + 20)].on[0] && vb[size_t(32 * 96 + 20)].on[1]);
        // and the noise riser (bars 38 and 39) stays at or under level 6
        int riser = 0;
        for (int tk = 38 * 96; tk < 40 * 96; ++tk) riser = std::max(riser, int(vb[size_t(tk)].vol[3]));
        CHECK(riser >= 4 && riser <= 6);
    }
}

TEST(no_note_rings_on_into_a_quiet_part_or_over_the_loop) {
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    Kits k;
    const int total = kDnbBars * 96;
    const std::vector<Tick> va = record(a, k.ok() ? &k.src : nullptr, total + 400);
    const std::vector<Tick> vb = record(b, nullptr, total + 400);
    // the first four bars of the intro have no lead on A, and no snare layer or reese on B: whatever the outro leaves must be over by the time the song comes round
    for (int t = total + 100; t < total + 4 * 96; ++t) {
        CHECK(!va[size_t(t)].on[0]);
        CHECK(!va[size_t(t)].on[3]);
    }
    for (int t = total + 96; t < total + 4 * 96; ++t) CHECK(!vb[size_t(t)].on[3]);
    // the break: no bass note rings past its bar (K 5C = 92 ticks)
    for (int bar = 32; bar < 40; ++bar) CHECK(!vb[size_t(bar * 96 + 95)].on[2]);
    // the last bar of the outro: no drums, no hats, no lead and no bass left sounding
    for (int t = (kDnbBars - 1) * 96 + 10; t < total; ++t) CHECK(!vb[size_t(t)].on[2] && !va[size_t(t)].on[3] && !va[size_t(t)].on[0] && !va[size_t(t)].on[2]);
}

TEST(the_mix_is_balanced_and_does_not_squash) {
    Kits k;
    if (!k.ok()) { std::printf("  (no Amen kits in ../demos/kits, skipped)\n"); return; }
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    constexpr int kRate = 22050;
    Player pa(a), pb(b);
    pa.setKits(&k.src); pb.setKits(&k.src);
    Dual both(pa, pb);
    both.start(0, 0);
    std::vector<int16_t> mix;
    int16_t buf[2 * 1024];
    while (both.running() && !both.looped() && mix.size() < size_t(kRate) * 2 * 200) {
        both.render(buf, 1024, kRate);
        mix.insert(mix.end(), buf, buf + 2048);
    }
    CHECK(mix.size() > size_t(kRate) * 2 * 90);
    int peak = 0;
    size_t hot = 0;
    for (int16_t v : mix) { peak = std::max(peak, std::abs(int(v))); if (std::abs(int(v)) > 19660) ++hot; }
    CHECK(peak < 32000);                                              // no flat clipping anywhere (full scale is 32767; the layered kick brings the loudest hit to about 0.95)
    CHECK(double(hot) / double(mix.size()) < 0.01);
    const size_t bar = mix.size() / kDnbBars;
    auto part = [&](int from, int bars) { return rmsOf(std::vector<int16_t>(mix.begin() + long(bar) * from, mix.begin() + long(bar) * (from + bars))); };
    const double intro = part(0, 8), drop1 = part(16, 16), brk = part(32, 8), drop2 = part(44, 16), outro = part(60, 8);
    // The drops are no quieter than the intro or the outro and clearly louder than the break. They used to be 25 % louder than the intro, but only because the noise rolls
    // at the end of every block were loud enough to buzz; with the rolls tamed the drops hit through the kit and the lead, not through the level.
    CHECK(drop1 > intro * 0.98 && drop2 > intro * 0.98);
    CHECK(drop1 > brk * 1.1 && drop2 > outro * 0.98);
    CHECK(intro > 0.1 && brk > 0.08);                                 // and nothing is quiet (the break is the thinnest part)
}

TEST(the_synth_kick_and_snare_tone_hit_with_the_kit_on_the_same_tick) {
    Kits k;
    if (!k.ok()) { std::printf("  (no Amen kits in ../demos/kits, skipped)\n"); return; }
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    static std::vector<int> kit;
    kit.clear();
    const std::vector<Tick> v = record(a, &k.src, 32 * 96);
    {
        Player q(a); q.setKits(&k.src); q.setWaveHook([](int pos, int tick) { if (pos == 16) kit.push_back(tick); });     // every kit hit
        q.start(0);
        const double tc = 10485760.0 / q.tempo();
        long long done = 0;
        for (int t = 0; t < 32 * 96; ++t) { const long long until = (long long)((t + 0.9) * tc); q.run(uint32_t(until - done)); done = until; }
    }
    // pulse 2 starts a note (volume jumps up from a rest) on a tick: those must be kit hits, and each full-level kick or snare of a drop bar has one
    int synth = 0, together = 0;
    for (int t = 16 * 96 + 1; t < 24 * 96; ++t) {
        const bool onset = v[size_t(t)].on[1] && v[size_t(t)].vol[1] > v[size_t(t - 1)].vol[1] + 3;
        if (!onset) continue;
        ++synth;
        for (int h : kit) if (std::abs(h - t) <= 1) { ++together; break; }
    }
    CHECK(synth >= 7 * 5);                                  // a kick or snare tone on most hits of the seven plain bars
    CHECK_EQ(together, synth);                              // and every one of them starts with a kit hit, never on its own
    // the kick falls in pitch: 220 Hz to 65 Hz in eight ticks, and its level is high at the start and nearly gone after eight
    CHECK(v[size_t(16 * 96)].freq[1] > v[size_t(16 * 96 + 7)].freq[1] + 200);         // a higher register value is a higher pitch
    CHECK(v[size_t(16 * 96)].vol[1] >= 9);
    CHECK(v[size_t(16 * 96 + 8)].vol[1] <= 1);
    // song B's noise snare layer lands on the kit's snares: steps 4 and 12 of the plain drop bars
    const std::vector<Tick> vb = record(b, nullptr, 32 * 96);
    for (int bar = 17; bar < 23; ++bar)
        for (int step : {4, 12}) {
            const int t = bar * 96 + step * 6;
            bool kitHit = false;
            for (int h : kit) kitHit = kitHit || std::abs(h - t) <= 1;
            CHECK(kitHit);
            CHECK(vb[size_t(t + 1)].on[3] && vb[size_t(t + 1)].vol[3] >= 5);
        }
}

TEST(the_songs_are_valid_lsdsng_files_and_play_without_a_rom) {
    static Song a, b;
    buildDnbA(a);
    buildDnbB(b);
    for (Song* s : {&a, &b}) {
        std::vector<uint8_t> out(9 + 64 * 512);
        const size_t n = saveLsdsng(*s, "DNB", 1, out.data());
        CHECK(n > 9 && (n - 9) % 512 == 0);
        static Song back;
        CHECK(loadLsdsng(back, out.data(), n));
        CHECK(std::memcmp(back.mem(), s->mem(), kMemSize) == 0);
    }
    // song B's three drawn waves differ from each other; the reese's frames morph and the sub is the same sine in every frame
    CHECK(std::memcmp(b.wave(0), b.wave(16), 16) != 0 && std::memcmp(b.wave(16), b.wave(32), 16) != 0);
    CHECK(std::memcmp(b.wave(16), b.wave(31), 16) != 0);
    CHECK(std::memcmp(b.wave(0), b.wave(15), 16) == 0);
    // the kit slots are the ones the header says
    bool k1 = false, k2 = false;
    for (int i = 0; i < kInstruments; ++i) {
        const uint8_t* in = a.instrument(i);
        if (in[0] != 2) continue;
        k1 = k1 || in[9] == kDnbKit1;
        k2 = k2 || in[2] == kDnbKit2;
    }
    CHECK(k1 && k2);
}
