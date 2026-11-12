// Drum and bass for two consoles, written from scratch: A minor, 170 BPM, 68 bars (1:36) in nine parts: intro, build, drop 1 (two rows), break, second build, drop 2 (two rows),
// outro. Song A is the drums (the two Amen kits on the wave channel: kicks and snares only, with a synth kick and snare tone from pulse 2 layered on the same ticks in the builds and drops), a lead, a slow arpeggio and hats; song B is the bass, root-and-fifth pads, a snare layer and the
// risers. Kept sparse and clean on purpose: no more than four tonal voices at once in a drop, and no notes a semitone or tritone apart. Both songs have the same length and tempo and SYNC
// on, so Play on either console starts both. The drums are the sample kits DM-AMEN1 and DM-AMEN2 (demos/kits), which the firmware has built in as kit slots kDnbKit1 and kDnbKit2; the rest is made of
// pulse, wave and noise instruments, tables and one groove, so a song file needs no ROM and no other kit.
#pragma once

#include "gb/builtin_kits.h"
#include "gb/song.h"

namespace gb {

constexpr int kDnbTempo = 170;
constexpr int kDnbKit1 = kBuiltinKit1;   // DM-AMEN1.kit, built into the firmware at slot 0x31 (shown as KIT 32)
constexpr int kDnbKit2 = kBuiltinKit2;   // DM-AMEN2.kit, built in at slot 0x32 (KIT 33)
constexpr int kDnbBars = 68;

void buildDnbA(Song& s, int kit1 = kDnbKit1, int kit2 = kDnbKit2);   // drums, lead, slow arpeggio and hats
void buildDnbB(Song& s);                                             // bass, pads, snare layer, stabs, risers

}  // namespace gb
