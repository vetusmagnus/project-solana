#!/bin/bash
# cmp_levels.sh NAME_A NAME_B: compares the frames of two all_levels.sh sets, level by level.
# Every level should report 0 differing pixels for a change that is meant to be exact.
source "$(dirname "$0")/common.sh"
a=$1; b=$2; [ -n "$b" ] || { sed -n '2,3p' "$0"; exit 1; }
for lv in $(seq 0 18); do
    echo "level $lv: $(python3 "$T/cmp_frames.py" "$OUT/still_${a}_$lv/d" "$OUT/still_${b}_$lv/d" | tail -1)"
done
