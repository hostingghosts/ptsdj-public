# Provenance

`polyend-gb` started on 2026-11-04 as a copy of `atari-fw` (the Atari-mode firmware project next to it, itself a fork of
SIDTracker 0.2 by Marcin Spoczynski, MIT, which vendors the OpenTracker hardware layer, MIT). The first commit of this
repository is that copy unchanged (without `build/` and the `atari_core` symlink, which pointed outside the tree).
`git show HEAD~1:...` therefore still has the Atari sequencer, UI screens and tests if anything is wanted back.

The second commit strips the Atari parts. What was removed: the `src/atw` song model, sequencer, audio engine, exporters,
project and Mac-session files, undo, the seven Atari screens, the old `Ui` shell (instrument picker, pad keyboard, pop-ups
all depended on the song), `atari_core`, the 6502 players and the Atari tests, plus `tools/release` and `tools/sidlink`
(SID-product tooling). What was kept: `src/platform` and `third_party` unchanged (one macro renamed, see below),
the `Screen` interface and colour/drawing helpers (`src/atw/ui/screen.h`, `frame.cpp`), the simulator, the host-test
harness, `tools/check_image.py`, `tools/sync_hal.sh`, and `docs/upstream/`.

Changes inside the kept hardware layer:
* `src/platform/core/ui/fonts/fonts.h`: the build macro `ATARI_BUILD` is now `GB_BUILD` (same effect: the 126 KB custom font
  blob is left out of the image).
* The Makefile no longer defines `SIDT_USB_STRINGS`, so the USB manufacturer/product strings read "OpenTracker" (the default
  branch already in `third_party/teensy3/usb_desc.h`). VID:PID is still the pid.codes test pair 1209:0001.
