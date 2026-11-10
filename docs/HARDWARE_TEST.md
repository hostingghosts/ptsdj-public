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
5. Project: NAME the song (Insert on NAME), SAVE SONG, power off, LOAD SONG; EXPORT WAV (progress bar) and check `GB/Export/<name>.wav`; EXPORT LSDJ (turn the jog wheel to pick A, B or A+B) writes `.lsdsng` and `.sav`.
6. Pads: sound only, never the song; on the Instr page a pad plays that instrument (wave and kit instruments too); Rec on the Phrase page arms, jog picks the instrument, Rec + Play loops the phrase over the song and records.
7. SYNC: set A to MASTER and B to SLAVE (Project); Play starts both; changing A's tempo changes B's; SYNC OFF on both, then Play on one console: the other must be silent (no stuck note). Master key: side by side; F1/F8 pick, F2/F7 + jog level, F3-F6 mute, Shift + F3-F6 solo.
8. Edit helpers: Shift + Insert + arrows select, Copy, Shift + Copy paste; Help key shows the help pages; CLEAN SONG DATA / CLEAN INSTR DATA ask for a second Insert.
9. Instrument library (Sample loader): save an instrument, load it into another slot.
10. New in 0.3.0: Delete alone clears a cell (Song, Chain, Phrase, Table); select several with Shift + Insert + arrows, then Delete clears them or the jog wheel changes them all.
11. Instr page: arrow Up onto the number, Left/Right or the jog wheel scroll instruments, pads play the one shown. Name keyboard: Delete = NO, `<` erases; everything is inside the border.
12. Hold F8 on any page: a pop-up explains it, Left/Right turn its pages, release closes it. Hold Shift + Note (Phrase commands) and Shift + Instr params (Table commands): cheat sheets until you let go.
13. Rec + Play on the Phrase page: the note you play should land on the step you heard it on. If it is still early or late by a step, tell me which and by how much.
14. Master key (linked): an A over F1 and a B over F8, no yellow frame; hold either and the F key dock rises, release and it drops.
15. New in 0.4.0, saving: SAVE SONG on A, then on B; the card now has `GB/Saves/A.srm` and `B.srm` (128 KB each). Change the song, SAVE under a new NAME: LOAD SONG lists both. Same name replaces. Shift + Delete, then Insert, erases a listed song. Loose `.lsdsng` files in `GB/Songs` show with a `+`.
16. EXPORT LSDJ: `GB/Export/LSDJ_A.srm` / `LSDJ_B.srm` appear. Copy one next to your LSDJ ROM under the ROM's name (`LSDJ.srm`), start LSDJ: the file manager (Project > LOAD/SAVE) should list the songs; report anything odd (a missing song, garbled names, a song that will not load).
17. Power-on chime: a recording of the supplied WAV now. Tell me if it still sounds short or different.
18. Help key and F8 pop-ups: text is left aligned, names in bold, a blank line between entries; more pages than before.
19. New in 0.4.1: hold F8 on a page with a long explanation; a tab with `<` and `>` appears at the lower left, over F1 and F2. F1 turns back, F2 forward, Left/Right still work.
21. New in 0.4.2: the song list is ` -TITLE` (a bold LOAD SONG title, a blank row under the hint). With kit files in `GB/Kits`, EXPORT LSDJ ends with `EXPORT DONE + ROM` and writes `GB/Export/LSDJ_KITS.gb` (1 MB). Run that ROM with `LSDJ_A.srm` renamed to match: GUESSWHO's hi-hat should sound like it does on the Tracker. Report if the ROM does not start. Kit slots 13-17 are program code in the stock ROM and are never replaced (status `KIT nn: USE 1A-37`): move such a kit to slot 1A-37.
22. New in 0.5.0, kits: put two or three WAV files in `GB/Samples`. Sample Loader: pick one, Insert (the Sample Editor opens), turn the jog wheel on END to trim, ADD TO KIT. Sample Playback: the kit lists it; Play or a pad hears it. Make a KIT instrument on the Instr page (type KIT, KIT 1 = 1A) and play note 1 from a phrase. Report how the samples sound against the WAV (too noisy? too quiet? clipped?).
23. EXPORT LSDJ now also writes `GB/Export/PTSDJ_A.gb` / `PTSDJ_B.gb`; run one with the matching `.srm` renamed; the kit should sound the same as on the Tracker.
24. Instrument library: Shift + the Instr key (it is not a row on the Instr page). Play on the kit page and the Sample Loader now sounds the whole sample, not one blip. On the kit page Insert on a sample of a ROM kit opens it in the Sample Editor.
25. Firmware loader: Config key (Project page), hold Shift: ? over F8 turns into FIRMWARE; press F8: the window lists the images in /Firmware. Insert twice flashes: try it with the same image first. Delete backs out.
20. Back to stock: Config > Firmware update, or the `ot_restore.txt` route, or the Polyend tool.
