# Demos

`DEMO_A.lsdsng` and `DEMO_B.lsdsng` are the firmware's own demo songs, written from `src/gb/demo.cpp` by `make demo`: song A is the
melody and song B the beat and bass, one piece in A minor at 120 BPM (chords Am - F - C - G, 16 bars, SYNC on in both). Nothing in
them comes from another song. Copy them to `GB/Songs` to load them as loose files; the firmware also has them built in
(first start with no saved song, or the `*DEMO_A` / `*DEMO_B` entry of LOAD SONG).

Other people's LSDJ songs are not part of this repository. Kits (`kits/*.kit`) are sample kits for the wave channel and stay.

The tests and the simulator check use other songs from this folder when they find them, and skip those parts when they do not.
To run them with your own songs, put `.lsdsng` files here (git ignores them); `kicks-v9.2.lsdsng` is the one the
simulator's playback check looks for.
