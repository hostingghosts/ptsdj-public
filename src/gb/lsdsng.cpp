#include "gb/lsdsng.h"

#include <cstring>

namespace gb {

namespace {
// Defaults the compressor abbreviates (E0 F0 n: n default wave frames, E0 F1 n: n default instruments).
const uint8_t kDefWave[16] = {0x71, 0x32, 0x33, 0x44, 0x45, 0x55, 0x66, 0x77, 0x78, 0x89, 0x99, 0xAA, 0xAB, 0xBC, 0xCD, 0xCE};
const uint8_t kDefInstr[16] = {0x00, 0x88, 0, 0, 0x7F, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0};
}  // namespace

bool loadLsdsng(Song& s, const uint8_t* d, size_t n) {
    if (n < 9 + 512) return false;
    const uint8_t* p = d + 9;
    const size_t len = n - 9;
    size_t i = 0, o = 0;
    uint8_t* m = s.mem();
    auto put = [&](const uint8_t* src, int count, int rep) {
        for (int r = 0; r < rep; ++r) {
            if (o + count > kMemSize) return false;
            std::memcpy(m + o, src, size_t(count));
            o += size_t(count);
        }
        return true;
    };
    while (i < len) {
        const uint8_t b = p[i];
        if (b == 0xC0) {
            if (i + 1 >= len) return false;
            if (p[i + 1] == 0xC0) { if (!put(p + i, 1, 1)) return false; i += 2; }
            else { if (i + 2 >= len) return false; const uint8_t v = p[i + 1]; if (!put(&v, 1, p[i + 2])) return false; i += 3; }
        } else if (b == 0xE0) {
            if (i + 1 >= len) return false;
            const uint8_t k = p[i + 1];
            if (k == 0xE0) { if (!put(p + i, 1, 1)) return false; i += 2; }
            else if (k == 0xFF) break;
            else if (k == 0xF0) { if (i + 2 >= len || !put(kDefWave, 16, p[i + 2])) return false; i += 3; }
            else if (k == 0xF1) { if (i + 2 >= len || !put(kDefInstr, 16, p[i + 2])) return false; i += 3; }
            else i = (i / 512 + 1) * 512;           // jump: the next block of the file
        } else { if (!put(p + i, 1, 1)) return false; ++i; }
    }
    return o == kMemSize && m[0x1E78] == 'r' && m[0x1E79] == 'b';
}

size_t saveLsdsng(const Song& s, const char* name, uint8_t version, uint8_t* out) {
    std::memset(out, 0, 9);
    std::strncpy(reinterpret_cast<char*>(out), name, 8);
    out[8] = version;
    const uint8_t* m = s.mem();
    uint8_t* blk = out + 9;
    size_t used = 0;                       // bytes written in the current block
    size_t blocks = 1;
    // Every block keeps room for the 2-byte jump or end marker at its end.
    auto room = [&](size_t need) {
        if (used + need + 2 > 512) {
            blk[used] = 0xE0; blk[used + 1] = uint8_t(blocks + 1);   // jump to the next block (1-based chain index)
            std::memset(blk + used + 2, 0, 512 - used - 2);
            blk += 512; used = 0; ++blocks;
        }
    };
    size_t i = 0;
    while (i < kMemSize) {
        // default wave frames / instruments
        size_t j = i;
        if (i >= off::kWave && i < off::kWave + 0x1000 && (i - off::kWave) % 16 == 0) {
            int cnt = 0; while (j + 16 <= off::kWave + 0x1000 && cnt < 255 && !std::memcmp(m + j, kDefWave, 16)) { j += 16; ++cnt; }
            if (cnt >= 2) { room(3); blk[used++] = 0xE0; blk[used++] = 0xF0; blk[used++] = uint8_t(cnt); i = j; continue; }
        }
        if (i >= off::kInstr && i < off::kInstr + 0x400 && (i - off::kInstr) % 16 == 0) {
            int cnt = 0; while (j + 16 <= off::kInstr + 0x400 && cnt < 255 && !std::memcmp(m + j, kDefInstr, 16)) { j += 16; ++cnt; }
            if (cnt >= 1) { room(3); blk[used++] = 0xE0; blk[used++] = 0xF1; blk[used++] = uint8_t(cnt); i = j; continue; }
        }
        size_t run = 1;
        while (i + run < kMemSize && run < 255 && m[i + run] == m[i]) ++run;
        if (run >= 4 && m[i] != 0xC0) { room(3); blk[used++] = 0xC0; blk[used++] = m[i]; blk[used++] = uint8_t(run); i += run; continue; }
        room(2);
        if (m[i] == 0xC0 || m[i] == 0xE0) { blk[used++] = m[i]; blk[used++] = m[i]; } else blk[used++] = m[i];
        ++i;
    }
    blk[used] = 0xE0; blk[used + 1] = 0xFF;    // end of file
    std::memset(blk + used + 2, 0, 512 - used - 2);
    return 9 + blocks * 512;
}

}  // namespace gb
