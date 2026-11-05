# polyend-gb

Firmware for the original Polyend Tracker (MK66, Teensy 3.6 core) that is meant to become a Game Boy tracker in the style of
LSDJ and other Game Boy synths and trackers. **Personal use only, all rights reserved** (see `LICENSE`, `NOTICE.md`).

State: an empty shell. It boots on the proven hardware layer, plays silence and shows a blank first screen (a 1.6 s
"GB TRACKER" splash on the device only). No sequencer, sound engine or screens yet.

```
make host-test                    # host unit tests (ASan/UBSan)
make sim                          # desktop simulator: build/sim/gb-sim
make -C sim check                 # headless run: first screen must be all background pixels, audio silent
make CROSS=<path>/arm-none-eabi-  # device image: build/PolyendTracGBTrkv010.ptf (24 chars), checked by tools/check_image.py
make check                        # host tests + simulator check + device image
```

The device build needs `arm-none-eabi-gcc` 10.3 (Polyend's tool-chain version). Install `libsdl2-dev` and `pkg-config` for the simulator.
Copy the `.ptf` to `/Firmware` on a FAT32 card and read `LEARNINGS.md` and `docs/upstream/hardware-test-plan.md` §0 first.

Docs: `LEARNINGS.md` (rules), `docs/HARDWARE_MAP.md` (one page), `docs/upstream/` (notes from SIDTracker/OpenTracker), `UPSTREAM.md` (provenance).
