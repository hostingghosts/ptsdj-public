#!/bin/bash
# make_shots.sh BASE_CARD: takes the screenshots of the guides. Each of sim/scripts/manual_a..f.txt runs in the simulator on a fresh copy of
# BASE_CARD (a card tree with GB/LSDJ.GB, GB/Instruments, GB/Samples, GB/Kits, a .ptf in Firmware and NO songs or saves, so the consoles start
# with the built-in demo). The pictures land in build/manual/shots; `@` in the scripts is that folder. Needs build/sim/gb-sim (make sim).
set -e
BASE=$(cd "$1" && pwd)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/manual
SHOTS=$OUT/shots
mkdir -p "$SHOTS"
for s in a b c d e f; do
    rm -rf "$OUT/sd-$s"
    cp -a "$BASE" "$OUT/sd-$s"
    sed "s#@#$SHOTS#g" "$ROOT/sim/scripts/manual_$s.txt" > "$OUT/manual_$s.run.txt"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$ROOT/build/sim/gb-sim" --sd "$OUT/sd-$s" --script "$OUT/manual_$s.run.txt"
    echo "script $s done"
done
