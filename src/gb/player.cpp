#include "gb/player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gb {

namespace {
#include "gb/tables.inc"
// Envelope step period in 1/360 s for release digit 1..15 (measured).
const uint8_t kEnvPeriod[16] = {0, 1, 2, 3, 4, 6, 8, 11, 15, 20, 27, 36, 48, 64, 86, 115};
constexpr uint32_t kApuHead = 92628;
constexpr int kSlideDelay = 0;
constexpr bool kAStartsNow = false;
constexpr double kTick = 10485760.0;      // cycles per tick = kTick / tempo
constexpr long long kSub = 70224 / 6;   // six steps per video frame (358.4 Hz), measured from vibrato beat periods
constexpr long long kKitChunk = 11712;   // cycles per 32-sample wave RAM chunk: one chunk every 2.79 ms (11.47 kHz sample rate)
}  // namespace

void Player::start(int row) {
    apu_.reset();
    apu_.write(0xFF26, 0x80);
    apu_.write(0xFF24, 0x77);
    apu_.write(0xFF25, 0xFF);
    apu_.advance(kApuHead);                               // the ROM powers the chip on about 92600 cycles before the first tick (frame sequencer phase)
    for (auto& v : v_) v = Voice();
    tempo_ = s_.at(off::kTempo);
    if (tempo_ < 40) tempo_ = 40;
    nr50_ = 0x77;
    rngState_ = 0x0052;                                   // LSDJ seeds the generator with 0x52 whenever playback starts
    seq_.start(row);
    tickNo_ = -1;
    running_ = true;
    hpInit_ = false;
    now_ = 0;
    next360_ = 0;
    tickAcc_ = 896 - tempo_;                              // the first tick comes with the first sub-step
}

void Player::writePan() {
    uint8_t n = 0;
    for (int c = 0; c < 4; ++c) {
        if (v_[c].pan & 1) n |= uint8_t(0x10 << c);
        if (v_[c].pan & 2) n |= uint8_t(1 << c);
    }
    apu_.write(0xFF25, n);
}

void Player::killNote(int ch) {
    Voice& v = v_[ch];
    v.on = false;
    if (ch == 2) { apu_.write(0xFF1A, 0x00); return; }   // the ROM switches the wave DAC off when a note is killed
    const uint16_t r = ch == 0 ? 0xFF12 : ch == 1 ? 0xFF17 : 0xFF21;
    for (int i = v.vol; i > 0; --i) { apu_.write(r, 0x09); apu_.write(r, 0x11); apu_.write(r, 0x18); }   // same stepping the envelope uses
    v.vol = 0;
}

void Player::noteOn(int ch, const StepEvent& e) {
    Voice& v = v_[ch];
    if (e.instr != 0xFF) v.instr = e.instr & 63;
    v.noteTick = tickNo_;
    const uint8_t* in = instr(v.instr);
    int n = e.note;
    {   // transposes that would leave the note range are ignored below it and wrap above it (measured on LSDJ 9.4.2)
        // each transpose is range-checked on its own, chain first, then the project's (measured on PIGGIES)
        auto fit = [](int from, int to) { if (to < 1) return from; if (to > 135) to -= 136; return std::clamp(to, 1, 127); };
        if (!((in[0] & 3) != 2 && (in[5] & 0x20))) {      // pulse, wave and noise instruments with byte 5 bit 5 set play untransposed (measured)
            n = fit(n, n + e.transpose);
            if (ch != 3) n = fit(n, n + int8_t(s_.at(off::kTranspose)));
        }
        n = std::clamp(n, 1, 127);
    }
    v.note = n;
    v.base = n;
    v.tTrans = 0;
    v.fade = -1;
    v.vibDepth = 0;
    v.vibSemis = 0;
    v.bendRate = v.bendAcc = 0;
    v.chordOn = false; v.chordOfs = 0;
    v.retrig = -1; v.retrigDv = 0;
    v.noiseS = 0; v.shapeOfs = 0; v.shapeAcc = 0;
    v.bendTgt = false;
    v.fine = -1; v.tune2 = 0;                                // F lasts until the next note (measured)
    v.startedTick = tickNo_;
    v.on = true;
    v.hwEnv = false;
    v.envCount = 0;
    v.envSteps = 0;
    v.envStage = 1;
    v.kill = -1;
    v.pan = in[7] & 3;
    writePan();
    if (muted_[ch]) return;
    const uint8_t lenEn = (in[3] & 0x40) ? 0x40 : 0;
    switch (ch) {
        case 0:
        case 1: {
            const uint16_t f = kPulseFreq[n];
            const uint16_t base = ch == 0 ? 0xFF10 : 0xFF15;
            if (ch == 0) apu_.write(0xFF10, uint8_t(~in[4]));
            apu_.write(base + 1, uint8_t((in[7] & 0xC0) | (in[3] & 0x3F)));
            apu_.write(base + 2, uint8_t((in[1] & 0xF0) | 8));
            apu_.write(base + 3, uint8_t(f & 0xFF));
            apu_.write(base + 4, uint8_t(0x80 | lenEn | (f >> 8)));
            v.vol = v.baseVol = in[1] >> 4;
            break;
        }
        case 2: {
            if ((in[0] & 3) == 2) { startKit(e); break; }
            v.kPlay = false;
            apu_.write(0xFF1C, in[1]);
            v.wPos = 0; v.wDir = 1; v.wCnt = 0; v.wAnim = (in[9] & 3) != 0;
            loadWave(in[3]);
            break;
        }
        default:
            apu_.write(0xFF20, in[3] & 0x3F);
            apu_.write(0xFF21, uint8_t((in[1] & 0xF0) | 8));
            apu_.write(0xFF22, kNoiseNr43[n]);
            apu_.write(0xFF23, uint8_t(0x80 | lenEn));
            v.vol = v.baseVol = in[1] >> 4;
            break;
    }
    if (ch < 2) refreshPitch(ch);                   // pulse 2 tune and the fine tune apply from the first moment
    v.tStep = false;
    if (in[6] & 0x20) {
        const int tb = in[6] & 0x1F;
        if (in[5] & 8) {                                  // byte 5 bit 3 (measured): one table row per note; the position carries over between notes
            if (v.stepTbl != tb) { startTable(ch, tb); v.tRow = v.stepRow[tb]; v.stepTbl = tb; }
            else { v.tbl = tb; v.eRow = 0; v.eWait = 0; }
            v.tStep = v.tStepNow = true;
        } else { startTable(ch, tb); v.stepTbl = -1; }
    } else { v.tbl = -1; v.stepTbl = -1; }
}

// Kit instrument: note n = 16 * b + a plays sample a-1 of kit 1 (byte 9) and sample b-1 of kit 2 (byte 2); both together are mixed
// nibble by nibble as clip(x + y - 8). Byte 8 shifts the wave frequency (signed), byte 11 limits the length in chunks (0 = whole
// sample), byte 13 is the start offset in chunks, bit 5 of byte 5 loops the sample, and the last chunk of a sample is not played (all measured on LSDJ 9.4.2).
void Player::startKit(const StepEvent& e) {
    Voice& v = v_[2];
    const uint8_t* in = instr(v.instr);
    v.kPlay = false;
    v.wAnim = false;
    if (!kits_) return;
    v.kit[0] = kits_->kit(in[9] & 0x7F);
    v.kit[1] = kits_->kit(in[2] & 0x7F);
    const int a = e.note & 15, bb = e.note >> 4;
    v.kHas[0] = a > 0 && v.kit[0].range(a - 1, v.kBeg[0], v.kEnd[0]);
    v.kHas[1] = bb > 0 && v.kit[1].range(bb - 1, v.kBeg[1], v.kEnd[1]);
    if (!v.kHas[0] && !v.kHas[1]) return;
    v.kPos = in[13] * 16;
    v.kChunks = 0;
    v.kLimit = in[11] ? in[11] * 2 - 1 : 0;           // byte 11 = n lets 2n - 1 chunks play (measured on 9.4.2)
    v.kLoop = (in[5] & 0x20) != 0;                  // only bit 5 loops the sample (measured)
    v.kFreq = std::clamp(0x749 + int8_t(in[8]), 0, 0x7FF);
    v.kPlay = true;
    apu_.write(0xFF1C, in[1]);
    nextKit_ = now_;
    kitChunk();
    nextKit_ = now_ + kKitChunk;
}

void Player::kitChunk() {
    Voice& v = v_[2];
    if (!v.kPlay || muted_[2]) return;
    int len[2] = {0, 0}, longest = 0;
    for (int k = 0; k < 2; ++k) if (v.kHas[k]) { len[k] = v.kEnd[k] - v.kBeg[k]; longest = std::max(longest, len[k]); }
    if (v.kPos + 32 > longest || (v.kLimit && v.kChunks >= v.kLimit)) {
        if (v.kLoop && (!v.kLimit || v.kChunks < v.kLimit)) { v.kPos = 0; }
        else { v.kPlay = false; apu_.write(0xFF1A, 0x00); return; }
    }
    uint8_t w[16];
    for (int i = 0; i < 16; ++i) {
        int nib[2] = {8, 8};
        int hi = 8, lo = 8;
        auto fetch = [&](int k, int byte) -> uint8_t { return (v.kHas[k] && byte < len[k]) ? v.kit[k].data[v.kBeg[k] + byte] : 0x88; };
        const uint8_t x = fetch(0, v.kPos + i), y = fetch(1, v.kPos + i);
        if (v.kHas[0] && v.kHas[1]) {
            hi = std::clamp((x >> 4) + (y >> 4) - 8, 0, 15); lo = std::clamp((x & 15) + (y & 15) - 8, 0, 15);
            w[i] = uint8_t(hi << 4 | lo);
        } else w[i] = v.kHas[0] ? x : y;
        (void)nib;
    }
    v.kPos += 16;
    ++v.kChunks;
    if (waveHook_) waveHook_(v.kPos, tickNo_);
    apu_.write(0xFF1A, 0x00);
    for (int i = 0; i < 16; ++i) apu_.write(0xFF30 + i, w[i]);
    apu_.write(0xFF1A, 0x80);
    apu_.write(0xFF1D, 0xE0);
    apu_.write(0xFF1E, 0x87);
    apu_.write(0xFF1D, uint8_t(v.kFreq & 0xFF));
    apu_.write(0xFF1E, uint8_t(v.kFreq >> 8));
}

// A new wave frame: DAC off, load the 16 bytes, DAC on, trigger with a dummy frequency, then the real one (as LSDJ does).
void Player::loadWave(int frame) {
    Voice& v = v_[2];
    const uint8_t* in = instr(v.instr);
    const uint16_t f = kWaveFreq[std::clamp(v.base + v.tTrans + v.chordOfs, 1, 127)];
    if (waveHook_) waveHook_(frame & 255, tickNo_);
    apu_.write(0xFF1A, 0x00);
    const uint8_t* w = s_.wave(frame & 255);
    for (int i = 0; i < 16; ++i) apu_.write(0xFF30 + i, w[i]);
    apu_.write(0xFF1A, 0x80);
    apu_.write(0xFF1D, 0xE0);
    apu_.write(0xFF1E, 0x87);
    apu_.write(0xFF1D, uint8_t(f & 0xFF));
    apu_.write(0xFF1E, uint8_t(((in[7] & 0x40) ? 0x40 : 0) | (f >> 8)));
}

void Player::waveTick() {
    Voice& v = v_[2];
    if (!v.on || !v.wAnim || muted_[2]) return;
    const uint8_t* in = instr(v.instr);
    const int interval = std::max(1, int(int8_t(in[11])) + 4);
    if (++v.wCnt < interval) return;
    v.wCnt = 0;
    const int len = in[10] & 15;
    // N = 16 - len frames per pass, frame = min(15, floor(k * 16 / (15 - len))) (fitted to the ROM's sequences)
    const int n = 16 - len;
    const double step = len >= 15 ? 99.0 : 16.0 / (15 - len);
    const int rep = (in[2] & 15) == 15 ? 0 : (in[2] & 15);
    const int mode = in[9] & 3;
    int k = int(v.wPos) + 1;
    if (k >= n) {
        if (mode == 1) { v.wAnim = false; return; }       // once: hold the last frame
        k = 0; while (k < n - 1 && std::floor(k * step) < rep) ++k;   // loop back to the repeat point
    }
    v.wPos = k;
    loadWave(in[3] + std::min(15, int(std::floor(k * step + 1e-9))));
}

void Player::writeDuty(int ch, int duty) {
    const uint8_t* in = instr(v_[ch].instr);
    const uint16_t r = ch == 0 ? 0xFF11 : 0xFF16;
    apu_.write(r, uint8_t((duty & 3) << 6 | (in[3] & 0x3F)));
}

void Player::setVolume(int ch, int target) {
    Voice& v = v_[ch];
    if (ch == 2 || muted_[ch]) return;
    const uint16_t r = ch == 0 ? 0xFF12 : ch == 1 ? 0xFF17 : 0xFF21;
    target = std::clamp(target, 0, 15);
    v.baseVol = target;
    { const uint8_t* in = instr(v.instr); v.envStage = target > (in[9] >> 4) ? 1 : target > (in[10] >> 4) ? 2 : 3; }
    while (v.vol < target) { apu_.write(r, 0x08); ++v.vol; }
    while (v.vol > target) { apu_.write(r, 0x09); apu_.write(r, 0x11); apu_.write(r, 0x18); --v.vol; }
}

// Vibrato depth digit -> peak semitones (measured), and the wave shapes selected by the P+L+VIB byte.
static const double kVibDepth[16] = {0, 0.25, 0.375, 0.5, 0.75, 1, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8};
static double vibShape(int shape, double ph) {
    ph -= std::floor(ph);
    switch (shape) {
        case 1: return 2.0 * ph - 1.0;                   // saw: pitch always rises, then jumps back (measured)
        case 3: return 0.0;                              // shape 3: no vibrato (measured)
        case 2: return ph < 0.5 ? 1.0 : -1.0;            // square
        default: return ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4;   // triangle (the ROM's default shape is not a sine)
    }
}


static const uint16_t kLinFreq[110] = {0,19,38,57,76,96,115,134,153,172,191,210,229,248,267,287,306,325,344,363,382,401,420,439,458,478,497,516,535,554,573,592,611,630,649,669,688,707,726,745,764,783,802,821,841,860,879,898,917,936,955,974,993,1012,1032,1051,1070,1089,1108,1127,1146,1165,1184,1203,1223,1242,1261,1280,1299,1318,1337,1356,1375,1395,1414,1433,1452,1471,1490,1509,1528,1547,1566,1586,1605,1624,1643,1662,1681,1700,1719,1738,1757,1777,1796,1815,1834,1853,1872,1891,1910,1929,1948,1968,1987,2006,2025,2044,59589,0};

// Pitch index (x256) in the linear table whose frequency is the wave note's measured frequency.
int Player::waveIndex(int n) {
    static int cache[256];
    static bool ready = false;
    if (!ready) {
        int16_t dummy = 0;
        for (int i = 1; i < 128; ++i) {
            int lo = 0, hi = 107 * 256;               // the table is monotone: find the first index reaching the wanted frequency
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                if (int(pitchToFreq(kLinFreq, mid, dummy)) < int(kWaveFreq[i])) lo = mid + 1; else hi = mid;
            }
            int best = lo;
            if (lo > 0 && std::abs(int(pitchToFreq(kLinFreq, lo - 1, dummy)) - int(kWaveFreq[i])) < std::abs(int(pitchToFreq(kLinFreq, lo, dummy)) - int(kWaveFreq[i]))) best = lo - 1;
            cache[i] = best;
        }
        ready = true;
    }
    if (n >= 1 && n < 128) return cache[n];
    return cache[n < 1 ? 1 : 127];
}

int Player::pitchIndex(int ch, int n) { return ch == 2 ? waveIndex(n) : (n - 1) * 256; }

// L: slide the pitch accumulator to 0 in value + 1 sub-steps (value 0 jumps there); the rate is |acc| / (value + 1), truncated (ROM 02:4363).
void Player::startSlide(int ch, int val) {
    Voice& v = v_[ch];
    if (val == 0) { v.bendAcc = 0; v.bendRate = 0; v.bendTgt = false; return; }
    const int c = std::min(val + 1, 255);
    const int mag = std::abs(int(v.bendAcc)) / c;
    v.bendRate = v.bendAcc > 0 ? -mag : mag;
    v.bendTgt = true;
    v.bendDelay = kSlideDelay;
}

// Bend rate per sub-step for the P value (1/256 semitone): round(v*v/8 + v/2), mirrored for negative values (ROM table at 02:43D8).
int Player::bendRateOf(uint8_t val) {
    const int a = val < 128 ? val : 256 - val;
    const int r = int(std::lround(a * a / 8.0 + a / 2.0));
    return val < 128 ? r : -r;
}

// Pitch (note index * 256 + fraction) -> frequency register, as the ROM does it: indices past the table wrap by 108 (adjusting the bend), -8..-1 give 0,
// and the fraction interpolates with the difference of the low bytes only.
uint16_t Player::pitchToFreq(const uint16_t* tab, int total, int16_t& bendAcc) {
    for (int guard = 0; guard < 4; ++guard) {
        const int d = (total >> 8) & 255, e = total & 255;
        if (d >= 0x6C) {
            if (d >= 0xF8) return 0;
            const int adj = d >= 0xB6 ? 0x6C00 : -0x6C00;
            total += adj; bendAcc = int16_t(bendAcc + adj);
            continue;
        }
        const uint16_t a = tab[d];
        if (!e) return a;
        const uint16_t b = tab[d + 1];
        const int diff = ((b & 255) - (a & 255)) & 255;
        unsigned hl = unsigned((diff * 2) & 255);
        const unsigned carry0 = hl >> 7;
        hl = ((hl << 1) & 255) << 8;
        if (carry0) hl += unsigned(e);
        for (int i = 0; i < 6; ++i) {
            const unsigned c = (hl >> 15) & 1;
            hl = (hl << 1) & 0xFFFF;
            if (c) hl = (hl + unsigned(e)) & 0xFFFF;
        }
        unsigned h = hl >> 8;
        if (hl & 0x80) ++h;
        return uint16_t((a & 0x7FF) + (h & 255));
    }
    return 0;
}

void Player::refreshPitch(int ch) {
    Voice& v = v_[ch];
    if (!v.on || muted_[ch]) return;
    const uint8_t* in = instr(v.instr);
    int n = v.base + v.tTrans + v.chordOfs + (ch == 3 ? v.shapeOfs : 0) + (ch == 1 ? (v.fine >= 0 ? v.tune2 : int8_t(in[2])) : 0);
    if (ch == 3) {                                    // noise: the note index wraps modulo 120 (S and table transposes just add to it)
        n = (((n - 1) % 120) + 120) % 120 + 1;               // ROM index n - 1 wraps modulo 120
        v.note = n;
        apu_.write(0xFF22, kNoiseNr43[n]);
        return;
    }
    v.note = std::clamp(n, 1, 127);
    const uint8_t lenEn = (in[3] & 0x40) ? 0x40 : 0;
    // LSDJ keeps the pitch as note + 8-bit fraction (1/256 semitone) plus bend, vibrato and slide, and interpolates the frequency table (measured, ROM 00:1B28)
    int total = (n - 1) * 256 + v.bendAcc + int(std::lround(v.vibSemis * 256.0));
    uint16_t f;
    if (ch == 2) {                                   // wave instruments look the pitch up in the ROM's linear table (about 19.2 per index) at 00:09B9
        const int baseIdx = (v.bendRate && !v.bendAcc) ? (waveIndex(n) & ~255) : waveIndex(n);   // until its first step a bend works from the whole table index (measured on kicks)
        total = baseIdx + v.bendAcc + int(std::lround(v.vibSemis * 256.0)) + ((v.bendRate || v.bendAcc) ? 6 : 0);   // +6/256 index: the fitted base is that much low once a bend is running (empirical; INFORMAL wave pitch matches exactly)
        f = pitchToFreq(kLinFreq, total, v.bendAcc);
    } else f = pitchToFreq(kPulseFreq + 1, total, v.bendAcc);
    if (ch < 2) {                                   // fine tune detunes in proportion to the period: pulse 1 down, pulse 2 up (fitted to the ROM, within one step; F y = fine y * 8)
        const uint32_t fine = v.fine >= 0 ? uint32_t(v.fine) * 8 : in[11];
        const uint32_t period = 2048u - f;
        const int delta = int((uint64_t(period) * fine * (ch == 0 ? 3869u : 3708u) + (1u << 23)) >> 24);
        f = uint16_t(std::clamp<int>(ch == 0 ? int(f) - delta : int(f) + delta, 0, 2047));
    }
    const uint16_t base = ch == 0 ? 0xFF13 : ch == 1 ? 0xFF18 : 0xFF1D;
    apu_.write(base, uint8_t(f & 0xFF));
    apu_.write(base + 1, uint8_t(lenEn | (f >> 8)));
}

void Player::startTable(int ch, int t) {
    Voice& v = v_[ch];
    v.tbl = t & 31;
    ++v.tGen;
    v.tStep = v.tStepNow = false;                         // a table started by A runs per tick (measured)
    v.tRow = 0; v.tWait = 0; v.eRow = 0; v.eWait = 0; v.tGroove = 0xFF; v.tGroovePos = 0;
    std::memset(v.hop, 0, sizeof v.hop);
    v.lastCmd[1] = v.lastCmd[2] = 0;                      // Z in a table column only remembers commands of the table it runs in (measured on SETBARLO)
}

void Player::tableTick(int ch) {
    Voice& v = v_[ch];
    if (v.tbl < 0 || !v.on) return;
    const int t = v.tbl;
    // amplitude column: x = volume, y = ticks it lasts (rows with y = 0 are skipped)
    if (v.eWait > 0) --v.eWait;
    else {
        for (int guard = 0; guard < 16 && v.eRow < 16; ++guard) {
            const uint8_t e = s_.at(uint16_t(off::kTblEnv + t * 16 + v.eRow++));
            if (e & 15) { setVolume(ch, e >> 4); v.eWait = (e & 15) - 1; break; }
        }
    }
    if (v.tStep) { if (!v.tStepNow) return; v.tStepNow = false; }
    else if (v.tWait > 0 && --v.tWait > 0) return;
    int row = v.tRow;
    const int gen = v.tGen;
    for (int guard = 0; guard < 40; ++guard) {
        const int o = t * 16 + row;
        const uint8_t tr = s_.at(uint16_t(off::kTblTrans + o));
        const int before = std::clamp(v.base + v.tTrans + v.chordOfs, 1, 127);
        v.tTrans = int8_t(tr);
        if (s_.at(uint16_t(off::kTblCmd1 + o)) == kCmdL || s_.at(uint16_t(off::kTblCmd2 + o)) == kCmdL) {   // a slide on a row that moves the pitch starts from the old pitch (measured on a kick)
            const int after = std::clamp(v.base + v.tTrans + v.chordOfs, 1, 127);
            v.bendAcc = int16_t(v.bendAcc + pitchIndex(ch, before) - pitchIndex(ch, after));
            v.base = after; v.tTrans = 0;            // the slide's target becomes the note, like an L on a phrase step
        }
        bool hopped = false;
        v.tWait = 1;
        for (int k = 0; k < 2 && !hopped; ++k) {
            const uint8_t c = s_.at(uint16_t((k ? off::kTblCmd2 : off::kTblCmd1) + o)), val = s_.at(uint16_t((k ? off::kTblVal2 : off::kTblVal1) + o));
            if (c == kCmdH && k == 1 && v.tStep) continue;     // a table that steps one row per note ignores a hop in its second column (measured)
            if (c == kCmdH) {
                const int n = val >> 4, dest = val & 15;
                if (n == 0 || v.hop[row] < n) { if (n) ++v.hop[row]; row = dest; hopped = true; }
                else v.hop[row] = 0;
            } else if (c) { slot_ = 1 + k; command(ch, c, val, true); slot_ = 0; }
        }
        if (v.tGen != gen) { refreshPitch(ch); if (kAStartsNow && !inA_) { inA_ = true; tableTick(ch); inA_ = false; } return; }   // A started another table: its row 0 runs now
        if (hopped) continue;
        if (v.tGroove != 0xFF) {
            const uint8_t* g = s_.groove(v.tGroove);
            if (v.tGroovePos > 15 || (v.tGroovePos && !g[v.tGroovePos])) v.tGroovePos = 0;
            v.tWait = g[v.tGroovePos] ? g[v.tGroovePos] : 1;
            ++v.tGroovePos;
        }
        row = (row + 1) & 15;
        break;
    }
    v.tRow = row;
    if (v.tStep) v.stepRow[t] = uint8_t(row & 15);
    refreshPitch(ch);
}

void Player::retrigVolume(int ch) {
    Voice& v = v_[ch];
    if (!v.retrigDv || ch == 2 || !v.on || muted_[ch]) return;
    v.baseVol = std::clamp(v.baseVol + v.retrigDv, 0, 15);
    v.vol = v.baseVol;
    { const uint8_t* in = instr(v.instr); v.envStage = v.vol > (in[9] >> 4) ? 1 : v.vol > (in[10] >> 4) ? 2 : 3; }
    v.envCount = 0;
    apu_.write(ch == 0 ? 0xFF12 : ch == 1 ? 0xFF17 : 0xFF21, uint8_t(v.vol << 4 | 8));
}

void Player::retrigger(int ch) {
    Voice& v = v_[ch];
    if (!v.on || muted_[ch]) return;
    retrigVolume(ch);
    const uint8_t* in = instr(v.instr);
    const uint8_t lenEn = (in[3] & 0x40) ? 0x40 : 0;
    const uint16_t f = ch == 2 ? kWaveFreq[v.note] : kPulseFreq[v.note];
    if (ch == 3) { apu_.write(0xFF23, uint8_t(0x80 | lenEn)); return; }
    const uint16_t base = ch == 0 ? 0xFF13 : ch == 1 ? 0xFF18 : 0xFF1D;
    apu_.write(base, uint8_t(f & 0xFF));
    apu_.write(base + 1, uint8_t(0x80 | lenEn | (f >> 8)));
    refreshPitch(ch);
}

unsigned Player::rng() {
    rngState_ = uint16_t(17u * rngState_ + 0x5C93u);
    return rngState_ >> 8;
}

void Player::masterVol(uint8_t val) {
    uint8_t nr = nr50_;
    auto adj = [](int cur, int d) {
        if (d < 8) return d;                                    // 0-7 absolute
        const int rel = d == 8 ? 0 : d <= 11 ? d - 8 : d >= 13 ? d - 16 : 0;   // 9-B up 1-3, D-F down 3-1
        return std::clamp(cur + rel, 0, 7);
    };
    const int l = adj((nr >> 4) & 7, val >> 4), r = adj(nr & 7, val & 15);
    nr50_ = uint8_t(l << 4 | r);
    apu_.write(0xFF24, nr50_);
}

void Player::command(int ch, uint8_t cmd, uint8_t val, bool fromTable, bool again) {
    Voice& v = v_[ch];
    if (cmd == kCmdZ) {                                   // randomize the last command: + random(hi) * 16 + random(lo), then run it again
        const uint8_t lc = v.lastCmd[slot_];
        if (!lc || lc == kCmdH) return;   // Z over these draws nothing (measured with G)
        const int hi = randMax(val >> 4), lo = randMax(val & 15);
        command(ch, lc, uint8_t(v.lastVal[slot_] + (hi << 4) + lo), fromTable, true);
        return;
    }
    if (!again && cmd != kCmdA && cmd != kCmdK) { v.lastCmd[slot_] = cmd; v.lastVal[slot_] = val; }   // Z always starts from the original value
    switch (cmd) {
        case kCmdA: if (val == 0x20) v.tbl = -1; else startTable(ch, val & 31); break;
        case kCmdG: if (fromTable) { v.tGroove = val & 31; v.tGroovePos = 0; } else seq_.setGroove(ch, val); break;
        case kCmdT: tempo_ = std::clamp<int>(val, 40, 255); break;
        case kCmdK: if (val == 0) killNote(ch); else v.kill = val; break;
        case kCmdE:
            if (ch == 2) apu_.write(0xFF1C, uint8_t(val == 0 ? 0 : val == 1 ? 0x60 : val == 2 ? 0x40 : 0x20));
            else if (!muted_[ch]) {                         // measured: set the volume, then step it by one every pace * 65536 cycles (software, via the usual NR12 writes)
                v.hwEnv = true;
                v.ePace = val & 7; v.eUp = (val & 8) != 0; v.eAcc = 0;
                setVolume(ch, val >> 4);
            }
            break;
        case kCmdW: if (ch < 2) writeDuty(ch, val); break;
        case kCmdP: {
            if (ch == 3) { v.noiseS = val; v.shapeAcc = v.shapeOfs; v.noiseFresh = true; break; }   // noise P: the same tick's step is skipped
            v.bendDelay = (fromTable && v.noteTick == tickNo_) ? 1 : 0;   // a table row that starts a bend on the note's own tick: the first step comes one sub-step later (measured on kicks)
            v.bendRate = bendRateOf(val); v.bendTgt = false;
            break;
        }
        case kCmdC:
            v.chord[0] = 0; v.chord[1] = val >> 4; v.chord[2] = val & 15;
            if (!again) {                                 // running C again keeps the phase (ROM); only starting or stopping resets it
                const bool was = v.chordOn;
                v.chordOn = val != 0;
                if (!was || !v.chordOn) { v.chordCnt = -1; v.chordOfs = 0; }
                if (was && !v.chordOn) refreshPitch(ch);
            }
            break;
        case kCmdR: {
            v.retrig = (val & 15) == 0 ? -1 : (val & 15); v.retrigCnt = fromTable ? 0 : -1;   // a phrase command runs before this tick's count, a table row after it
            const int x = val >> 4;
            v.retrigDv = x == 0 || x == 8 ? 0 : x < 8 ? x : x - 16;       // measured: signed volume step per retrigger, also applied when the command runs
            if (v.retrigDv) retrigger(ch);                                 // volume steps need the trigger to take effect
            break;
        }
        case kCmdS:
            if (ch == 3) {
                v.shapeOfs += int8_t(val); v.shapeAcc = v.shapeOfs;               // noise S: a plain signed byte added to the note index (measured: SFF = -1)
                refreshPitch(3);
            } else if (ch == 0) {
                const int x = val >> 4, y = val & 15;
                const uint8_t b = x ? uint8_t(0x7F + (x - 1) * 16 + y) : uint8_t(0x7F + y);
                apu_.write(0xFF10, uint8_t(~b));
                retrigger(0);
            }
            break;
        case kCmdM: masterVol(val); break;
        case kCmdF: if (ch < 2) { v.fine = val & 15; v.tune2 = int(val >> 4); refreshPitch(ch); } break;
        case kCmdV:   // the first step after the command is still 0
            if (!again && !v.vibDepth) v.vibPhase = -((val >> 4) + 1) / 64.0;   // running V again (or changing it) keeps the phase, like C
            v.vibSpeed = val >> 4; v.vibDepth = val & 15;
            if (!v.vibDepth) { v.vibSemis = 0; refreshPitch(ch); }
            break;
        case kCmdL: startSlide(ch, val); refreshPitch(ch); break;
        case kCmdO: v.pan = val & 3; writePan(); break;
        default: break;
    }
}

void Player::onStep(const StepEvent& e) {
    Voice& v = v_[e.channel];
    int delay = 0;
    if (e.cmd == kCmdB) {                                 // B xy: the step's note plays with chance 1 - (1 - x/15) * (1 - y/15)
        const unsigned x = e.val >> 4, y = e.val & 15;
        const unsigned r1 = rng() % 15, r2 = rng() % 15;
        if (r1 >= x && r2 >= y) return;
    }
    switch (e.cmd) {
        case kCmdD: delay = e.val; break;
        case kCmdH:
            if (e.val == 0xFF) { for (int c = 0; c < 4; ++c) seq_.stop(c); apu_.write(0xFF26, 0x00); running_ = false; return; }
            if (e.val < 0x10) { seq_.hopNext(e.channel, e.val); return; }   // the hop replaces the step: its note does not play
            break;
        default: break;
    }
    const bool sounding = v.on && (e.channel == 2 || v.vol > 0);   // a note whose envelope has run out counts as ended: L retriggers it (measured on NOLIGHT)
    if (e.cmd == kCmdL && e.note && sounding) {
        int n = std::clamp(e.note + e.transpose + (e.channel != 3 ? int8_t(s_.at(off::kTranspose)) : 0), 1, 127);
        v.bendAcc = int16_t(v.bendAcc + pitchIndex(e.channel, v.base) - pitchIndex(e.channel, n));   // start from where the old note was, slide to the new one
        v.base = n;
        startSlide(e.channel, e.val);
        refreshPitch(e.channel);
        return;
    }
    if (e.cmd == kCmdL && e.note && !sounding && v.base > 0 && delay == 0) {   // the ROM slides from the last note's pitch even after it was killed: the note starts there
        const int from = v.base;
        noteOn(e.channel, e);
        v.bendAcc = int16_t(pitchIndex(e.channel, from) - pitchIndex(e.channel, v.base));
        startSlide(e.channel, e.val);
        refreshPitch(e.channel);
        return;
    }
    if (e.note) {
        if (delay > 0) { v.pending = delay; v.delayed = e; }
        else noteOn(e.channel, e);
    }
    if (e.cmd && e.cmd != kCmdD && e.cmd != kCmdH) command(e.channel, e.cmd, e.val, false);
}

void Player::tick() {
    ++tickNo_;
    for (int c = 0; c < 4; ++c) {
        Voice& v = v_[c];
        if (v.kill > 0 && --v.kill == 0) killNote(c);
        if (v.pending > 0 && --v.pending == 0) noteOn(c, v.delayed);
    }
    seq_.tick([&](const StepEvent& e) { onStep(e); });
    for (int c = 0; c < 4; ++c) {
        Voice& v = v_[c];
        if (!v.on) continue;
        if (v.chordOn) {
            if (v.chordCnt < 0) { v.chordCnt = 1; v.chordAge = 0; }   // the tick the command ran on: unchanged, then x, y, 0, x ...
            else if (++v.chordAge % (instr(v.instr)[8] + 1) == 0) {   // byte 8 + 1 ticks per chord note (measured)
                v.chordOfs = v.chord[2] == 0 ? ((v.chordCnt & 1) ? v.chord[1] : 0) : v.chord[v.chordCnt % 3];   // Cx0 alternates x, 0
                ++v.chordCnt;
                refreshPitch(c);
            }
        }
        if (v.retrig > 0 && ++v.retrigCnt >= v.retrig) { v.retrigCnt = 0; retrigger(c); }
    }
    waveTick();
    for (int c = 0; c < 4; ++c) {
        tableTick(c);
        if (v_[c].chordOn && v_[c].chordCnt < 0) { v_[c].chordCnt = 1; v_[c].chordAge = 0; }   // a table C ran after this tick's chord step: its first tick is spent
    }
    {   // noise P/shape step: after the table rows of the tick (measured), skipped on the tick a P command ran
        Voice& v = v_[3];
        if (v.on && v.noiseS) {
            if (v.noiseFresh) v.noiseFresh = false;
            else { v.shapeAcc += int8_t(v.noiseS) / 4.0; v.shapeOfs = int(v.shapeAcc); refreshPitch(3); }
        }
    }
    if (!seq_.playing()) running_ = false;
}

void Player::sub360() {
    for (int c = 0; c < 4; ++c) {
        Voice& v = v_[c];
        if (!v.on || c == 3) continue;
        bool ch = false;
        if (v.bendDelay > 0) --v.bendDelay;
        else if (v.bendRate) {
            v.bendAcc = int16_t(v.bendAcc + v.bendRate);
            if (v.bendTgt && (v.bendRate > 0 ? v.bendAcc >= 0 : v.bendAcc <= 0)) { v.bendAcc = 0; v.bendRate = 0; v.bendTgt = false; }   // arrived
            ch = true;
        }
        if (ch) refreshPitch(c);
    }
    for (int c = 0; c < 4; ++c) {
        Voice& v = v_[c];
        if (!v.on || !v.vibDepth || c == 3) continue;
        const uint8_t b5 = instr(v.instr)[5];
        v.vibPhase += (v.vibSpeed + 1) / 64.0;
        // the direction bit is a half-period phase shift (for the saw too: it never runs backwards)
        v.vibSemis = kVibDepth[v.vibDepth] * vibShape((b5 >> 1) & 3, v.vibPhase + ((b5 & 1) ? 0.0 : 0.5));
        refreshPitch(c);
    }
    for (int c : {0, 1, 3}) {
        Voice& v = v_[c];
        if (!v.on || muted_[c]) continue;
        if (v.hwEnv) {
            if (v.ePace) {
                v.eAcc += kSub;
                while (v.eAcc >= v.ePace * 65536.0) {
                    v.eAcc -= v.ePace * 65536.0;
                    setVolume(c, v.eUp ? std::min(v.vol + 1, 15) : std::max(v.vol - 1, 0));
                    v.baseVol = v.vol;
                }
            }
            continue;
        }
        const uint8_t* in = instr(v.instr);
        // three-stage envelope (measured): byte 1 = start level and rate to the level in byte 9's high nibble, byte 9's low nibble is the rate from there
        // to byte 10's high nibble, byte 10's low nibble the rate to silence; a rate of 0 holds. Each stage moves the volume towards its level, up or down.
        // An E fade overrides all stages.
        const int t1 = in[9] >> 4, t2 = in[10] >> 4;
        if (v.envStage == 1 && v.vol == t1) v.envStage = 2;
        if (v.envStage == 2 && v.vol == t2) v.envStage = 3;
        bool up = false;
        uint8_t nib;
        if (v.fade >= 0) nib = uint8_t(v.fade);
        else if (v.envStage == 1) { nib = uint8_t(in[1] & 15); up = v.vol < t1; }
        else if (v.envStage == 2) { nib = uint8_t(in[9] & 15); up = v.vol < t2; }
        else nib = uint8_t(in[10] & 15);
        if (!nib || (up ? v.vol >= 15 : v.vol <= 0)) continue;
        if (++v.envCount >= kEnvPeriod[nib]) {
            v.envCount = 0;
            ++v.envSteps;
            const uint16_t r = c == 0 ? 0xFF12 : c == 1 ? 0xFF17 : 0xFF21;
            if (up) { apu_.write(r, 0x08); ++v.vol; }          // volume up by one: a running channel steps on an NRx2 write with a zero pace and the up bit
            else {
                apu_.write(r, 0x09);                    // volume down by one through the DMG "zombie" write sequence
                apu_.write(r, 0x11);
                apu_.write(r, 0x18);
                --v.vol;
            }
        }
    }
}

void Player::run(uint32_t cycles) {
    const long long end = now_ + cycles;
    while (now_ < end) {
        long long nextEv = running_ ? next360_ : end;
        if (v_[2].kPlay) nextEv = std::min(nextEv, nextKit_);
        long long t = std::min<long long>(end, std::max<long long>(now_, nextEv));
        if (t > now_) { apu_.advance(uint32_t(t - now_)); now_ = t; }
        if (v_[2].kPlay && now_ >= nextKit_) { kitChunk(); nextKit_ += kKitChunk; continue; }
        if (!running_) continue;
        if (now_ >= next360_) {
            sub360();                                                       // the ROM's 358 Hz interrupt: sub-step first, then the tempo accumulator decides on a tick
            next360_ += kSub;
            tickAcc_ += tempo_;
            if (tickAcc_ >= 896) { tickAcc_ -= 896; tick(); }
        }
    }
}

void Player::mute(int ch, bool m) {
    muted_[ch] = m;
    if (!m) return;
    static const uint16_t dac[4] = {0xFF12, 0xFF17, 0xFF1A, 0xFF21};
    apu_.write(dac[ch], 0x00);                       // DAC off: the channel falls silent
    v_[ch].on = false;
    if (ch == 2) v_[2].kPlay = false;
}

void Player::render(int16_t* out, int n, int rate) {
    const uint32_t perFx = uint32_t((uint64_t(4194304) << 16) / uint32_t(rate));   // cycles per output sample, 16.16
    for (int i = 0; i < n; ++i) {
        carryFx_ += perFx;
        const uint32_t c = carryFx_ >> 16;
        carryFx_ &= 0xFFFF;
        run(c);
        float l, r;
        apu_.takeAverage(c ? c : 1, l, r);
        if (!hpInit_) { prevL_ = l; prevR_ = r; hpL_ = hpR_ = 0; hpInit_ = true; }
        // one-pole high-pass: the real output stage is AC coupled
        hpL_ = l - prevL_ + 0.9995f * hpL_; prevL_ = l;
        hpR_ = r - prevR_ + 0.9995f * hpR_; prevR_ = r;
        out[2 * i] = int16_t(std::clamp(hpL_ * 50.0f, -32767.0f, 32767.0f));
        out[2 * i + 1] = int16_t(std::clamp(hpR_ * 50.0f, -32767.0f, 32767.0f));
    }
}

}  // namespace gb
