// Upgrade of songs saved by older LSDJ versions to the 9.x memory layout (format byte 0x16). The rules were measured by loading the
// owner's old songs through LSDJ 9.4.2 itself (tools/oracle/upgradecheck.cpp compares them); not everything is covered.
#pragma once

#include "gb/song.h"

namespace gb {

// Converts `s` in place when its format byte is older than kFormatVersion. Returns false if the version is unknown (> 0x16 or < 7).
bool upgradeSong(Song& s);

}  // namespace gb
