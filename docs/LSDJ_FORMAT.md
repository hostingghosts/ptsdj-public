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
- Pulse/noise envelope has three stages (bytes 1, 9, 10): byte 1 = start level (high) and rate (low) down to byte 9's high nibble; byte 9's low nibble is the rate from there
  down to byte 10's high nibble; byte 10's low nibble is the rate to 0; rate 0 holds; rates use the release-digit table. The old "byte 9 with 0xC0" finding was this with a target above the start.
- Instrument byte 5 bit 3 (pulse and noise): the instrument's table advances one row per note (position carries over between notes) instead of one per tick.
  A table started by A runs per tick. A B command inside a table does nothing.
- B (command byte 2) is a chance: the step's note plays only if a random test passes (B FF always, B 00 never; measured by looping one step and counting triggers). The demos use it on pulse and noise instruments, not kits. src/gb/player.cpp models it with the same generator as Z; the ROM's exact random sequence is not reproduced, so a given run differs from the ROM, the odds are the same.
- T (tempo) is measured: T v sets the tempo to v at once (6 ticks of groove at tempo 128 become 4 after T C0, 12 after T 40); the tick that carries it is a partial tick, which the sub-step accumulator in player.cpp reproduces.

## Wave animation and kits (measured)
- Wave instrument: b3 = first frame; b9 = play mode (0 manual, 1 once, 2 and 3 loop in the ROM); b11 = speed, one frame update every (int8 b11 + 4) ticks;
  b10 = length: N = 16 - b10 frames per pass, frame = min(15, floor(k * 16 / (15 - b10))); b2 low nibble = loop-back frame (15 acts as 0).
  Every frame change repeats the note-start sequence: DAC off, 16 bytes, DAC on, trigger with 0x7E0, real frequency. Frame sequences match the ROM.
- Kit instrument (type 2): b9 = KIT 1 number, b2 = KIT 2 number, note = 16 * m + n: sample n of KIT 1 and sample m of KIT 2 together. Kit n is ROM bank n + 8 for n <= 0x12 and bank n + 13 for 0x13 <= n <= 0x32 (the code banks 27-31 are skipped; measured by running every kit number in LSDJ 9.4.2 with tagged banks; 0x33-0x3A read banks past the end of the ROM and stay silent, 0x3B-0x3F wrap to banks 8-12). The stock ROM's kits are 00-14, banks 34-63 are empty.
  Kit bank: up to 16 little-endian offsets (0x4000 based) at 0: one per sample plus an END entry that points to where the data ends (the 0xFF padding of the stock kits starts there, and a kit with 15 samples fills all 16 entries). LSDJ takes the end of the last sample from that entry, so a last sample without it is silent or plays garbage (found on hardware, 0.8.4; the stock kits' name counts agree: TR-606 has 7 names and 8 entries). Names 3 chars each at 0x22, kit name near 0x52, samples are raw 4-bit data.
  Kit instrument byte 10 (P+L+VIB) must be D0 (all 28+ kit instruments in the demo songs have it, byte 14 is F3): with 0 there a layered note (KIT 2) plays garbage instead of the mix, a single-kit note is fine (found 0.8.7 by playing both instruments in the ROM under the oracle; the Tracker's own kit default had 0).
  Layering (verified in 9.4.2 with BELIEF's kit 4 + kit 4 note 0x33, wave RAM compared chunk for chunk): the chunk written is clip(x + y - 8) per nibble, so two full-scale samples clip; stock samples are quieter. The Tracker's Sample Editor has a LEVEL row (100/75/50/25%) that scales a sample toward 8 as it is stored.
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

## Found by reading the ROM (bank 2 holds the player; tools/re/sm83.py, drive.cpp `cov`/`covdump`/`trace` commands)
- Random generator (ROM 00:33EB): S = 17 * S + 0x5C93 (16 bit), result = high byte of the new S. Playback start sets S = 0x0052 (00:3818).
  rand15() = result % 15; randMax(m) = 0 without a call when m = 0, else result % (m + 1).
- B xy (command dispatch at 02:7715, handler 02:773B): two rand15 calls; the step's note is skipped only when r1 >= x and r2 >= y, so it plays with chance
  1 - (1 - x/15)(1 - y/15). B00 never plays, B0F and BFx always. A skipped step writes nothing. Verified against the ROM for B09, B04, B81.
- Z vv (02:73CA): value = original value of the last non-Z command + (randMax(hi) << 4) + randMax(lo); the effect keeps running with the new value (a chord
  keeps its phase). Z always starts from the original value, not the previous random one. Exact against the ROM for chords.
- 02:737C also draws from the generator but is editing code (cell edit; never runs in playback, checked with coverage on i'mpuzzled, Friendbender,
  revision, kicks, TRAINJAM). Playback consumers are only B and Z; bank 1 uses it for song memory init. So the i'mpuzzled drift is not the generator.
- Z memory is per slot: the phrase, table column 1 and table column 2 each remember their own last command (ROM 02:73CA, 02:50DF); Z in a column with no earlier
  command of its own does nothing. Done. Column 2 quirk not modelled: with O1 then Z03 in column 2 the ROM changes the pan only every 16 ticks, not every pass.
- C keeps its phase when run again (same or new value); only a note trigger, starting from off, or C00 resets it. A table C (runs after the chord step) spends its first tick: base, x, y.
- Vibrato default shape is a triangle (rises 1/16 of the peak per step at speed 0), not a sine; first step after the command is 0.
- The sub-tick clock (envelope, vibrato, slide, bend steps) runs 6 times per video frame (70224/6 = 11704 cycles), not at 360 Hz: vibrato aliasing against ticks gives a beat of exactly 64 ticks at tempo 128 (7 steps per tick). Residual drift at tempo 100 (<= 12 register units over 70 ticks) is unexplained.
- Table started by A begins at its row 0 on the next tick; a table row that runs A must not overwrite the new table's position.
- R xy: x is a signed volume step (1-7 up, 9-F down by 16-x, 0 and 8 none), applied when the command runs and at every retrigger; a retrigger reloads the last programmed
  volume (start, E, table amplitude) plus the steps, not the decayed volume; y = 0 applies the step once. Vibrato: shape field (b5 bits 1-2) 0 triangle, 1 saw (pitch
  always rises), 2 square, 3 none; b5 bit 0 clear is a half-period phase shift. Saw with bit 0 clear runs ~1/1.4 as fast as expected (unexplained).
- Pitch pipeline (ROM 00:1B28, 02:4363, 02:43D8): pitch = (note - 1) * 256 + 16-bit accumulator (bend/slide) + vibrato, as index.fraction; frequency = table[index], the
  fraction interpolates by (low-byte difference of the next entry) * fraction (shift-add, rounded). Index >= 0x6C wraps by 108 (the accumulator is adjusted too), 0xB6..0xF7
  wraps up by 108, 0xF8..0xFF gives frequency 0. Bend rate per sub-step = round(v*v/8 + v/2) (mirrored for negatives), added to the accumulator each sub-step.
  L (slide) = accumulator to 0 at |acc| / (value + 1) per sub-step (value 0 jumps), stops on arrival; a slide starts one sub-step late; a table P on a later tick
  starts one sub-step late when the bend was idle. Wave instruments use the linear table at 00:09B9 (~19.2 per index), pulse the exponential one at 01:40FA.
- Instrument byte 8 (pulse): ticks per chord note minus one (0 = one tick each).
- Not solved: a table transpose of -128 with L (SETBARLO/INFORMAL kick drums) - the ROM keeps sweeping down to frequency 44 instead of returning.
- E xy on pulse/noise: the volume is set to x, then stepped by the player every (y & 7) * 65536 cycles (5.6 sub-steps per unit), down, or up when y >= 8 (y & 7 = 0 holds);
  the ROM's NR12 log shows plain volume writes (x<<4|8 and the usual 09/11/18 down-steps), not the chip's own envelope. The normal envelope stages are off after E.
  LIQUID now matches the ROM exactly over the first 200 ticks.
- Scheduler (measured): one 358 Hz interrupt (11704 cycles); each interrupt runs the sub-step (envelope, vibrato, bend, slide) first, then adds tempo to an accumulator and
  runs a sequencer tick when it reaches 896 (so a tick is 896 / tempo sub-steps: 7 at tempo 128, mean tick spacing 81927 cycles in the ROM log). With this ordering no
  extra "one sub-step late" rules are needed: table V/P/L started on any tick match the ROM (tools/oracle/timing_check.py, 0 mismatches).
- Table groove (G in a table): each row takes the next groove entry in turn. A table started by A runs its row 0 on the next tick. Re-running V keeps the vibrato phase.
- Noise: S is a plain signed byte added to the note index (SFF = -1); the index wraps modulo 120 on (n - 1); the per-tick P step (value / 4, fractional part kept) runs after
  the tick's table rows and is skipped on the tick a P command ran. Noise P, S and tables now match the ROM (kicks: 12 mismatches in 200 ticks).
- The ROM powers the APU on ~92600 cycles before the first tick; the player advances its frame sequencer by that much at start (small effect on sweeps).

## Synth and Wave screens (measured on the ROM)
- Map: Synth (Y) is above Phrase, Wave (W) above Instrument. Synth params (16 bytes at 0x3EB2 + 16n): 0 signal (0 saw, 1 square, 2 triangle, 3 W.FX), 1 filter (0-5: LOWP HIGHP BANDP ALLP
  NOTCH -----), 2 Q (start hi nibble, end lo), 3 dist (CLIP FOLD WRAP), 4 phase mode (PINCH WARP RESYNC RESYN2), 5/9 volume start/end, 6/10 cutoff, 7/11 phase (0-1F, A+Up = 8),
  8/12 vshift, 13/14 limit (shown as byte + 0F, wraps). Rows top to bottom: signal, filter, dist, phase mode, volume, cutoff, q, vshift, limit, phase. A+Right/Left +-1, A+Up/Down +-16
  (Q +-2). B+Left/Right changes synth. Frames 16n..16n+15 interpolate start to end.
- The generator is bank 29 of the ROM: 0x68F9 sets up (WRAM C441 = synth), then the main loop calls 0x6035 once per frame (C9F0 = frame, C9F1 = job flag) until 16 are done. It
  reads signal tables in banks 29/30 (additive harmonics 63/k, precomputed filtered waves per cutoff step). src/gb/synthgen.cpp runs it in an SM83 interpreter (src/gb/sm83.cpp)
  from the owner's ROM on the card; 12 random parameter sets match the ROM bit for bit (tools/oracle/synthcheck.py). About 355k instructions per synth.
- Wave screen: column c shows sample (c + 1) & 31, hex rows show 15 - sample, dots at y = 48 + 4 * value. Up/Down +-1 (Up raises the dot), A+Up/Down flips (15 - v), A+Left/Right draws
  (moves and copies the value), B+Left/Right changes wave +-1, B+Up/Down +-16, a lone B copies the wave, Select+A pastes.
- Instrument byte 5 bit 5 (0x20) on pulse, wave and noise instruments: the note is played untransposed (neither chain nor project transpose). Not for kits. F (pulse tune/fine) lasts only until the next note.
- Old-song upgrade: pulse/noise b1 low nibble >= 12 becomes (n - 3) and byte 9 = F0 when it was empty; version 7 wave byte 2 low nibble is inverted (15 - n). Matches the ROM on all 15 old demos.

## Table rows with a slide (measured on LIQUID's wave kick)
A table row that has an L command and a transpose that moves the pitch starts the slide from the old pitch and the row's note becomes the new base (as an L on a phrase step does). Without it the bend accumulator went negative against the low note and wrapped by 108 to the top of the table: a 3 kHz beep instead of a sweep.

## Pulse fine tune (instrument byte 11, F y = byte y*8)
Proportional to the note's period, not a fixed amount: pulse 1 period x (1 + fine * 2.306e-4), pulse 2 period x (1 - fine * 2.21e-4), rounded; within one divider step of the ROM (fitted on 8 notes x 24 fine values). Old songs set byte 11 on upgrade, so this was most of the remaining pitch error on them.

## Envelope stages move up as well as down
Byte 1 low nibble = rate towards byte 9's high nibble; byte 9 low nibble = rate towards byte 10's high nibble; byte 10 low nibble = rate to silence. A stage whose level is above the current volume ramps UP (one NR22 write of 0x08 per step, same periods as the down steps). The stage is remembered (starts at 1 on a note; a level set by E, the table amplitude column or R re-derives it by comparing with the two levels).

## Wave channel
Kill switches the wave DAC off (NR30 = 0), it does not just zero NR32. A bend started by a table row on the note's own tick makes its first step one sub-step later, and until that first step the base pitch is the whole table index (2006 instead of 2017 for note 73).

## Pulse fine tune: see above. Tick phase
The ROM's tick accumulator keeps a leftover from before Play, so tick edges sit at an arbitrary sub-step offset per run (no rule found from the tempo). tools/oracle: SUBSTEPS=1 compares per 358 Hz sub-step instead of per tick; TICKPHASE=r (playdump) sets the leftover to find the best alignment.

## Kits (measured on 9.4.2, more)
Byte 11 = n plays 2n-1 chunks (0 = the whole sample); only bit 5 of byte 5 loops; the last chunk of a sample is not played (a sample of 18 chunks plays 17). A kit number with no kit bank in the ROM (e.g. GUESSWHO's kit 22 on this ROM) plays whatever the bank holds; we play nothing.

## L after a kill
An L slide on a step whose note was killed still starts at the last note's pitch and glides to the new one (note retriggers at the old pitch).

## Wave bend base
Once a bend runs, the fitted note index is about 6/256 low; +6 makes INFORMAL's wave pitch exact. Probably the real note-to-index mapping has a per-note fraction we do not know.

## Open (per-song, phase-aligned, ignoring pitch differences of 2 or less): see tools/oracle/README.md
SETBARLO noise pan stepping with per-note tables; TRIAC/NICEVIBE/NOLIGHT pulse duty and volume at some events; i'mpuzzled duty/pitch events one to two sub-steps apart from the ROM; Friendbender noise pitch; revision (Z); kicks: hardware sweep phase.

## Per-note tables (byte 5 bit 3), measured on SETBARLO's noise channel
Each table remembers the row it stopped at; an instrument that comes back after another instrument's note resumes there. A hop (H) in the second column is ignored in this mode; a hop in the first column jumps and runs the destination row in the same step. Z in a table column only remembers commands of the table it was started in (the memory is cleared when a table starts).

## More measurements (accuracy pass 2)
- Transposes are range-checked one after the other: chain first, then the project's. A chain transpose that would leave the range is dropped, the project transpose still applies.
- L on a note whose envelope has run out (volume 0) retriggers it, starting from the last note's pitch, like L after a kill.
- Noise vibrato (V) runs once per tick, phase += (speed + 1) / 48, starting at -(speed + 1) / 48, and shifts the noise index by round(8 x semitones of depth). The ROM also rewrites NR44 (trigger) at every shift; we only write NR43.

## Hand-drawn waves (measured in the ROM)
Editing a wave on the Wave page sets a bit for its synth (wave >> 4) in the 16-bit word at 0x3FC4: synths 0-7 are the bits of 0x3FC5, synths 8-F the bits of 0x3FC4. Changing a parameter on the Synth page of a flagged synth opens "OVERWRITE HAND-DRAWN WAVES? YES / NO" (NO selected) and applies nothing: NO keeps the flag; YES clears it and the edit has to be made again, which then regenerates the waves. The Synth page's "WAVE 00-0F" row cannot be selected: no range select.
SYNC is project byte 0x3FBD (0 off, 1 LSDJ, 3 MIDI, 5 KEYBD, 6 ARDUINOBOY as the option list shows them).

## Export check against the ROM
A .sav written by Song::writeSav (PRESSURE, SYNC set to LSDJ) opens in the oracle ROM: the Project page shows tempo 152 and SYNC LSDJ, and the 92 note triggers of the first 400 frames are identical to the same song loaded from the ROM's own conversion. The .lsdsng files are not loaded by the ROM here (they need LSDJ's file manager); their format is covered by the round-trip tests.


## How close the player is to the ROM (0.8.8, tools/oracle/compare.py on the demo songs, first 240 ticks, channels 1, 2 and 4)
Mismatches are counted per tick and per register field against the ROM running the same song. Most are one-step differences from where the 360 Hz sub-step lands
(volume off by one, frequency off by 1-3 units, NR51 shown as 0xBB while a kit sample ends); those stay. The larger ones, for the next round:
- TRAINJAM channel 1 (211 frequency mismatches): a table with V (vibrato) next to T A4 and M commands; the vibrato phase differs.
- Friendbender, NOLIGHT, i_mpuzzled, revision: duty and frequency on pulse 2 with long tables.
- kicks-v9.2 and PRESSURE: pulse 1 sweeps; the frame sequencer phase at the first note is not matched (tick phase leftover in the ROM, see above).

## The demo songs (src/gb/demo.cpp), checked against the ROM
- Built in code, two songs that make one piece: A minor, tempo 0x78 (120 BPM with the default 6-tick groove), 4 song rows of 4 bars, SYNC byte 1 in both. Chords come from one phrase moved by chain transposes (0, -4, +3, -2); the chord command C 37 / C 47 gives minor / major.
- The wave channel sounds one octave below a pulse for the same note number (note 34 = 440 Hz on a pulse, 220 Hz on the wave channel). The bell and bass are written an octave higher for that reason.
- A wave instrument keeps its synth number in the high nibble of byte 3 (i'mpuzzled's WBAS1/2/3 hold 00, 10, 20). The demo draws its waves straight into the synth's 16 frames and sets the hand-drawn bit, so it plays without the ROM's synth routine.
- Oracle run (tools/oracle compare.py, the LSDJ 9.4.2 ROM against gb::Player) over a full pass, 1530 ticks, 4 channels, 5 fields: song A differs in 4 places and song B in 286 of 6120 channel-ticks, every one the envelope level off by one step at a single tick. Enabled, frequency, duty and pan agree on every tick, so notes, chords, the kick's table, the bell's table and the noise hits are the same in the ROM.

## Instrument pages in the ROM (measured by driving the LSDJ 9.4.2 ROM: DOWN to a row, Left/Right to a value, A+Right / A+Left, diff the instrument bytes)
Each page lists its rows from the top; a row with several values lists their bytes in order. The editor (the Row tables kPulse, kWave, kKit, kNoise in src/gb/ui/lsdj.cpp)
has the same rows and values, and Left / Right pick the value just as in the ROM.
* **Pulse:** TYPE b0; ENV b1 / b9 / b10 (three stages); WAVE b7 bits 6-7; OUTPUT b7 bits 0-1 (L is bit 0, R bit 1, each lit on its own); LENGTH b3; SWEEP b4; PITCH b5;
  TRANSP b5 bit 5 (set = OFF, notes ignore transposes); PU2 TSP b2; FINETUNE b11; CMD/RATE b8; TABLE: mode OFF / ON / STEP (b6 bit 5, then b5 bit 3) and the number (b6 bits 0-4,
  not editable while OFF). The same TABLE row on all four types.
* **Wave:** VOLUME and OUTPUT on one row (b1: 3 = 20, 2 = 40, 1 = 60, 0 = 00, and the 0xA8 of real songs reads as 3; b7); PITCH b5; TRANSP b5 bit 5; FINETUNE b12; CMD/RATE b8;
  PLAY b9 & 3 (MANUAL, ONCE, LOOP, PINGPONG; stepping up clears the low nibble of b3); **WAVE b3** (all of it in MANUAL, one step at a time; the row reads SYNTH in the other
  modes and moves the high nibble, 25 > 35); SPEED b11 (signed, shown + 4); LENGTH b10 (shown 15 - low nibble); LOOP POS b2 low nibble (shown as b2 - b10, F is a dash; a step that
  would go below b10's low nibble does nothing: with b10 = 05, 08 goes to 07 and 05 and 04 stay).
* **Kit:** the top kit line is b2 (low 6 bits) and the bottom line b9; the ROM shows kit numbers from 1 (byte 00 reads 01). VOLUME b1 and OUTPUT b7; PITCH b5; FINETUNE b8;
  OFFSET b12 / b13; LEN b3 / b11 (0 reads ALL); LOOP b5 bit 6 / bit 5 (only bit 5 loops in the player); SPEED b2 bit 6 (1X, 0.5X); CLIP b10 low bits (HARD, SOFT, FOLD, WRAP, no
  further); TABLE as above.
* **Noise:** ENV b1 / b9 / b10; OUTPUT b7; LENGTH b3; PITCH b2 (FREE when 0, SAFE when not; the player's noise frequencies match the ROM for both); VIBRATO b5; TRANSP b5 bit 5;
  CMD/RATE b8; TABLE as above.
* **Kit SPEED 0.5X (b2 bit 6):** the pitch register is 0x692 instead of 0x749 (FINETUNE adds to either) and a chunk lasts 23424 cycles instead of 11712. The kit number is b2 & 0x3F,
  so a kit with the flag set used to play silence in the player.

Found and fixed in 1.0.0: the editor used to write AUTOMATE to b9 bit 7 (the second envelope stage on pulse and noise, the kit number on kits; it is now the STEP mode of the TABLE row),
TUNING to b8 as a 12T/FIX/DRM choice (b8 is CMD/RATE, and FINETUNE on kits), and on wave instruments SYNTH to b2 (the ROM's LOOP POS) and LENGTH to b3 (the wave number itself: one press
turned 20 into 40, synth 2 into synth 4). Songs saved before the fix with AUTOMATE ON carry a stray bit in b9; real songs can have that bit legitimately (an envelope stage of 8 or more),
so it cannot be repaired. Kit numbers are shown counted from 01 as in the ROM (the instrument page, the kit pages, messages and the default name KIT16 for slot 15; the bytes and the kit file names still count from 00). Still different: the two kit lines are in the other order; the ENV and PITCH values are shown as hex, not as
pictures; the player ignores CMD/RATE, the second OFFSET and LEN values, the first LOOP flag and CLIP (only HARD is played).

## Notes in the phrase column, and there is no OFF note (measured on the LSDJ 9.4.2 ROM)
- Notes run 01 (C-2) to 6C (108). A+Left / A+Right clamp to that range; A+Up / A+Down move an octave only if the result stays inside it (05 A+Down stays 05, 0D goes to 01,
  60 A+Up goes to 6C, 61 and 64 A+Up stay). Any byte above 6C shows as ??? and no byte shows OFF; 00 is the empty ---.
- No key combination makes an off note: Select+B and Select+A leave a note alone, A+B (or B then A) clears the cell to empty (note 00, instrument FF), a second A on an empty cell
  does nothing new. LSDJ silences a note with the K (kill) command, so PTSDJ has no OFF note either.
