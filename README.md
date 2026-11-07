# polyend-gb

LSDJ-style Game Boy tracker for the original Polyend Tracker (MK66). Personal use only (see `LICENSE`, `NOTICE.md`).

State: song model + .lsdsng/.sav files, sequencer, GB sound chip, player (close to LSDJ 9.4.2, see `docs/LSDJ_FORMAT.md`), LSDJ-look
screens (Song, Chain, Phrase, Instrument, Table, Groove, Project), 8 channels (two linked Game Boys, tracks 1-4 = song A, 5-8 = song B,
summed stereo), WAV export. Synth and Wave screens (Select+Up from Phrase = Synth, then Right = Wave; the ROM's own generator rebuilds a synth's 16 frames after each edit,
so they need `GB/LSDJ.GB` on the card, like the kits). Project > LOAD SONG FILE A/B lists `GB/Songs` and loads a song into that console. Drawing on the Wave page marks the synth hand-drawn; editing that synth then asks "OVERWRITE HAND-DRAWN WAVES?" (NO preselected, as in the ROM). The Synth page's WAVE 00-0F row is display only in the ROM, so there is no range select to build.
Desktop simulator first; nothing tested on the Tracker.

Controls on the Tracker: arrows = D-pad, Insert = A, Delete = B, Shift = Select, Play = Start. Jog wheel = A + left/right (Shift: A + up/down).
Page keys jump straight to a page: Song = Song, Pattern = Chain, Note = Phrase, Instr = Instr, Instr params = Table, Fx1 = Synth, Fx2 = Wave,
Config = Project, File = song files, Performance = Groove. Shift + arrows still move between screens; Shift + Song swaps console A/B.
Pads play the sound (never the song): the cursor channel on Song / Chain / Phrase, the instrument's own channel on Instr / Table, the wave being edited on
Synth / Wave. Shift + a pad row sets the octave. On the Phrase page the pads insert notes at the cursor; Rec turns on Rec mode instead: the jog wheel picks
the instrument, pads only play it, and Play loops this phrase on the cursor channel and records the pads into it (quantised to steps) until Play stops it.
On the Synth and Wave pages Play auditions the wave instead of starting the song. Shift + Insert mutes a channel on the Song page, Shift + Delete a console.
Project: SYNC OFF / LSDJ (project byte 0x3FBD). With both projects on LSDJ, Play on either console starts both and the other follows its tempo
(the virtual link cable); with SYNC off Play starts only that console. EXPORT LSDJ writes `GB/Export/Snn_A/B.lsdsng` and `.sav` for two real Game Boys.

```
make host-test                    # unit tests
make sim; build/sim/gb-sim        # simulator (keys: arrows, Insert, Delete, Shift, Space)
make -C sim check                 # screen tour (4 DMG shades only) + demo song to WAV
make CROSS=<path>/arm-none-eabi-  # build/PolyendTracGBTrkv010.ptf (24 chars, checked)
```
Two consoles: the Song key switches the screen between tracks 1-4 (A) and 5-8 (B); Play starts and stops both together, each keeping its own
tempo and L/R panning. Project screen: SAVE/LOAD SESSION A+B (`GB/Songs/Snn_A/B.lsdsng` (slot nn 00-99, A+Left/Right on the row), each opens on a real LSDJ) and EXPORT WAV A+B
(`GB/Export/Snn.wav`, one pass until every channel loops; the screen waits while it renders).
Songs: `GB/Songs/*.lsdsng` on the card (`GB/AUTOLOAD.LSDSNG` and `GB/AUTOLOAD_B.LSDSNG` open at start). `demos/` holds sample songs and kits.
Docs: `LEARNINGS.md`, `docs/LSDJ_FORMAT.md`, `docs/HARDWARE_MAP.md`, `UPSTREAM.md`. `tools/oracle/` checks against an LSDJ ROM.

Extra sample kits: put 16 KB LSDJ `.kit` files in `GB/Kits` on the card. The first two characters of the name (hex, 00-3F) are the kit slot the file replaces, e.g. `0A_808.kit` replaces kit 0A (the number a song's kit instrument shows); up to 16 files, read at boot. The Project readout ends with `K<n>`, the number of kit files loaded.
