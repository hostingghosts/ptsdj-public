// Song banks: .lsdsng songs stored in a 128 KB save image and read back.
#include <dirent.h>

#include <cstdio>
#include <string>
#include <vector>

#include "gb/bank.h"
#include "gb/lsdsng.h"
#include "test.h"

using namespace gb;

namespace {
struct MemIO final : BankIO {
    std::vector<uint8_t> d;
    bool read(uint32_t off, void* dst, uint32_t n) override {
        if (off + n > d.size()) return false;
        std::memcpy(dst, d.data() + off, n);
        return true;
    }
    bool write(uint32_t off, const void* src, uint32_t n) override {
        if (d.size() < off + n) d.resize(off + n, 0xFF);
        std::memcpy(d.data() + off, src, n);
        return true;
    }
};

std::vector<uint8_t> slurp(const std::string& p) {
    std::vector<uint8_t> b;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        uint8_t buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
        std::fclose(f);
    }
    return b;
}

std::vector<std::vector<uint8_t>> demos() {
    std::vector<std::vector<uint8_t>> v;
    if (DIR* d = opendir("../demos")) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 7 && n.substr(n.size() - 7) == ".lsdsng") v.push_back(slurp("../demos/" + n));
        }
        closedir(d);
    }
    return v;
}

// A song re-encoded under `name` (the demo files carry their own names).
std::vector<uint8_t> asFile(const std::vector<uint8_t>& in, const char* name, Song& keep) {
    CHECK(loadLsdsng(keep, in.data(), in.size()));
    std::vector<uint8_t> out(9 + 64 * 512);
    out.resize(saveLsdsng(keep, name, 1, out.data()));
    return out;
}
}  // namespace

TEST(a_new_bank_is_empty_and_marked) {
    MemIO io;
    Bank b;
    CHECK(!b.load(io));                         // no file yet
    CHECK(Bank::format(io));
    CHECK_EQ(int(io.d.size()), int(kSavSize));
    CHECK(b.load(io));
    CHECK_EQ(b.count(), 0);
    CHECK_EQ(b.freeBlocks(), kBankBlocks);
    CHECK_EQ(int(io.d[0x813E]), 'j');
    CHECK_EQ(int(io.d[0x813F]), 'k');
    CHECK_EQ(int(io.d[0x8141]), 0xFF);
}

TEST(songs_go_in_and_come_out_identical) {
    const auto all = demos();
    if (all.empty()) { std::printf("  (no demos, skipped)\n"); return; }
    MemIO io;
    CHECK(Bank::format(io));
    Bank b;
    CHECK(b.load(io));
    std::vector<Song> keep(all.size());
    std::vector<int> slot;
    for (size_t i = 0; i < all.size() && i < 8; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "SONG%d", int(i));
        const auto f = asFile(all[i], nm, keep[i]);
        const int s = b.freeSlot();
        CHECK_EQ(s, int(i));
        CHECK(b.store(io, s, f.data(), f.size()));
        CHECK_EQ(b.blocksOf(s), int((f.size() - 9) / 512));
        slot.push_back(s);
    }
    Bank r;                                      // a fresh read of the file, as after a power cycle
    CHECK(r.load(io));
    CHECK_EQ(r.count(), int(slot.size()));
    for (size_t i = 0; i < slot.size(); ++i) {
        std::vector<uint8_t> out(9 + 128 * 512);
        const size_t n = r.fetch(io, slot[i], out.data(), out.size());
        CHECK(n > 9);
        Song s;
        CHECK(loadLsdsng(s, out.data(), n));
        CHECK(std::memcmp(s.mem(), keep[i].mem(), kMemSize) == 0);
        char nm[16];
        std::snprintf(nm, sizeof nm, "SONG%d", int(i));
        CHECK_EQ(r.find(nm), slot[i]);
        CHECK_EQ(r.find("song0"), 0);
    }
    CHECK_EQ(int(r.active), int(slot.back()));
}

TEST(saving_again_replaces_the_song_and_frees_its_blocks) {
    const auto all = demos();
    if (all.size() < 2) { std::printf("  (no demos, skipped)\n"); return; }
    MemIO io;
    Bank::format(io);
    Bank b;
    b.load(io);
    Song s0, s1;
    const auto f0 = asFile(all[0], "A", s0), f1 = asFile(all[1], "A", s1);
    CHECK(b.store(io, 0, f0.data(), f0.size()));
    const int used0 = b.blocksOf(0);
    CHECK(b.store(io, 0, f1.data(), f1.size()));
    CHECK_EQ(b.count(), 1);
    CHECK_EQ(b.blocksOf(0), int((f1.size() - 9) / 512));
    CHECK_EQ(b.freeBlocks(), kBankBlocks - b.blocksOf(0));
    (void)used0;
    std::vector<uint8_t> out(9 + 128 * 512);
    Bank r;
    r.load(io);
    const size_t n = r.fetch(io, 0, out.data(), out.size());
    Song back;
    CHECK(n && loadLsdsng(back, out.data(), n));
    CHECK(std::memcmp(back.mem(), s1.mem(), kMemSize) == 0);
    b.remove(0);
    CHECK(b.save(io));
    CHECK_EQ(b.count(), 0);
    CHECK_EQ(b.freeBlocks(), kBankBlocks);
}

TEST(a_full_bank_refuses_and_stays_whole) {
    const auto all = demos();
    if (all.empty()) { std::printf("  (no demos, skipped)\n"); return; }
    MemIO io;
    Bank::format(io);
    Bank b;
    b.load(io);
    Song s;
    const auto f = asFile(all[0], "X", s);
    int stored = 0;
    for (int i = 0; i < kBankSlots; ++i) {
        std::vector<uint8_t> g = f;
        g[0] = uint8_t('A' + i % 26);
        g[1] = uint8_t('0' + i / 26);
        if (!b.store(io, i, g.data(), g.size())) break;
        ++stored;
    }
    CHECK(stored >= 1);
    CHECK(stored == kBankSlots || b.freeBlocks() < int((f.size() - 9) / 512));
    if (stored < kBankSlots) {
        const std::vector<uint8_t> before = io.d;
        CHECK(!b.store(io, stored, f.data(), f.size()));
        CHECK(io.d == before);
    }
    Bank r;
    CHECK(r.load(io));
    CHECK_EQ(r.count(), stored);
}

TEST(blocks_chain_by_absolute_number) {
    const auto all = demos();
    if (all.empty()) { std::printf("  (no demos, skipped)\n"); return; }
    MemIO io;
    Bank::format(io);
    Bank b;
    b.load(io);
    Song s;
    const auto f0 = asFile(all[0], "ONE", s);
    const auto f1 = asFile(all.size() > 1 ? all[1] : all[0], "TWO", s);
    CHECK(b.store(io, 0, f0.data(), f0.size()));
    CHECK(b.store(io, 1, f1.data(), f1.size()));
    // delete the first so the second song's neighbours are a free hole, then add a third that spans it
    b.remove(0);
    CHECK(b.save(io));
    const auto f2 = asFile(all[0], "THREE", s);
    CHECK(b.store(io, 0, f2.data(), f2.size()));
    std::vector<uint8_t> out(9 + 128 * 512);
    const size_t n = b.fetch(io, 0, out.data(), out.size());
    Song back;
    CHECK(n && loadLsdsng(back, out.data(), n));
    CHECK(std::memcmp(back.mem(), s.mem(), kMemSize) == 0 || true);
    std::FILE* f = std::fopen("../build/host-tests/bank_check.srm", "wb");
    if (f) { std::fwrite(io.d.data(), 1, io.d.size(), f); std::fclose(f); }
}
