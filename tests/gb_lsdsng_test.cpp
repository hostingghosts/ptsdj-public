// .lsdsng codec: decode the demo songs, re-encode, decode again, memory identical.
#include <dirent.h>

#include <cstdio>
#include <string>
#include <vector>

#include "gb/lsdsng.h"
#include "test.h"

using namespace gb;

namespace {
std::vector<uint8_t> slurp(const std::string& p) {
    std::vector<uint8_t> b;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        uint8_t buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
        std::fclose(f);
    }
    return b;
}
}  // namespace

TEST(demo_songs_roundtrip_through_the_codec) {
    DIR* d = opendir("../demos");
    if (!d) { std::printf("  (no demos directory, skipped)\n"); return; }
    int songs = 0;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() < 7 || n.substr(n.size() - 7) != ".lsdsng") continue;
        const auto in = slurp("../demos/" + n);
        Song a;
        CHECK(loadLsdsng(a, in.data(), in.size()));
        std::vector<uint8_t> out(9 + 64 * 512);
        const size_t len = saveLsdsng(a, "TEST", 1, out.data());
        CHECK(len > 9 && (len - 9) % 512 == 0);
        Song b;
        CHECK(loadLsdsng(b, out.data(), len));
        CHECK(std::memcmp(a.mem(), b.mem(), kMemSize) == 0);
        ++songs;
    }
    closedir(d);
    CHECK(songs >= 1);
}

TEST(garbage_is_rejected) {
    std::vector<uint8_t> junk(2000, 0x41);
    Song s;
    CHECK(!loadLsdsng(s, junk.data(), junk.size()));
    CHECK(!loadLsdsng(s, junk.data(), 10));
}
