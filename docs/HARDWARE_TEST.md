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
4. Project: SAVE SESSION A+B, power off, LOAD SESSION A+B; EXPORT WAV A+B (the screen waits while it renders); check `GB/Export/Snn.wav` on a computer.
5. Back to stock: Config > Firmware update, or the `ot_restore.txt` route, or the Polyend tool.
