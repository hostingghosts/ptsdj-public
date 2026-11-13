# Learnings

Hard-won rules for this firmware, taken from the upstream notes in `docs/upstream/` (written for SIDTracker and
OpenTracker, which share this hardware layer). Each point names its source doc. Hardware facts are in
`docs/HARDWARE_MAP.md`. **None of this firmware has been run on a Tracker yet**; "field-proven" below means proven by
OpenTracker or SIDTracker, not by this repository.

## The five rules

1. **The hardware layer is the base that boots. Do not change it without hardware to test.** `src/platform` and
   `third_party` come from OpenTracker, whose bring-up order is "field-proven, keep exactly" (`audio.md`, *Bring-up
   order*; `firmware-map.md`, *Boot sequence*). Examples of what broke when order changed: writing the I2S `MDR` while
   `DUF` was set left MCLK at 180 MHz and the codec silent; starting RX DMA before TX left TX stuck (`audio.md`). The
   linker layout fixes where vectors, the flash config (0x400) and `.data` sit (`firmware-map.md`, *Flash layout*), and
   the memory map has DMA constraints (no burst across SRAM_L/SRAM_U; `hardware.md`, *Memory map*). USB runs at ISR
   priority 112 beside the audio render at 0x50 (`hardware.md`, *DMA and interrupt resources*); a USB-disk attempt in the
   parent `polyend-atari` repository was backed out to "the V2.0 base" (its commit message `99e827c`; not a doc, so treat as anecdote). Local changes already made on top
   of OpenTracker are listed in `src/platform/PROVENANCE.md`. Only the macro rename and the USB-string choice in
   `UPSTREAM.md` differ in this tree.

2. **The card must be FAT32.** FatFs is built with `FF_FS_EXFAT = 0`, so SDXC cards formatted exFAT will not mount
   (`storage.md`, *Filesystem*). Also: one volume, 32-bit LBA, `FF_FS_REENTRANT = 0`, so FatFs is not thread-safe and must
   never be called from an ISR, and at most 14 files may be open at once (`storage.md`).

3. **The FT81x display list has a size budget: 2048 words (8 KB) per frame, and the driver does not check overflow**
   (`display.md`, *Model* and *Unknowns*). Keep every frame well under it; the parent project's limit was 1800 words,
   and `tests/gb_ui_test.cpp` (`display_list_stays_within_budget`) keeps a 1900-word guard with a conservative cost model
   (`display.md`, *SIDTracker display-list budget*). A `CMD_TEXT` costs several words; one `VERTEX2II` per glyph inside one
   `BEGIN(BITMAPS)` per colour is much cheaper (`GlyphLayer` in `src/atw/ui/screen.h`; x >= 512 needs
   `VERTEX_TRANSLATE_X`). The simulator does not model the limit (`sim/README.md`), so test it on the host with the
   counting display.

4. **The bootloader only accepts 24-character image names.** `PolyendTrac*.ptf`, exactly 24 characters: the bootloader
   ignores longer names and then flashes a *random* file from `/Firmware` (`firmware-map.md`, *Image format*). The 9 characters after `PolyendTrac` are free; this image is `PolyendTracPTSDJv103.ptf`. The
   Makefile enforces the length with `$(error)`, but `isUpdateImageName()` in the app accepts 15-96 characters, so it does
   not (`firmware-map.md`). Every image must also pass `tools/check_image.py` (flash config FSEC 0xDE / FOPT 0xF9, sane
   vector table, nothing past 0xD6460, record types 00/01/02 only), which is what stops a bad build from locking the MCU
   (`firmware-map.md`).

5. **Keep SD access out of the audio path.** All SD access runs in the main loop and blocks it; the audio ISR keeps
   running and preempts the polled waits, so audio survives but UI and input stall (`storage.md`, *SD card interface*).
   The audio callback itself must fill exactly 128 frames, never block and never allocate (`audio.md`, *Chain*). Do file
   work between playback rows or when stopped, never inside the callback, and keep the sound engine free of `IStorage`.

## More from the upstream docs

* **The render is an ISR and steals main-loop time.** It runs at priority 0x50 and preempts the UI, SD, I2C and display;
  budget is 522,240 cycles per 2.90 ms block, alarm at 70 % (`audio.md`, *ISR priorities*, *Sample rate and budget*).
  On the K66 the QEMU instruction counts were optimistic: about 1.4-1.7 cycles per instruction, and effects
  cost twice their instruction count (`audio.md`, *First K66 numbers*). Plan for 1.5x.
* **The device sample rate is 44,117.647 Hz, not 44,100.** Compute pitch and tick timing for the real rate; the simulator
  runs at 44,100 (`audio.md`, *Sample rate*; `sim/README.md`). Count audio samples for the tick clock, not a timer
  (`hardware.md`, *Timer resources*).
* **Do not write the EEPROM while audio runs.** `eeprom_write_byte()` leaves HSRUN, halves the core clock, and its
  re-entry rewrites `SIM_CLKDIV1` with the SDRAM at 180 MHz; the HAL restores OUTDIV3 after each write (`hardware.md`,
  *MCU and clocks*; `storage.md`, *Non-volatile settings*). Keep settings on the SD card.
* **EEPROM byte 0 is the power and update flag**; the app area starts at byte 10, and if the `EEPROM` magic there is
  missing, stock reads the update flag as 1 (`firmware-map.md`; `storage.md`). Do not overwrite stock's config image
  blindly.
* **The headphone amp is left muted by the codec init.** The app must call `setHeadphoneVolume()` or headphones stay
  silent (`audio.md`, *Chain*; done in `src/app/device_main.cpp`).
* **Boot order matters, and the app only starts with working SDRAM** (else the hardware self-test runs)
  (`firmware-map.md`, *Boot sequence*; `src/app/device_main.cpp`).
* **I2C is polled and must survive the audio ISR.** A wait expires only after 1 ms *and* 2000 polls, because the render
  can preempt the loop for longer than 1 ms (`hardware.md`, *Buses and devices*; `audio.md`, *ISR priorities*).
* **Input has no velocity and no hold event.** Pads send 127 on press; time holds from Down/Up (`input.md`).
* **Recovery before testing, every session.** Keep only the stock image plus one test image in `/Firmware`, everything
  else elsewhere, because the rear reset button installs a *random* `.ptf`. Ways back: stock Config > Firmware update;
  `/Firmware/ot_restore.txt` naming the stock image (checked right after the SD mounts, before any other driver, renamed
  `.done` so a bad image cannot loop); the reset button; the Polyend tool (`firmware-map.md`, *Way back to stock*).
* **Host first, hardware last.** Simulator plus ASan/UBSan host tests catch most problems; the QEMU instruction counter
  gave a floor, not cycles (`audio.md`).
* **Licences.** SIDTracker and OpenTracker are MIT and may be used with attribution (`NOTICE.md`); GPL code (GoatTracker,
  reSID, RMT) is not copied.

## Not verified, per the upstream docs
Underrun counter on hardware, DMA channel numbers, mute polarity of PTD6, SDHC DMA into SDRAM, SRAM_L cache for DMA, and
per-frame SPI time (`audio.md`, `hardware.md`, `storage.md`, `display.md`, *Unknowns*). Treat the first flash of this image
as the first hardware test; follow `docs/HARDWARE_TEST.md` first.
