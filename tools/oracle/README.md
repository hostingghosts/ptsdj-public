# LSDJ oracle

`drive.cpp`: scripts button presses into your own LSDJ ROM under the gambatte core from lsdpack 0.0.5 (GPL; never copied here
or linked into firmware), dumps SRAM, logs APU writes (`REGLOG=file`). Used to derive the song layout and check the player.

Build (outside repo, `reference/oracle`): copy lsdpack's `libgambatte`, un-comment `Cartridge::saveSavedata`, then
`g++ -std=c++14 -O2 -w -DHAVE_STDINT_H -Ilp/libgambatte/{common,include,src,src/mem} drive.cpp $(find lp/libgambatte/src -name '*.cpp' ! -name file_zip.cpp) -lz -o drive`.
Run in a dir holding `lsdj.gb` (+ optional `lsdj.sav`): `drive lsdj.gb script.txt`. Script: `wait N`, `press A+RIGHT N`, `save`.
