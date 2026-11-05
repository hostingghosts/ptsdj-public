// Atari tracker -- project files on the SD card (AtariTracker/Projects/<NAME>.atr, and AtariTracker/autosave.atr).
//
// One binary file, little endian, a CRC-32 over everything before the last four bytes:
//   "ATRP", version 9, chip, region, speed, releaseFrames, orderLen, restartPos, trackVolume[4], tiaTrack[2] (version 2), tiaTune (version 4), funk (version 5), engineSpeed (version 7), sfx (version 8), stepsPerBeat (version 9), name[25], author[25],
//   patternCount, orderLen x {pattern, repeat},
//   per pattern that is not empty (ascending): index, rows, rows x 4 x {mask, fields present in the mask}
//     (mask b0 note, b1 instrument, b2 volume, b3 fx1 type + value, b4 fx2 type + value),
//   (version 3) the ADSR settings: count, count x {slot, attack, decay, sustain, peak, dist, curve},
//   the instruments (core/src/insfile.h, "ATRI" block), CRC-32.
// A file is checked completely (CRC, then every value) before the song is touched: a damaged or foreign file leaves the song as it was.
// Saving writes `<path>.tmp` and renames it over the old file, so a power cut keeps the previous version.
#pragma once

#include <string>

#include "atw/atsong.h"
#include "hal_api.h"

namespace atw {

bool saveProject(ot::IStorage& fs, const Song& song, const char* path, std::string* error = nullptr);
// Replaces `song` (its patterns must exist) when the whole file is good; otherwise returns false and changes nothing.
bool loadProject(ot::IStorage& fs, Song& song, const char* path, std::string* error = nullptr);

}  // namespace atw
