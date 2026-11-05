# GB tracker desktop simulator

The portable app (`src/atw`) runs unchanged against host implementations of the HAL
interfaces. The host HALs in `sim/src` (all files except `main.cpp`) are
vendored from OpenTracker (`src/platform/PROVENANCE.md`); `sim/src/main.cpp`
is ours.

```
make -C sim                       # build/sim/gb-sim
build/sim/gb-sim          # window; SD card = ~/.gbtracker-sd
make -C sim check                 # headless smoke run -> build/sim/smoke-*.png, smoke.wav
```

Options: `--sd DIR`, `--scale 2`, `--no-audio`, `--quit-after MS`; headless:
`--script FILE`, `--screenshot OUT.png`, `--panel`, `--wav OUT.wav`.

Differences from the device: audio runs at 44,100 Hz (device 44,117.6 Hz;
pitch must be computed for the real rate); there is
no underrun counter and no firmware update; the FT81x display-list size
limit is not modelled.

## Keys

Tab or Caps Lock switches between COMMAND and PAD mode.

| Key | Control |
|---|---|
| F1–F8 | screen buttons |
| F9 / F10 / F12 | Pattern / Song / Help |
| arrows, Shift, Space (Play), Insert, Delete, Enter (jog push) | as labelled |
| Page Up / Page Down, mouse wheel, `[` `]` | jog wheel |
| COMMAND mode: R Rec, C Copy, N / I / X / V Note / Instr / Fx1 / Fx2 | |
| COMMAND mode: keys 1..0 = module keys 0x08..0x11 | module keys |
| PAD mode: `1`…`=`, `Q`…`]`, `A`…`\`, `Z`…`/` | pad rows 0–3 (bottom row = octave) |
| Ctrl+Q | quit |

The mouse works on every panel control; right-click latches a button.

## Scripts

`wait MS`, `press` / `release` / `tap NAME [MS]` / `hold NAME [MS]`,
`pad N down|up|tap [VEL]`, `enc DELTA`, `screenshot FILE`, `panelshot FILE`,
`echo TEXT`. Button names are the panel names (`PLAY`, `REC`, `F1`,
`INSTRPARAMS`, `PERFORMANCE`, `SONG`, `CONFIG`, `FILE`, `MASTER`, `PATTERN`,
…) or numeric ids. Time is virtual: one app loop per simulated millisecond,
one audio block every 128 / 44,100 s, so runs are deterministic.
Example: `scripts/smoke.txt`.
