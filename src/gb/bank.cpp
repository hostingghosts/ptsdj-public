#include "gb/bank.h"

#include <cctype>
#include <cstring>

namespace gb {

namespace {
// Where the jump number sits in a block of the compressed stream (E0 nn, nn = next block), -1 if the block ends the song instead.
int jumpPos(const uint8_t* b) {
    int i = 0;
    while (i < kBankBlockSize) {
        if (b[i] == 0xC0) i += (i + 1 < kBankBlockSize && b[i + 1] == 0xC0) ? 2 : 3;
        else if (b[i] == 0xE0) {
            if (i + 1 >= kBankBlockSize) return -1;
            const uint8_t n = b[i + 1];
            if (n == 0xE0) i += 2;
            else if (n == 0xFF) return -1;
            else if (n == 0xF0 || n == 0xF1) i += 3;
            else return i + 1;
        } else ++i;
    }
    return -1;
}

constexpr uint32_t kTableSize = kBankData - kBankTable;    // names, versions, markers and the block table: 512 bytes
}  // namespace

bool Bank::load(BankIO& io) {
    *this = Bank();
    uint8_t t[kTableSize];
    if (!io.read(kBankTable, t, kTableSize)) return false;
    if (t[0x13E] != 'j' || t[0x13F] != 'k') return false;
    for (int s = 0; s < kBankSlots; ++s) {
        std::memcpy(name[s], t + 8 * s, 8);
        name[s][8] = 0;
        for (int i = 0; i < 8; ++i) if (name[s][i] && !std::isprint(static_cast<unsigned char>(name[s][i]))) name[s][i] = '?';
        version[s] = t[0x100 + s];
    }
    active = t[0x140];
    for (int b = 0; b < kBankBlocks; ++b) alloc[b] = t[0x141 + b] < kBankSlots ? t[0x141 + b] : 0xFF;
    return true;
}

bool Bank::save(BankIO& io) const {
    uint8_t t[kTableSize];
    std::memset(t, 0, sizeof t);
    for (int s = 0; s < kBankSlots; ++s) {
        std::memcpy(t + 8 * s, name[s], 8);
        t[0x100 + s] = version[s];
    }
    t[0x13E] = 'j';
    t[0x13F] = 'k';
    t[0x140] = active;
    for (int b = 0; b < kBankBlocks; ++b) t[0x141 + b] = alloc[b];
    return io.write(kBankTable, t, kTableSize);
}

bool Bank::format(BankIO& io) {
    uint8_t f[4096];
    std::memset(f, 0xFF, sizeof f);
    for (uint32_t off = 0; off < kSavSize; off += sizeof f)
        if (!io.write(off, f, sizeof f)) return false;
    return Bank().save(io);
}

int Bank::count() const {
    int n = 0;
    for (int s = 0; s < kBankSlots; ++s) n += used(s);
    return n;
}

int Bank::nthUsed(int n) const {
    for (int s = 0; s < kBankSlots; ++s)
        if (used(s) && n-- == 0) return s;
    return -1;
}

int Bank::find(const char* nm) const {
    for (int s = 0; s < kBankSlots; ++s) {
        if (!used(s)) continue;
        int i = 0;
        for (; i < 8; ++i) {
            const int a = std::toupper(static_cast<unsigned char>(name[s][i])), b = i < 8 && nm[i] ? std::toupper(static_cast<unsigned char>(nm[i])) : 0;
            if (a != b) break;
            if (!a) return s;
        }
        if (i == 8) return s;
    }
    return -1;
}

int Bank::freeSlot() const {
    for (int s = 0; s < kBankSlots; ++s) if (!used(s)) return s;
    return -1;
}

int Bank::freeBlocks() const {
    int n = 0;
    for (int b = 0; b < kBankBlocks; ++b) n += alloc[b] == 0xFF;
    return n;
}

int Bank::blocksOf(int slot) const {
    int n = 0;
    for (int b = 0; b < kBankBlocks; ++b) n += alloc[b] == slot;
    return n;
}

bool Bank::store(BankIO& io, int slot, const uint8_t* d, size_t n) {
    if (slot < 0 || slot >= kBankSlots || n < 9 + kBankBlockSize || (n - 9) % kBankBlockSize) return false;
    const int need = int((n - 9) / kBankBlockSize);
    if (need > kBankBlocks) return false;
    // Blocks for the new song: free ones first, so a failed write leaves the old song whole; the slot's own blocks only when that is not enough.
    uint8_t pick[kBankBlocks];
    int got = 0;
    for (int b = 0; b < kBankBlocks && got < need; ++b) if (alloc[b] == 0xFF) pick[got++] = uint8_t(b);
    if (got < need) {
        got = 0;
        for (int b = 0; b < kBankBlocks && got < need; ++b) if (alloc[b] == 0xFF || alloc[b] == slot) pick[got++] = uint8_t(b);
        if (got < need) return false;
    }
    uint8_t blk[kBankBlockSize];
    for (int k = 0; k < need; ++k) {
        std::memcpy(blk, d + 9 + size_t(k) * kBankBlockSize, kBankBlockSize);
        const int j = jumpPos(blk);
        if (k + 1 < need) {
            if (j < 0) return false;                    // a block in the middle that does not continue: not a song
            blk[j] = uint8_t(pick[k + 1] + 1);
        }
        if (!io.write(kBankData + uint32_t(pick[k]) * kBankBlockSize, blk, kBankBlockSize)) return false;
    }
    for (int b = 0; b < kBankBlocks; ++b) if (alloc[b] == slot) alloc[b] = 0xFF;
    for (int k = 0; k < need; ++k) alloc[pick[k]] = uint8_t(slot);
    std::memcpy(name[slot], d, 8);
    name[slot][8] = 0;
    version[slot] = d[8];
    active = uint8_t(slot);
    return save(io);
}

size_t Bank::fetch(BankIO& io, int slot, uint8_t* out, size_t cap) const {
    if (!used(slot) || cap < 9 + kBankBlockSize) return 0;
    int b = -1;
    for (int i = 0; i < kBankBlocks; ++i) if (alloc[i] == slot) { b = i; break; }
    if (b < 0) return 0;
    std::memcpy(out, name[slot], 8);
    out[8] = version[slot];
    bool seen[kBankBlocks] = {};
    size_t k = 0;
    while (b >= 0) {
        if (9 + (k + 1) * kBankBlockSize > cap || seen[b]) return 0;
        seen[b] = true;
        uint8_t* blk = out + 9 + k * kBankBlockSize;
        if (!io.read(kBankData + uint32_t(b) * kBankBlockSize, blk, kBankBlockSize)) return 0;
        ++k;
        const int j = jumpPos(blk);
        if (j < 0) break;
        const int next = blk[j] - 1;
        if (next < 0 || next >= kBankBlocks || alloc[next] != slot) return 0;
        blk[j] = uint8_t(k + 1);                       // .lsdsng numbering: the next block of the file, 1-based
        b = next;
    }
    return 9 + k * kBankBlockSize;
}

void Bank::remove(int slot) {
    if (slot < 0 || slot >= kBankSlots) return;
    std::memset(name[slot], 0, sizeof name[slot]);
    version[slot] = 0;
    for (int b = 0; b < kBankBlocks; ++b) if (alloc[b] == slot) alloc[b] = 0xFF;
    if (active == slot) active = 0;
}

bool Bank::writeWorking(BankIO& io, const Song& s) {
    for (uint32_t off = 0; off < kMemSize; off += 4096)
        if (!io.write(off, s.mem() + off, 4096)) return false;
    return true;
}

}  // namespace gb
