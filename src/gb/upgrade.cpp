#include "gb/upgrade.h"

#include <cstring>

namespace gb {

namespace {
// Release digit of the pulse/noise envelope byte: old -> new (8 = none in the old format, 9-F were the "increase" half).
uint8_t releaseNibble(uint8_t n) {
    static const uint8_t m[8] = {0, 5, 7, 8, 9, 10, 11, 11};
    if (n == 8 || n == 0) return 0;
    return m[n & 7];
}
bool isOldDefaultPulse(const uint8_t* r) {
    return r[0] == 0 && r[2] == 0x88 && r[4] == 0 && r[5] == 0x7F && r[6] == 1 && r[8] == 3;
}
}  // namespace

bool upgradeSong(Song& s) {
    uint8_t* m = s.mem();
    const uint8_t ver = m[0x7FFF];
    if (ver == kFormatVersion) return true;
    if (ver < 7 || ver > kFormatVersion) return false;

    if (ver < 9) {   // version 7: no B command yet, every command from B on moved up one letter
        for (int i = 0; i < 255 * 16; ++i) if (m[off::kPhraseCmd + i] >= 2) ++m[off::kPhraseCmd + i];
        for (int i = 0; i < 32 * 16; ++i) {
            if (m[off::kTblCmd1 + i] >= 2) ++m[off::kTblCmd1 + i];
            if (m[off::kTblCmd2 + i] >= 2) ++m[off::kTblCmd2 + i];
        }
    }
    for (int j = 0; j < kInstruments; ++j) {
        uint8_t* in = s.instrument(j);
        const int type = in[0] & 3;
        if (type == 0 && isOldDefaultPulse(in)) {
            static const uint8_t def[16] = {0x00, 0xA0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00};
            std::memcpy(in, def, 16);
            continue;
        }
        if (type == 0 || type == 3) in[1] = uint8_t((in[1] & 0xF0) | releaseNibble(in[1] & 15));
        if (type == 1) {                                   // wave: first frame = synth x 16, play modes renumbered
            if (ver < 9) { const uint8_t lo = in[2] & 15; in[2] = uint8_t((in[2] & 0xF0) | (lo == 0 ? 15 : lo + 1)); }
            in[3] = uint8_t(m[off::kInstr + j * 16 + 2] & 0xF0);
            if (ver < 0xA) in[9] = uint8_t((in[9] + 1) & 3);
        }
    }
    // 0x1D00-0x1E77 holds text tables that moved between versions (LSDJ's own upgrade leaves junk there): use a fresh song's.
    Song fresh;
    std::memcpy(m + 0x1D00, fresh.mem() + 0x1D00, 0x178);
    m[0x7FFF] = kFormatVersion;
    return true;
}

}  // namespace gb
