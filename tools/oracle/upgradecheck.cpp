// upgradecheck raw.mem converted.mem: apply gb::upgradeSong to a raw old-format song and count bytes that differ from LSDJ's own conversion.
#include <cstdio>
#include <cstring>
#include <vector>
#include "gb/upgrade.h"
static std::vector<uint8_t> slurp(const char* p) { std::vector<uint8_t> b; if (FILE* f = std::fopen(p, "rb")) { b.resize(0x8000); b.resize(std::fread(b.data(), 1, b.size(), f)); std::fclose(f); } return b; }
int main(int argc, char** argv) {
    auto raw = slurp(argv[1]), cnv = slurp(argv[2]);
    gb::Song s; std::memcpy(s.mem(), raw.data(), gb::kMemSize);
    gb::upgradeSong(s);
    int bad[8] = {}, total = 0; const char* names[8] = {"notes", "instr", "tables", "cmds/vals", "waves", "phrase instr", "project/alloc", "other"};
    for (int i = 0; i < 0x8000; ++i) {
        if (s.mem()[i] == cnv[i] || (i >= 0x1D00 && i < 0x1E78)) continue;   // the text tables: LSDJ's own upgrade leaves junk there
        ++total;
        int k = i < 0xFF0 ? 0 : (i >= 0x3080 && i < 0x3480) ? 1 : (i >= 0x1690 && i < 0x1890) || (i >= 0x3480 && i < 0x3E80) ? 2 : (i >= 0x4000 && i < 0x5FF0) ? 3 : (i >= 0x6000 && i < 0x7000) ? 4 : (i >= 0x7000 && i < 0x7FF0) ? 5 : (i >= 0x3E80 && i < 0x4000) ? 6 : 7;
        ++bad[k];
    }
    std::printf("%d differ:", total);
    for (int k = 0; k < 8; ++k) if (bad[k]) std::printf(" %s %d", names[k], bad[k]);
    std::printf("\n");
}
