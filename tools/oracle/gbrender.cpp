// gbrender song.(sav|lsdsng) seconds out.wav [rate]: render a song with gb::Player to a 16-bit stereo WAV.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "gb/lsdsng.h"
#include "gb/player.h"
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    FILE* f = std::fopen(argv[1], "rb"); if (!f) return 1;
    std::vector<uint8_t> b(1 << 18); size_t n = std::fread(b.data(), 1, b.size(), f); std::fclose(f);
    gb::Song s;
    if (!(n >= gb::kMemSize && s.readSav(b.data(), n)) && !gb::loadLsdsng(s, b.data(), n)) { std::fprintf(stderr, "cannot read song\n"); return 1; }
    const int rate = argc > 4 ? std::atoi(argv[4]) : 44100, frames = int(std::atof(argv[2]) * rate);
    gb::Player p(s); p.start(0);
    std::vector<int16_t> pcm(size_t(frames) * 2);
    p.render(pcm.data(), frames, rate);
    FILE* o = std::fopen(argv[3], "wb");
    uint32_t dataBytes = uint32_t(pcm.size() * 2), hdr[11] = {0x46464952, 36 + dataBytes, 0x45564157, 0x20746d66, 16, 0x00020001, uint32_t(rate), uint32_t(rate) * 4, 0x00100004, 0x61746164, dataBytes};
    std::fwrite(hdr, 4, 11, o); std::fwrite(pcm.data(), 2, pcm.size(), o); std::fclose(o);
}
