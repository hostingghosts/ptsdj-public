# Demos

Real LSDJ songs (.lsdsng, same container as .lsdprj) supplied by the owner of this repo for testing and demonstration.
They are other people's music: not covered by LICENSE, personal use only, do not republish. Names are the original file
names. Kits (`kits/*.kit`) are sample kits for the wave channel.

Format: only Friendbender, i_mpuzzled, kicks and revision are in the current (9.x) memory format. The others were saved by
older LSDJ versions (format byte 0x07-0x0B at 0x7FFF) and go through `gb::upgradeSong` on load. It covers commands, envelope releases,
wave fields and default instruments; the old noise-note encoding is not converted yet, so noise parts of those songs can differ.
