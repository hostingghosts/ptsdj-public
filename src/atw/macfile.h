// Atari tracker -- the Mac Atari app's session files (AtariTracker/Mac/<NAME>.atari.json), the format of core/src/macsession.h.
//
//   saveMacSession  the whole song, written in pieces (nothing but a small buffer in memory). POKEY only (the app's TIA layout is unknown).
//                   What the app's file cannot hold is dropped: the effects, volume-only rows, the loop point, the per-track mixer. The app counts whole bars of 16 rows, so a pattern's length is rounded up to a bar.
//   loadMacSession  streams the file (core/src/json.h JsonStream) in three rounds, so a large session never has to fit in memory:
//                   1. sizes and tempo, 2. the same walk without storing, 3. the walk that fills `song`.
//                   Every value is checked in round 2, so a bad file leaves the song as it was. POKEY sessions only; instruments beyond the
//                   Tracker's 48 are dropped (an error when a note uses one), patterns beyond 64 are an error.
#pragma once

#include <string>

#include "atw/atsong.h"
#include "hal_api.h"

namespace atw {

bool saveMacSession(ot::IStorage& fs, const Song& song, const char* path, std::string* error = nullptr);
// `title` becomes the song's name. Replaces `song` (its patterns must exist) only when the whole file is good.
bool loadMacSession(ot::IStorage& fs, Song& song, const char* path, const char* title, std::string* error = nullptr);

}  // namespace atw
