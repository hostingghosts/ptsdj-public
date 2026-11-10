// LSDJ sample kits: 16 KB banks (offset table at 0, 15 names, raw 4-bit samples streamed to the wave channel).
// The ROM's own kits are banks 8.. of an LSDJ ROM image; extra kits come from .kit files. Nothing here is bundled.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gb {

constexpr size_t kKitBankSize = 0x4000;

struct Kit {
    const uint8_t* data = nullptr;       // one 16 KB bank, or null when the kit is not available
    int samples() const;                 // how many samples the offset table lists
    bool range(int i, int& begin, int& end) const;   // byte range of sample i inside the bank
};

class KitSource {
public:
    virtual ~KitSource() = default;
    virtual Kit kit(int index) const = 0;
};

// Kits from a memory image of an LSDJ ROM (kit n is bank 8 + n) with optional per-index overrides from .kit files.
class RomKits final : public KitSource {
public:
    void setRom(const uint8_t* rom, size_t size) { rom_ = rom; size_ = size; }
    void setOverride(int index, const uint8_t* bank) { if (index >= 0 && index < 64) over_[index] = bank; }
    const uint8_t* override_(int index) const { return index >= 0 && index < 64 ? over_[index] : nullptr; }
    Kit kit(int index) const override;
private:
    const uint8_t* rom_ = nullptr;
    size_t size_ = 0;
    const uint8_t* over_[64] = {};
};

}  // namespace gb
