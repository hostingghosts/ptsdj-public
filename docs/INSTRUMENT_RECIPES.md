# Instrument recipes for the first listening test

Two instruments that use the newest POKEY settings. They are starting points, not tuned sounds: the values come from how the
settings work (checked against the emulated chip and the 6502 players), not from listening. Change them by ear.

## Where things are

* **Instr. Params** opens the instrument editor; press it again (or Left / Right) for the next page:
  POKEY: GENERAL, ENVELOPE, TABLE, AUDCTL, SOUNDS. TIA: GENERAL, STEPS, SOUNDS.
* **Sample loader** jumps straight to SOUNDS. CATEGORY: STARTERS (our sounds), RMT (the 35 RMT instruments), CARD (`.rti` files in
  `AtariTracker/Instruments/` on the SD card). The jog chooses the PRESET, the pads play it, **KEEP** (F8) puts it in the slot,
  **CANCEL** (F7) or leaving the page restores the old sound.
* F7 / F8 on the other pages: previous / next instrument slot. On GENERAL: F2 COPY, F3 PASTE.
* `.rti` files do not carry buzz style or auto-modulation: set them by hand after loading one.

## 1. 16-bit guitar (HSB auto-modulation)

A 16-bit sound joins two POKEY channels and plays on the second one, so it belongs on **track 2** (join 1+2) or **track 4**
(join 3+4). The instrument is refused on other tracks.

1. Pick a slot. Sample loader, STARTERS, PRESET **Bass 16-bit**, KEEP. (It already has join 1+2 and the 1.79 MHz clock on channel 1.)
2. GENERAL page:
   * **Vibrato delay: 2** (the auto-modulation, vibrato and frequency shift only start after this many frames; 0 = never).
   * **Vibrato depth: 0**, **Frequency shift: 0**.
   * **HSB auto-modulation: 1.** Every frame this adds 256 x the setting to the 16-bit pitch value, so the pitch sweeps through its range and
     wraps. Try 2 and 3 (faster, rougher) and negative values (the sweep runs backwards).
   * **Buzz style:** Standard.
3. ENVELOPE page: distortion **A** (pure tone) is the cleanest; **E** (gritty) is rougher and more guitar-like.
4. Pattern screen, track 2, low octaves (a 16-bit pair on the 1.79 MHz clock sounds in the bass range).

If nothing sweeps: the vibrato delay must be above 0, the AUDCTL page must show a JOIN, and the instrument must be on track 2 / 4.
On an 8-bit voice the setting does nothing.

## 2. Degenerate buzz bass (buzz style)

1. Pick another slot. Sample loader, STARTERS, PRESET **Buzz bass**, KEEP (plain 64 kHz clock, three buzzy steps).
2. GENERAL page: **Buzz style: Degenerate.**
3. AUDCTL page: **15 kHz off.** On the 15 kHz clock the style sounds standard (that clock cannot make the collapsed pattern).

The pitch is the same as the standard buzz bass, but the poly 4 pattern loops every 5 steps instead of 15: a thinner, more nasal buzz.
Distortion **E** (gritty) steps are affected too. To compare: GENERAL, F2 COPY, F3 PASTE into the next slot, set that slot back to
Standard, and play the same note on two tracks.

Expect coarser tuning than the standard buzz: only dividers that are multiples of 3 (and not of 5) work, so some notes, especially high
ones, are further from pitch. Use lower octaves.

## Using both at once

Guitar on track 2 (join 1+2) takes channel 1 away from track 1. To use a buzz bass on track 1 or 2 at the same time, put the guitar on
**track 4**: on its AUDCTL page turn on **join 3+4** and **1.79 MHz on channel 3**, and turn off join 1+2 and 1.79 MHz on channel 1.

## What to report back

For each: does it sound the way you expected, which settings sound best, and whether the tuning is usable. Those answers decide whether
the sawtooth / triangle voice (three phase-locked channels as one voice) is worth building.
