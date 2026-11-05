// playdump song.sav ticks: run gb::Player and print channel state at the end of each tick (same columns as regreplay).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb/player.h"
int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb"); std::vector<uint8_t> b(gb::kSavSize); size_t n = std::fread(b.data(), 1, b.size(), f); std::fclose(f);
    gb::Song s; if (!s.readSav(b.data(), n)) return 1;
    gb::Player p(s); p.start(0);
    const int ticks = std::atoi(argv[2]);
    long long done = 0;
    for (int k = 0; k < ticks; ++k) {
        const long long until = (long long)((k + 0.9) * 10485760.0 / p.tempo());   // tempo may change: approximate with the current one
        if (until > done) { p.run(uint32_t(until - done)); done = until; }
        for (int c = 0; c < 4; ++c) { auto st = p.apu().state(c); std::printf("%d %d %d %d %d %d %02x\n", k, c, st.enabled, st.vol, st.freq, st.duty, p.apu().pan()); }
    }
}
