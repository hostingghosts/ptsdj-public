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
Play on the Chain, Phrase and Table pages plays just that chain / phrase / table on the cursor track and loops (the Table page plays a note on the instrument that uses it, or a plain pulse); Play again stops; Shift + Play plays the song. The Song page's Play is unchanged.
NEW SONG (first entry of the Load Song list, and the NEW SONG row below LOAD SONG on the Project page): asks for a name, then replaces that console's song with the empty template. The Project page's technical line (callback cost, late blocks, ROM size, kits) only shows while Shift is held, with FIRMWARE over F8.
Project: NAME, TEMPO, TRANSPOSE, SYNC (OFF / MASTER / SLAVE), CLONE, LOOK (display colours), PRELISTEN, HELP, CLEAN SONG DATA / CLEAN INSTR DATA (Insert twice),
SAVE SONG, LOAD SONG, EXPORT WAV, EXPORT LSDJ (A, B or A + B; pick with the jog wheel). Saving and exporting show a progress bar.
Songs live in one bank file per Game Boy, like a real LSDJ cartridge: `GB/Saves/A.srm` (song A) and `GB/Saves/B.srm` (song B), 128 KB each with up to 32 songs
(same layout as a cartridge `.sav`; `.srm` is just the name emulators and flash carts give it). SAVE SONG files the song on screen under its project name (the
same name replaces it, a new name takes the next free slot; "BANK FULL" if all 32 slots or the space are used). LOAD SONG lists that console's bank; loose
`.lsdsng` files in `GB/Songs` follow with a `+` and are filed into the bank by the next save; Shift + Delete then Insert erases a bank song.
EXPORT LSDJ saves the chosen song(s) into the bank(s) and copies each bank to `GB/Export/LSDJ_A.srm` / `LSDJ_B.srm` (plus the song as a single `.lsdsng`, and the ROMs with your kits, see below):
rename the copy to your LSDJ ROM's name (`LSDJ.srm`) for the Game Boy, emulator or flash cart.
Sample kits are not in a song, they are banks of the LSDJ ROM: a song that uses a kit your cartridge's ROM lacks (GUESSWHO uses kits 16 and 1D) plays noise on a real Game Boy.
When you have kits (made on the Tracker or `.kit` files in `GB/Kits`), EXPORT LSDJ also writes `GB/Export/PTSDJ_A.gb` and `PTSDJ_B.gb`: the ROM from the card with each console's kits put into their slots (kit n = bank 8 + n); use these ROMs on the cartridges or emulators. Only the kit banks change, plus the two global checksum bytes of the cartridge header (0x14E-0x14F), which are worked out again so the cartridge test passes (a real LSDJ ROM has them right; checked against the 9.4.2 ROM: only bank 0's two bytes and the kit banks differ).
Only empty or kit banks are replaced: the stock ROM keeps program code where kits 13 to 17 would go, so use slots 1A to 37 for your own kits (name the file `1A_NAME.kit` and set the instrument's kit to 1A). A kit left alone shows as `KIT nn: USE 1A-37` after the export.
The song list shows ` -TITLE` for a song in the bank and ` +TITLE` for a loose file.
SYNC: with both projects on, Play on either console starts both; the slave follows the master's tempo (the master's tempo edit also goes into the slave's project).
Output level: the mix is 1.6 times the chips' own level with a soft knee above 60 % of full scale (`Dual::finish`), so quiet songs are louder and loud ones are rounded off, not clipped. The headphone amplifier setting is unchanged.
Noise channel cost: a killed hi-hat leaves the noise channel enabled at volume 0 with its shift register still stepping up to 500,000 times a second; the emulator now skips those steps while the channel is silent and catches the register up exactly by a jump when a write or trigger needs it (`Apu::setLazyNoise`, checked against step-by-step over 6000 random operations).
Master key (while linked) shows both consoles side by side. An A sits above F1 and a B above F8; hold either to raise a dock that names every F key. F1 / F8 pick
the console the keys control, F2 / F7 held + jog = level A / B, F3-F6 mute (Shift: solo) the four tracks of the active console.
Recording from the pads is placed by what you hear: the sequencer's lead over the speakers (the audio queue) is taken off the step position.
Instrument library (`GB/Instruments/*.gbi`): Shift + the Instr key; Play previews the highlighted sound without touching the song (it sits in a temporary memory, a table borrows an empty slot and gives it back; the pads play it too), Insert puts it into the song; hidden files (`._...` from a Mac) and files that are too small to be instruments are not listed; up to 400 files, Left / Right turn a page of 13. `presets/instruments/` holds 272 pulse and noise presets made from the demo songs by `tools/make_presets.cpp` (named for what they sound like, see the README in `build/presets/PTSDJ_instrument_presets.zip`); copy them to `GB/Instruments` on the card. The first row saves the current instrument
(name keyboard), the others load into it (its table too).
Sample kits (per console): drum kits are banks of the LSDJ ROM, up to 15 samples and about 2.8 s of 4-bit sound at 11.46 kHz. Put WAV files (PCM 8/16/24 bit, any rate) in `GB/Samples`.
Sample Loader key: the kit number is at the top (jog wheel); pick a file, Insert converts it (resampled, normalised, noise-shaped to 4 bits) and adds it to the kit at once; a check mark shows files already in it (remembered in `<slot>_<name>.src` beside the kit file); Delete takes that file's sample out again; Shift + Insert opens it in the Sample Editor to cut first; Play hears it. Sample Editor key: START and END trim it (the jog wheel or Insert + Left/Right;
Insert + Up/Down moves 16 times as far), NAME (3 letters), KIT (1A to 37, the slots that are empty in a stock ROM), ADD TO KIT saves the kit at once. The SAMPLES row steps through the kit's samples with the jog wheel: cut or rename one and REPLACE SAMPLE (after a yes/no question: it can't be undone) writes it over that sample. Sample Playback key: the
kit page, kit number at the top (Left/Right or jog; ROM kits can be heard, not changed), kit NAME, the 15 samples (Insert opens a sample of any kit, ROM kits too, in the Sample Editor to cut and add to a kit of
your own; Shift + Insert renames one of yours, Delete then Insert erases it, Play or a pad hears one).
Kits are saved as `GB/Kits/A/<slot>_<name>.kit` and `GB/Kits/B/...` (LSDJ kit files); `.kit` files in `GB/Kits` itself serve both consoles. A KIT instrument's KIT 1 / KIT 2 on the Instr page
pick the kits; note n plays sample n of KIT 1 (note 16 * m + n adds sample m of KIT 2). EXPORT LSDJ writes `GB/Export/PTSDJ_A.gb` and `PTSDJ_B.gb`: the card's ROM with that console's kits
put into their slots (never over program code: kits 13 to 17 stay).
Firmware loader: on the Project page hold Shift, the ? over F8 turns into FIRMWARE; press F8 for a window listing `Firmware/*.ptf` (what the bootloader accepts), Insert twice flashes it,
Delete goes back. The `Firmware/ot_restore.txt` route still works.
Sample Recorder (line / mic input) is not built yet.
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
Songs: `GB/Saves/A.srm` and `B.srm` (the banks); loose `GB/Songs/*.lsdsng` can be loaded too. At power-on each console opens the song it saved last (the bank's active slot), or an empty one if it never saved. `demos/` holds sample songs and kits.
Docs: `LEARNINGS.md`, `docs/LSDJ_FORMAT.md`, `docs/HARDWARE_MAP.md`, `UPSTREAM.md`. `tools/oracle/` checks against an LSDJ ROM.

Extra sample kits: put 16 KB LSDJ `.kit` files in `GB/Kits` on the card. The first two characters of the name (hex, 00-3F) are the kit slot the file replaces, e.g. `0A_808.kit` replaces kit 0A (the number a song's kit instrument shows); up to 16 files, read at boot. The Project readout ends with `K<n>`, the number of kit files loaded.
