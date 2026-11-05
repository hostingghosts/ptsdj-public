// mksng in.mem out.lsdsng [name]: wrap a 32 KB song memory (any format byte) as an .lsdsng for loading through the ROM.
#include <cstdio>
#include <cstring>
#include <vector>
#include "gb/lsdsng.h"
int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "rb"); gb::Song s; std::fread(s.mem(), 1, gb::kMemSize, f); std::fclose(f);
    std::vector<uint8_t> out(9 + 64 * 512); const size_t n = gb::saveLsdsng(s, argc > 3 ? argv[3] : "TEST", 1, out.data());
    f = std::fopen(argv[2], "wb"); std::fwrite(out.data(), 1, n, f); std::fclose(f);
}
