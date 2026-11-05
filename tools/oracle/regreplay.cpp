// regreplay reg.log tempo ticks: replay the ROM's register writes into gb::Apu and print the channel state at the end of each tick.
// Tick 0 starts at the first channel trigger. Output: tick ch enabled vol freq duty pan(NR51)
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb/apu.h"
struct W { long long cc; unsigned reg, val; };
int main(int argc, char** argv) {
    std::vector<W> w; FILE* f = std::fopen(argv[1], "r"); unsigned long fr; unsigned long long cc; unsigned r, v;
    while (std::fscanf(f, "%lu %llu %x %x", &fr, &cc, &r, &v) == 4) w.push_back({(long long)cc, r, v});
    std::fclose(f);
    const double tempo = std::atof(argv[2]); const int ticks = std::atoi(argv[3]);
    const double tk = 10485760.0 / tempo;
    long long t0 = -1;
    for (auto& x : w) if ((x.reg == 0x14 || x.reg == 0x19 || x.reg == 0x1E || x.reg == 0x23) && (x.val & 0x80)) { t0 = x.cc; break; }
    if (t0 < 0) return 1;
    gb::Apu apu; size_t wi = 0; long long now = w.empty() ? 0 : w[0].cc;
    auto runTo = [&](long long t) {
        while (wi < w.size() && w[wi].cc <= t) { if (w[wi].cc > now) { apu.advance(uint32_t(w[wi].cc - now)); now = w[wi].cc; } apu.write(0xFF00 + w[wi].reg, uint8_t(w[wi].val)); ++wi; }
        if (t > now) { apu.advance(uint32_t(t - now)); now = t; }
    };
    for (int k = 0; k < ticks; ++k) {
        runTo(t0 + (long long)((k + 0.9) * tk));
        for (int c = 0; c < 4; ++c) { auto s = apu.state(c); std::printf("%d %d %d %d %d %d %02x\n", k, c, s.enabled, s.vol, s.freq, s.duty, apu.pan()); }
    }
}
