// The two sample kits the built-in demo plays (DM-AMEN1 and DM-AMEN2, from demos/kits), compiled into the firmware so the demo has its drums on a card with no kit files.
// They sit in kit slots 0x31 and 0x32 (shown as KIT 32 and 33), the last two of the user's slots 0x15 to 0x32: in an LSDJ ROM those are banks 62 and 63, free in a stock ROM, so
// EXPORT LSDJ can put the kits into the exported ROM and the demo plays its drums on a real Game Boy. The user's own kits fill the slots from 0x15 up, so these are the least likely
// to be in the way, and a .kit file in the same slot still replaces them.
#pragma once

#include <cstdint>

namespace gb {

constexpr int kBuiltinKit1 = 0x31;
constexpr int kBuiltinKit2 = 0x32;

const uint8_t* builtinKit(int index);   // the 16 KB bank for kit slot `index`, or null for any other slot

}  // namespace gb
