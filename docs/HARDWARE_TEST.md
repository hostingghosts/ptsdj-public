# First hardware test (nothing here has run on a Tracker yet)

Image: `build/PolyendTracGBTrkv010.ptf` (24-char name, `make CROSS=... ` checks it). Way back: reflash the stock `.ptf`.

Before flashing (LEARNINGS.md, "Recovery before testing"):
1. SD card: `/Firmware` holds only the stock image (`PolyendTracker_CE_1.0.0.ptf`) and this test image; everything else elsewhere (the rear reset button installs a random `.ptf`).
2. Put the stock image's name in `/Firmware/ot_restore.txt`: the app checks it right after the SD mounts, installs that image on the next boot and renames the file `.done`.
3. Copy your own `GB/LSDJ.GB` to the card (kits and the synth need it) and a demo to `GB/AUTOLOAD.LSDSNG` (optional `GB/AUTOLOAD_B.LSDSNG`).

Checks, in order (stop at the first failure and report what the screen showed):
1. Boots to the green LSDJ screen; keys move the cursor; the jog wheel edits.
2. Play: both consoles start and stop together; no clicks or dropouts (A alone, then A+B).
3. Synth screen (Fx1 (Synth page)): change SIGNAL; the frames rebuild. The bottom line shows `REBUILD nnnK CYC` (device cycles at 180 MHz: 18000K = 100 ms). Report that number; under ~30000K (170 ms) feels instant.
4. Power-on: the block letters PTSDJ drop to the middle of the grey-lens display, then the two-note chime.
5. Project: NAME the song (Insert on NAME), SAVE SONG, power off, LOAD SONG; EXPORT WAV (progress bar) and check `GB/Export/<name>.wav`; EXPORT LSDJ writes `.lsdsng` and `.sav` for A and B.
6. Pads: sound only, never the song; on the Instr page a pad plays that instrument (wave and kit instruments too); Rec on the Phrase page arms, jog picks the instrument, Rec + Play loops the phrase over the song and records.
7. SYNC: set A to MASTER and B to SLAVE (Project); Play starts both; changing A's tempo changes B's; SYNC OFF on both, then Play on one console: the other must be silent (no stuck note). Master key: side by side; F1/F8 pick, F2/F7 + jog level, F3-F6 mute, Shift + F3-F6 solo.
8. Edit helpers: Shift + Insert + arrows select, Copy, Shift + Copy paste; Help key shows the help pages; CLEAN SONG DATA / CLEAN INSTR DATA ask for a second Insert.
9. Instrument library (Sample loader): save an instrument, load it into another slot.
10. Back to stock: Config > Firmware update, or the `ot_restore.txt` route, or the Polyend tool.
