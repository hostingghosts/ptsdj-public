// regfine reg.log ch steps stepCycles: channel state sampled every stepCycles from the first trigger (freq vol duty en).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb/apu.h"
struct W { long long cc; unsigned reg, val; };
int main(int argc, char** argv) {
    std::vector<W> w; FILE* f = std::fopen(argv[1], "r"); unsigned long fr; unsigned long long cc; unsigned r, v;
    while (std::fscanf(f, "%lu %llu %x %x", &fr, &cc, &r, &v) == 4) w.push_back({(long long)cc, r, v});
    const int ch = std::atoi(argv[2]), n = std::atoi(argv[3]); const double step = std::atof(argv[4]);
    long long t0 = -1;
    for (auto& x : w) if ((x.reg == 0x14 || x.reg == 0x19 || x.reg == 0x1E || x.reg == 0x23) && (x.val & 0x80)) { t0 = x.cc; break; }
    gb::Apu apu; size_t wi = 0; long long now = w[0].cc;
    for (int k = 0; k < n; ++k) {
        long long t = t0 + (long long)(k * step);
        while (wi < w.size() && w[wi].cc <= t) { if (w[wi].cc > now) { apu.advance(uint32_t(w[wi].cc - now)); now = w[wi].cc; } apu.write(0xFF00 + w[wi].reg, uint8_t(w[wi].val)); ++wi; }
        if (t > now) { apu.advance(uint32_t(t - now)); now = t; }
        auto s = apu.state(ch); std::printf("%d %d %d %d\n", s.freq, s.vol, s.duty, s.enabled);
    }
}
