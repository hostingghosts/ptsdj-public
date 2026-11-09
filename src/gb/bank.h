// An LSDJ save file (.sav / .srm, 128 KB) as a bank of songs: one per Game Boy. Layout as in docs/LSDJ_FORMAT.md:
// 0x0000 the working song, 0x8000 the file table (32 names, versions, 'jk', active slot, block table), 0x8200 191 blocks of 512 bytes.
// A song is stored as the blocks of its .lsdsng; the E0 jump bytes are renumbered to the absolute block (+1).
#pragma once

#include <cstddef>
#include <cstdint>

#include "gb/song.h"

namespace gb {

constexpr int kBankSlots = 32, kBankBlocks = 191, kBankBlockSize = 512;
constexpr uint32_t kBankTable = 0x8000, kBankData = 0x8200;

// Byte access to the file (a card file in the firmware, memory in the tests). False when the range cannot be read or written in full.
struct BankIO {
    virtual ~BankIO() = default;
    virtual bool read(uint32_t off, void* dst, uint32_t n) = 0;
    virtual bool write(uint32_t off, const void* src, uint32_t n) = 0;
};

struct Bank {
    char name[kBankSlots][9] = {};
    uint8_t version[kBankSlots] = {};
    uint8_t active = 0;
    uint8_t alloc[kBankBlocks];                 // owner slot of each block, 0xFF = free
    Bank() { for (auto& a : alloc) a = 0xFF; }

    // Reads the file table. A file that is missing, short or unmarked gives an empty bank and false (the file needs format() before a save).
    bool load(BankIO& io);
    // Writes 128 KB of 0xFF and an empty file table.
    static bool format(BankIO& io);
    // Writes the table back (names, versions, active slot, block table).
    bool save(BankIO& io) const;

    bool used(int slot) const { return slot >= 0 && slot < kBankSlots && name[slot][0]; }
    int count() const;
    int nthUsed(int n) const;                   // the slot of the n-th song in use, -1 if none
    int find(const char* nm) const;             // slot with this name (case-insensitive, first 8 characters), -1 if none
    int freeSlot() const;                       // first unused slot, -1 if all 32 are used
    int freeBlocks() const;
    int blocksOf(int slot) const;

    // Stores a song (the bytes of a .lsdsng: 8 name, 1 version, blocks) in `slot` and makes it the active one. The old blocks of the slot are
    // released only if the new ones do not fit beside them. False if there is no room (nothing is changed) or the file cannot be written.
    bool store(BankIO& io, int slot, const uint8_t* lsdsng, size_t n);
    // The slot's song as .lsdsng bytes into `out` (room for `cap`); returns the size, 0 on failure.
    size_t fetch(BankIO& io, int slot, uint8_t* out, size_t cap) const;
    void remove(int slot);                      // forget the song (call save() afterwards)
    static bool writeWorking(BankIO& io, const Song& s);   // the working song at 0x0000, the part the Game Boy starts with
};

}  // namespace gb
