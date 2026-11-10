# polyend-gb

LSDJ-style Game Boy tracker for the original Polyend Tracker (MK66). Personal use only (see `LICENSE`, `NOTICE.md`).

State: song model + .lsdsng/.sav files, sequencer, GB sound chip, player (close to LSDJ 9.4.2, see `docs/LSDJ_FORMAT.md`), LSDJ-look
screens (Song, Chain, Phrase, Instrument, Table, Groove, Project), 8 channels (two linked Game Boys, tracks 1-4 = song A, 5-8 = song B,
summed stereo), WAV export. Synth and Wave screens (Select+Up from Phrase = Synth, then Right = Wave; the ROM's own generator rebuilds a synth's 16 frames after each edit,
so they need `GB/LSDJ.GB` on the card, like the kits). Project > LOAD SONG FILE A/B lists `GB/Songs` and loads a song into that console. Drawing on the Wave page marks the synth hand-drawn; editing that synth then asks "OVERWRITE HAND-DRAWN WAVES?" (NO preselected, as in the ROM). The Synth page's WAVE 00-0F row is display only in the ROM, so there is no range select to build.
Desktop simulator first; nothing tested on the Tracker.

Controls on the Tracker: arrows = D-pad, Insert = A, Delete (the backspace key) = B, Shift = Select, Play = Start. Jog wheel = A + left/right (Shift: A + up/down).
Page keys: Song = Song, Pattern = Chain, Note = Phrase, Instr = Instr, Instr params = Table, Fx1 = Synth, Fx2 = Wave, Config = Project, File = song files,
Sample loader = instrument library, Performance = Groove, Help = help pages. Shift + arrows still move between screens; Shift + Song swaps console A/B.
Pads play the sound (never the song); the instrument decides the channel (pulse on PU1/PU2, wave and kit on WAV, noise on NOI). Shift + a pad row = octave.
Phrase page: with Rec off the pads enter notes at the cursor (step mode). Rec arms: the jog wheel picks the instrument and the pads sound it.
Rec + Play loops the phrase on screen on the cursor channel while the song plays behind it, and the pads record into it (quantised to steps); Play stops.
Copy / paste: Shift + Insert + Up/Down (or Left/Right) selects cells on Song, Chain, Phrase, Table and Groove pages; Copy copies, Shift + Copy pastes at the
cursor. Only on the same page. With CLONE = DEEP (Project page) pasting chains or phrases makes new copies; SLIM pastes the same numbers.
Delete alone clears the cell under the cursor (or every selected cell) on Song, Chain, Phrase and Table; with a selection the jog wheel (or Insert + arrows)
changes every filled cell. Shift + Insert alone mutes a channel on the Song page, Shift + Delete a console.
Instr page: move up onto the instrument number, then Left/Right or the jog wheel scroll through the instruments (the pads play the one on screen). Insert on NAME opens
the name keyboard (Insert types, the < key erases, Play = OK, Delete = NO); the project name works the same way.
Help: the Help key opens the pages (Left/Right turn them, Delete leaves). Hold F8 (outside the side by side view, a ? shows above it) for a pop-up that explains the
page you are on; Left/Right turn its pages, or F1 (back) and F2 (forward) with the free hand: a tab with < and > grows out of the bezel above them. Hold Shift + Note for the Phrase page's command cheat sheet, Shift + Instr params for the Table page's; they go away on release.
Project: NAME, TEMPO, TRANSPOSE, SYNC (OFF / MASTER / SLAVE), CLONE, LOOK (display colours), PRELISTEN, HELP, CLEAN SONG DATA / CLEAN INSTR DATA (Insert twice),
SAVE SONG, LOAD SONG, EXPORT WAV, EXPORT LSDJ (A, B or A + B; pick with the jog wheel). Saving and exporting show a progress bar.
Songs live in one bank file per Game Boy, like a real LSDJ cartridge: `GB/Saves/A.srm` (song A) and `GB/Saves/B.srm` (song B), 128 KB each with up to 32 songs
(same layout as a cartridge `.sav`; `.srm` is just the name emulators and flash carts give it). SAVE SONG files the song on screen under its project name (the
same name replaces it, a new name takes the next free slot; "BANK FULL" if all 32 slots or the space are used). LOAD SONG lists that console's bank; loose
`.lsdsng` files in `GB/Songs` follow with a `+` and are filed into the bank by the next save; Shift + Delete then Insert erases a bank song.
EXPORT LSDJ saves the chosen song(s) into the bank(s) and copies each bank to `GB/Export/LSDJ_A.srm` / `LSDJ_B.srm` (plus the song as a single `.lsdsng`):
rename the copy to your LSDJ ROM's name (`LSDJ.srm`) for the Game Boy, emulator or flash cart.
Sample kits are not in a song, they are banks of the LSDJ ROM: a song that uses a kit your cartridge's ROM lacks (GUESSWHO uses kits 16 and 1D) plays noise on a real Game Boy.
When kit files from `GB/Kits` are loaded, EXPORT LSDJ also writes `GB/Export/LSDJ_KITS.gb`: the ROM from the card with those kits put into their slots (kit n = bank 8 + n); use that ROM on the cartridge or emulator.
The song list shows ` -TITLE` for a song in the bank and ` +TITLE` for a loose file.
SYNC: with both projects on, Play on either console starts both; the slave follows the master's tempo (the master's tempo edit also goes into the slave's project).
Master key (while linked) shows both consoles side by side. An A sits above F1 and a B above F8; hold either to raise a dock that names every F key. F1 / F8 pick
the console the keys control, F2 / F7 held + jog = level A / B, F3-F6 mute (Shift: solo) the four tracks of the active console.
Recording from the pads is placed by what you hear: the sequencer's lead over the speakers (the audio queue) is taken off the step position.
Instrument library (Sample loader key): GB/Instruments/*.gbi. The first row saves the current instrument (name keyboard), the others load into it (its table too).
At power-on the PTSDJ block letters drop onto the display and the Game Boy chime plays (a recording of `assets/boot_chime.wav`, built into the firmware with
`tools/make_chime.py`).
Help pages (Help key, F8 pop-ups, the cheat sheets) are left aligned: the name of each key or function is bold, with a blank line between entries; the text is
in `src/gb/ui/lsdj.cpp` as sections of name/text entries that paginate by themselves.

```
make host-test                    # unit tests
make sim; build/sim/gb-sim        # simulator (keys: arrows, Insert, Delete, Shift, Space)
make -C sim check                 # screen tour (4 DMG shades only) + demo song to WAV
make CROSS=<path>/arm-none-eabi-  # build/PolyendTracGBTrkv010.ptf (24 chars, checked)
```
Two consoles: the Song key switches the screen between tracks 1-4 (A) and 5-8 (B); Play starts and stops both together, each keeping its own
tempo and L/R panning. Project screen: SAVE SONG (into `GB/Saves/A.srm` or `B.srm`), LOAD SONG, EXPORT WAV (`GB/Export/<name>.wav`, one pass until every channel loops).
Songs: `GB/Saves/A.srm` and `B.srm` (the banks); loose `GB/Songs/*.lsdsng` can be loaded too (`GB/AUTOLOAD.LSDSNG` and `GB/AUTOLOAD_B.LSDSNG` open at start). `demos/` holds sample songs and kits.
Docs: `LEARNINGS.md`, `docs/LSDJ_FORMAT.md`, `docs/HARDWARE_MAP.md`, `UPSTREAM.md`. `tools/oracle/` checks against an LSDJ ROM.

Extra sample kits: put 16 KB LSDJ `.kit` files in `GB/Kits` on the card. The first two characters of the name (hex, 00-3F) are the kit slot the file replaces, e.g. `0A_808.kit` replaces kit 0A (the number a song's kit instrument shows); up to 16 files, read at boot. The Project readout ends with `K<n>`, the number of kit files loaded.
