#include "gb/apu.h"

#include <algorithm>

namespace gb {

namespace {
// Step 0 is the step a triggered channel starts on; tables fitted to the reference emulator's output.
const uint8_t kDuty[4][8] = {{0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 1, 1}, {0, 0, 0, 0, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 0, 0}};
}

void Apu::reset() {
    p1_ = Pulse();
    p2_ = Pulse();
    w_ = Wave();
    n_ = Noise();
    power_ = false;
    nr50_ = nr51_ = nr10_ = 0;
    fsTimer_ = 8192;
    fsStep_ = 0;
    accL_ = accR_ = 0;
    recompute();
}

int32_t Apu::noisePeriod() const {
    const int d = n_.div ? n_.div * 16 : 8;
    return int32_t(d) << n_.shift;
}

int Apu::pulseOut(const Pulse& p) const { return (p.enabled && p.dac && kDuty[p.duty][p.step]) ? p.vol : 0; }
int Apu::waveOut() const {
    if (!w_.enabled || !w_.dac) return 0;
    const uint8_t b = w_.ram[w_.pos >> 1];
    const int s = (w_.pos & 1) ? (b & 15) : (b >> 4);
    static const uint8_t sh[4] = {4, 0, 1, 2};
    return s >> sh[w_.volCode];
}
int Apu::noiseOut() const { return (n_.enabled && n_.dac && !(n_.lfsr & 1)) ? n_.vol : 0; }

Apu::ChState Apu::state(int ch) const {
    switch (ch) {
        case 0: return {p1_.enabled, p1_.dac, p1_.vol, p1_.freq, p1_.duty};
        case 1: return {p2_.enabled, p2_.dac, p2_.vol, p2_.freq, p2_.duty};
        case 2: return {w_.enabled, w_.dac, w_.volCode, w_.freq, 0};
        default: return {n_.enabled, n_.dac, n_.vol, n_.div | (n_.shift << 4) | (n_.narrow ? 8 : 0), 0};
    }
}

int Apu::channelLevel(int ch) const {
    switch (ch) {
        case 0: return pulseOut(p1_);
        case 1: return pulseOut(p2_);
        case 2: return waveOut();
        default: return noiseOut();
    }
}

void Apu::recompute() {
    // Each channel with its DAC on is bipolar: 2 x level - 15 (a stopped channel sits at -15); DAC off adds nothing.
    const int o[4] = {pulseOut(p1_), pulseOut(p2_), waveOut(), noiseOut()};
    const bool dac[4] = {p1_.dac, p2_.dac, w_.dac, n_.dac};
    int l = 0, r = 0;
    for (int i = 0; i < 4; ++i) {
        if (!dac[i]) continue;
        const int v = 2 * o[i] - 15;
        if (nr51_ & (0x10 << i)) l += v;
        if (nr51_ & (1 << i)) r += v;
    }
    mixL_ = l * (((nr50_ >> 4) & 7) + 1);
    mixR_ = r * ((nr50_ & 7) + 1);
}

void Apu::mixNow(int& l, int& r) const {
    l = power_ ? mixL_ : 0;
    r = power_ ? mixR_ : 0;
}

uint8_t Apu::read(uint16_t a) const {
    if (a >= 0xFF30 && a < 0xFF40) return w_.ram[a - 0xFF30];
    return 0xFF;
}

// Writing NRx2 while the channel runs changes its volume ("zombie mode", DMG behaviour).
static uint8_t nr2raw(uint8_t init, bool up, uint8_t pace) { return uint8_t(init << 4 | (up ? 8 : 0) | pace); }
static void zombie(uint8_t& vol, bool& active, uint8_t oldNr2, uint8_t newNr2) {
    if (!(oldNr2 & 7) && active) ++vol;
    else if (!(oldNr2 & 8)) vol += 2;
    if ((oldNr2 ^ newNr2) & 8) vol = uint8_t(16 - vol);
    vol &= 15;
}

void Apu::envTickOne(uint8_t& vol, uint8_t& timer, uint8_t pace, bool up, bool& active, bool enabled) {
    if (!enabled || !active) return;
    if (--timer == 0) {
        timer = pace ? pace : 8;
        if (pace == 0) return;
        if (up && vol < 15) ++vol;
        else if (!up && vol > 0) --vol;
        else active = false;
    }
}

bool Apu::sweepCalc(bool apply) {
    uint16_t n = p1_.shadow >> p1_.swShift;
    n = p1_.swDown ? uint16_t(p1_.shadow - n) : uint16_t(p1_.shadow + n);
    if (n > 2047) {
        p1_.enabled = false;
        return false;
    }
    if (apply && p1_.swShift) {
        p1_.shadow = n;
        p1_.freq = n;
    }
    return true;
}

void Apu::trigger1() {
    Pulse& p = p1_;
    p.enabled = p.dac;
    if (p.length == 0) p.length = 64;
    p.timer = pulsePeriod(p);
    p.step = 0;
    p.vol = p.envInit;
    p.envTimer = p.envPace ? p.envPace : 8;
    p.envActive = true;
    p.shadow = p.freq;
    p.swTimer = p.swPace ? p.swPace : 8;
    p.swOn = p.swPace || p.swShift;
    if (p.swShift) sweepCalc(false);
}
void Apu::trigger2() {
    Pulse& p = p2_;
    p.enabled = p.dac;
    if (p.length == 0) p.length = 64;
    p.timer = pulsePeriod(p);
    p.step = 0;
    p.vol = p.envInit;
    p.envTimer = p.envPace ? p.envPace : 8;
    p.envActive = true;
}
void Apu::trigger3() {
    w_.enabled = w_.dac;
    if (w_.length == 0) w_.length = 256;
    w_.timer = wavePeriod() + 6;
    w_.pos = 0;
}
void Apu::trigger4() {
    n_.enabled = n_.dac;
    if (n_.length == 0) n_.length = 64;
    n_.timer = noisePeriod();
    n_.vol = n_.envInit;
    n_.envTimer = n_.envPace ? n_.envPace : 8;
    n_.envActive = true;
    n_.lfsr = 0x7FFF;
}

void Apu::write(uint16_t a, uint8_t v) {
    if (a >= 0xFF30 && a < 0xFF40) {
        w_.ram[a - 0xFF30] = v;
        recompute();
        return;
    }
    if (a == 0xFF26) {
        const bool on = (v & 0x80) != 0;
        if (!on && power_) {
            const uint8_t keepL1 = 0, keepL2 = 0;
            (void)keepL1; (void)keepL2;
            uint8_t ram[16];
            std::copy(w_.ram, w_.ram + 16, ram);
            reset();
            std::copy(ram, ram + 16, w_.ram);
        } else if (on && !power_) {
            power_ = true;
            fsStep_ = 0;
            fsTimer_ = 8192;
        }
        recompute();
        return;
    }
    if (!power_) return;
    switch (a) {
        case 0xFF10:
            nr10_ = v;
            p1_.swPace = (v >> 4) & 7;
            p1_.swDown = (v & 8) != 0;
            p1_.swShift = v & 7;
            break;
        case 0xFF11: p1_.duty = v >> 6; p1_.length = 64 - (v & 63); break;
        case 0xFF12:
            if (p1_.enabled) zombie(p1_.vol, p1_.envActive, nr2raw(p1_.envInit, p1_.envUp, p1_.envPace), v);
            p1_.envInit = v >> 4; p1_.envUp = (v & 8) != 0; p1_.envPace = v & 7; p1_.dac = (v & 0xF8) != 0;
            if (!p1_.dac) p1_.enabled = false;
            break;
        case 0xFF13: p1_.freq = uint16_t((p1_.freq & 0x700) | v); break;
        case 0xFF14:
            p1_.freq = uint16_t((p1_.freq & 0xFF) | ((v & 7) << 8));
            p1_.lenEn = (v & 0x40) != 0;
            if (v & 0x80) trigger1();
            break;
        case 0xFF16: p2_.duty = v >> 6; p2_.length = 64 - (v & 63); break;
        case 0xFF17:
            if (p2_.enabled) zombie(p2_.vol, p2_.envActive, nr2raw(p2_.envInit, p2_.envUp, p2_.envPace), v);
            p2_.envInit = v >> 4; p2_.envUp = (v & 8) != 0; p2_.envPace = v & 7; p2_.dac = (v & 0xF8) != 0;
            if (!p2_.dac) p2_.enabled = false;
            break;
        case 0xFF18: p2_.freq = uint16_t((p2_.freq & 0x700) | v); break;
        case 0xFF19:
            p2_.freq = uint16_t((p2_.freq & 0xFF) | ((v & 7) << 8));
            p2_.lenEn = (v & 0x40) != 0;
            if (v & 0x80) trigger2();
            break;
        case 0xFF1A:
            w_.dac = (v & 0x80) != 0;
            if (!w_.dac) w_.enabled = false;
            break;
        case 0xFF1B: w_.length = uint16_t(256 - v); break;
        case 0xFF1C: w_.volCode = (v >> 5) & 3; break;
        case 0xFF1D: w_.freq = uint16_t((w_.freq & 0x700) | v); break;
        case 0xFF1E:
            w_.freq = uint16_t((w_.freq & 0xFF) | ((v & 7) << 8));
            w_.lenEn = (v & 0x40) != 0;
            if (v & 0x80) trigger3();
            break;
        case 0xFF20: n_.length = 64 - (v & 63); break;
        case 0xFF21:
            if (n_.enabled) zombie(n_.vol, n_.envActive, nr2raw(n_.envInit, n_.envUp, n_.envPace), v);
            n_.envInit = v >> 4; n_.envUp = (v & 8) != 0; n_.envPace = v & 7; n_.dac = (v & 0xF8) != 0;
            if (!n_.dac) n_.enabled = false;
            break;
        case 0xFF22: n_.shift = v >> 4; n_.narrow = (v & 8) != 0; n_.div = v & 7; break;
        case 0xFF23:
            n_.lenEn = (v & 0x40) != 0;
            if (v & 0x80) trigger4();
            break;
        case 0xFF24: nr50_ = v; break;
        case 0xFF25: nr51_ = v; break;
        default: break;
    }
    recompute();
}

void Apu::lengthTick() {
    auto tick = [](bool en, uint16_t& len, bool& enabled) {
        if (en && len > 0 && --len == 0) enabled = false;
    };
    tick(p1_.lenEn, p1_.length, p1_.enabled);
    tick(p2_.lenEn, p2_.length, p2_.enabled);
    tick(w_.lenEn, w_.length, w_.enabled);
    tick(n_.lenEn, n_.length, n_.enabled);
}

void Apu::sweepTick() {
    Pulse& p = p1_;
    if (p.swTimer > 0 && --p.swTimer == 0) {
        p.swTimer = p.swPace ? p.swPace : 8;
        if (p.swOn && p.swPace) {
            if (sweepCalc(true)) sweepCalc(false);
        }
    }
}

void Apu::envTick() {
    envTickOne(p1_.vol, p1_.envTimer, p1_.envPace, p1_.envUp, p1_.envActive, p1_.enabled);
    envTickOne(p2_.vol, p2_.envTimer, p2_.envPace, p2_.envUp, p2_.envActive, p2_.enabled);
    envTickOne(n_.vol, n_.envTimer, n_.envPace, n_.envUp, n_.envActive, n_.enabled);
}

void Apu::frameStep() {
    const uint8_t s = fsStep_;
    if ((s & 1) == 0) lengthTick();
    if (s == 2 || s == 6) sweepTick();
    if (s == 7) envTick();
    fsStep_ = (fsStep_ + 1) & 7;
}

void Apu::advance(uint32_t cycles) {
    if (!power_) return;
    while (cycles) {
        int32_t step = int32_t(std::min<uint32_t>(cycles, 0x40000000u));
        step = std::min(step, fsTimer_);
        if (p1_.enabled) step = std::min(step, p1_.timer);
        if (p2_.enabled) step = std::min(step, p2_.timer);
        if (w_.enabled) step = std::min(step, w_.timer);
        if (n_.enabled) step = std::min(step, n_.timer);
        if (step < 1) step = 1;
        accL_ += int64_t(mixL_) * step;
        accR_ += int64_t(mixR_) * step;
        cycles -= uint32_t(step);
        fsTimer_ -= step;
        bool changed = false;
        if (p1_.enabled && (p1_.timer -= step) <= 0) { p1_.timer += pulsePeriod(p1_); p1_.step = (p1_.step + 1) & 7; changed = true; }
        if (p2_.enabled && (p2_.timer -= step) <= 0) { p2_.timer += pulsePeriod(p2_); p2_.step = (p2_.step + 1) & 7; changed = true; }
        if (w_.enabled && (w_.timer -= step) <= 0) { w_.timer += wavePeriod(); w_.pos = (w_.pos + 1) & 31; changed = true; }
        if (n_.enabled && (n_.timer -= step) <= 0) {
            n_.timer += noisePeriod();
            const uint16_t x = uint16_t((n_.lfsr ^ (n_.lfsr >> 1)) & 1);
            n_.lfsr = uint16_t((n_.lfsr >> 1) | (x << 14));
            if (n_.narrow) n_.lfsr = uint16_t((n_.lfsr & ~0x40) | (x << 6));
            changed = true;
        }
        if (fsTimer_ <= 0) { fsTimer_ += 8192; frameStep(); changed = true; }
        if (changed) recompute();
    }
}

void Apu::takeAverage(uint32_t cycles, float& l, float& r) {
    l = float(double(accL_) / cycles);
    r = float(double(accR_) / cycles);
    accL_ = accR_ = 0;
}

}  // namespace gb
