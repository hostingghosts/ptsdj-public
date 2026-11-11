// make_demo <out dir>: builds the two demo songs (src/gb/demo.cpp), writes them as .lsdsng and .sav, renders them playing together to a WAV
// (the same Dual mixer the firmware uses for EXPORT WAV, two passes so the loop point can be heard), plus each song alone, and
// prints what was measured: length, loudness per section, and the pitches the tonal channels actually play.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gb/demo.h"
#include "gb/dual.h"
#include "gb/lsdsng.h"
#include "gb/player.h"

using namespace gb;

namespace {

void writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    uint8_t h[44] = {};
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) h[o + i] = uint8_t(v >> (8 * i)); };
    auto w16 = [&](int o, uint32_t v) { h[o] = uint8_t(v); h[o + 1] = uint8_t(v >> 8); };
    std::memcpy(h, "RIFF", 4); w32(4, 36 + bytes); std::memcpy(h + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2);
    w32(24, uint32_t(rate)); w32(28, uint32_t(rate) * 4); w16(32, 4); w16(34, 16); std::memcpy(h + 36, "data", 4); w32(40, bytes);
    std::fwrite(h, 1, 44, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}

void writeSong(const std::string& path, const Song& s, const char* name) {
    std::vector<uint8_t> buf(9 + 64 * 512);
    const size_t n = saveLsdsng(s, name, 1, buf.data());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    std::fwrite(buf.data(), 1, n, f);
    std::fclose(f);
    std::printf("  %s: %zu bytes\n", path.c_str(), n);
}

// Renders `passes` loops of whatever is running; returns interleaved stereo and the frames of one pass.
std::vector<int16_t> render(Dual& d, int rate, int passes, size_t* onePass) {
    std::vector<int16_t> pcm;
    int16_t buf[2 * 1024];
    size_t first = 0;
    while (d.running() && pcm.size() < size_t(rate) * 2 * 400) {
        d.render(buf, 1024, rate);
        pcm.insert(pcm.end(), buf, buf + 2048);
        if (!first && d.looped()) first = pcm.size() / 2;
        if (first && pcm.size() / 2 >= first * size_t(passes)) break;
    }
    if (onePass) *onePass = first;
    return pcm;
}

double rms(const std::vector<int16_t>& p, size_t from, size_t to) {   // frames
    double s = 0; size_t n = 0;
    for (size_t i = from * 2; i < to * 2 && i < p.size(); ++i) { s += double(p[i]) * p[i]; ++n; }
    return n ? std::sqrt(s / double(n)) / 32768.0 : 0;
}

double peak(const std::vector<int16_t>& p) {
    int m = 0;
    for (int16_t v : p) m = std::max(m, std::abs(int(v)));
    return m / 32768.0;
}

// Share of samples above 60 % of full scale, where the firmware's mixer starts to round peaks off.
double hot(const std::vector<int16_t>& p) {
    size_t n = 0;
    for (int16_t v : p) if (std::abs(int(v)) > 19660) ++n;
    return p.empty() ? 0 : 100.0 * double(n) / double(p.size());
}

const char* kPc[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// The pitch classes a song's tonal channels play, read from the chip registers once per tick.
void pitches(const char* label, Song& s) {
    Player p(s);
    p.start(0);
    const double tickCycles = 10485760.0 / p.tempo();
    long long done = 0;
    int hist[3][12] = {};
    int ticks = 0;
    while (p.running() && !p.songLooped() && ticks < 20000) {
        const long long until = (long long)((ticks + 0.9) * tickCycles);
        p.run(uint32_t(until - done)); done = until; ++ticks;
        for (int c = 0; c < 3; ++c) {
            const Apu::ChState st = p.apu().state(c);
            if (!st.enabled || !st.dac || (c < 2 && st.vol == 0) || (c == 2 && st.vol == 0)) continue;
            const double hz = (c == 2 ? 65536.0 : 131072.0) / (2048 - st.freq);
            const double semis = 69 + 12 * std::log2(hz / 440.0);
            const int n = int(std::lround(semis));
            if (std::fabs(semis - n) > 0.4) continue;
            ++hist[c][((n % 12) + 12) % 12];
        }
    }
    std::printf("  %s: %d ticks (%.2f s)\n", label, ticks, ticks / (0.4 * p.tempo()));
    static const char* nm[3] = {"PU1", "PU2", "WAV"};
    for (int c = 0; c < 3; ++c) {
        std::printf("    %s:", nm[c]);
        for (int k = 0; k < 12; ++k) if (hist[c][k]) std::printf(" %s=%d", kPc[k], hist[c][k]);
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: make_demo <out dir>\n"); return 1; }
    const std::string dir = argv[1];
    static Song a, b;
    buildDemoA(a);
    buildDemoB(b);
    std::printf("songs:\n");
    writeSong(dir + "/DEMO_A.lsdsng", a, kDemoNameA);
    writeSong(dir + "/DEMO_B.lsdsng", b, kDemoNameB);
    for (int c = 0; c < 2; ++c) {                       // 128 KB saves with the song as working memory: what the LSDJ ROM (and tools/oracle) loads
        std::vector<uint8_t> sav(kSavSize);
        (c ? b : a).writeSav(sav.data());
        const std::string path = dir + (c ? "/DEMO_B.sav" : "/DEMO_A.sav");
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(sav.data(), 1, sav.size(), f); std::fclose(f); }
    }

    std::printf("pitches heard:\n");
    pitches("A", a);
    pitches("B", b);

    const int rate = 44100;
    Player pa(a), pb(b);
    Dual d(pa, pb);
    size_t pass = 0;
    d.start(0, 0);
    const std::vector<int16_t> mix = render(d, rate, 2, &pass);
    writeWav(dir + "/DEMO_A+B.wav", mix, rate);
    std::printf("mix: one pass %.2f s, file %.2f s, peak %.3f, %.2f %% of samples above 60 %%\n", double(pass) / rate, double(mix.size() / 2) / rate, peak(mix), hot(mix));

    for (int c = 0; c < 2; ++c) {
        Song s2;
        if (c == 0) buildDemoA(s2); else buildDemoB(s2);
        Player x(s2), y(s2);
        Dual one(x, y);
        one.startOne(0, 0);
        size_t pass1 = 0;
        const std::vector<int16_t> solo = render(one, rate, 1, &pass1);
        std::printf("%s alone: one pass %.2f s, peak %.3f\n", c ? "B" : "A", double(pass1) / rate, peak(solo));
        const size_t q = pass1 / 4;
        std::printf("  rms by section:");
        for (int k = 0; k < 4; ++k) std::printf(" %.3f", rms(solo, q * size_t(k), q * size_t(k + 1)));
        std::printf("\n");
        if (c == 0) writeWav(dir + "/DEMO_A_only.wav", solo, rate); else writeWav(dir + "/DEMO_B_only.wav", solo, rate);
    }
    const size_t q = pass / 4;
    std::printf("mix rms by section:");
    for (int k = 0; k < 4; ++k) std::printf(" %.3f", rms(mix, q * size_t(k), q * size_t(k + 1)));
    std::printf("\n");
    return 0;
}
