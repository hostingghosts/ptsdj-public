// make_dnb <out dir> [kit dir] [kit1 slot] [kit2 slot]: builds the drum and bass pair (src/gb/dnb.cpp), writes DNB_A and DNB_B as .lsdsng, renders the pair
// to DNB_A+B.wav with the firmware's mixer (the two Amen kits are read from <kit dir>/DM-AMEN1.kit and DM-AMEN2.kit, default demos/kits), and prints what was
// measured: length, loudness per part, each channel's share of the mix, and every pitch the tonal channels play against A minor.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gb/dnb.h"
#include "gb/dual.h"
#include "gb/kits.h"
#include "gb/lsdsng.h"
#include "gb/player.h"

using namespace gb;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> v;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
        std::fclose(f);
    }
    return v;
}

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
    if (n > 9 + 64 * 512) { std::fprintf(stderr, "song too big\n"); std::exit(1); }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    std::fwrite(buf.data(), 1, n, f);
    std::fclose(f);
    std::printf("  %s: %zu bytes\n", path.c_str(), n);
}

// One pass of whatever is running, as interleaved stereo.
std::vector<int16_t> pass(Dual& d, int rate, int maxSeconds = 400) {
    std::vector<int16_t> pcm;
    int16_t buf[2 * 1024];
    while (d.running() && !d.looped() && pcm.size() < size_t(rate) * 2 * maxSeconds) {
        d.render(buf, 1024, rate);
        pcm.insert(pcm.end(), buf, buf + 2048);
    }
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

double hot(const std::vector<int16_t>& p) {
    size_t n = 0;
    for (int16_t v : p) if (std::abs(int(v)) > 19660) ++n;
    return p.empty() ? 0 : 100.0 * double(n) / double(p.size());
}

// Song A's second pulse plays the synth kick and snare tone (a pitch that falls through every semitone) in these bars, so it is a drum there, not a voice.
bool synthDrumBar(int bar) { return (bar >= 8 && bar < 32) || (bar >= 44 && bar < 60); }

const char* kPc[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
bool inScale(int pc) { return pc == 9 || pc == 11 || pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7; }

void pitches(const char* label, Song& s, const KitSource* kits) {
    Player p(s);
    p.setKits(kits);
    p.start(0);
    const double tickCycles = 10485760.0 / p.tempo();
    long long done = 0;
    int hist[3][12] = {}, off[3] = {};
    int ticks = 0;
    while (p.running() && !p.songLooped() && ticks < 40000) {
        const long long until = (long long)((ticks + 0.9) * tickCycles);
        p.run(uint32_t(until - done)); done = until; ++ticks;
        for (int c = 0; c < 3; ++c) {
            const Apu::ChState st = p.apu().state(c);
            if (!st.enabled || !st.dac || st.vol == 0) continue;
            if (c == 1 && label[0] == 'A' && synthDrumBar((ticks - 1) / 96)) continue;
            if (c == 2 && p.apu().state(2).freq >= 0x700) continue;     // a kit plays samples through the wave channel at its own pitch
            const double hz = (c == 2 ? 65536.0 : 131072.0) / (2048 - st.freq);
            const double semis = 69 + 12 * std::log2(hz / 440.0);
            const int n = int(std::lround(semis));
            if (std::fabs(semis - n) > 0.4) ++off[c];
            ++hist[c][((n % 12) + 12) % 12];
        }
    }
    std::printf("  %s: %d ticks (%.2f s)\n", label, ticks, ticks / (0.4 * p.tempo()));
    static const char* nm[3] = {"PU1", "PU2", "WAV"};
    for (int c = 0; c < 3; ++c) {
        std::printf("    %s:", nm[c]);
        int bad = 0;
        for (int k = 0; k < 12; ++k) if (hist[c][k]) { std::printf(" %s=%d%s", kPc[k], hist[c][k], inScale(k) ? "" : "!"); if (!inScale(k)) bad += hist[c][k]; }
        std::printf("   (between notes %d, outside A minor %d)\n", off[c], bad);
    }
}

}  // namespace

// Which notes sound together: per tick the pitch classes of the tonal voices of both consoles (the kits and the noise are not tonal). A semitone or a tritone between two of them
// is a clash; the count over the ticks with two or more voices is the number to keep near zero for a clean arrangement. Also how many tonal voices play at once in the drops.
void harmony(Song& a, Song& b, const KitSource* kits) {
    Player pa(a), pb(b);
    pa.setKits(kits); pb.setKits(kits);
    pa.start(0); pb.start(0);
    const double tc = 10485760.0 / pa.tempo();
    long long da = 0, db = 0;
    int multi = 0, clash = 0, maxVoices = 0;
    long long voiceSum = 0, dropTicks = 0;
    std::vector<int> clashAt;
    for (int t = 0; t < kDnbBars * 96; ++t) {
        const long long until = (long long)((t + 0.9) * tc);
        pa.run(uint32_t(until - da)); da = until;
        pb.run(uint32_t(until - db)); db = until;
        int pcs[5], n = 0;
        for (int c = 0; c < 2; ++c) { if (c == 1 && synthDrumBar(t / 96)) continue; const Apu::ChState st = pa.apu().state(c); if (st.enabled && st.dac && st.vol >= 2) { const double hz = 131072.0 / (2048 - st.freq); pcs[n++] = ((int(std::lround(69 + 12 * std::log2(hz / 440.0))) % 12) + 12) % 12; } }
        for (int c = 0; c < 3; ++c) { const Apu::ChState st = pb.apu().state(c); if (st.enabled && st.dac && (c == 2 || st.vol >= 2)) { const double hz = (c == 2 ? 65536.0 : 131072.0) / (2048 - st.freq); pcs[n++] = ((int(std::lround(69 + 12 * std::log2(hz / 440.0))) % 12) + 12) % 12; } }
        const int bar = t / 96;
        if ((bar >= 16 && bar < 32) || (bar >= 44 && bar < 60)) { voiceSum += n; ++dropTicks; maxVoices = std::max(maxVoices, n); }
        if (n < 2) continue;
        ++multi;
        bool bad = false;
        for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) { const int d = std::abs(pcs[i] - pcs[j]) % 12, ic = std::min(d, 12 - d); if (ic == 1 || ic == 6) bad = true; }
        if (bad) { ++clash; clashAt.push_back(t / 96); }
    }
    std::printf("harmony: %d ticks with two or more tonal voices, %d with a semitone or tritone between them (%.2f %%)\n", multi, clash, multi ? 100.0 * clash / multi : 0.0);
    std::printf("  tonal voices at once in the drops: average %.2f, most %d\n", dropTicks ? double(voiceSum) / double(dropTicks) : 0.0, maxVoices);
    int perBar[kDnbBars] = {};
    for (int bar : clashAt) ++perBar[bar];
    std::printf("  clash ticks by bar:");
    for (int i = 0; i < kDnbBars; ++i) if (perBar[i]) std::printf(" %d:%d", i, perBar[i]);
    std::printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: make_dnb <out dir> [kit dir] [kit1 slot hex] [kit2 slot hex]\n"); return 1; }
    const std::string dir = argv[1];
    const std::string kdir = argc > 2 ? argv[2] : "demos/kits";
    const int k1 = argc > 3 ? int(std::strtol(argv[3], nullptr, 16)) : kDnbKit1;
    const int k2 = argc > 4 ? int(std::strtol(argv[4], nullptr, 16)) : kDnbKit2;
    std::vector<uint8_t> a1 = readFile(kdir + "/DM-AMEN1.kit"), a2 = readFile(kdir + "/DM-AMEN2.kit");
    if (a1.size() != kKitBankSize || a2.size() != kKitBankSize) { std::fprintf(stderr, "need %s/DM-AMEN1.kit and DM-AMEN2.kit (16 KB each)\n", kdir.c_str()); return 1; }
    RomKits kits;
    kits.setOverride(k1, a1.data());
    kits.setOverride(k2, a2.data());

    static Song a, b;
    buildDnbA(a, k1, k2);
    buildDnbB(b);
    std::printf("songs (kits in slots %02X and %02X):\n", k1, k2);
    writeSong(dir + "/DNB_A.lsdsng", a, kDnbNameA);
    writeSong(dir + "/DNB_B.lsdsng", b, kDnbNameB);
    for (int c = 0; c < 2; ++c) {                       // 128 KB saves with the song as working memory: what the LSDJ ROM (and tools/oracle) loads
        std::vector<uint8_t> sav(kSavSize);
        (c ? b : a).writeSav(sav.data());
        const std::string path = dir + (c ? "/DNB_B.sav" : "/DNB_A.sav");
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(sav.data(), 1, sav.size(), f); std::fclose(f); }
    }
    std::printf("pitches heard:\n");
    pitches("A", a, &kits);
    pitches("B", b, &kits);
    harmony(a, b, &kits);

    const int rate = 44100;
    auto renderPair = [&](int muteA, int muteB) {            // bit masks of channels to cut
        static Song sa, sb;
        buildDnbA(sa, k1, k2);
        buildDnbB(sb);
        Player pa(sa), pb(sb);
        pa.setKits(&kits); pb.setKits(&kits);
        for (int c = 0; c < 4; ++c) { pa.mute(c, (muteA >> c) & 1); pb.mute(c, (muteB >> c) & 1); }
        Dual d(pa, pb);
        d.start(0, 0);
        return pass(d, rate);
    };
    const std::vector<int16_t> mix = renderPair(0, 0);
    writeWav(dir + "/DNB_A+B.wav", mix, rate);
    const size_t frames = mix.size() / 2;
    std::printf("mix: one pass %.2f s, peak %.3f, %.2f %% of samples above 60 %%\n", double(frames) / rate, peak(mix), hot(mix));

    const int bars[9] = {8, 8, 8, 8, 8, 4, 8, 8, 8};
    const char* names[9] = {"intro", "build", "drop1a", "drop1b", "break", "build2", "drop2a", "drop2b", "outro"};
    std::printf("loudness by part (rms, peak per bar):\n");
    size_t at = 0;
    const double barFrames = double(frames) / kDnbBars;
    for (int i = 0; i < 9; ++i) {
        const size_t from = size_t(std::lround(at * barFrames)), to = size_t(std::lround((at + bars[i]) * barFrames));
        std::vector<int16_t> seg(mix.begin() + long(from * 2), mix.begin() + long(std::min(to, frames) * 2));
        std::printf("  %-7s %2d bars  rms %.3f  peak %.3f\n", names[i], bars[i], rms(mix, from, to), peak(seg));
        at += size_t(bars[i]);
    }
    std::printf("each channel alone (rms in drop 1 / break / drop 2):\n");
    static const char* chn[4] = {"PU1", "PU2", "WAV", "NOI"};
    const size_t partBars[3][2] = {{16, 8}, {32, 8}, {52, 8}};
    for (int con = 0; con < 2; ++con)
        for (int c = 0; c < 4; ++c) {
            const int keep = ~(1 << c) & 15;
            const std::vector<int16_t> solo = con ? renderPair(15, keep) : renderPair(keep, 15);
            std::printf("  %s %s ", con ? "B" : "A", chn[c]);
            for (const auto& pb : partBars)
                std::printf(" %.3f", rms(solo, size_t(std::lround(double(pb[0]) * barFrames)), size_t(std::lround(double(pb[0] + pb[1]) * barFrames))));
            std::printf("   peak %.3f\n", peak(solo));
        }
    return 0;
}
