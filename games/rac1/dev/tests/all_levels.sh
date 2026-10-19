#!/bin/bash
# all_levels.sh NAME [VAR=value ...]: run.sh still NAME for every level 0..18, a few at a time.
#
# JOBS (default 3) runs at once. Keep it low: headless runs give different frames when the
# machine is loaded (the game's state depends on wall-clock time somewhere), so a comparison
# made under heavy load shows differences that are not real. Rerun a level before believing one.
#
# Then compare two sets: ./cmp_levels.sh NAME_A NAME_B
source "$(dirname "$0")/common.sh"
name=$1; shift || { sed -n '2,9p' "$0"; exit 1; }
JOBS=${JOBS:-3}
for lv in $(seq 0 18); do
    "$T/run.sh" still "$name" "$lv" "$@" &
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 2; done
done
wait
