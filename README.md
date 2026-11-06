# polyend-gb

LSDJ-style Game Boy tracker for the original Polyend Tracker (MK66). Personal use only (see `LICENSE`, `NOTICE.md`).

State: song model + .lsdsng/.sav files, sequencer, GB sound chip, player (close to LSDJ 9.4.2, see `docs/LSDJ_FORMAT.md`), LSDJ-look
screens (Song, Chain, Phrase, Instrument, Table, Groove, Project), 8 channels (two linked Game Boys, tracks 1-4 = song A, 5-8 = song B,
summed stereo), WAV export. Synth and Wave screens (Select+Up from Phrase = Synth, then Right = Wave; the ROM's own generator rebuilds a synth's 16 frames after each edit,
so they need `GB/LSDJ.GB` on the card, like the kits). Not yet: file browser, wave range select, the "hand-drawn waves?" prompt.
Desktop simulator first; nothing tested on the Tracker.

Controls (Game Boy key = Tracker key): D-pad = arrows, A = Insert, B = Delete, Select = Shift, Start = Play. Jog wheel =
A+left/right (Shift: A+up/down). Pads enter notes in the phrase screen. Shift+arrows move between screens.

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
