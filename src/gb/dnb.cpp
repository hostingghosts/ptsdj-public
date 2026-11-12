#include "gb/dnb.h"

#include <cmath>
#include <array>
#include <utility>
#include <cstring>
#include <initializer_list>
#include <map>
#include <vector>

#include "gb/player.h"

namespace gb {

namespace {

constexpr double kPi = 3.14159265358979323846;

// LSDJ note numbers: 1 = C2, 13 = C3, 34 = A4. The wave channel sounds an octave below a pulse for the same number.
constexpr int N(int octave, int semitone) { return 1 + (octave - 2) * 12 + semitone; }
enum { C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

// The chords Am - Am - F - G (i i VI VII of A minor): one phrase played at four transposes from A. The pads and the bass play only the root and the fifth, so the
// same phrase fits every bar and nothing clashes with the melody.
constexpr int8_t kProg[4] = {0, 0, -4, -2};

struct Step { int at, note, instr; int cmd = 0, val = 0; };
using Steps = std::vector<Step>;

class Writer {
public:
    explicit Writer(Song& s) : s_(s) { s_.reset(); }

    int instrument(const char* name, std::initializer_list<uint8_t> bytes) {
        const int i = ni_++;
        uint8_t* in = s_.instrument(i);
        std::memset(in, 0, 16);
        int k = 0;
        for (uint8_t b : bytes) in[k++] = b;
        s_.markInstrument(i);
        std::memset(s_.mem() + off::kInstrNames + i * 5, 0, 5);
        std::memcpy(s_.mem() + off::kInstrNames + i * 5, name, std::strlen(name));
        return i;
    }

    int phrase(const Steps& steps) {
        const int p = np_++;
        for (const Step& st : steps) {
            s_.note(p, st.at) = uint8_t(st.note);
            if (st.note) s_.phraseInstr(p, st.at) = uint8_t(st.instr);
            s_.phraseCmd(p, st.at) = uint8_t(st.cmd);
            s_.phraseVal(p, st.at) = uint8_t(st.val);
        }
        s_.markPhrase(p);
        return p;
    }

    // A chain of phrases; with `prog` each phrase is moved to the chord of its bar (kProg, repeating every four bars).
    int chain(const std::vector<int>& phrases, bool prog = false) {
        const int c = nc_++;
        int k = 0;
        for (int p : phrases) {
            s_.chainPhrase(c, k) = uint8_t(p);
            s_.chainTrans(c, k) = prog ? uint8_t(kProg[k & 3]) : 0;
            ++k;
        }
        s_.markChain(c);
        return c;
    }

    // The same with the transpose of each phrase given (for a chain that follows the chords and then plays something that must stay in A).
    int chainT(const std::vector<int>& phrases, const std::vector<int>& trans) {
        const int c = nc_++;
        for (size_t k = 0; k < phrases.size(); ++k) {
            s_.chainPhrase(c, int(k)) = uint8_t(phrases[k]);
            s_.chainTrans(c, int(k)) = uint8_t(int8_t(trans[k]));
        }
        s_.markChain(c);
        return c;
    }

    void row(int r, int pu1, int pu2, int wav, int noi) {
        s_.songCell(r, 0) = uint8_t(pu1);
        s_.songCell(r, 1) = uint8_t(pu2);
        s_.songCell(r, 2) = uint8_t(wav);
        s_.songCell(r, 3) = uint8_t(noi);
    }

    // A groove of one length (every row or step lasts `ticks` ticks); tables pick it with the G command.
    void groove(int g, int ticks) {
        std::memset(s_.groove(g), 0, 16);
        s_.groove(g)[0] = uint8_t(ticks);
    }

    void clearTable(int t) {
        for (uint16_t base : {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2})
            std::memset(s_.mem() + base + t * 16, 0, 16);
    }
    // One table row: amplitude column (volume x, lasts y ticks), transpose, and two commands.
    void trow(int t, int r, int env, int tr, int c1 = 0, int v1 = 0, int c2 = 0, int v2 = 0) {
        const int o = t * 16 + r;
        s_.at(uint16_t(off::kTblEnv + o)) = uint8_t(env);
        s_.at(uint16_t(off::kTblTrans + o)) = uint8_t(int8_t(tr));
        s_.at(uint16_t(off::kTblCmd1 + o)) = uint8_t(c1);
        s_.at(uint16_t(off::kTblVal1 + o)) = uint8_t(v1);
        s_.at(uint16_t(off::kTblCmd2 + o)) = uint8_t(c2);
        s_.at(uint16_t(off::kTblVal2 + o)) = uint8_t(v2);
    }

    // The 16 frames of a synth drawn into the song (and marked hand drawn): frame k is a sum of harmonics 1..h(k) with weight 1/n^tilt, h growing from
    // `h0` to `h1`, at a level that grows from `g0` to `g1`; with h0 = h1 = 1 it is a plain sine.
    void waveMorph(int synth, int h0, int h1, double tilt, double g0, double g1) {
        for (int k = 0; k < 16; ++k) {
            const int h = h0 + (h1 - h0) * k / 15;
            const double g = g0 + (g1 - g0) * k / 15;
            double peak = 0, v[32];
            for (int i = 0; i < 32; ++i) {
                double x = 0;
                for (int n = 1; n <= h; ++n) x += std::sin(2 * kPi * n * i / 32) / std::pow(double(n), tilt);
                v[i] = x;
                peak = std::fmax(peak, std::fabs(x));
            }
            uint8_t* f = s_.wave(synth * 16 + k);
            std::memset(f, 0, 16);
            for (int i = 0; i < 32; ++i) {
                int q = int(std::lround(7.5 + 7.0 * g * v[i] / peak));
                q = q < 0 ? 0 : q > 15 ? 15 : q;
                f[i / 2] = uint8_t(f[i / 2] | (i & 1 ? q : q << 4));
            }
        }
        s_.at(off::kHandDrawn + 1) = uint8_t(s_.at(off::kHandDrawn + 1) | (1u << synth));
    }

    void project() {
        s_.at(off::kTempo) = uint8_t(kDnbTempo);
        s_.at(off::kSync) = 1;
    }

private:
    Song& s_;
    int ni_ = 0, np_ = 0, nc_ = 0;
};

// Duty and pan byte (b7): the top two bits are the duty (12.5, 25, 50, 75 %), the low two the pan (3 = both sides).
constexpr uint8_t kD12 = 0x03, kD25 = 0x43, kD50 = 0x83, kD75 = 0xC3;


// Table numbers.
enum { T_ARPM, T_ARPJ, T_LEAD, T_PUMP, T_RISE, T_SUB, T_PUMP2, T_KICK, T_SNTN };

// The kit notes: 16 * (sample of kit 2 + 1) + (sample of kit 1 + 1). Only the clean slices are used: in kit 1 the kicks 1KK 8KK 9KK and the snares 2SN 6SN ASN, in kit 2 the snares FSN ISN and the
// crash LCR. The slices called HH carry the break's own hats in the middle of the sample, off the grid, so the hats come from the noise channel instead.
enum KS : int { KICK = 0x02, KICKH = 0x09, KICKX = 0x0A, SNR = 0x03, SNR2 = 0x60, SNRG = 0x07, SNRA = 0x0B, SNRF = 0x30, CRASH = 0x90 };

}  // namespace

// ------------------------------------------------------------------------------------------------------------------------------------------------
// Song A: the drums (wave channel: AMEN1 + AMEN2), the lead (pulse 1), a slow arpeggio (pulse 2), hats (noise).
// ------------------------------------------------------------------------------------------------------------------------------------------------
void buildDnbA(Song& s, int kit1, int kit2) {
    Writer w(s);
    w.groove(2, 6);       // a table row every 6 ticks (a step)

    // --- tables -------------------------------------------------------------------------------------------------------------------------------
    // The slow arpeggio of the quiet parts: one row a step, up the chord and back down (the hop returns to row 0). Minor on the Am bars, major on F and G.
    for (int major = 0; major < 2; ++major) {
        const int t = major ? T_ARPJ : T_ARPM, third = major ? 4 : 3;
        const int tr[6] = {0, third, 7, 12, 7, third};
        w.clearTable(t);
        for (int r = 0; r < 6; ++r) w.trow(t, r, 0, tr[r], r == 0 ? kCmdG : 0, 2);
        w.trow(t, 6, 0, 0, kCmdH, 0x00);
    }
    // The lead: the pulse starts at 50 % and the vibrato comes in after a step and a half (row 2 runs again and again, row 3 hops back to it).
    w.clearTable(T_LEAD);
    w.trow(T_LEAD, 0, 0, 0, kCmdG, 2, kCmdW, 2);
    w.trow(T_LEAD, 1, 0, 0);
    w.trow(T_LEAD, 2, 0, 0, kCmdV, 0x22);
    w.trow(T_LEAD, 3, 0, 0, kCmdH, 0x02);
    // The synth kick (pulse 2): a pulse that starts at A3 and falls to C2 in eight ticks, 21 semitones, then stays there (row 8 hops back to row 7). Layered under the kit's kick on the
    // same tick, the click and the thump come from the pulse and the weight from the sample.
    w.clearTable(T_KICK);
    {
        const int fall[8] = {0, -5, -9, -13, -16, -18, -20, -21};
        for (int r = 0; r < 8; ++r) w.trow(T_KICK, r, 0, fall[r]);
        w.trow(T_KICK, 8, 0, -21, kCmdH, 0x07);
    }
    // The synth snare's tone: E3 falling a fifth to A2 in four ticks, under the kit's snare and the noise of song B.
    w.clearTable(T_SNTN);
    {
        const int fall[4] = {0, -3, -5, -7};
        for (int r = 0; r < 4; ++r) w.trow(T_SNTN, r, 0, fall[r]);
        w.trow(T_SNTN, 4, 0, -7, kCmdH, 0x03);
    }

    // --- instruments --------------------------------------------------------------------------------------------------------------------------
    auto kit = [&](const char* nm, uint8_t vol) {
        return w.instrument(nm, {2, vol, uint8_t(kit2), 0, 0, 0, 0, 0x03, 0, uint8_t(kit1), 0xD0, 0, 0, 0, 0xF3, 0});
    };
    const int drum = kit("DRUM", 0x20);               // full level
    const int soft = kit("SOFT", 0x40);               // half level: ghost snares, the intro and the outro
    const int snrS = w.instrument("SNRS", {2, 0x20, uint8_t(kit2), 0, 0, 0, 0, 0x03, 0, uint8_t(kit1), 0xD0, 6, 0, 0, 0xF3, 0});   // 2*6-1 = 11 chunks (31 ms): the snare burst of a fill
    const int lead = w.instrument("LEAD", {0, 0x65, 0, 0, 0xFF, 0, 0x20 | T_LEAD, kD50, 0, 0x30, 0xD0, 0, 0, 0, 0xF3, 0});          // 6 falls to 3 and holds: the level of the break's long notes, now everywhere
    const int leds = w.instrument("LEDS", {0, 0x96, 0, 0, 0xFF, 0, 0, kD25, 0, 0x00, 0xD0, 0, 0, 0, 0xF3, 0});                       // a short pluck
    const int hk = w.instrument("HOOK", {0, 0x56, 0, 0, 0xFF, 0, 0, kD25, 0, 0x00, 0xD0, 0, 0, 0, 0xF3, 0});                       // the second drop's plucks, well under the lead
    const int arpm = w.instrument("ARPM", {0, 0x20, 0, 0, 0xFF, 0, 0x20 | T_ARPM, kD25, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});
    const int arpj = w.instrument("ARPJ", {0, 0x20, 0, 0, 0xFF, 0, 0x20 | T_ARPJ, kD25, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});
    const int skck = w.instrument("SKCK", {0, 0xC4, 0, 0, 0xFF, 0, 0x20 | T_KICK, kD50, 0, 0x00, 0xD0, 0, 0, 0, 0xF3, 0});      // 12 falls to 0 in 12 * 4/360 s = 0.13 s
    const int ssnt = w.instrument("SSNT", {0, 0x83, 0, 0, 0xFF, 0, 0x20 | T_SNTN, kD50, 0, 0x00, 0xD0, 0, 0, 0, 0xF3, 0});      // 8 falls to 0 in 8 * 3/360 s = 0.07 s
    const int nhat = w.instrument("NHAT", {3, 0x23, 0, 0, 0xFF, 0, 0, 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});                // 17 ms
    const int noht = w.instrument("NOHT", {3, 0x39, 0, 0, 0xFF, 0, 0, 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});                // 0.2 s
    const int nshk = w.instrument("NSHK", {3, 0x23, 0, 0, 0xFF, 0, 0, 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});                // 17 ms, soft
    constexpr int HAT_NOTE = 57;

    // --- drums: kick and snare only, one slice per hit --------------------------------------------------------------------------------------
    // Kick on step 0 and 10, snare on 4 and 12 (the two-step), written once and varied by one hit a bar.
    // Every drum phrase also writes its synth layer for pulse 2: a synth kick on each full-level kick of the kit and a synth snare tone on each full-level snare, on the same step, so
    // the two hit together. Ghost snares, the fill's bursts and the half-level beats of the intro and outro get no layer. synthOf maps a drum phrase to its layer.
    std::map<int, int> synthOf;
    auto drumPhrase = [&](const Steps& v) {
        const int p = w.phrase(v);
        Steps layer;
        for (const Step& st : v) {
            if (st.instr != drum) continue;
            const int low = st.note & 0xF;
            if (low == 0x2 || low == 0x9 || low == 0xA) layer.push_back({st.at, N(3, A), skck});
            else if (st.note == SNR || st.note == SNR2) layer.push_back({st.at, N(3, E), ssnt});
        }
        if (!layer.empty()) synthOf[p] = w.phrase(layer);
        return p;
    };
    auto hits = [&](std::initializer_list<std::pair<int, int>> h, int ins = -1) {
        Steps v;
        for (const auto& e : h) v.push_back({e.first, e.second, ins < 0 ? drum : ins});
        return drumPhrase(v);
    };
    const int sil = w.phrase({});
    // The breaks are chopped in the jungle way (after the Amen): a kick on 0 and 2, the snares on 4 and 12, ghost snares in between at half level (soft), and a kick
    // on the and of the third beat. One bar of the cycle is varied each time round, and every eight bars the fills close the block.
    auto brk = [&](std::initializer_list<std::pair<int, int>> h) {
        Steps v;
        for (const auto& e : h) v.push_back({e.first, e.second, (e.second == SNRG || e.second == SNRA) ? soft : drum});
        return drumPhrase(v);
    };
    const int dA = brk({{0, KICK}, {2, KICK}, {4, SNR}, {7, SNRG}, {9, SNRG}, {10, KICK}, {12, SNR}, {15, SNRG}});
    const int dB = brk({{0, KICK}, {2, KICK}, {4, SNR}, {7, SNRG}, {10, KICK}, {11, KICKH}, {12, SNR}, {14, SNRG}});
    const int dC = brk({{0, KICK}, {4, SNR}, {6, KICK}, {7, SNRG}, {10, KICKH}, {12, SNR2}, {13, SNRG}, {15, SNRG}});            // the kick on 6 and a heavier one on 10
    const int dD = brk({{0, KICK}, {2, KICK}, {4, SNR}, {7, SNRG}, {9, SNRG}, {10, KICKH}, {12, SNR}, {14, SNRA}, {15, SNRG}});    // a ghost run to close
    const int dH = hits({{0, KICK}, {8, SNR}});                                                 // half time
    const int dHb = hits({{0, KICK}, {8, SNR}, {11, KICK}});
    const int dX = brk({{0, KICK | CRASH}, {2, KICK}, {4, SNR}, {7, SNRG}, {10, KICKH}, {12, SNR}, {15, SNRG}});                 // the first bar of a drop: kick and crash together
    const int dQ = hits({{0, KICK}, {8, SNR}}, soft);                                           // the same at half level (the intro and the outro)
    const int dFill = drumPhrase({{0, KICK, drum}, {4, SNR, drum}, {10, KICKH, drum}, {12, SNR, drum}, {13, SNRG, snrS}, {14, SNR, snrS}, {15, SNRA, snrS}});
    const int dFill2 = drumPhrase({{0, KICK, drum}, {4, SNR, drum}, {8, SNR, drum}, {10, SNRG, snrS}, {11, SNR, snrS}, {12, SNRA, snrS}, {13, SNR, snrS}, {14, SNRA, snrS}, {15, SNRF, snrS}});
    const int dCr = w.phrase({{0, CRASH, drum}});                                               // a crash by itself: the break and the outro

    // --- lead (pulse 1): absolute notes over Am Am F G, only chord tones and the odd passing note ---------------------------------------------------------
    auto line = [&](int ins, std::initializer_list<std::array<int, 3>> v3) {            // {step, note, ticks before the K, 0 = none}
        Steps v;
        for (const auto& e : v3) v.push_back({e[0], e[1], ins, e[2] ? kCmdK : 0, e[2]});
        return w.phrase(v);
    };
    const int l1 = line(lead, {{0, N(4, A), 0}, {6, N(5, C), 0}, {10, N(5, E), 0x1A}});
    const int l2 = line(lead, {{0, N(5, D), 0}, {6, N(5, C), 0}, {10, N(4, A), 0x1A}});
    const int l3 = line(lead, {{0, N(5, C), 0}, {6, N(4, A), 0}, {10, N(5, C), 0x1A}});
    const int l4 = line(lead, {{0, N(5, D), 0}, {6, N(4, B), 0}, {10, N(4, G), 0x1A}});
    const int m1 = line(lead, {{0, N(5, E), 0}, {4, N(5, D), 0}, {6, N(5, C), 0}, {10, N(5, E), 0x1A}});
    const int m2 = line(lead, {{0, N(5, D), 0}, {6, N(5, E), 0}, {10, N(5, C), 0x1A}});
    const int m3 = line(lead, {{0, N(5, C), 0}, {4, N(4, A), 0}, {8, N(5, F), 0x1A}});
    const int m4 = line(lead, {{0, N(5, D), 0}, {4, N(4, B), 0}, {8, N(5, G), 0x1A}});
    // The hook of the second drop: eighth notes up and down the chord, short plucks.
    const int hook[4][4] = {{N(4, A), N(5, C), N(5, E), N(5, A)}, {N(4, A), N(5, C), N(5, E), N(5, A)}, {N(4, A), N(5, C), N(5, F), N(5, A)}, {N(4, B), N(5, D), N(5, G), N(5, B)}};
    auto hookBar = [&](int bar, bool end) {
        const int* t = hook[bar];
        const int ix[8] = {0, 1, 2, 3, 2, 1, 2, end ? 3 : 1};
        Steps v;
        for (int i = 0; i < 8; ++i) v.push_back({i * 2, t[ix[i]], hk});
        return w.phrase(v);
    };
    const int h1 = hookBar(0, false), h2 = hookBar(1, true), h3 = hookBar(2, false), h4 = hookBar(3, true);
    // The break: one long note a bar, held nearly to the end, with the vibrato from the table.
    auto longNote = [&](int note) { return w.phrase({{0, note, lead, kCmdK, 0x5C}}); };
    const int b4 = longNote(N(4, B));
    const int b5 = longNote(N(5, A)), b6 = longNote(N(5, E)), b7 = longNote(N(5, F)), b8 = longNote(N(5, D));
    // The intro and the outro: two notes a bar, root and third of the chord.
    const int i1 = line(leds, {{0, N(4, A), 0}, {8, N(5, C), 0}});
    const int i3 = line(leds, {{0, N(5, C), 0}, {8, N(4, A), 0}});
    const int i4 = line(leds, {{0, N(4, B), 0}, {8, N(5, D), 0}});

    // --- slow arpeggio (pulse 2), written in A and moved by the chain ---------------------------------------------------------------------------
    const int aM = w.phrase({{0, N(3, A), arpm, kCmdK, 0x5C}}), aJ = w.phrase({{0, N(3, A), arpj, kCmdK, 0x5C}});

    // --- noise: hats and the rolls ------------------------------------------------------------------------------------------------------------
    // Eighth hats, the pan alternating left and right (O); the open hat on the last off-beat of the bar.
    auto hats = [&](bool open, bool pick) {
        Steps v;
        for (int i = 0; i < 16; i += 2) {
            const bool o = open && i == 14;
            v.push_back({i, HAT_NOTE, o ? noht : nhat, kCmdO, o ? 3 : ((i & 2) ? 2 : 1)});
        }
        if (pick) v.push_back({15, HAT_NOTE, nhat, kCmdO, 2});
        return w.phrase(v);
    };
    const int nh = hats(false, false), nho = hats(true, false), nhp = hats(true, true);
    Steps shk;
    for (int i = 2; i < 16; i += 4) shk.push_back({i, HAT_NOTE, nshk, kCmdO, (i & 4) ? 2 : 1});
    const int nsh = w.phrase(shk);

    // --- chains: eight bars each (four in the second build) ------------------------------------------------------------------------------------
    auto c8 = [&](std::initializer_list<int> p, bool prog = false) { return w.chain(std::vector<int>(p), prog); };
    // The synth layer of a drum chain (pulse 2): the layer phrase of each drum phrase, silence where it has none.
    auto kc = [&](std::initializer_list<int> p) {
        std::vector<int> v;
        for (int d : p) { const auto it = synthOf.find(d); v.push_back(it == synthOf.end() ? sil : it->second); }
        return w.chain(v);
    };
    // Intro: a crash, a soft beat in the second half, the shaker, the lead's two notes.
    const int chDrumIntro = c8({dCr, sil, sil, sil, dQ, dQ, dQ, dQ});
    const int chLeadIntro = c8({sil, sil, sil, sil, i1, i1, i3, i4});
    const int chArpIntro = c8({aM, aM, aJ, aJ, aM, aM, aJ, aJ}, true);
    const int chNoiIntro = c8({sil, sil, sil, sil, nsh, nsh, nsh, nsh});
    // Build: half time, then the break; the roll closes it.
    const int chDrumBuild = c8({dH, dH, dHb, dH, dA, dA, dB, dFill});
    const int chLeadBuild = c8({sil, sil, sil, sil, l1, l2, l3, l4});
    const int chSynBuild = kc({dH, dH, dHb, dH, dA, dA, dB, dFill});
    const int chNoiBuild = c8({nsh, nsh, nsh, nsh, nh, nh, nh, nsh});
    // Drop 1: the break, the lead and the hats; nothing else on A.
    const int chDrumDrop1a = c8({dX, dA, dB, dA, dC, dA, dB, dFill});
    const int chDrumDrop1b = c8({dX, dB, dA, dC, dD, dA, dB, dFill2});
    const int chSynDrop1a = kc({dX, dA, dB, dA, dC, dA, dB, dFill});
    const int chSynDrop1b = kc({dX, dB, dA, dC, dD, dA, dB, dFill2});
    const int chLeadDrop1a = c8({l1, l2, l3, l4, l1, l2, l3, l4});
    const int chLeadDrop1b = c8({m1, m2, m3, m4, l1, l2, l3, l4});
    const int chNoiDrop1a = c8({nh, nh, nh, nh, nho, nho, nho, nho});
    const int chNoiDrop1b = c8({nho, nho, nho, nho, nho, nho, nho, nho});
    // Break: no drums but a crash, the long notes and the slow arpeggio.
    const int chDrumBreak = c8({dCr, sil, sil, sil, dCr, sil, sil, sil});
    const int chLeadBreak = c8({sil, sil, sil, b4, b5, b6, b7, b8});            // out for the first three bars (about 0:45 to 0:49), back on bar 35
    const int chArpBreak = c8({aM, aM, aJ, aJ, aM, aM, aJ, aJ}, true);
    const int chNoiBreak = c8({sil, sil, sil, sil, sil, sil, sil, sil});
    // Build 2: four bars of tightening fills and a roll.
    const int chDrumBuild2 = w.chain({dH, dA, dFill2, dFill});
    const int chLeadBuild2 = w.chain({sil, sil, sil, sil});
    const int chArpBuild2 = w.chain({aM, aM, aJ, aJ}, true);
    const int chNoiBuild2 = w.chain({nsh, nh, nh, nsh});
    // Drop 2: the hook, the break with more kicks, the open hats.
    const int chDrumDrop2a = c8({dX, dB, dA, dC, dB, dA, dB, dFill});
    const int chDrumDrop2b = c8({dX, dD, dB, dC, dA, dD, dB, dFill2});
    const int chSynDrop2a = kc({dX, dB, dA, dC, dB, dA, dB, dFill});
    const int chSynDrop2b = kc({dX, dD, dB, dC, dA, dD, dB, dFill2});
    const int chLeadDrop2a = c8({h1, h2, h3, h4, h1, h2, h3, h4});
    const int chLeadDrop2b = c8({m1, m2, m3, m4, h1, h2, h3, h4});
    const int chNoiDrop2a = c8({nhp, nhp, nhp, nhp, nhp, nhp, nhp, nhp});
    const int chNoiDrop2b = c8({nhp, nhp, nhp, nhp, nhp, nhp, nhp, nh});
    // Outro: the break at half level, thinning out; the lead's two notes, the arpeggio alone at the end.
    const int chDrumOutro = c8({dQ, dQ, dQ, dQ, dCr, sil, sil, sil});
    const int chLeadOutro = c8({i1, i1, i3, i4, sil, sil, sil, sil});
    const int chArpOutro = c8({aM, aM, aJ, aJ, aM, aM, aJ, aJ}, true);
    const int chNoiOutro = c8({nh, nh, nh, nsh, nsh, nsh, nsh, sil});

    int r = 0;
    w.row(r++, chLeadIntro, chArpIntro, chDrumIntro, chNoiIntro);
    w.row(r++, chLeadBuild, chSynBuild, chDrumBuild, chNoiBuild);
    w.row(r++, chLeadDrop1a, chSynDrop1a, chDrumDrop1a, chNoiDrop1a);
    w.row(r++, chLeadDrop1b, chSynDrop1b, chDrumDrop1b, chNoiDrop1b);
    w.row(r++, chLeadBreak, chArpBreak, chDrumBreak, chNoiBreak);
    w.row(r++, chLeadBuild2, chArpBuild2, chDrumBuild2, chNoiBuild2);
    w.row(r++, chLeadDrop2a, chSynDrop2a, chDrumDrop2a, chNoiDrop2a);
    w.row(r++, chLeadDrop2b, chSynDrop2b, chDrumDrop2b, chNoiDrop2b);
    w.row(r++, chLeadOutro, chArpOutro, chDrumOutro, chNoiOutro);
    (void)chNoiBreak;
    w.project();
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
// Song B: the bass (wave channel), root-and-fifth pads (both pulses), a snare layer and the risers.
// ------------------------------------------------------------------------------------------------------------------------------------------------
void buildDnbB(Song& s) {
    Writer w(s);
    w.groove(2, 6);

    // --- waves ----------------------------------------------------------------------------------------------------------------------------------
    w.waveMorph(0, 1, 1, 1.0, 1.0, 1.0);          // synth 0: a plain sine, the sub
    w.waveMorph(1, 1, 5, 1.2, 0.8, 1.0);          // synth 1: sine opening up to a soft saw over the bar, the reese
    w.waveMorph(2, 1, 3, 1.6, 0.7, 0.9);          // synth 2: darker, for the break

    // --- tables ---------------------------------------------------------------------------------------------------------------------------------
    // The pump ("sidechain"): the volume dips to 1 on the kick and climbs back to 3 in 7 ticks; the amplitude column holds the last level after row 4.
    w.clearTable(T_PUMP);
    w.trow(T_PUMP, 0, 0x11, 0);
    w.trow(T_PUMP, 1, 0x21, 0);
    w.trow(T_PUMP, 2, 0x32, 0);
    w.trow(T_PUMP, 3, 0x33, 0);
    w.clearTable(T_PUMP2);                                  // the same for the fifth, which sits a level lower
    w.trow(T_PUMP2, 0, 0x11, 0);
    w.trow(T_PUMP2, 1, 0x21, 0);
    w.trow(T_PUMP2, 2, 0x22, 0);
    w.trow(T_PUMP2, 3, 0x23, 0);
    // The riser: a climb through A minor pentatonic, a note every step (6 ticks) over the bar, and the volume rising through the amplitude column (8 levels of 12 ticks).
    w.clearTable(T_RISE);
    {
        const int climb[16] = {0, 0, 3, 3, 5, 5, 10, 10, 12, 12, 15, 15, 17, 17, 22, 22};          // A C D G, two octaves: no semitone or tritone against the roots and fifths of Am F G
        for (int i = 0; i < 16; ++i) w.trow(T_RISE, i, i < 8 ? ((i + 2) << 4 | 12) : 0, climb[i], i == 0 ? kCmdG : 0, 2);
    }
    // The sub: a slow vibrato that comes in after four steps and then stays (row 4 runs again and again, row 5 hops back to it).
    w.clearTable(T_SUB);
    w.trow(T_SUB, 0, 0, 0, kCmdG, 2);
    w.trow(T_SUB, 4, 0, 0, kCmdV, 0x12);
    w.trow(T_SUB, 5, 0, 0, kCmdH, 0x04);

    // --- instruments --------------------------------------------------------------------------------------------------------------------------
    // Wave: b1 volume (0x20 = full), b2 loop point (F = the start), b3 first frame (synth x 16), b9 play mode (2 = loop), b10 length (frames = 16 - low nibble),
    // b11 speed (+4 = ticks a frame).
    const int sub = w.instrument("SUB", {1, 0x20, 0x0F, 0x00, 0xFF, 0, 0x20 | T_SUB, 0x03, 0, 0, 0, 0, 0, 0, 0, 0});
    const int rees = w.instrument("REESE", {1, 0x20, 0x0F, 0x10, 0xFF, 0, 0, 0x03, 0, 2, 0, 2, 0, 0, 0, 0});               // 16 frames of 6 ticks: the sweep starts again on every note
    const int dark = w.instrument("DARK", {1, 0x40, 0x0F, 0x20, 0xFF, 0, 0, 0x03, 0, 2, 0, 2, 0, 0, 0, 0});               // the break's note, half level
    const int stab = w.instrument("STAB", {0, 0xA5, 0, 0, 0xFF, 0, 0, kD25, 0, 0x00, 0xD0, 0, 0, 0, 0xF3, 0});             // 11 falls to 0
    const int pad = w.instrument("PAD", {0, 0x30, 0, 0, 0xFF, 0, 0x20 | T_PUMP, kD50, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});       // the root, held at 3, pumped
    const int pad2 = w.instrument("PAD2", {0, 0x20, 0, 0, 0xFF, 0, 0x20 | T_PUMP2, kD25, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});    // the fifth, held at 2, pumped
    const int rise = w.instrument("RISE", {0, 0x20, 0, 0, 0xFF, 0, 0x20 | T_RISE, kD75, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});
    const int nsn = w.instrument("NSNR", {3, 0xB4, 0, 0, 0xFF, 0, 0, 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});
    const int nrol = w.instrument("NROL", {3, 0x10, 0, 0, 0xFF, 0, 0, 0x03, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0});
    constexpr int SNR_NOTE = 35, HAT_NOTE = 53;

    const int sil = w.phrase({});
    // --- bass (wave), written in A: the root, its octave (12) and its fifth (7) only, so one phrase follows every chord; {step, interval, K ticks} -----
    auto bass = [&](int ins, std::initializer_list<std::array<int, 3>> hits) {
        Steps v;
        for (const auto& e : hits) v.push_back({e[0], N(2, A) + e[1], ins, kCmdK, e[2]});
        return w.phrase(v);
    };
    const int bsA = bass(rees, {{0, 0, 0x1C}, {6, 12, 0x08}, {10, 0, 0x0C}});
    const int bsB = bass(rees, {{0, 0, 0x16}, {6, 12, 0x08}, {10, 0, 0x0E}, {13, 7, 0x0A}});
    const int bsC = bass(rees, {{0, 0, 0x2E}, {8, 0, 0x2E}});                                                   // half time, for the builds
    const int bsD = bass(rees, {{0, 0, 0x14}, {4, 12, 0x08}, {6, 0, 0x0C}, {10, 0, 0x0E}, {12, 12, 0x08}, {14, 7, 0x08}});
    const int bsE = bass(rees, {{0, 0, 0x0C}, {4, 0, 0x0C}, {8, 0, 0x0C}, {12, 12, 0x0C}});                    // the fill: it steps up
    const int bsS = bass(sub, {{0, 0, 0x5C}});                                                                 // one long sub note a bar
    const int bsT = bass(sub, {{0, 0, 0x2C}, {8, 7, 0x2C}});                                                   // root then fifth
    const int bsL = bass(dark, {{0, 12, 0x5C}});                                                               // the break's dark morphing note, an octave up

    // --- pads and stabs (pulse): the root on pulse 1, the fifth on pulse 2, pumped on the kicks at step 0 and 10 -------------------------------------------
    const int pdR = w.phrase({{0, N(3, A), pad}, {10, 0, 0, kCmdA, T_PUMP}});
    const int pdF = w.phrase({{0, N(3, E), pad2}, {10, 0, 0, kCmdA, T_PUMP2}});
    const int pbR = w.phrase({{0, N(3, A), pad}}), pbF = w.phrase({{0, N(3, E), pad2}});        // the break has no kick to duck for
    auto stabs = [&](std::initializer_list<int> at) {
        Steps v;
        for (int a : at) v.push_back({a, N(4, A), stab});
        return w.phrase(v);
    };
    const int stA = stabs({6, 14}), stB = stabs({3, 6, 14});
    const int rs = w.phrase({{0, N(3, A), rise}});

    // --- noise: the snare's layer and the risers ------------------------------------------------------------------------------------------------
    const int nsc = w.phrase({{4, SNR_NOTE, nsn}, {12, SNR_NOTE, nsn}});
    const int nsc1 = w.phrase({{8, SNR_NOTE, nsn}});
    const int nsw = w.phrase({{0, HAT_NOTE, nrol, kCmdR, 0x1C}, {9, 0, 0, kCmdR, 0x0C}});                                              // a slow riser: a hit every 12 ticks, one level louder each, up to 6, then held
    const int nsx = w.phrase({{0, 0, 0, kCmdR, 0x06}, {15, 0, 0, kCmdK, 0x00}});                              // and held, a hit every 6 ticks (never quicker: that buzzes), then cut for the drop

    // --- chains ---------------------------------------------------------------------------------------------------------------------------
    auto c8 = [&](std::initializer_list<int> p, bool prog = false) { return w.chain(std::vector<int>(p), prog); };
    const int padsR = c8({pdR, pdR, pdR, pdR, pdR, pdR, pdR, pdR}, true);
    const int padsF = c8({pdF, pdF, pdF, pdF, pdF, pdF, pdF, pdF}, true);
    const int padsFlate = c8({sil, sil, sil, sil, pdF, pdF, pdF, pdF}, true);
    const int breakR = c8({pbR, pbR, pbR, pbR, pbR, pbR, sil, sil}, true);                                    // the pads stop for the riser: it climbs through notes that would clash with them
    const int breakF = w.chainT({pbF, pbF, pbF, pbF, pbF, pbF, rs, rs}, {0, 0, -4, -2, 0, 0, 0, 0});            // the riser stays in A
    const int mute8 = c8({sil, sil, sil, sil, sil, sil, sil, sil});
    // Intro: the pads pumping, the sub on the root, then the root and fifth.
    const int chIntroSub = c8({bsS, bsS, bsS, bsS, bsT, bsT, bsT, bsT}, true);
    // Build: the reese comes in half time in the second half, a clap-less snare layer on the third beat, the roll.
    const int chBuildBass = c8({bsS, bsS, bsS, bsS, bsC, bsC, bsC, bsC}, true);
    const int chBuildNoi = c8({sil, sil, sil, sil, nsc1, nsc1, nsc1, nsc1});
    // Drops: the pads and the reese; the snare layer.
    const int chDrop1Bass = c8({bsA, bsA, bsB, bsA, bsA, bsB, bsA, bsE}, true);
    const int chDrop1bBass = c8({bsB, bsA, bsB, bsA, bsB, bsB, bsA, bsE}, true);
    const int chDrop1Noi = c8({nsc, nsc, nsc, nsc, nsc, nsc, nsc, nsc});
    const int chDrop1Stab = c8({sil, sil, sil, sil, sil, sil, sil, sil});
    // Break: pads, the dark note, no drums; the riser in the last two bars.
    const int chBreakBass = c8({bsL, bsL, bsL, bsL, bsL, bsL, bsL, bsL}, true);
    const int chBreakNoi = c8({sil, sil, sil, sil, sil, sil, nsw, nsx});
    // Build 2: a riser every bar over the bass and the fills, then the roll.
    const int chBuild2Stab = w.chain({sil, sil, sil, sil});
    const int chBuild2Rise = w.chain({rs, rs, rs, rs});
    const int chBuild2Bass = w.chain({bsC, bsC, bsE, bsE}, true);
    const int chBuild2Noi = w.chain({sil, nsc1, nsc1, nsc1});
    // Drop 2: the busier bass, offbeat stabs, the pads.
    const int chDrop2Stab = c8({stA, stA, stA, stA, stA, stA, stA, stB}, true);
    const int chDrop2Bass = c8({bsD, bsB, bsD, bsB, bsD, bsB, bsD, bsE}, true);
    const int chDrop2bBass = c8({bsD, bsD, bsB, bsD, bsD, bsD, bsB, bsE}, true);
    // Outro: pads, the sub, the stabs once more.
    const int chOutroSub = c8({bsT, bsT, bsT, bsT, bsS, bsS, bsS, sil}, true);

    int r = 0;
    w.row(r++, padsR, padsFlate, chIntroSub, mute8);
    w.row(r++, padsR, padsF, chBuildBass, chBuildNoi);
    w.row(r++, padsR, padsF, chDrop1Bass, chDrop1Noi);
    w.row(r++, padsR, padsF, chDrop1bBass, chDrop1Noi);
    w.row(r++, breakR, breakF, chBreakBass, chBreakNoi);
    w.row(r++, chBuild2Stab, chBuild2Rise, chBuild2Bass, chBuild2Noi);
    w.row(r++, chDrop2Stab, padsF, chDrop2Bass, chDrop1Noi);
    w.row(r++, chDrop2Stab, padsF, chDrop2bBass, chDrop1Noi);
    w.row(r++, padsR, padsFlate, chOutroSub, mute8);
    (void)chDrop1Stab;
    w.project();
}

}  // namespace gb
