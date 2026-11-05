# LSDJ 9.4.2 format and behaviour (measured with tools/oracle; offsets are in the 32 KB working song)

.sav = 128 KB: 0x0000-0x7FFF working song; 0x8000+ file table (names 0x8000, versions 0x8100, 'jk' 0x813E, active slot 0x8140,
block alloc 0x8141, 512-byte compressed blocks from 0x8200). A working song with an empty file table boots and plays.
Banks of 8 KB carry 'rb' at 0x1E78, 0x3E80, 0x7FF0.

| Offset | Content |
|---|---|
| 0x0000 | phrase notes 255x16 (0 empty; 1 = C2, 13 = C3) |
| 0x1090 | grooves 32x16 (tick counts, ends at first 0) |
| 0x1290 | song 256 rows x 4 channels (row-major), chain id, 0xFF empty |
| 0x1690 | table envelope column 32x16 |
| 0x2040 | instrument alloc, 64 bytes |
| 0x2080 / 0x2880 | chain phrase ids / chain transposes, 128x16 |
| 0x3080 | instruments 64x16 (byte0 type 0 pulse 1 wave 2 kit 3 noise) |
| 0x3480 0x3680 0x3880 0x3A80 0x3C80 | table transpose, cmd1, val1, cmd2, val2 (32x16 each) |
| 0x3E82 / 0x3EA2 | phrase alloc (255 bits) / chain alloc (128 bits) |
| 0x3EB2 | synth params 16x16 |
| 0x3FB4 / 0x3FB5 | tempo / transpose (more project bytes to 0x3FC0) |
| 0x4000 / 0x4FF0 | phrase commands / values 255x16 |
| 0x6000 | wave frames 256x16 (synth n owns frames 16n..16n+15; the ROM regenerates them on synth edits) |
| 0x7000 | phrase instrument ids 255x16, 0xFF none |

Pulse instrument bytes: 1 envelope (NR12 = vol<<4|8, LSDJ steps volume itself with NR12 writes); 3 low 6 bits NR11 length,
bit6 length enable; 4 sweep (NR10 = ~byte); 7 bits 6-7 duty, bits 0-1 pan (bit0 L, bit1 R); 11 fine tune; 6 = 0x20|table.

Timing: tick rate = 0.4 x tempo Hz (10485760/tempo cycles). Sequencer: all channels start at the cursor row; a channel
whose next row cell is empty or past row 255 restarts at row 0; groove ticks per step; verified by tools/oracle/seqcheck.py.

APU (src/gb/apu.cpp, tools/oracle/apucmp.cpp): per side sum over routed DAC-on channels of (2 x level - 15), times
(NR50 volume + 1), x64 in the reference output. Duty tables fitted. Reference-emulator time offset differs per run, so
apucmp is given a fitted c0; the reference applies volume writes lazily, so envelope write bursts differ for ~1000 samples.

Todo: remaining instrument bytes per type, command bytes, tables/synth semantics, project bytes, kits, speech, compression.

## Player rules measured so far (tools/oracle/compare.py, fuzzcompare.py)
- Envelope: release digit n (1..15) steps volume down by one every {1,2,3,4,6,8,11,15,20,27,36,48,64,86,115}[n-1] x 1/360 s,
  independent of tempo; 0 = no fade. LSDJ does it with the NR12 write sequence 09,11,18 (DMG "zombie" write, -1 volume).
- Pulse: NR10 = ~byte4; NR11 = (byte7 & C0) | (byte3 & 3F); NR12 = (byte1 & F0) | 8; byte3 bit6 = length enable; byte7 bits0-1 pan (L,R).
- Wave: NR32 = byte1 raw; per note: DAC off, load 16 wave bytes, DAC on, trigger with 0x7E0, then write the real frequency.
- Noise: NR41 = byte3 & 3F, NR42 = (byte1 & F0) | 8, NR43 from the note table (chain transpose applies, project transpose does not).
- Commands (byte = letter order, B=2 exists): D delay, G groove, H hop (replaces the step, destination step plays the same tick,
  HFF stops the song and writes NR52=0), K kill (steps volume to 0 after n ticks), T tempo (not yet verified).

## Tables, vibrato, more commands (measured)
- Command letters in the ROM string: `-ABCDEFGHKLMOPRSTVWZ` (A=1 ... H=8, K=9, L=10, M=11, O=12, P=13, R=14, S=15, T=16, V=17, W=18, Z=19).
- Song memory byte 0x7FFF = format version (0x16 for 9.x). Older songs are upgraded by LSDJ on load.
- Table: one row per tick (G in a row changes its speed), loops after row 15; transpose is signed; H xy = hop x times (0 forever) to row y
  and the destination row runs on the same tick; amplitude column x,y sets volume x for y ticks on its own pointer (y=0 skipped, no restart on wrap).
  Volume up = NR12 write 0x08 per step, down = the 09,11,18 sequence per step.
- Vibrato V sd: speed s -> period 64/(s+1) samples at 360 Hz; depth d -> peak semitones [.25,.375,.5,.75,1,1.5,2,2.5,3,3.5,4,5,6,7,8][d-1];
  applied to the period (so register steps scale with 2048-f); P+L+VIB byte: bit0 direction, bits1-2 shape (sine, saw, square), 0x10 = per tick.
- O: 0 none, 1 left, 2 right, 3 both (NR51 bits). Pulse 2 tune = byte 2 signed semitones; pulse fine = byte 11 (about -b*30/255 register units on pulse 1).
- P (pitch bend) rate is not linear in the value and is not modelled yet. L, S, R, B, C, F, M, Z, W-on-wave not modelled yet.

## More commands (measured, tick-exact unless noted)
- C xy: from the tick after the command the note cycles x, y, 0 semitones per tick (x, y, 0, x ...). Matches the ROM exactly.
- R xy: retrigger every y ticks (y = 0 off). R with x >= 8 retriggers every ~4 steps of the 360 Hz clock: not modelled.
- S xy (pulse 1): NR10 = ~b with b = 0x7F + (x-1)*16 + y (x = 0: 0x7F + y), then the note is retriggered.
- M ld: absolute 0-7 per side; 8 = no change, 9-B = +1..+3, D-F = -3..-1; written to NR50.
- F xy (pulse): fine = -y register units on pulse 1 (+y on pulse 2); x = pulse 2 tune in semitones.
- L (slide, same step as a note): no retrigger; pitch moves linearly in semitones to the new note over `value` steps of the 360 Hz clock.
- P v (signed): pitch bend in semitones per 360 Hz step = 0.00163*|v| + 0.0005*v^2 (fit; tick-level error about 1-3 register units).
- Wave instrument bytes (type 1): b1 bits 6-5 = NR32 volume code, b3 = first wave frame (synth x 16), b7 bit6 = length enable, b9 = play mode
  (0 manual .. 3), b10/b11 = length/speed of the frame animation (not modelled: only the first frame plays).
- Pulse/noise: byte 9 with bits 7-6 set replaces the release rate after the first step (low nibble as the release digit).
- B (command byte 2) is used on kit instruments in the demos and its effect is unknown; Z (random) cannot match the ROM's random numbers.

## Wave animation and kits (measured)
- Wave instrument: b3 = first frame; b9 = play mode (0 manual, 1 once, 2 and 3 loop in the ROM); b11 = speed, one frame update every (int8 b11 + 4) ticks;
  b10 = length: N = 16 - b10 frames per pass, frame = min(15, floor(k * 16 / (15 - b10))); b2 low nibble = loop-back frame (15 acts as 0).
  Every frame change repeats the note-start sequence: DAC off, 16 bytes, DAC on, trigger with 0x7E0, real frequency. Frame sequences match the ROM.
- Kit instrument (type 2): b9 = kit number (ROM bank 8 + n; kits above the ROM's own come from the owner's patched ROM or .kit files), note n = sample n-1.
  Kit bank: 15 little-endian sample offsets (0x4000 based) at 0, names 3 chars each at 0x22, kit name near 0x52, samples are raw 4-bit data.
  Playback = 16-byte chunks (32 samples) written to wave RAM every 11712 cycles, each with the note-start sequence and frequency 0x749. Sample lengths and chunk timing match the ROM.
- Noise: NR43 index = note index (mod 120) in the measured table; S xy adds 16*x + y (signed nibbles) to it, table transposes add to it directly.
  P and C on noise (S every tick / every second tick) are not modelled correctly yet.

## Upgrading older songs (measured with the ROM's own file-screen load; src/gb/upgrade.cpp, tools/oracle/upgradecheck.cpp)
- Format byte 0x7FFF: 0x07, 0x09, 0x0A, 0x0B (old) -> 0x16. Loading an .lsdsng through the ROM (Project > Load/Save song, slot, A) converts it;
  tools/oracle/lsdsng.py builds the file table, probe.run drives the keys, `shot` in drive.cpp shows the screen, `trace` logs SRAM/WRAM writes with
  the writing PC and ROM bank (the conversion lives in ROM bank 28 at about 0x6740-0x6D60; tools/re/sm83.py disassembles it).
- Version 7 only: no B command, phrase and table command codes >= 2 go up by one.
- Pulse and noise: the low nibble of bytes 1, 9 and 10 (release digits) goes through 0->0, 1..7 -> 5,7,8,9,10,11,11, 8 -> 0, 9..F as (n-8).
- Pulse: fine tune (byte 11) = (byte 7 & 0x3C) << 1. The old default pulse instrument becomes `00 A0 00 00 FF 00 00 03 00 00 D0 00 00 00 F3 00`.
- Wave: new b3 = old b2 & 0xF0; version 7 also b2 low nibble 0 -> 15 else +1; play mode (before 0x0A) new = (old + 1) & 3.
- R command: the period digit counts from 1 now (y -> y + 1) unless y = 15 or x = 8.
- Noise notes: the high nibble of the instrument's old shape byte (b4) moves by (note - 1) / 12 - 3 octaves (stopping at 0 and F), the result is looked
  up in a 256-entry table (the grid in upgrade.cpp: wide noise 1 + 4 * (hi - 2) + {0,1,2,3,5,7,11,15}[lo - 8] compressed near the top, narrow +60).
- Not converted: synth parameter bytes (0x3EB2..), project bytes 0x3FCA-B, and the text tables at 0x1D00 (LSDJ leaves junk there; a fresh song's are used).
- Transposes: a note whose chain + project transpose would fall below 1 is played untransposed; above 135 it wraps by 136.
- Pulse 2 tune (byte 2) and the fine tune apply from the moment the note starts.
