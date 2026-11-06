// playdump song.sav ticks: run gb::Player and print channel state at the end of each tick (same columns as regreplay).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb/player.h"
#include "gb/kits.h"
#include "gb/upgrade.h"
static void hook(int frame, int tick) { std::fprintf(stderr, "W %d %d\n", tick, frame); }
int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb"); std::vector<uint8_t> b(gb::kSavSize); size_t n = std::fread(b.data(), 1, b.size(), f); std::fclose(f);
    gb::Song s; if (!s.readSav(b.data(), n)) return 1;
    gb::upgradeSong(s);
    static std::vector<uint8_t> rom; gb::RomKits kits;
    if (const char* rp = std::getenv("GB_ROM")) { if (FILE* rf = std::fopen(rp, "rb")) { rom.resize(1 << 20); rom.resize(std::fread(rom.data(), 1, rom.size(), rf)); std::fclose(rf); kits.setRom(rom.data(), rom.size()); } }
    gb::Player p(s); p.setKits(&kits); if (std::getenv("WAVELOG")) p.setWaveHook(hook); p.start(0); if (const char* tp = std::getenv("TICKPHASE")) p.setTickPhase(std::atoi(tp));
    const int ticks = std::atoi(argv[2]);
    long long done = 0;
    for (int k = 0; k < ticks; ++k) {
        const long long until = std::getenv("SUBSTEPS") ? k * 11704LL + 8000 : (long long)((k + 0.9) * 10485760.0 / p.tempo());   // tempo may change: approximate with the current one
        if (until > done) { p.run(uint32_t(until - done)); done = until; }
        for (int c = 0; c < 4; ++c) { auto st = p.apu().state(c); std::printf("%d %d %d %d %d %d %02x\n", k, c, st.enabled, st.vol, st.freq, st.duty, p.apu().pan()); }
    }
}
