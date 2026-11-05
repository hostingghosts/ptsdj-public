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
