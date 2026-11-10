// Kit banks and WAV conversion.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gb/kitbank.h"
#include "gb/wavconv.h"
#include "test.h"

using namespace gb;

namespace {
struct MemReader final : WavReader {
    std::vector<uint8_t> d;
    int read(uint32_t off, void* dst, int n) override {
        if (off >= d.size()) return 0;
        const int k = int(std::min<size_t>(size_t(n), d.size() - off));
        std::memcpy(dst, d.data() + off, size_t(k));
        return k;
    }
};

void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }

// A 16-bit WAV of a sine wave.
MemReader sine(int rate, int channels, double hz, double seconds, double amp) {
    MemReader r;
    const int frames = int(rate * seconds);
    r.d = {'R', 'I', 'F', 'F'};
    put32(r.d, uint32_t(36 + frames * channels * 2));
    for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}) r.d.push_back(uint8_t(c));
    put32(r.d, 16); put16(r.d, 1); put16(r.d, uint16_t(channels)); put32(r.d, uint32_t(rate)); put32(r.d, uint32_t(rate * channels * 2)); put16(r.d, uint16_t(channels * 2)); put16(r.d, 16);
    for (char c : {'d', 'a', 't', 'a'}) r.d.push_back(uint8_t(c));
    put32(r.d, uint32_t(frames * channels * 2));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) put16(r.d, uint16_t(int16_t(std::lround(std::sin(6.2831853 * hz * i / rate) * amp * 32767))));
    return r;
}
}  // namespace

TEST(a_sine_wave_becomes_whole_chunks_of_nibbles_at_full_range) {
    MemReader r = sine(44100, 2, 220, 0.1, 0.25);
    WavInfo w;
    CHECK(wavOpen(r, w));
    CHECK_EQ(w.rate, 44100);
    CHECK_EQ(w.channels, 2);
    std::vector<int16_t> tmp(kKitCapacity * 2);
    std::vector<uint8_t> out(kKitCapacity);
    bool cut = true;
    const int n = wavToNibbles(r, w, tmp.data(), int(tmp.size()), out.data(), int(out.size()), cut);
    CHECK(!cut);
    CHECK(n > 0 && n % 16 == 0);
    CHECK(std::abs(n - int(0.1 * kKitRate / 2)) <= 16);                      // 0.1 s at the kit rate, two samples per byte
    int lo = 15, hi = 0;
    for (int i = 0; i < n; ++i) { lo = std::min({lo, out[i] >> 4, out[i] & 15}); hi = std::max({hi, out[i] >> 4, out[i] & 15}); }
    CHECK(lo <= 1);                                                         // a quiet file is scaled up to the whole 4-bit range
    CHECK_EQ(hi, 15);
}

TEST(a_long_file_is_cut_to_what_a_kit_holds_and_junk_is_refused) {
    MemReader r = sine(22050, 1, 100, 6.0, 0.9);
    WavInfo w;
    CHECK(wavOpen(r, w));
    std::vector<int16_t> tmp(kKitCapacity * 2);
    std::vector<uint8_t> out(kKitCapacity);
    bool cut = false;
    const int n = wavToNibbles(r, w, tmp.data(), int(tmp.size()), out.data(), int(out.size()), cut);
    CHECK(cut);
    CHECK(n <= kKitCapacity);
    MemReader junk;
    junk.d = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    WavInfo w2;
    CHECK(!wavOpen(junk, w2));
}

TEST(kits_take_samples_name_them_and_give_the_room_back_when_one_is_removed) {
    std::vector<uint8_t> b(kKitBytes);
    KitBank::clear(b.data());
    CHECK_EQ(KitBank::count(b.data()), 0);
    std::vector<uint8_t> s1(100, 0x9A), s2(300, 0x7C);
    CHECK(KitBank::add(b.data(), "KIK", s1.data(), int(s1.size())));
    CHECK(KitBank::add(b.data(), "SNR", s2.data(), int(s2.size())));
    CHECK_EQ(KitBank::count(b.data()), 2);
    CHECK(KitBank::hasSamples(b.data()));
    char nm[4];
    KitBank::name(b.data(), 1, nm);
    CHECK(std::strcmp(nm, "SNR") == 0);
    CHECK_EQ(KitBank::length(b.data(), 0), KitBank::paddedSize(100));        // 112 bytes of sample and a silent chunk
    CHECK_EQ(KitBank::data(b.data(), 0)[0], 0x9A);
    CHECK_EQ(KitBank::data(b.data(), 1)[0], 0x7C);
    const int freeBefore = KitBank::freeBytes(b.data());
    CHECK(KitBank::remove(b.data(), 0));
    CHECK_EQ(KitBank::count(b.data()), 1);
    CHECK_EQ(KitBank::data(b.data(), 0)[0], 0x7C);
    KitBank::name(b.data(), 0, nm);
    CHECK(std::strcmp(nm, "SNR") == 0);
    CHECK_EQ(KitBank::freeBytes(b.data()), freeBefore + KitBank::paddedSize(100));
    KitBank::setKitName(b.data(), "DRUMS");
    char kn[9];
    KitBank::kitName(b.data(), kn);
    CHECK(std::strncmp(kn, "DRUMS", 5) == 0);
}

TEST(a_kit_stops_at_15_samples_or_when_it_is_full) {
    std::vector<uint8_t> b(kKitBytes);
    KitBank::clear(b.data());
    std::vector<uint8_t> s(64, 0x55);
    for (int i = 0; i < kKitMaxSamples; ++i) CHECK(KitBank::add(b.data(), "S", s.data(), int(s.size())));
    CHECK(!KitBank::add(b.data(), "X", s.data(), int(s.size())));
    KitBank::clear(b.data());
    std::vector<uint8_t> big(kKitCapacity - 16, 0x55);
    CHECK(KitBank::add(b.data(), "BIG", big.data(), int(big.size()) - 16));
    CHECK(!KitBank::add(b.data(), "TWO", big.data(), 64));
}

TEST(the_demo_amen_kits_are_read_as_kits) {
    std::FILE* f = std::fopen("../demos/kits/DM-AMEN1.kit", "rb");
    if (!f) { std::printf("  (no demo kit, skipped)\n"); return; }
    std::vector<uint8_t> b(kKitBytes);
    CHECK_EQ(int(std::fread(b.data(), 1, b.size(), f)), kKitBytes);
    std::fclose(f);
    CHECK(KitBank::hasSamples(b.data()));
    CHECK_EQ(KitBank::count(b.data()), 14);
    char kn[9];
    KitBank::kitName(b.data(), kn);
    CHECK(std::strncmp(kn, "AMEN1", 5) == 0);
    CHECK_EQ(KitBank::length(b.data(), 0), 0x4530 - 0x4060);
    CHECK(KitBank::freeBytes(b.data()) < 200);                               // the last sample runs to the end of the bank
}
