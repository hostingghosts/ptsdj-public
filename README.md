# PTSDJ - for the original Polyend Tracker 

LSDJ-style Game Boy tracker for the original Polyend Tracker (MK66). Personal use only (see `LICENSE`, `NOTICE.md`).
PTSDJ turns the original Polyend Tracker (MK66) into a Game Boy music maker in the style of LSDJ (Little Sound DJ). It runs on the Tracker’s own screen, keys, pads and jog wheel, and is built from scratch: it contains no LSDJ code.

What it does

Two linked Game Boys. Each has its own song, tempo and drum kits, giving 8 tracks: song A on tracks 1 to 4 and song B on tracks 5 to 8, summed in stereo. SYNC makes one console the master and the other follows its tempo, and the Master key shows both side by side.
The LSDJ workflow. Song, Chain, Phrase, Instrument, Table, Groove, Synth, Wave and Project pages, the same commands, and a player close to LSDJ 9.4.2. The Tracker’s keys and pads are mapped onto the Game Boy buttons, and the pads play notes or record live. Every page has a built-in help pop-up (hold F8).
Sound. Pulse, wave, kit and noise instruments. Kits can be made from your own WAV files, a line or mic recording, or layered samples, and a built-in drum and bass demo pair ships with two Amen kits.
A mix you control. Master volume (hold Master and turn the jog wheel, with a meter on the battery light) and the Master Mix page (Shift + Master): an output limiter with drive, ceiling and release, then the master volume. These also apply to WAV export.
Files and exports. Songs live in cartridge-style .srm bank files and load or save .lsdsng. Export writes WAV audio, and your songs plus a ROM that carries your drum kits for a real Game Boy, flash cart or emulator.

You need:
-an original Polyend Tracker (MK66);
-your own copy of the LSDJ ROM as GB/LSDJ.GB on the SD card, for the kits, the Synth page and the ROM export. It is not included.
Installing and leaving. Copy the .ptf into Firmware/ on the SD card and update from the Tracker’s firmware menu. To go back, reflash a stock Polyend .ptf. Your Tracker projects are never touched.

Status and limits. The core firmware is checked on hardware and its exports run on a real Game Boy. The master volume, limiter and Master Mix page (1.0.3) are tested on the desktop but not yet on the Tracker. A few effects differ slightly from LSDJ, and the Game Boy is the final word on how a song sounds.

Licence and credits. Personal, non-commercial use only. It builds on the open-source Polyend Tracker firmware, which allows non-commercial use. LSDJ is by Johan Kotlinski. This project is not made by or connected to Polyend or the makers of LSDJ.
