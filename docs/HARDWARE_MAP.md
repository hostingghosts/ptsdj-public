# Hardware map (original Polyend Tracker, MK66)

One page. Facts come from `docs/upstream/hardware.md` (H), `audio.md` (A), `storage.md` (S), `display.md` (D),
`firmware-map.md` (F), `input.md` (I); constants live in `src/platform/board.h`. Nothing here was re-measured; the
upstream docs mark the unverified items, and so does the last section.

| Block | What | Notes (source) |
|---|---|---|
| MCU | NXP MK66FX1M0, Cortex-M4F, 180 MHz (HSRUN), bus 90 MHz, 16 MHz crystal | Teensy 3.6 style runtime (H) |
| Flash | 1 MB; images must stay below **0xD6460** (877,664 B) | vectors at 0, flash config at 0x400 (F) |
| SRAM | SRAM_L 0x1FFF0000 (64 KB) + SRAM_U 0x20000000 (192 KB) | stack top 0x20030000; DMA bursts must not cross the L/U boundary (H) |
| SDRAM | 16 MB at 0x70000000, 16-bit FlexBus | slow random access; the boot only starts the app if the SDRAM test passes (H, F) |
| EEPROM | FlexRAM, 4 KB at 0x14000000 | byte 0 = power/update flag, app area from byte 10; a write drops out of HSRUN (S, F) |
| Display | FT81x, 800x480, SPI2 up to 30 MHz, polled | display list of at most **2048 words**, no overflow check in the driver (D) |
| Audio | SGTL5000 codec on I2C0 0x0A, I2S0 master, TX DMA ring | Fs 44,117.647 Hz, block 128 frames = 2.90 ms = 522,240 cycles; render runs in the DMA ISR (A) |
| SD card | SDHC 4-bit, FatFs R0.15a, no exFAT | polled waits, main loop only, max 14 open files (S) |
| Buttons | TCA8418 on I2C0 0x34, 33 buttons, ids F1-F8 = 0-7, Power = 0x2a virtual | polled, no pin interrupts (I) |
| Pads | TCA8418 on I2C2 0x34, 48 pads (12x4), on/off only, no velocity | pad LEDs: IS31FL3731 at 0x74 (H, I) |
| Jog wheel | FTM2 quadrature decoder, read about every 100 ms | `setRawDetents` gives one event per detent (I) |
| MIDI | DIN on UART4 31,250 Bd, plus USB MIDI | (H) |
| USB | Teensy USB device, IRC48M | ISR priority 112; this tree identifies as 1209:0001 "OpenTracker" |
| Power | switch PTC3 (low = pressed), rail enable PTA24, update request PTA1 | hold 1.5 s powers off (H, F) |

## Interrupt priorities (lower = more urgent) (H, A)
SysTick 32, I2S RX DMA 0x40, MIDI UART 64, **I2S TX DMA 0x50 (audio render)**, USB 112, everything else 128. The render
preempts the whole main loop (UI, SD, I2C, display), so its load directly steals main-loop time.

## Boot order (F), as in `src/app/device_main.cpp`
`diagInit` -> `boardEarlyInit` (rail + SD only) -> restore-file check -> `bootPowerGate` -> `boardInit` (I2C, SDRAM test,
display, codec, inputs) -> app if SDRAM is ok, otherwise the hardware self-test.

## Memory budget for a Game Boy engine
Static SRAM of the platform is about 50 KB (HAL + core), leaving roughly 200 KB for engine state, UI and stack (H, estimate
from OpenTracker's symbol table; re-measure with `arm-none-eabi-size build/gbtracker.elf`). This blank shell is
text 84,984 / data 2,796 / bss 46,116 bytes. Timers free for the engine: PIT0-3, LPTMR, FTM0/1/3 (H). The tick should
count audio samples, not a timer (A).

## Not verified on hardware (as the upstream docs state)
Underrun counter, DMA channel numbers, PTD6 mute polarity, SDHC DMA into SDRAM, whether SRAM_L needs the cache clear
(H, A, S). **This GB firmware itself has been run on an original Tracker, hardware version 2, but those items were not singled out in testing.**
