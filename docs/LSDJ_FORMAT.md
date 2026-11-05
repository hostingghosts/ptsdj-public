# LSDJ 9.4.2 save format (derived with tools/oracle)

`.sav` = 128 KB. 0x0000-0x7FFF is the working song (4 x 8 KB banks); 0x8000+ is the file table (names 0x8000, versions 0x8100,
'jk' 0x813E, active slot 0x8140, block alloc 0x8141, 512-byte compressed blocks from 0x8200; lsdmanager, MIT).
A working song with an empty file table boots and plays (checked: edited .sav -> APU writes).

Confirmed from a blank song + UI edits (offset: meaning):
- 0x0000: phrase notes, 255 x 16 (0 = empty; first note entered = 0x03). 0x0FF0: 16 x 0xFF.
- 0x1090: grooves 32 x 16 (default `06 06 00..`). 0x1290: song rows, 0xFF = empty.
- 0x2080: chain phrase ids 128 x 16 (0xFF empty). 0x7000: phrase instrument ids 255 x 16 (0xFF empty).
- Bank markers 'rb' at 0x1E78, 0x3E80, 0x5E?? (todo), 0x7FF0. Instrument alloc starts 0x3E82. Instrument params ~0x3080+.
- Default new pulse instrument writes at 0x3081..0x3087; project state near 0x3FC1.

Todo: chain transposes, phrase commands/values, instrument layouts per type, tables, synths, words, project fields, block
compression for file slots.
