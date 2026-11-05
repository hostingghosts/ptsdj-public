# Installing the Atari tracker firmware, and the way back to stock

**Status:** nothing here has been run on a real Tracker yet. The image passes `tools/check_image.py` (bootloader checks), the host tests and the
simulator, which is as far as a PC can take it. The first flash is the first hardware test: do the preparation below *before* it.

## What the image is
`build/PolyendTracAtariv010.ptf` (build: `scripts/afw.sh device`). The name is exactly 24 characters and starts with `PolyendTrac`, which is what the
stock bootloader accepts. It runs on the original Tracker (MK66) only.

## Before the first flash (preparation)
1. Put the **stock** image on the SD card in `/Firmware/` and leave it there (for CE 1.0.0 that is `PolyendTracker_CE_1.0.0.ptf`, the file you flashed from).
2. Copy `PolyendTracAtariv010.ptf` into `/Firmware/` next to it.
3. Write a text file `/Firmware/ot_restore.txt` **only when you want to flash** (see below). Do not leave one lying around.
4. Back up the card (your Polyend projects are ordinary files; the Atari tracker never touches them, it only uses `/AtariTracker/`).

## Flash the Atari tracker
* Either: stock **Config > Firmware update**, choose `PolyendTracAtariv010.ptf`.
* Or, without using the screen at all: create `/Firmware/ot_restore.txt` whose first line is `PolyendTracAtariv010.ptf`, and power on.
  The Tracker renames the file to `ot_restore.done` before flashing, so a failed flash cannot loop.

## Way back to stock (any of these works)
1. **Atari tracker running:** Config > **F7 FIRMWARE** lists the images in `/Firmware`, pick the stock one, **F8 INSTALL**, confirm (the same hand-off the stock updater uses; an unsaved song is autosaved first).
2. **Atari tracker does not start, or the screen stays dark:** put `PolyendTracker_CE_1.0.0.ptf` (the stock file name) on the first line of
   `/Firmware/ot_restore.txt`, power on. This check runs first at boot, before the display and the audio drivers, and needs only the SD card.
3. **Rear reset button:** installs a `.ptf` from `/Firmware/`; with the stock file as the only one there it is the stock firmware.

## What stays on the card
* `/AtariTracker/Projects/*.atr`, `autosave.atr`, `Export/`, `Mac/`: only the Atari tracker reads or writes them. Stock firmware ignores them.
* Stock projects and samples are never opened by the Atari tracker, and it never writes outside `/AtariTracker/` and `/Firmware/`.

## First hardware session (suggested order)
1. Boot, look at the screens, press pads (sound from line out and headphones).
2. Play a pattern 10 minutes; watch for dropouts.
3. Save a project, power-cycle, reopen it (autosave restores an unsaved song).
4. Export a `.a78` and a `.bin`, run them in an emulator / on the 2600; export a WAV.
5. Try the way back (option 1, then option 2) and confirm stock boots.
