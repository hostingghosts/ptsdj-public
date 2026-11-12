// The built-in demo: two songs that play together as one piece, a drum and bass track in A minor at 170 BPM over both consoles (src/gb/dnb.cpp has the music).
// Song A is the drums (the two Amen kits, built into the firmware: builtin_kits.h), the lead and the hats; song B is the bass, the pads and the risers.
// Both songs have the same length and tempo and SYNC on, so Play on either console starts both. The waves are drawn into the song, so the demo needs no ROM.
#pragma once

#include "gb/dnb.h"
#include "gb/song.h"

namespace gb {

constexpr const char* kDemoNameA = "DEMO_A";
constexpr const char* kDemoNameB = "DEMO_B";
constexpr int kDemoTempo = kDnbTempo;

void buildDemoA(Song& s);   // drums, lead, slow arpeggio and hats
void buildDemoB(Song& s);   // bass, pads, snare layer and risers

}  // namespace gb
