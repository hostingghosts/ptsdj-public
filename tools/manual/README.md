# Building the PTSDJ guides

`quickstart.py` and `manual.py` build `docs/PTSDJ_Quick_Start.pdf` and `docs/PTSDJ_Manual.pdf` with reportlab and Pillow
(`python3 -m venv v && v/bin/pip install reportlab pillow`). The screenshots come from the simulator:

1. Put an SD card tree at `build/manual/sd` (GB/LSDJ.GB, a few songs in GB/Songs, presets in GB/Instruments, WAVs in GB/Samples, a .ptf in Firmware).
2. Run the scripts `sim/scripts/manual_*.txt` (replace `@` with the shots folder) with `build/sim/gb-sim --sd build/manual/sd --script ...`;
   the screenshots land in `build/manual/shots`. Run each script from a fresh copy of the SD tree.
3. Run `quickstart.py` and `manual.py`.
