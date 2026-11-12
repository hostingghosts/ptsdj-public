# Demos

`DEMO_A.lsdsng` and `DEMO_B.lsdsng` are the firmware's own demo songs, written from `src/gb/demo.cpp` by `make demo`: song A is the
melody and song B the beat and bass, one piece in A minor at 120 BPM (chords Am - F - C - G, 16 bars, SYNC on in both). Nothing in
them comes from another song. Copy them to `GB/Songs` to load them as loose files; the firmware also has them built in
(first start with no saved song, or the `*DEMO_A` / `*DEMO_B` entry of LOAD SONG).

Other people's LSDJ songs are not part of this repository. Kits (`kits/*.kit`) are sample kits for the wave channel and stay.

The tests and the simulator check use other songs from this folder when they find them, and skip those parts when they do not.
To run them with your own songs, put `.lsdsng` files here (git ignores them); `kicks-v9.2.lsdsng` is the one the
simulator's playback check looks for.

## Drum and bass (DNB_A, DNB_B)

`DNB_A.lsdsng` and `DNB_B.lsdsng` are written by `make dnb` from `src/gb/dnb.cpp`: one drum and bass piece in A minor at 170 BPM over two consoles, 68 bars (1:36), nine parts. They are **not** built into the firmware.
Song A plays the two Amen kits on its wave channel, so they need those kits in the kit slots:

| file on the card | kit slot (file name) | shown on screen as |
| --- | --- | --- |
| `GB/Kits/15_DM-AMEN1.kit` | 15 | KIT 16 |
| `GB/Kits/16_DM-AMEN2.kit` | 16 | KIT 17 |

(`make dnb` copies them with these names to `build/dnb/kits`.) Put both songs in `GB/Songs`, then Project > LOAD SONG FILE A and B; SYNC is on in both, so Play on either console starts both.
`build/dnb/DNB_A+B.wav` is the same piece rendered with the firmware's mixer. To use other slots, run `build/dnb/make_dnb <out> demos/kits <kit1 hex> <kit2 hex>` and load the songs it writes.
