# Upstream and provenance

This directory is the firmware for the Atari-mode Polyend Tracker. It starts from **SIDTracker 0.2** by Marcin Spoczynski
(https://github.com/sandlbn/SidTracker, MIT licence, see `LICENSE`), copied on 2026-11-03, which in turn vendors the hardware layer of
**OpenTracker**, a clean-room firmware for the same hardware (MIT, `src/platform/LICENSE.OpenTracker`, details in
`src/platform/PROVENANCE.md`). Third-party code keeps its own licence: PJRC Teensy core (`third_party/teensy3/LICENSE.txt`),
FatFs (`third_party/fatfs/LICENSE.txt`), Roboto fonts (`third_party/fonts`).

## What was left out of the copy
* `fixtures/private/` (third-party GoatTracker songs of unknown licence) and `examples/` (SID songs), the 9 MB of screenshots in `docs/`.
* The upstream manual and design notes are kept as text in `docs/upstream/`; they describe the SID product, not this one.
* `README.md` of upstream is `README.upstream.md`.

## Local changes (newest first)
4. `src/atw/app.*`, `src/atw/ui/*` (new): the Atari app and UI; `app/device_main.cpp` and `sim/src/main.cpp` run it when `ATARI_BUILD` is defined (alias `appns`); `sim/scripts/atari_smoke.txt`.
3. `src/atw/` (new): the Atari song model, sequencer and audio engine, with `tests/atari_song_test.cpp`; `tests/Makefile` compiles the core sources it needs.
2. `src/app/engine.*`, `Makefile`, `sim/Makefile`: `ATARI_BUILD` makes the POKEY engine of `core/src` (reached through the symlink `atari_core/atari`) the audio engine of the device image and the simulator; pads play POKEY voices 0-3. The SID code is still compiled and unused; host tests build without `ATARI_BUILD`.
1. `sim/Makefile`: `src/c64` added to the portable sources (the 0.2 snapshot did not link without it).
