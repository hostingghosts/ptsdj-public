# Attribution

This project is built on MIT-licensed work. The MIT notices must stay with the copied code.

| Work | Author | Licence | Where it is here |
|---|---|---|---|
| SIDTracker 0.2 (https://github.com/sandlbn/SidTracker) | Marcin Spoczynski | MIT, `LICENSE.SIDTracker` | the repository layout, `sim/`, `tests/` harness, `tools/check_image.py`, the UI shell idea in `src/atw/ui` |
| OpenTracker (clean-room firmware for the same hardware) | see `src/platform/LICENSE.OpenTracker` | MIT | `src/platform/**`, the host HALs in `sim/src` (everything except `main.cpp`), `src/app/device_main.cpp` boot order |
| PJRC Teensy core | PJRC | `third_party/teensy3/LICENSE.txt` | `third_party/teensy3` |
| FatFs | ChaN | `third_party/fatfs/LICENSE.txt` | `third_party/fatfs` |
| Roboto, DejaVu, stb_image | their authors | licences next to the files | `third_party/fonts`, `sim/third_party` |

Not used, by decision: any code from Polyend's own Tracker CE source, and any code from RASTER Music Tracker (GPL).
The proprietary Polyend libraries (`libPolySynth.a`, `libPolyDSP.a`) are not part of this tree.
