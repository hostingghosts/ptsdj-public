// seqdump song.sav ticks -> "channel tick" for every step that has a note (compare with the ROM via seqcheck.py)
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb/seq.h"
int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb"); std::vector<uint8_t> b(gb::kSavSize); size_t n = std::fread(b.data(), 1, b.size(), f); std::fclose(f);
    gb::Song s; if (!s.readSav(b.data(), n)) return 1;
    gb::Seq q(s); q.start(0);
    const int ticks = std::atoi(argv[2]);
    for (int t = 0; t < ticks; ++t)
        q.tick([&](const gb::StepEvent& e) { if (e.note) std::printf("%d %d\n", e.channel, t); });
}
