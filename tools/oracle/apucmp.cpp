// Compare gb::Apu against the reference emulator's audio. Usage: apucmp reg.log aud.raw [c0 [from_frame to_frame]]
// reg.log lines: frame cc reg val; aud.raw: per frame {u32 n, n x u32 (hi16 = left, lo16 = right, signed)}.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gb/apu.h"

struct W { long long cc; unsigned reg, val; };

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    std::vector<W> w;
    { FILE* f = std::fopen(argv[1], "r"); unsigned long fr; unsigned long long cc; unsigned r, v;
      while (std::fscanf(f, "%lu %llu %x %x", &fr, &cc, &r, &v) == 4) w.push_back({(long long)cc, r, v}); std::fclose(f); }
    std::vector<uint32_t> s;
    { FILE* f = std::fopen(argv[2], "rb"); uint32_t n;
      while (std::fread(&n, 4, 1, f) == 1) { size_t o = s.size(); s.resize(o + n); if (std::fread(&s[o], 4, n, f) != n) break; } std::fclose(f); }
    long long c0 = argc > 3 ? std::atoll(argv[3]) : 0;
    long long from = argc > 4 ? std::atoll(argv[4]) : 0, to = argc > 5 ? std::atoll(argv[5]) : (long long)s.size();
    gb::Apu apu;
    size_t wi = 0;
    long long bad = 0, n = 0, firstBad = -1;
    for (long long k = 0; k < to; ++k) {
        const long long cc = c0 + 2 * k;
        while (wi < w.size() && w[wi].cc <= cc) { apu.write(0xFF00 + w[wi].reg, uint8_t(w[wi].val)); ++wi; }
        apu.advance(2);
        if (k < from) continue;
        int l, r;
        apu.mixNow(l, r);
        const int gl = int16_t(s[k] >> 16), gr = int16_t(s[k] & 0xFFFF);
        const int el = -l * 64, er = -r * 64;
        ++n;
        if (gl != el || gr != er) { if (firstBad < 0) firstBad = k; ++bad; if (bad < 12 || (std::getenv("V") && bad < 200)) std::printf("  k=%lld gb=(%d,%d) mine=(%d,%d)\n", k, gl, gr, el, er); }
    }
    std::printf("c0=%lld compared=%lld mismatches=%lld first=%lld\n", c0, n, bad, firstBad);
}
