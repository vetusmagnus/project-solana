#!/bin/bash
# walk_cmp.sh LEVEL...: walking frames, native TieProc against the game's, per level
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=~/rac1.iso
for lv in "$@"; do
    ./run.sh walk tieon "$lv" &
    ./run.sh walk tieoff "$lv" UYA_NTIEPROC=0
    wait
    echo "level $lv: $(python3 cmp_frames.py out/walk_tieon_$lv/d out/walk_tieoff_$lv/d | tail -1)"
done
