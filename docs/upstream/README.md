# Hardware notes

Notes on the Polyend Tracker's hardware, inherited from SIDTracker and OpenTracker, which share this firmware's hardware layer (`src/platform`): `hardware.md` (MCU, clocks, memory map, DMA), `audio.md`, `storage.md` (the SD card is FAT32 only), `display.md` (the display-list budget), `input.md` and `firmware-map.md` (image layout, update and recovery).

They were written for SIDTracker, so a few lines mention its planning notes (`APPROACH`, `PLAN`, `DECISIONS`) or SID-specific figures. Those notes are not part of this repository; the hardware facts do not depend on them. `docs/HARDWARE_MAP.md` is the one-page summary and `LEARNINGS.md` the short list of rules taken from these notes.
