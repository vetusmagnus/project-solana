#!/bin/bash
# walkab.sh A B "LEVELS" "VARS_A" "VARS_B": walking frames of two settings per level, compared
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=~/rac1.iso
a=$1; b=$2; levels=$3; va=$4; vb=$5
for lv in $levels; do
    ./run.sh walk "$a" "$lv" $va > /dev/null &
    ./run.sh walk "$b" "$lv" $vb > /dev/null
    wait
    echo "level $lv: $(python3 cmp_frames.py out/walk_${a}_$lv/d out/walk_${b}_$lv/d | tail -1)"
done
