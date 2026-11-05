#include "gb/player.h"

#include <algorithm>
#include <cmath>

namespace gb {

namespace {
#include "gb/tables.inc"
// Envelope step period in 1/360 s for release digit 1..15 (measured).
const uint8_t kEnvPeriod[16] = {0, 1, 2, 3, 4, 6, 8, 11, 15, 20, 27, 36, 48, 64, 86, 115};
constexpr double kTick = 10485760.0;      // cycles per tick = kTick / tempo
constexpr double kSub = 4194304.0 / 360.0;
}  // namespace

void Player::start(int row) {
    apu_.reset();
    apu_.write(0xFF26, 0x80);
    apu_.write(0xFF24, 0x77);
    apu_.write(0xFF25, 0xFF);
    for (auto& v : v_) v = Voice();
    tempo_ = s_.at(off::kTempo);
    if (tempo_ < 40) tempo_ = 40;
    seq_.start(row);
    running_ = true;
    now_ = 0;
    nextTick_ = 0;
    next360_ = kSub;
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
    if (ch == 2) { apu_.write(0xFF1C, 0x00); return; }
    const uint16_t r = ch == 0 ? 0xFF12 : ch == 1 ? 0xFF17 : 0xFF21;
    for (int i = v.vol; i > 0; --i) { apu_.write(r, 0x09); apu_.write(r, 0x11); apu_.write(r, 0x18); }   // same stepping the envelope uses
    v.vol = 0;
}

void Player::noteOn(int ch, const StepEvent& e) {
    Voice& v = v_[ch];
    if (e.instr != 0xFF) v.instr = e.instr & 63;
    const uint8_t* in = instr(v.instr);
    int n = e.note;
    n += e.transpose;
    if (ch != 3) n += int8_t(s_.at(off::kTranspose));
    n = std::clamp(n, 1, 127);
    v.note = n;
    v.on = true;
    v.envCount = 0;
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
            v.vol = in[1] >> 4;
            break;
        }
        case 2: {
            const uint16_t f = kWaveFreq[n];
            apu_.write(0xFF1C, in[1]);
            apu_.write(0xFF1A, 0x00);
            const uint8_t* w = s_.wave(0);
            for (int i = 0; i < 16; ++i) apu_.write(0xFF30 + i, w[i]);
            apu_.write(0xFF1A, 0x80);
            apu_.write(0xFF1D, 0xE0);
            apu_.write(0xFF1E, 0x87);
            apu_.write(0xFF1D, uint8_t(f & 0xFF));
            apu_.write(0xFF1E, uint8_t(lenEn | (f >> 8)));
            break;
        }
        default:
            apu_.write(0xFF20, in[3] & 0x3F);
            apu_.write(0xFF21, uint8_t((in[1] & 0xF0) | 8));
            apu_.write(0xFF22, kNoiseNr43[n]);
            apu_.write(0xFF23, uint8_t(0x80 | lenEn));
            v.vol = in[1] >> 4;
            break;
    }
}

void Player::onStep(const StepEvent& e) {
    Voice& v = v_[e.channel];
    int delay = 0;
    switch (e.cmd) {
        case kCmdD: delay = e.val; break;
        case kCmdT: tempo_ = std::clamp<int>(e.val, 40, 255); break;
        case kCmdG: seq_.setGroove(e.channel, e.val); break;
        case kCmdH:
            if (e.val == 0xFF) { for (int c = 0; c < 4; ++c) seq_.stop(c); apu_.write(0xFF26, 0x00); running_ = false; return; }
            if (e.val < 0x10) { seq_.hopNext(e.channel, e.val); return; }   // the hop replaces the step: its note does not play
            break;
        default: break;
    }
    if (e.note) {
        if (delay > 0) { v.pending = delay; v.delayed = e; }
        else noteOn(e.channel, e);
    }
    if (e.cmd == kCmdK) {
        if (e.val == 0) killNote(e.channel); else v.kill = e.val;
    }
}

void Player::tick() {
    for (int c = 0; c < 4; ++c) {
        Voice& v = v_[c];
        if (v.kill > 0 && --v.kill == 0) killNote(c);
        if (v.pending > 0 && --v.pending == 0) noteOn(c, v.delayed);
    }
    seq_.tick([&](const StepEvent& e) { onStep(e); });
    if (!seq_.playing()) running_ = false;
}

void Player::sub360() {
    for (int c : {0, 1, 3}) {
        Voice& v = v_[c];
        if (!v.on || muted_[c]) continue;
        const uint8_t nib = instr(v.instr)[1] & 15;
        if (!nib || v.vol <= 0) continue;
        if (++v.envCount >= kEnvPeriod[nib]) {
            v.envCount = 0;
            const uint16_t r = c == 0 ? 0xFF12 : c == 1 ? 0xFF17 : 0xFF21;
            apu_.write(r, 0x09);                    // volume down by one through the DMG "zombie" write sequence
            apu_.write(r, 0x11);
            apu_.write(r, 0x18);
            --v.vol;
        }
    }
}

void Player::run(uint32_t cycles) {
    const long long end = now_ + cycles;
    while (now_ < end) {
        double nextEv = running_ ? std::min(nextTick_, next360_) : double(end);
        long long t = std::min<long long>(end, std::max<long long>(now_, (long long)std::ceil(nextEv)));
        if (t > now_) { apu_.advance(uint32_t(t - now_)); now_ = t; }
        if (!running_) continue;
        if (now_ >= (long long)std::ceil(next360_) && next360_ <= nextTick_) { sub360(); next360_ += kSub; }
        else if (now_ >= (long long)std::ceil(nextTick_)) { tick(); nextTick_ += kTick / tempo_; }
    }
}

void Player::render(int16_t* out, int n, int rate) {
    const double per = 4194304.0 / rate;
    for (int i = 0; i < n; ++i) {
        carry_ += per;
        const uint32_t c = uint32_t(carry_);
        carry_ -= c;
        run(c);
        float l, r;
        apu_.takeAverage(c ? c : 1, l, r);
        // one-pole high-pass: the real output stage is AC coupled
        hpL_ = l - prevL_ + 0.9995f * hpL_; prevL_ = l;
        hpR_ = r - prevR_ + 0.9995f * hpR_; prevR_ = r;
        out[2 * i] = int16_t(std::clamp(hpL_ * 62.0f, -32767.0f, 32767.0f));
        out[2 * i + 1] = int16_t(std::clamp(hpR_ * 62.0f, -32767.0f, 32767.0f));
    }
}

}  // namespace gb
