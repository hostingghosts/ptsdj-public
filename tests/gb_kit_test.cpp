// Kit banks and WAV conversion.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gb/kitbank.h"
#include "gb/kits.h"
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
    CHECK_EQ(KitBank::count(b.data()), 13);                                  // 14 table entries: the last one is where the data ends
    char kn[9];
    KitBank::kitName(b.data(), kn);
    CHECK(std::strncmp(kn, "AMEN1", 5) == 0);
    CHECK_EQ(KitBank::length(b.data(), 0), 0x4530 - 0x4060);
    CHECK(KitBank::freeBytes(b.data()) < 200);
    CHECK_EQ(KitBank::length(b.data(), 12), 0x7f70 - 0x7af0);                // the last sample ends at the end entry
}

TEST(replacing_a_sample_keeps_the_others_and_gives_back_or_takes_room) {
    std::vector<uint8_t> b(kKitBytes);
    KitBank::clear(b.data());
    std::vector<uint8_t> a(100, 0x91), m(300, 0x72), z(60, 0x63);
    CHECK(KitBank::add(b.data(), "AAA", a.data(), int(a.size())));
    CHECK(KitBank::add(b.data(), "MMM", m.data(), int(m.size())));
    CHECK(KitBank::add(b.data(), "ZZZ", z.data(), int(z.size())));
    const int freeBefore = KitBank::freeBytes(b.data());
    std::vector<uint8_t> bigger(500, 0x55);
    CHECK(KitBank::replace(b.data(), 1, "NEW", bigger.data(), int(bigger.size())));      // the middle one grows
    CHECK_EQ(KitBank::count(b.data()), 3);
    CHECK_EQ(KitBank::data(b.data(), 0)[0], 0x91);
    CHECK_EQ(KitBank::data(b.data(), 1)[0], 0x55);
    CHECK_EQ(KitBank::data(b.data(), 2)[0], 0x63);
    CHECK_EQ(KitBank::length(b.data(), 1), KitBank::paddedSize(500));
    char nm[4];
    KitBank::name(b.data(), 1, nm);
    CHECK(std::strcmp(nm, "NEW") == 0);
    CHECK_EQ(KitBank::freeBytes(b.data()), freeBefore - (KitBank::paddedSize(500) - KitBank::paddedSize(300)));
    std::vector<uint8_t> tiny(32, 0x44);
    CHECK(KitBank::replace(b.data(), 2, "ZIP", tiny.data(), int(tiny.size())));         // the last one shrinks
    CHECK_EQ(KitBank::data(b.data(), 2)[0], 0x44);
    CHECK_EQ(KitBank::length(b.data(), 2), KitBank::paddedSize(32));
    std::vector<uint8_t> huge(kKitCapacity, 0x11);
    CHECK(!KitBank::replace(b.data(), 0, "BIG", huge.data(), int(huge.size())));        // no room: nothing changes
    CHECK_EQ(KitBank::data(b.data(), 0)[0], 0x91);
}

TEST(rounding_error_is_pushed_away_from_the_bass_so_a_kick_is_less_gritty) {
    MemReader r = sine(44100, 1, 80, 0.4, 0.8);
    WavInfo w;
    CHECK(wavOpen(r, w));
    std::vector<int16_t> tmp(kKitCapacity * 2);
    std::vector<uint8_t> out(kKitCapacity);
    bool cut;
    const int n = wavToNibbles(r, w, tmp.data(), int(tmp.size()), out.data(), int(out.size()), cut);
    CHECK(n > 0);
    const int samples = int(0.4 * kKitRate) - 8;
    std::vector<double> shaped(samples), plain(samples);
    for (int k = 0; k < samples; ++k) {
        shaped[k] = ((k & 1) ? (out[k >> 1] & 15) : (out[k >> 1] >> 4)) - 8.0;
        plain[k] = std::floor(8.0 + std::sin(6.2831853 * 80 * (k + 0.5) / kKitRate) * 7.0 + 0.5) - 8.0;
    }
    // take away the best sine at 80 Hz (its amplitude and phase), then look at what is left below about 700 Hz (an average over 16 samples)
    auto lowError = [&](const std::vector<double>& v) {
        double ss = 0, cc = 0, sc = 0, vs = 0, vc = 0;
        for (int k = 0; k < samples; ++k) {
            const double sn = std::sin(6.2831853 * 80 * (k + 0.5) / kKitRate), cs = std::cos(6.2831853 * 80 * (k + 0.5) / kKitRate);
            ss += sn * sn; cc += cs * cs; sc += sn * cs; vs += v[k] * sn; vc += v[k] * cs;
        }
        const double det = ss * cc - sc * sc, A = (vs * cc - vc * sc) / det, B = (vc * ss - vs * sc) / det;
        std::vector<double> res(samples);
        for (int k = 0; k < samples; ++k) res[k] = v[k] - A * std::sin(6.2831853 * 80 * (k + 0.5) / kKitRate) - B * std::cos(6.2831853 * 80 * (k + 0.5) / kKitRate);
        double sum = 0;
        int cnt = 0;
        for (int k = 64; k + 16 < samples - 64; k += 16) {
            double a2 = 0;
            for (int j = 0; j < 16; ++j) a2 += res[k + j];
            sum += a2 * a2 / 256.0;
            ++cnt;
        }
        return std::sqrt(sum / cnt);
    };
    const double e1 = lowError(shaped), e0 = lowError(plain);
    std::printf("  low-frequency error: shaped %.3f plain %.3f\n", e1, e0);
    CHECK(e1 < e0 * 0.8);
}

TEST(a_kit_has_an_end_entry_after_its_last_sample_like_lsdjs_own_kits) {
    std::vector<uint8_t> b(kKitBytes);
    KitBank::clear(b.data());
    std::vector<uint8_t> a(100, 0x91), m(300, 0x72);
    CHECK(KitBank::add(b.data(), "AAA", a.data(), int(a.size())));
    CHECK(KitBank::add(b.data(), "MMM", m.data(), int(m.size())));
    CHECK_EQ(KitBank::count(b.data()), 2);
    CHECK_EQ(KitBank::entries(b.data()), 3);
    const int end = (b[4] | b[5] << 8) - 0x4000;                              // entry 2: after the last sample
    CHECK_EQ(end, KitBank::used(b.data()));
    CHECK_EQ(KitBank::length(b.data(), 1), end - ((b[2] | b[3] << 8) - 0x4000));
    Kit k; k.data = b.data();
    int bg, en;
    CHECK(k.range(1, bg, en));                                                // the player reads the same end
    CHECK_EQ(en, end);
    CHECK(!k.range(2, bg, en));
    KitBank::remove(b.data(), 0);
    CHECK_EQ(KitBank::count(b.data()), 1);
    CHECK_EQ(KitBank::entries(b.data()), 2);
    KitBank::remove(b.data(), 0);
    CHECK_EQ(KitBank::entries(b.data()), 0);
    // a kit file from before the end entry: its last sample gets one
    KitBank::clear(b.data());
    KitBank::add(b.data(), "AAA", a.data(), int(a.size()));
    KitBank::add(b.data(), "MMM", m.data(), int(m.size()));
    b[4] = b[5] = 0;                                                          // take the end entry off again
    CHECK_EQ(KitBank::count(b.data()), 2);
    KitBank::normalize(b.data());
    CHECK_EQ(KitBank::count(b.data()), 2);
    CHECK_EQ(KitBank::entries(b.data()), 3);
}

TEST(times_are_shown_from_whole_hundredths_because_the_devices_printf_has_no_floats) {
    CHECK_EQ(secHundredths(0), 0);
    CHECK_EQ(secHundredths(5730), 100);                 // 5730 bytes = 11460 samples = one second
    CHECK_EQ(secHundredths(kKitCapacity), 284);
}

TEST(a_lower_level_scales_a_sample_around_the_centre_as_it_is_stored) {
    uint8_t src[4] = {0x0F, 0x88, 0x7A, 0xF0}, dst[4];
    KitBank::copyLevel(dst, src, 4, 100);
    CHECK(std::memcmp(src, dst, 4) == 0);
    KitBank::copyLevel(dst, src, 4, 50);
    CHECK_EQ(int(dst[0]), 0x4B);                        // 0 -> 4, 15 -> 11
    CHECK_EQ(int(dst[1]), 0x88);                        // silence stays silence
    CHECK_EQ(int(dst[3]), 0xB4);
    // two such samples layered (x + y - 8) no longer clip
    for (int v = 0; v < 16; ++v) { const int h = 8 + (v - 8) * 50 / 100; CHECK(h + h - 8 >= 0 && h + h - 8 <= 15); }
}
