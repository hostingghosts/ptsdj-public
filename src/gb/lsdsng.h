// LSDJ .lsdsng / .lsdprj: 8-byte name, 1 version byte, then 512-byte compressed blocks (RLE 0xC0, 0xE0 specials).
#pragma once

#include <cstddef>
#include <cstdint>

#include "gb/song.h"

namespace gb {

// Decode a .lsdsng image into `s`. False if the stream is malformed or does not fill the 32 KB song.
bool loadLsdsng(Song& s, const uint8_t* data, size_t n);
// Encode `s` as a .lsdsng; returns the byte count (name 8 bytes, version byte, blocks). `out` needs 9 + 64 * 512 bytes.
size_t saveLsdsng(const Song& s, const char* name, uint8_t version, uint8_t* out);

}  // namespace gb
