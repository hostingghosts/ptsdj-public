#include "gb/kits.h"
#include "gb/kitbank.h"

namespace gb {

int Kit::samples() const { return data ? KitBank::count(data) : 0; }

bool Kit::range(int i, int& begin, int& end) const {
    const int n = samples();
    if (!data || i < 0 || i >= n) return false;
    begin = (data[i * 2] | data[i * 2 + 1] << 8) - 0x4000;
    end = i + 1 < KitBank::entries(data) ? (data[i * 2 + 2] | data[i * 2 + 3] << 8) - 0x4000 : int(kKitBankSize);   // the entry after the last sample is where it ends
    return begin >= 0x60 && end > begin && end <= int(kKitBankSize);
}

Kit RomKits::kit(int index) const {
    Kit k;
    if (index >= 0 && index < 64 && over_[index]) { k.data = over_[index]; return k; }
    const int bank = kitBank(index);
    const size_t off = bank < 0 ? 0 : size_t(bank) * kKitBankSize;
    if (rom_ && bank >= 0 && off + kKitBankSize <= size_ && rom_[off] == 0x60 && rom_[off + 1] == 0x40) k.data = rom_ + off;
    return k;
}

}  // namespace gb
