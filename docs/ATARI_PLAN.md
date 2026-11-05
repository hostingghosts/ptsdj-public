# Atari fork: plan

Goal (user, 2026-11-03): the Tracker is a **full composing instrument** for the Atari POKEY (4 voices) and TIA (2 voices), compatible with the
Mac Atari app (sessions, `.rti`), for the user's personal use. It exports WAV, assembly for a developer, playable 7800 `.a78` and 2600 `.bin`,
and Mac-app sessions. Everything the Tracker does is first built and tested on the PC (simulator, host tests, QEMU cycle counts); the Tracker
only receives milestones that already pass.

## What is reused unchanged
* `../core/src`: the chip emulation (`pokey`, `tia`, `fastpokey`), the players (`voice`, `bank`), instruments and `.rti` (`instrument`), the RMT
  and starter presets (`presets`, `rmtpresets.h`), the exporters (`romdata`, `macsession`, assembly) and their tests. Reached through the symlink
  `atari_core/atari`; the single source of truth stays in `core/`.
* From SIDTracker: the hardware layer (display, input, audio, SD, MIDI, restore and firmware update), the UI framework and screens' layout,
  the simulator and the test setup.

## Mapping SIDTracker to Atari
| SIDTracker | Atari |
|---|---|
| 6 tracks, 2 SID chips | POKEY: 4 tracks. TIA: 2 tracks |
| GoatTracker instruments (AD/SR, wave / pulse / filter tables) | RMT-style instruments (`PokeyInstrument`: envelope, table, AUDCTL, vibrato, fade-out), 48 per chip; TIA instruments (`TiaInstrument`) |
| Pattern cell: note, instrument, command + parameter | Same cell; commands V U D A Y S B C X (`core/src/song.h` FxType) |
| Order list with transpose and repeat | Playlist (song mode) |
| Perform page (live pulse width / filter) | Live AUDCTL bits, track volumes, mutes (to be designed) |
| PCM drum kit, effects (delay, reverb) | Dropped: no sampler (the chips are the instrument) |
| `.sid` export, GoatTracker import/export | WAV, assembly, `.a78`, `.bin`, Mac session, `.rti` import/save |

## Phases (each ends with: host tests, simulator script, device image check)
1. **Engine** (done): `ATARI_BUILD`; the POKEY engine is the audio engine; pads play voices 0-3.
2. **Sequencer and song model**: 4 tracks, Atari cells and instruments, order list; the row player drives `VoiceBank` per video frame
   (NTSC 60 / PAL 50); the SID player, chips and drum/effects code come out. Host tests move from SID to Atari.
3. **Screens**: pattern (4 tracks, TIA 2), instrument pages (GENERAL, ENVELOPE, TABLE, AUDCTL, SOUNDS), hold-Instr picker with names, song,
   master (mixer, meters), perform, settings (chip, region), help.
4. **Files and exports**: project save/load (own binary format), `.rti` import/save, Mac session in and out, WAV, assembly, `.a78`, `.bin`.
5. **Hardware sessions** (the user): selftest and the way back first (`/Firmware/ot_restore.txt`), then sound, then each milestone.

## Status
* Phase 1 done (engine). **Phase 2 done: `src/atw/`** is the Atari song core, built next to the SID code so the app keeps working until the
  screens are converted: `atsong.*` (song model: 4 tracks, cells of note / instrument / volume / two effects, 48 instruments per chip, order list
  with repeats, effect letters, conversions to and from the core song), `sequencer.*` (row sequencer, one video frame at a time), `audio.*`
  (audio engine: POKEY / TIA engine, sequencer, command ring, live pads, hold / rebuild). `tests/atari_song_test.cpp`: 62 core songs convert both
  ways and the sequencer plays 8 million samples identical to the core reference sequencer; repeats, restart, break, tempo, pattern mode and TIA's
  two tracks are checked.
* **Phase 3a done: the Atari app runs.** `src/atw/app.*` (app shell) and `src/atw/ui/*` (UI core with the hold-Instr picker, status and dialogs; Pattern,
  Instrument, Song and Settings screens) are selected by `ATARI_BUILD` in the simulator and the device main (the SID app stays compiled for the host
  tests until it is removed). Pattern screen: 4 tracks (TIA 2), cells `C-4 01 F V0C s04` (note, instrument, volume, two effects), effect letters, pads as
  instruments / hex / effect list, mute page, copy / paste, follow playback. Hold Instr: all 48 slots by name (5 starter sounds, 35 RMT instruments, empty
  slots). Instrument screen: a first view only. Song screen: order list with repeats and restart. Settings: chip, region, master volume.
  Device image: 271 KB flash, 72 KB static RAM. `sim/scripts/atari_smoke.txt` drives the simulator and takes screenshots into `build/sim/atari-*.png`.
* **Phase 3b done: the instrument pages.** `src/atw/ui/instrument_screen.*` on top of `atari::InstrumentEditor` (now `bind()`s to the song's instruments): POKEY
  GENERAL, ENVELOPE (bar graph, insert/delete step), TABLE, AUDCTL, SOUNDS (5 starter + 35 RMT + `.rti` files on the card, jog tries a sound, KEEP/CANCEL);
  TIA GENERAL, STEPS, SOUNDS. Naming on the pad keyboard (Ui::keyboard), copy / paste / init (with confirm), `.rti` save to `AtariTracker/Instruments/`.
  `tests/atari_ui_test.cpp` drives it all over fake HALs and checks every screen stays under the FT81x display-list budget (1900 words). `.rti` import
  from the card is done through the SOUNDS page. 15 host suites pass; device image checks ok (291 KB flash, 82 KB static RAM).
* **Phase 4a done: project files and autosave.** `src/atw/project.*`: one binary `.atr` per project in `AtariTracker/Projects/` (header, order list, only the
  non-empty patterns with packed cells, the instruments as the core `ATRI` block, CRC-32). Loading checks the CRC and every value before the song is touched
  (a damaged file changes nothing); saving writes `.tmp` then renames, so a full card or a power cut keeps the old file. Config screen: NEW, OPEN (list), SAVE,
  SAVE AS (pad keyboard, asks before replacing). Autosave to `AtariTracker/autosave.atr` three seconds after the last edit and never while playing; the
  next power-on restores it, and saving a project removes it. Tests in `atari_ui_test.cpp` (round trip of 12 random songs, byte flips, truncation, wrong
  values with a valid CRC, full card, UI flow, autosave / restore). Image 305 KB flash. `core/src/insfile.*` now honours `InstrumentEditor::bind`.
* **Phase 4b done: undo / redo.** `src/atw/undo.*`: each input event is bracketed; what the current screen edits (pattern, instrument, or song settings +
  order list) is copied first and kept as one step if the event edited the song; edits of the same object within 400 ms are one step (a jog turn = one undo).
  Shift+Rec = undo, Shift+Sample Rec = redo; the screen jumps to what changed; 64 steps / 96 KB, oldest dropped; cleared by NEW / OPEN. 5 tests.
  Cursor, mute and playback state are not part of undo. Image 320 KB flash.
* **Phase 4c done: MIDI in.** `src/atw/midi_in.h` (running status, SysEx and real-time skipped) polled from `App::loop` for the DIN and USB ports, any channel.
  Note 24 = C-1, so 60 = C-4. Notes play on the cursor track with the current instrument (a held key keeps the track until released) and, with Rec on the
  Pattern screen, are written at the cursor like pads (undoable). CC 123 / 120 = all notes off. Not done: MIDI clock / start / stop, velocity, MIDI out,
  multi-track chords. 5 tests (parser, record, range, garbage).
* **Phase 4d done: Master and Perform screens.** Master key: master volume (jog; moved from Config into the UI state), a bar per track showing what the
  voices write to the chip (0-F, from `Engine::trackLevel`), and a held output peak. Performance key: Left/Right track, jog / Up / Down = volume 0-F of the note
  sounding on the track (exactly what a V effect does, so it exports), F1-F4 mute, F5-F8 solo (solo overrides mute); with Rec on while the song plays a change is
  written as a V effect at the playing row (first free effect slot). `Song::trackVolume` is still stored but unused: a per-track mix level cannot be played by
  the exported ROMs. 3 tests. Image 324 KB flash.
* **Phase 4e done: exports.** File key = Export screen: WAV, assembly (DASM song data + 6502 player + README), `.a78` (7800, POKEY at `$4000` or TIA) and
  2600 `.bin` (TIA only), into `AtariTracker/Export/` on the card, named after the project. `src/atw/export.*` plans the order list (repeats written out, the
  restart entry is the loop point, at most 254 plays) and feeds the core builders (`romdata`) pattern by pattern; the ROM song data is byte-identical to the
  core exporter's (tested for POKEY and TIA). WAV: a second engine plays the order list once at 44.1 kHz, stops where it would loop (`Sequencer::setStopAtEnd`)
  and renders a 0.5 s tail; it runs in small steps from the main loop (progress bar, CANCEL = F7, all other input ignored meanwhile) and is identical to what
  plays live up to the end of the song. 12 tests. Image 383 KB flash. **Not measured on the device:** how long a WAV takes (the engine uses 4-7 % of the M4
  in real time, so expect roughly 15x real time).
* **Phase 4f done: Mac sessions.** Config: F5 MAC SAVE writes `AtariTracker/Mac/<NAME>.atari.json` (core `writeMacSession`, POKEY only, nothing but a small
  buffer in memory); F6 MAC OPEN lists that folder and F8 loads it as a new unsaved project (`src/atw/macfile.*`). The loader streams the file in three rounds
  (sizes and tempo; a checking walk that stores nothing; the walk that fills the song), so a large session never has to fit in memory and a bad file leaves the song
  as it was. Member order in the file does not matter. Dropped on the way: instruments beyond the Tracker's 48 (an error if a note uses one), the Tracker's
  effects, volume-only rows, the loop point and the track mixer on save; a pattern is rounded up to whole 16-row bars. TIA sessions are refused both ways until the
  app's TIA layout is known. Tested against the core reader on the sample file from the app. Image 432 KB flash (json + session code, +49 KB).
* **Phase 4g done: SID code removed.** `src/` now holds only `atw/` (the Atari app), `app/device_main.cpp`, and `platform/` (OpenTracker HAL). The SID engine,
  C64 player, GoatTracker/.sid compatibility, old UI and their 13 test suites, the oracle/demo/blastgrid tools and the old Makefile targets are gone (git history keeps
  them). The shared screen framework (colours, `listRow`, glyph layers) moved to `src/atw/ui/screen.h` + `frame.cpp`. Image is now `build/PolyendTracAtariv010.ptf`
  (24 characters, version 0.1.0). Flash 428 KB: the SID code was never linked, so the image did not shrink. Host tests: 2 suites (song, UI), simulator smoke run passes.
* **Phase 4h done: Atari theme, firmware screen, rollback doc.** The palette is warm black with Atari orange (cursor, logo, tracks) and Atari blue
  (instruments, second logo word). Config F7 FIRMWARE lists the `.ptf` images in `/Firmware` that the bootloader accepts, F8 INSTALL asks first, autosaves an unsaved song, then
  hands off to the bootloader (the way back to stock from inside the app; it was missing after the SID screens were removed). `docs/INSTALL_AND_ROLLBACK.md`
  has the preparation, the flash, three ways back and a first hardware session. Tested with a stub for the hand-off; the real hand-off is untested on hardware.
* **Phase 5a done: first hardware feedback.** Fixed or added: buzz-bass tuning (poly counters run freely and the divider only samples them, so dividers
  whose period shares a factor with the poly length made the pitch jump: the tables now pick the nearest divider that does not), 16-bit instruments warn when
  played on the low track of their pair, WAV export is exactly one pass of the song with the ring-out mixed onto the loop start (a seamless loop), BPM in the
  Song screen (F3/F4, shown in the header), Shift+Insert = Home, metronome (on/off, volume) and a one-bar pre-roll for Rec+Play in Config (kept in
  `AtariTracker/settings.txt`), hold Note / Fx1 / Fx2 for a pad map like the instrument picker (a tap still jumps to the column; releasing Instr returns to the
  column you were in), hold Step / Len and turn the jog, recording follows the player head (nearest row) while playing, Shift+arrows select steps and tracks
  with Copy / Shift+Copy, Shift page UNDO / REDO, header text no longer overlaps, ADSR on the Envelope page (attack, decay, sustain, peak, tone, curve; release is the song's
  release), TIA shows four tracks with two greyed and Shift+F1..F4 chooses which two play (`Song::tiaTrack`, project version 3), TIA notes a waveform cannot play in
  tune are shown red on the pads and in the pattern, and power-off fades the sound out and ramps the codec down before muting (no pop).
* **Phase 5b done: stock-Tracker workflow and track-bound instruments.** Left / Right move between tracks (the field stays); Note / Instr / Fx1 / Fx2 pick the field
  (Instr again: volume, Fx again: next digit); Copy and Shift+Copy need Rec; Insert puts an empty step at the cursor or selection in every selected track;
  Shift+Delete is the stock backspace (the step before goes, the rest moves up); Shift+arrows select only with Rec on or while stopped. POKEY instruments that belong to
  particular tracks (16-bit join: track 2 / 4; 1.79 MHz on channel 1 / 3: track 1 / 3; auto filter: tracks 1-2; `instrumentTrackMask`) are not offered on others: skipped
  by pads, jog and INS -/+, red in the picker and pattern, refused with a message. 9 of the 48 starter / RMT instruments are bound.
* **Phase 6a done: second hardware round (not yet run on the Tracker).** Header laid out from the right with measured text widths (nothing can overlap; title is
  dropped first); the power button shows a SHUTTING DOWN bar for the stock 1.5 s hold, fades the sound out from 15 % and release cancels (stock `mtSleep` behaviour);
  boot splash "ATARI TRACKER" (device and sim main only); Rec + Play with pre-roll keeps the click going while recording; releasing a key during live record writes a
  note off at the player head (next row if released inside the row of the note); Shift+Insert (Home) held + Up selects the whole track, + Left / Right takes in the next;
  BPM changes live while playing (`Sequencer::setLiveSpeed`) and F8 on the Song screen is tap tempo (nearest whole speed); Export screen has whole-song and this-pattern
  variants of WAV / assembly / .a78 / .bin (`planPattern`, `Exporter::start(kind, title, onlyPattern)`; a pattern WAV loops on itself); pattern FX reworked in the C++
  reference and both 6502 players (verified identical on every frame): slide is now a fraction of the divider per frame with an 8.8 accumulator (POKEY value/256 of the
  divider, TIA value/16 units), vibrato FX is a slow 12-frame triangle proportional to the divider (separate state from the instrument's RMT vibrato). Embedded ROM blobs
  regenerated. Sim Makefile now tracks core headers (a stale object crashed the simulator after a struct change).
* **Phase 6b done: TIA optimised tuning (idea from TIATracker, GPL v2, no code taken).** Song setting `tiaTune` (Config > TIA TUNING, project version 4, session key `tiaTune`):
  0 concert pitch, 1 optimised = each waveform's scale shifted by up to +-50 cents (searched once per waveform, most notes within 15 cents) so waveforms 6, 7, 14
  and friends have far more in-tune notes. Only the note -> divider tables change (`kTiaDiv[region][tune][wave][note]`), so the 6502 players are untouched; the pad and
  pattern red flags follow the chosen tuning. Waveforms are shifted independently, so two different sounds on the two channels are no longer exactly in tune with each other.
* **Phase 6c done: three more TIATracker ideas (no code taken), all verified frame-for-frame against the C++ reference in both 6502 players.**
  Overlay percussion (TIA instrument "Percussion overlay": a one-shot hit that keeps the interrupted note and resumes it afterwards; saved: instrument, note,
  volume, step, count; arpeggio / slide are not kept), Pure combined (TIA instrument "Pure combined": waveforms 12 + 4 in one scale, per note whichever is closer,
  tables 16 / 17 of the header's waveform table, optimised tuning applies), Funk speed (Song screen F5 / F6: odd rows last `funk` frames; project version 5, session
  key `funk`, POKEY header byte +22, TIA header byte +3). Instrument flags live in bits 7 (overlay) and 6 (combined) of the loopStart byte. The 2600 wrapper's own
  variables moved to $E0-$E2 because the TIA player now uses $80-$CC.
* **Phase 6g done: game API and tunes.** Song screen F7 marks an order entry as the start of a tune (project version 6, `OrderEntry::tune`; core `Song::tunes`,
  session keys `tunes` / `startTune`). The exports carry a tune table (count, then start / end / loop per tune; header +23 POKEY, +20 TIA) and a start-tune byte; the
  players got `PkStartTune` / `TiaStartTune` (A = tune), `PkStop` / `TiaStop` and `PkMute` / `TiaMute` (the game takes channels for sound effects). Verified in py65 against
  the C++ reference (start tunes 0, 1, 2 frame for frame; the API calls in `check_api`). Earlier in this round: effects delay, retrigger, level slide, portamento; track
  volumes in exports; prefixed player symbols (`PK_` / `TI_`); the 2600 TIA player now needs 113 of the 128 RAM bytes (a slim build is the next robustness step).
* **Phase 6h done: slim TIA player.** `asm/tia_player.asm` has `;#IF NAME ... ;#ENDIF` regions (OVL overlay, DLY delay, RTG retrigger, VSL level slide, POR portamento;
  nested, several names = any). The assembly export scans the song (`tiaFeaturesNeeded`: the two TIA tracks' effects, and overlay instruments that are actually used) and
  writes `tia_player.asm` without the regions it does not need (`filterPlayerSource`); variables are allocated one after the other (`TZP2`), so the zero-page need falls from
  113 to as little as 63 bytes, and the README says how many. Every verification song is assembled with its own slimmed player and checked against the C++ reference. The
  Tracker's own .a78 / .bin ROMs use the full player (prebuilt blobs). `scripts/package-firmware.sh` now fails when the device build or image check fails.
* **Phase 6i done: instrument step tools** (RMT's envelope / table editing keys): on the ENVELOPE, TABLE and TIA STEPS pages F3 SET LOOP (the loop returns to the step being
  edited), F4 / F5 VOL - / VOL + (every step's volume, RMT "change all steps"), F6 CLEAR (the step back to empty), next to the existing INS STEP (duplicates) and DEL STEP.
  Still open from the RMT comparison: engine speed 1-4 per frame, a drawn volume envelope, sound-effect patterns (the game API only takes channels so far).
* **Phase 6j done: engine speed 1 / 2 / 4** (Config > ENGINE SPEED; project version 7; core `Song::engineSpeed`, session key `engineSpeed`). The player ticks that many times per
  video frame (RMT's "S"); speeds, funk, cut / delay / retrigger and instrument steps all count ticks, so the 6502 players are unchanged (they are simply called more often).
  Engine, WAV export and BPM follow; the event-driven engine is bit-identical to the tick-accurate chain at 1, 2 and 4 (`testEngineSpeed`). The assembly README tells a game to call
  the player N times per frame; the playable .a78 / .bin ROMs refuse N > 1 (their wrappers call it once a frame). Cost: a POKEY tick is up to ~9500 cycles, so 2x fits an NTSC 7800
  frame (29868 cycles) only without much else; 4x is for TIA (about 2500 cycles a tick).
* **Phase 6k done: drawn volume envelope.** On the ENVELOPE and TIA STEPS pages Shift + pads draw the volumes: 12 columns are 12 steps (the group that holds the edited step), the
  rows are 15 / 10 / 5 / 0, the pads' lights show the bars and the jog fine-tunes. Still open: sound-effect patterns for games (design: a pattern flagged SFX played one-shot on a
  channel the game names, reusing the voice machinery, with its own row counter; the game API only takes channels so far).
* **Phase 6l done: sound effects for games.** Pattern screen Shift + F7 marks the cursor track of the pattern as a sound effect (project version 8, `Song::sfx`, session key `sfx`; up to 16).
  The exports carry an effect table (header +26 POKEY, +23 TIA) and the players got `PkPlaySfx` / `TiaPlaySfx` (A = effect, X = channel): the effect's track plays once on that channel
  (its own row counter at the song's speed, tempo and break effects ignored), the music's events on the channel are read but not played meanwhile, the channel is released at the end.
  Verified in py65 against the C++ reference with game-style trigger lists (`sfx.txt`, songs sfx1 / sfx2 on both chips). The TIA player's effect code and 7 zero-page bytes are behind
  `TI_SFX` (the exporter writes `TI_SFX equ 1`; the standalone 2600 ROM has no RAM for it). The 7800 wrapper's song base moved to ROM + $1400 (the POKEY player grew past $1000).

* **Phase 6m done: first table-only timbre.** POKEY instrument setting BUZZ STYLE (GENERAL page): standard, or degenerate (family `Degen3`: poly4 sampled so that it collapses to a 5 step
  loop, same pitch layout as Buzzy). The 6502 players are unchanged: the family is baked into each step's data byte and the exporters write the extra tables only when used (families 5
  to 6, 36 tables). Checked on the emulated chip (the output really loops every 5 samples at the pitch the divider says, 64 kHz and 1.79 MHz; on 15 kHz it falls back to standard).
  Tried and dropped: the "every 5th clock" version (3 step pattern, constant for some start phases = silence). Not in `.rti`; the Mac app does not know it. Remaining RMT-variant ideas:
  sawtooth / triangle recipes and 16-bit auto-modulation (see the chat notes); no song-level driver version, by decision.

* **Phase 6n done: HSB auto-modulation** (POKEY instrument setting, -32..31, GENERAL page): our own version of what the RMT patch notes describe for the 16-bit "distortion guitar" -
  an automatic step added to the high byte of the frequency every frame (after the vibrato delay), so a joined voice sweeps and wraps. It shares the effect-delay gate with vibrato / frequency
  shift. Stored in the upper six bits of the instrument's vibrato byte (the player already masks it), so the 6502 player needed 12 lines and the data format did not change. Verified
  against the C++ player on every frame (random songs and a directed one) and by a unit test. Not RMT's exact recipe (the docs give none): judge it by ear. Sawtooth / triangle were examined
  and left out: they need three phase-locked channels as one voice.

* **Phase 6o done (hardware feedback round 4, UI only, no engine change):** TABLE page shortcuts (F4/F5 all values -/+, Shift F4 fill, Shift F5 ramp first->last,
  Shift F6 clear all); **table mode** = grid of the whole pitch table on the Sample recorder key (arrows move, jog edits, help text beside it; Instr key leaves; TIA: opens the
  steps page); switching the envelope to ADSR now reads attack/decay/sustain/peak/tone off the current steps instead of resetting them; Song page has a help panel and clearer
  TUNE messages; Shift on the pattern page shows what F7 SFX does. Loop click: Pokey::write(AUDF) does not reload the divider, so frequency writes are not the cause; waiting
  for an example instrument.

* **Phase 6p done: filter sweep option** (POKEY instrument setting "Filter sweep: Free beat / Out and back", GENERAL page). Diagnosis of the "click at the end of the loop"
  on the RMT filter instruments: channel 3/4 runs one divider step off channel 1/2 (RMT does this on purpose), so the filter pulse widens each cycle until it wraps from
  almost full to zero, one step per beat (a click on low notes). Out and back: the player writes STIMER (both counters restart together), widens for F frames, narrows for F-1,
  repeats (F = ((divider+1)/16)^2/8, 0 = high notes keep the free beat). Instrument flag: tabFlags bit 5 in the player data, bit 8 in the `.ins` file, session key `filterSweep`.
  Verified: 6502 player = C++ player on every frame and every STIMER write (75 songs incl. a directed `sweep` song), engine = tick chain, biggest level step 14.0 -> 2.2.
  The sweep is quieter (the pulse never gets wider than about half a cycle). Not the RMT sound, so it is opt-in.

* **Phase 6q done:** TIA table mode (grid: one column per step, rows waveform / divider / volume / pitch, 16 steps at a time) next to the POKEY pitch-table grid, both on the
  Sample recorder key. **Mac app update packet** (`scripts/make-mac-packet.sh` -> `dist/mac_update_packet.zip`): README (priorities P0-P4, acceptance test), ENGINE_SPEC, session
  format, tuning tables (JSON + Swift, six POKEY families, TIA concert/optimised/combined), 17 conformance vectors (session file + per-frame chip registers, self-checked by
  `atari_mac_vectors --check`), reference C++ and 6502 sources. The Swift file was generated but not compiled (no swiftc here).

* **v2 branch, cleanup 1 (flash 527 KB -> 340 KB, -36 %, no feature removed):** (a) the 126 KB custom font image is no longer built into the Atari firmware (the UI only
  uses the display's ROM fonts; `applyFontSet` was never called, so the image was uploaded to the display at every boot and never used; `OT_NO_CUSTOM_FONTS`, set for
  `ATARI_BUILD`, `OT_KEEP_CUSTOM_FONTS` brings it back); (b) the TIA "note out of tune" colouring reads bit tables (`kTiaInTune`, generated, tested against the
  floating-point definition) instead of calling `divcompute` (so `divcompute.cpp` and libm `pow`/`log2` are gone from the device); (c) JSON numbers are parsed by
  `parseJsonNumber` instead of `strtod` (tested against strtod on 20 000 numbers); (d) the two player sources embedded for the assembly export are LZ77-compressed
  (`core/src/lz.h`, encoder in `gen_romblobs.py`; 58 KB -> 21 KB; the export text is byte-identical, checked by `make verify-roms`). RAM unchanged (89 KB of 256 KB).

* (was next, done) phase 3b: the instrument pages (GENERAL, ENVELOPE, TABLE, AUDCTL, SOUNDS; TIA: GENERAL, STEPS, SOUNDS) with `.rti`, naming on the pad keyboard, copy /
  reset; then project files and autosave, undo, MIDI in, master and perform screens, exports, Mac session load / save, and removing the SID code.

## Safety (from SIDTracker, kept)
* Images are `PolyendTrac*.ptf`, exactly 24 characters (the stock bootloader ignores other names): the Atari image is
  `PolyendTracAtariv010.ptf`. `tools/check_image.py` runs on every build.
* Keep stock plus one test image in `/Firmware`; the rear reset button installs a random `.ptf` there.
* Way back: stock Config > Firmware update, or `/Firmware/ot_restore.txt` naming the stock image (read before any driver starts).

## Decisions (answered by the user, 2026-11-03)
1. Instrument page tabs: **GENERAL, ENVELOPE, TABLE, AUDCTL, SOUNDS**, in that order (TIA: GENERAL, STEPS, SOUNDS).
2. Effect letters: the first letter of the effect's name; an effect that cannot be used in a mode is left out of that mode; a letter already
   taken becomes lower case. With our effects:

   | Letter | Effect | POKEY | TIA |
   |---|---|---|---|
   | V | Volume | yes | yes |
   | S | Slide up | yes | yes |
   | s | Slide down | yes | yes |
   | A | Arpeggio | yes | yes |
   | v | Vibrato | yes | left out (POKEY only) |
   | T | Tempo (frames per row; was "Speed") | yes | yes |
   | B | Break pattern | yes | yes |
   | a | AUDCTL | yes | left out (POKEY only) |
   | C | Cut (note off after n frames) | yes | yes |

   New effects can be added later with the same rule (for example Delay = D, Retrigger = R).
3. Perform page: the most stable live controls: **track volume, mute and solo** (recordable as V). AUDCTL bits stay in the instruments and the `a`
   effect, because switching the clock or a 16-bit join live changes the pitch of other voices.
4. **The Tracker loads Mac sessions** (`.atari.json`) as well as writing them.
5. TIA mode: two tracks, the other two hidden.
* **Phase 6r: steps per beat, fill/ramp for TIA.** `Song::stepsPerBeat` (1, 2, 4, 8, 16, 32, 64; default 4; project file version 9, undo snapshot). Pattern screen Shift + F8:
  tap steps through the values (wraps), hold and turn sets one. Drives the row shading, the metronome / count-in clicks (accent on every fourth beat) and the BPM
  shown (BPM counts beats: `bpmOfSpeed(..., stepsPerBeat)`); exports and playback speed are unchanged. Table keys: Shift + F4 fill, F5 ramp (cursor step to last
  step, either direction), F6 clear all, now on the TIA grid / STEPS page too (row under the cursor; volume outside the grid).
* **Phase 6s: rows per beat (the Mac app's Steps/beat), live pattern change.** `Song::stepsPerBeat` 1..64 now changes the real timing: a beat lasts 4 * speed ticks however
  many rows it holds, rows are whole ticks and the remainder carries (`core/src/rowtime.h`, `rowTicks`), in the reference sequencer, the song walker, the firmware sequencer and
  both 6502 players (header flags bits 1-3 = log2; new RAM: TIA 1 byte, POKEY 4), verified frame by frame on all test songs (random songs use every value). Session key
  `stepsPerBeat`, Mac session written with the same steps/beat. Shift + F8 on the Pattern screen picks it. PAT -/+ (Pattern F1/F2, Perform F5/F6) while playing queues the pattern,
  starting at its top when the playing one has ended (song mode: played once, then the order list carries on); Perform: F1-F4 mute, with Shift solo, F7 cancels.
