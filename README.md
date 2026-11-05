# polyend-gb

LSDJ-style Game Boy tracker for the original Polyend Tracker (MK66). Personal use only (see `LICENSE`, `NOTICE.md`).

State: song model + .lsdsng/.sav files, sequencer, GB sound chip, player (partial), LSDJ-look screens (Song, Chain, Phrase,
Instrument, Table, Groove, Project). Not yet: synth/wave screens, kits, most commands, 8-channel dual engine, WAV export.
Desktop simulator first; nothing tested on the Tracker.

Controls (Game Boy key = Tracker key): D-pad = arrows, A = Insert, B = Delete, Select = Shift, Start = Play. Jog wheel =
A+left/right (Shift: A+up/down). Pads enter notes in the phrase screen. Shift+arrows move between screens.

```
make host-test                    # unit tests
make sim; build/sim/gb-sim        # simulator (keys: arrows, Insert, Delete, Shift, Space)
make -C sim check                 # screen tour (4 DMG shades only) + demo song to WAV
make CROSS=<path>/arm-none-eabi-  # build/PolyendTracGBTrkv010.ptf (24 chars, checked)
```
Songs: `GB/Songs/*.lsdsng` on the card (`GB/AUTOLOAD.LSDSNG` opens at start). `demos/` holds sample songs and kits.
Docs: `LEARNINGS.md`, `docs/LSDJ_FORMAT.md`, `docs/HARDWARE_MAP.md`, `UPSTREAM.md`. `tools/oracle/` checks against an LSDJ ROM.
