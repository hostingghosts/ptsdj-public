// Two original demo songs that play together as one piece: song A is the melody, song B is the beat and the bass.
// A minor, 120 BPM, 16 bars (4 sections of 4 bars: intro, verse, chorus, verse 2), chords Am - F - C - G, one per bar.
// Both songs have the same length and tempo and SYNC on, so Play on either console starts both. Nothing here is taken from
// another song; the waves are drawn into the song, so the demo needs no ROM.
#pragma once

#include "gb/song.h"

namespace gb {

constexpr const char* kDemoNameA = "DEMO_A";
constexpr const char* kDemoNameB = "DEMO_B";
constexpr int kDemoTempo = 120;

void buildDemoA(Song& s);   // melody: lead, chord pad, bell
void buildDemoB(Song& s);   // beat: kick, offbeat stabs, bass, hats and snare

}  // namespace gb
