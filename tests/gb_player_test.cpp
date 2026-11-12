// Player rules measured against LSDJ 9.4.2 (see docs/LSDJ_FORMAT.md): the random generator, B gate and Z.
#include "gb/dual.h"
#include "gb/kitbank.h"
#include "gb/player.h"
#include "test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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

TEST(bend_matches_measured_register_values) {
    // measured on LSDJ: note 25 with P40 gives these pulse 1 registers at the end of ticks 0-4 (the fifth wraps past the table end)
    const uint8_t in[16] = {0, 0xF0, 0, 0, 0x7F, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 0) = 0;
    s.note(0, 0) = 25; s.phraseInstr(0, 0) = 0; s.phraseCmd(0, 0) = kCmdP; s.phraseVal(0, 0) = 0x40;
    for (int i = 0; i < 16; ++i) s.instrument(0)[i] = in[i];
    s.at(off::kTempo) = 128;
    Player p(s);
    p.start(0);
    const int want[6] = {1809, 1947, 2005, 2029, 2040, 375};
    long long done = 0;
    for (int t = 0; t < 6; ++t) {
        const long long until = (long long)((t + 0.9) * 10485760.0 / 128);
        p.run(uint32_t(until - done)); done = until;
        CHECK(std::abs(p.apu().state(0).freq - want[t]) <= 1);
    }
}

// A table row that moves the pitch and slides (T 80 + L20 on a kick) keeps the sound continuous and glides down: no wrap to the top of the table.
TEST(table_slide_row_glides_from_the_old_pitch_without_wrapping) {
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 2) = 0;
    s.note(0, 0) = 80;
    s.phraseInstr(0, 0) = 0;
    uint8_t* in = s.instrument(0);
    in[0] = 1; in[1] = 0xF0; in[2] = 0x0F; in[5] = 0x60; in[6] = 0x20; in[7] = 3;    // wave instrument running table 0
    s.at(off::kTblCmd1 + 0) = kCmdP; s.at(off::kTblVal1 + 0) = 0xB0;
    s.at(off::kTblTrans + 1) = 0x80; s.at(off::kTblCmd1 + 1) = kCmdL; s.at(off::kTblVal1 + 1) = 0x20;
    s.at(off::kTempo) = 96;
    Player p(s);
    p.start(0);
    int prev = 4096, jumps = 0, minF = 4096;
    for (int i = 0; i < 40 * 4; ++i) {
        p.run(11704 / 4);
        const int f = p.apu().state(2).freq;
        if (f > prev + 8) ++jumps;
        prev = f; minF = std::min(minF, f);
    }
    CHECK_EQ(jumps, 0);
    CHECK(minF < 400);
}

TEST(the_output_limiter_amplifies_quiet_material_and_turns_loud_peaks_down_instead_of_distorting_them) {
    const float kPi = 3.14159265f;
    {   // quiet: exactly the drive louder once the delay has passed, a clean sine
        OutputLimiter lim;
        float worst = 0;
        for (int i = 0; i < 4000; ++i) {
            float l = 0.1f * std::sin(2 * kPi * 440.f * float(i) / 44100.f), r = l;
            lim.process(l, r);
            if (i >= 200) worst = std::max(worst, std::fabs(l - lim.drive() * 0.1f * std::sin(2 * kPi * 440.f * float(i - (OutputLimiter::kLook - 1)) / 44100.f)));
        }
        CHECK(worst < 1e-4f);
    }
    {   // loud: never above the ceiling, and the shape stays a sine (only its size changes slowly), also on a signal that jumps from quiet to loud
        OutputLimiter lim;
        float top = 0, worstSlope = 0, prev = 0;
        for (int i = 0; i < 44100; ++i) {
            const float amp = i < 441 * 23 ? 0.05f : i < 441 * 68 ? 0.9f : 0.05f;   // switches at zero crossings of the 100 Hz sine (441 frames a cycle)
            float l = amp * std::sin(2 * kPi * 100.f * float(i) / 44100.f), r = 0.5f * l;
            lim.process(l, r);
            top = std::max(top, std::fabs(l));
            if (i > 1) worstSlope = std::max(worstSlope, std::fabs(l - prev));
            prev = l;
        }
        CHECK(top <= lim.ceiling() + 1e-4f);
        CHECK(top > lim.ceiling() - 0.02f);                           // and it does use the headroom
        CHECK(worstSlope < 0.06f);                                           // 100 Hz near full scale: a step of 0.014 per frame; no jumps from the gain changes
    }
    {   // noise at the loudest: never above the ceiling
        OutputLimiter lim;
        uint32_t s = 12345;
        float top = 0;
        for (int i = 0; i < 100000; ++i) {
            s = s * 1664525u + 1013904223u;
            float l = (float(s >> 8) / float(1 << 24) * 2.f - 1.f) * 1.0f, r = -l;
            lim.process(l, r);
            top = std::max(top, std::max(std::fabs(l), std::fabs(r)));
        }
        CHECK(top <= lim.ceiling() + 1e-4f);
    }
    {   // silence stays silent, and after a loud passage the gain comes back to 1
        OutputLimiter lim;
        float l = 0, r = 0;
        for (int i = 0; i < 100; ++i) { l = 1.f; r = 1.f; lim.process(l, r); }
        CHECK(lim.gain() < 0.9f);
        for (int i = 0; i < 12000; ++i) { l = 0; r = 0; lim.process(l, r); }
        CHECK(lim.gain() > 0.99f);
        CHECK_EQ(l, 0.f);
    }
}

TEST(the_output_limiter_follows_its_settings_drive_ceiling_release_and_off) {
    const float kPi = 3.14159265f;
    auto settle = [&](OutputLimiter& lim, float amp, int frames, float* top) {
        float t = 0;
        for (int i = 0; i < frames; ++i) {
            float l = amp * std::sin(2 * kPi * 100.f * float(i) / 44100.f), r = l;
            lim.process(l, r);
            if (i > 2000) t = std::max(t, std::fabs(l));
        }
        if (top) *top = t;
    };
    OutputLimiter::Settings d;
    CHECK_EQ(d.driveHalfDb, 18);                                          // the defaults: +9.0 dB, -0.5 dBFS, 125 ms, on
    CHECK_EQ(d.ceilHalfDb, -1);
    CHECK_EQ(d.releaseMs, 125);
    {   // the drive: +6 dB (12 half dB) doubles a quiet sine
        OutputLimiter lim;
        OutputLimiter::Settings s; s.driveHalfDb = 12;
        lim.set(s); lim.reset();
        float top = 0; settle(lim, 0.1f, 6000, &top);
        CHECK(std::fabs(top - 0.1f * 1.9953f) < 2e-3f);
    }
    {   // the ceiling: -6 dB (-12 half dB) holds a loud sine to 0.501
        OutputLimiter lim;
        OutputLimiter::Settings s; s.ceilHalfDb = -12;
        lim.set(s); lim.reset();
        float top = 0; settle(lim, 0.9f, 20000, &top);
        CHECK(top <= 0.5013f + 1e-4f);
        CHECK(top > 0.48f);
    }
    {   // off: the mix passes at the chips' own level, only delayed, however loud
        OutputLimiter lim;
        OutputLimiter::Settings s; s.on = false;
        lim.set(s); lim.reset();
        float top = 0; settle(lim, 0.9f, 6000, &top);
        CHECK(std::fabs(top - 0.9f) < 1e-3f);
        CHECK(lim.gain() > 0.9999f);
    }
    {   // release: a short release brings the gain back sooner than a long one
        int back[2] = {};
        for (int k = 0; k < 2; ++k) {
            OutputLimiter lim;
            OutputLimiter::Settings s; s.releaseMs = k ? 500 : 25;
            lim.set(s); lim.reset();
            float l = 1.f, r = 1.f;
            for (int i = 0; i < 200; ++i) { l = r = 1.f; lim.process(l, r); }
            while (lim.gain() < 0.99f && back[k] < 100000) { l = r = 0.f; lim.process(l, r); ++back[k]; }
        }
        CHECK(back[0] * 5 < back[1]);
    }
    {   // out of range values are clamped
        OutputLimiter::Settings s; s.driveHalfDb = 99; s.ceilHalfDb = 7; s.releaseMs = 9999;
        s.clamp();
        CHECK_EQ(s.driveHalfDb, 36);
        CHECK_EQ(s.ceilHalfDb, -1);
        CHECK_EQ(s.releaseMs, 500);
    }
}

// ---- kit SPEED (byte 2 bit 6) ----------------------------------------------------------------------------------------------

namespace {
struct OneKit final : KitSource {
    std::vector<uint8_t> bank = std::vector<uint8_t>(kKitBytes, 0xFF);
    int index = 4;
    OneKit() {
        KitBank::clear(bank.data());
        std::vector<uint8_t> smp(16 * 300, 0x9A);                 // 300 chunks, about 0.84 s at the normal speed: it does not end during the test
        KitBank::add(bank.data(), "KIK", smp.data(), int(smp.size()));
    }
    Kit kit(int i) const override { Kit k; if (i == index) k.data = bank.data(); return k; }
};

// Chunks played in 0.1 s by a kit instrument whose second kit is byte 2 and whose note picks sample 1 of it; also the pitch register at the end.
int chunksIn100ms(uint8_t byte2, int* freq) {
    static OneKit kits;
    static int n; n = 0;
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    s.chainPhrase(0, 0) = 0;
    s.songCell(0, 2) = 0;
    s.note(0, 0) = 0x10;                                         // sample 1 of the second kit, nothing from the first
    s.phraseInstr(0, 0) = 0;
    uint8_t* in = s.instrument(0);
    in[0] = 2; in[1] = 0x20; in[2] = byte2; in[7] = 3; in[9] = 0; in[10] = 0xD0;
    Player p(s);
    p.setKits(&kits);
    p.setWaveHook([](int, int) { ++n; });
    p.start(0);
    for (int i = 0; i < 1000; ++i) p.run(1049);                  // 0.1 s in small steps
    if (freq) *freq = p.apu().state(2).freq;
    return n;
}
}  // namespace

TEST(a_kit_with_the_speed_flag_plays_at_half_speed_and_is_not_a_different_kit) {
    int f1 = 0, f2 = 0;
    const int normal = chunksIn100ms(0x04, &f1);
    const int slow = chunksIn100ms(0x44, &f2);                   // kit 4 with bit 6 set: the same kit, half the speed (measured: 0x692 and 23424 cycles per chunk)
    CHECK(normal >= 80 && normal <= 95);                         // one chunk per 11712 cycles
    CHECK(slow >= 40 && slow <= 48);
    CHECK_EQ(f1, 0x749);
    CHECK_EQ(f2, 0x692);
}

namespace {
// One voice of a one-phrase song at tempo 150, the state of channel `ch` read at the end of each tick (as the oracle does): one value per tick of `field`.
std::vector<int> ticksOf(const uint8_t (&in)[16], int ch, int cmd, int val, int note, int n, int field, const std::vector<std::array<uint8_t, 6>>& table = {}) {
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, ch) = 0;
    s.note(0, 0) = uint8_t(note); s.phraseInstr(0, 0) = 0; s.phraseCmd(0, 0) = uint8_t(cmd); s.phraseVal(0, 0) = uint8_t(val);
    for (int i = 0; i < 16; ++i) s.instrument(0)[i] = in[i];
    for (size_t r = 0; r < table.size(); ++r) {            // env, transpose, cmd 1, val 1, cmd 2, val 2
        s.at(uint16_t(off::kTblEnv + r)) = table[r][0]; s.at(uint16_t(off::kTblTrans + r)) = table[r][1];
        s.at(uint16_t(off::kTblCmd1 + r)) = table[r][2]; s.at(uint16_t(off::kTblVal1 + r)) = table[r][3];
        s.at(uint16_t(off::kTblCmd2 + r)) = table[r][4]; s.at(uint16_t(off::kTblVal2 + r)) = table[r][5];
    }
    s.at(off::kTempo) = 150;
    Player p(s);
    p.start(0);
    std::vector<int> out;
    long long done = 0;
    for (int t = 0; t < n; ++t) {
        const long long until = (long long)((t + 0.9) * 10485760.0 / 150);
        p.run(uint32_t(until - done)); done = until;
        out.push_back(field == 0 ? p.apu().state(ch).freq : p.apu().state(ch).vol);
    }
    return out;
}
}  // namespace

TEST(a_hop_to_its_own_row_does_nothing_and_the_table_wraps_after_row_15) {
    // measured on LSDJ 9.4.2 (pulse 2, note 22, rows 0 1 2 3 = +0 +3 +5 +7, row 3 holds H 03): the +7 lasts one tick, then the table runs on through empty rows
    // and starts again at tick 16 (row 0). To hold a row, hop to the row before it (H 02 on row 3 holds row 2).
    const uint8_t in[16] = {0, 0xF0, 0, 0, 0x7F, 0, 0x20, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<std::array<uint8_t, 6>> tbl = {{0, 0, 0, 0, 0, 0}, {0, 3, 0, 0, 0, 0}, {0, 5, 0, 0, 0, 0}, {0, 7, kCmdH, 0x03, 0, 0}};
    const auto f = ticksOf(in, 1, 0, 0, 22, 40, 0, tbl);
    const int want[4] = {1452, 1547, 1602, 1650};
    for (int t = 0; t < 4; ++t) CHECK_EQ(f[t], want[t]);
    for (int t = 4; t < 16; ++t) CHECK_EQ(f[t], 1452);
    for (int t = 0; t < 4; ++t) CHECK_EQ(f[16 + t], want[t]);
    const std::vector<std::array<uint8_t, 6>> back = {{0, 0, 0, 0, 0, 0}, {0, 3, 0, 0, 0, 0}, {0, 5, 0, 0, 0, 0}, {0, 7, kCmdH, 0x02, 0, 0}};
    const auto g = ticksOf(in, 1, 0, 0, 22, 40, 0, back);
    for (int t = 2; t < 40; ++t) CHECK_EQ(g[t], 1602);
}

TEST(r_triggers_the_note_again_at_once_and_each_trigger_restarts_the_envelope) {
    // measured: noise note with envelope 9 falling, R 03: the volume goes 8 7 5 and starts again at 8 every 3 ticks (it used to fall to 0 and stay there);
    // R 00 on a step of its own triggers the note again too (volume 8 again at that tick, then the envelope falls).
    const uint8_t in[16] = {3, 0x94, 0, 0, 0x7F, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0};
    const auto v = ticksOf(in, 3, kCmdR, 0x03, 41, 24, 1);
    const int want[9] = {8, 7, 5, 8, 7, 5, 8, 7, 5};
    for (int t = 0; t < 24; ++t) CHECK_EQ(v[t], want[t % 3]);
    Song s;
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) s.songCell(r, c) = 0xFF;
    for (int c = 0; c < 16; ++c) s.chainPhrase(0, c) = c == 0 ? 0 : 0xFF;
    s.songCell(0, 3) = 0;
    s.note(0, 0) = 41; s.phraseInstr(0, 0) = 0;
    s.phraseCmd(0, 2) = kCmdR; s.phraseVal(0, 2) = 0x00;
    for (int i = 0; i < 16; ++i) s.instrument(0)[i] = in[i];
    s.at(off::kTempo) = 150;
    Player p(s);
    p.start(0);
    std::vector<int> vol;
    long long done = 0;
    for (int t = 0; t < 24; ++t) {
        const long long until = (long long)((t + 0.9) * 10485760.0 / 150);
        p.run(uint32_t(until - done)); done = until;
        vol.push_back(p.apu().state(3).vol);
    }
    CHECK_EQ(vol[5], 1); CHECK_EQ(vol[6], 0); CHECK_EQ(vol[11], 0); CHECK_EQ(vol[12], 8); CHECK_EQ(vol[13], 7); CHECK_EQ(vol[18], 0);
}

TEST(wave_vibrato_and_bends_work_in_semitones_but_in_drum_mode_in_the_linear_table) {
    // measured on LSDJ 9.4.2: the same V 44 swings the wave channel's register by an amount proportional to its period (bigger on low notes); a wave instrument with
    // byte 5 bit 6 set (the kick drums) works in the ROM's linear table, where the same vibrato swings the register by about the same amount on any note.
    auto swing = [&](int note, uint8_t b5) {
        const uint8_t in[16] = {1, 0x20, 0x0F, 0, 0xFF, b5, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0};
        const auto f = ticksOf(in, 2, kCmdV, 0x44, note, 60, 0);
        int lo = 4096, hi = 0;
        for (size_t t = 4; t < f.size(); ++t) { lo = std::min(lo, f[t]); hi = std::max(hi, f[t]); }
        return hi - lo;
    };
    CHECK(swing(10, 0x00) > 2 * swing(34, 0x00));
    CHECK(swing(10, 0x40) < 2 * swing(34, 0x40));
    CHECK(swing(10, 0x00) > 30);
}
