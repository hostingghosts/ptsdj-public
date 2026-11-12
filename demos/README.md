# Demos

`DEMO_A.lsdsng` and `DEMO_B.lsdsng` are the firmware's own demo songs, written by `make demo` from `src/gb/dnb.cpp`: one drum and bass piece in A minor at 170 BPM
over two consoles, 68 bars (1:36) in nine parts. Song A is the drums, the lead and the hats; song B is the bass, the pads and the risers. SYNC is on in both, so Play on either
console starts both. Nothing in them comes from another song. Copy them to `GB/Songs` to load them as loose files; the firmware also has them built in (first start with no
saved song, or the `*DEMO_A` / `*DEMO_B` entry of LOAD SONG).

The drums are the two sample kits in `kits/` (DM-AMEN1 and DM-AMEN2), which are compiled into the firmware (`src/gb/builtin_kits.inc`, made from them by `make embed-kits`) as kit
slots 0x31 and 0x32 (shown as KIT 32 and 33): the last two of the user kit slots, banks 62 and 63 of an LSDJ ROM, so EXPORT LSDJ puts them into the exported `.gb` and the demo
plays its drums on a real Game Boy. A `.kit` file in the same slot replaces them. The loose `.lsdsng` files use those slots, so in another player they need the same two kits in
kit slots 0x31 and 0x32.
`build/demo/DEMO_A+B.wav` is the same piece rendered with the firmware's mixer (`make demo` also prints what was measured: the notes against A minor, clashes, loudness).

Other people's LSDJ songs are not part of this repository. Kits (`kits/*.kit`) are sample kits for the wave channel and stay.

The tests and the simulator check use other songs from this folder when they find them, and skip those parts when they do not.
To run them with your own songs, put `.lsdsng` files here (git ignores them); `kicks-v9.2.lsdsng` is the one the
simulator's playback check looks for.
