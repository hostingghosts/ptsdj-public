#include "gb/song.h"

namespace gb {

namespace {
struct Run { uint8_t n, v; };
const Run kDefaults[] = {
#include "gb/defaults.inc"
};
}  // namespace

void Song::reset() {
    size_t p = 0;
    for (const Run& r : kDefaults) {
        std::memset(m_ + p, r.v, r.n);
        p += r.n;
    }
}

uint8_t Song::defaultAt(uint16_t o) {
    size_t p = 0;
    for (const Run& r : kDefaults) {
        if (o < p + r.n) return r.v;
        p += r.n;
    }
    return 0;
}

void Song::writeSav(uint8_t* out) const {
    std::memset(out, 0xFF, kSavSize);
    std::memcpy(out, m_, kMemSize);
    std::memset(out + 0x8000, 0, 0x140);            // names, versions
    out[0x813E] = 'j';
    out[0x813F] = 'k';
}

bool Song::readSav(const uint8_t* in, size_t n) {
    if (n < kMemSize || in[0x1E78] != 'r' || in[0x1E79] != 'b') return false;
    std::memcpy(m_, in, kMemSize);
    return true;
}

}  // namespace gb
