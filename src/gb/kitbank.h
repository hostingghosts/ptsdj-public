// LSDJ sample kits as the Tracker makes them. A kit is a 16 KB bank: 15 little-endian sample offsets (0x4000 based) at 0, a 3 character name per sample
// at 0x22, the kit name at 0x52 and raw 4-bit data (two samples per byte, high nibble first) from 0x60. A sample is a whole number of 16 byte chunks (32
// samples, one wave RAM load); the last chunk of a sample is not played, so every sample here ends with one chunk of silence. The offset table has one entry
// more than the kit has samples: the last one points to the end of the data, and LSDJ takes it as the end of the last sample (a sample without it is silent). Samples play at a fixed rate: one chunk per 11712 cycles, about 11.46 kHz.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gb {

constexpr int kKitMaxSamples = 15, kKitHeader = 0x60, kKitBytes = 0x4000, kKitRate = 11460;
constexpr int kKitCapacity = kKitBytes - kKitHeader;       // bytes of sample data in a bank
// LSDJ finds kit n in ROM bank n + 8 for kits 00-12, and in bank n + 13 from kit 13 on (the banks between are program code); measured on LSDJ 9.4.2.
// The stock ROM's kits are 00-14 (banks 8-26 and 32-33); banks 34-63 are empty, so kits 15-32 (30 of them) are free for your own.
constexpr int kKitFirstUserSlot = 0x15, kKitLastUserSlot = 0x32;
inline int kitBank(int kit) { return kit < 0 ? -1 : kit <= 0x12 ? kit + 8 : (kit + 13) & 63; }   // the ROM bank LSDJ reads for this kit number
inline int kitOfBank(int bank) { return bank >= 8 && bank <= 26 ? bank - 8 : bank >= 32 && bank <= 63 ? bank - 13 : -1; }   // the kit number of a ROM bank, -1 for code banks

// Operations on a 16 KB kit image (the caller owns the memory).
struct KitBank {
    static void clear(uint8_t* b);                                   // an empty kit
    static bool hasSamples(const uint8_t* b) { return b[0] == 0x60 && b[1] == 0x40; }
    static int count(const uint8_t* b);                              // samples (the end entry is not one)
    static void normalize(uint8_t* b);                               // adds the end entry to a kit file that has none (kits saved by older builds)
    static int length(const uint8_t* b, int i);                      // bytes of sample i
    static const uint8_t* data(const uint8_t* b, int i);
    static void name(const uint8_t* b, int i, char out[4]);          // 3 characters
    static void setName(uint8_t* b, int i, const char* nm);
    static void kitName(const uint8_t* b, char out[9]);
    static void setKitName(uint8_t* b, const char* nm);
    static int used(const uint8_t* b);                               // bytes of data in use (padding at the end of the bank is free)
    static int freeBytes(const uint8_t* b) { return kKitCapacity - used(b); }
    // Appends a sample of `n` bytes of packed nibbles (padded to whole chunks plus a silent chunk). False if the kit is full (15 samples or no room).
    static bool add(uint8_t* b, const char* nm, const uint8_t* bytes, int n);
    // Swaps sample i for new data and a new name (padded as add does); the samples after it move. False if the kit has no room for the difference.
    static bool replace(uint8_t* b, int i, const char* nm, const uint8_t* bytes, int n);
    static bool remove(uint8_t* b, int i);                           // the samples after it move up
    static int dataEnd(const uint8_t* b);                            // where the last chunk with sound ends
    static int entries(const uint8_t* b);                            // offset table entries in use
    static bool hasEnd(const uint8_t* b);
    static int paddedSize(int n) { return (n + 15) / 16 * 16 + 16; }
};

}  // namespace gb
