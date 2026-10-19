#!/bin/bash
# walkab_par.sh A B "LEVELS" "VARS_A" "VARS_B": walkab.sh, with every run on UYA_FIXEDTIME=1 and JOBS runs at
# once (default 10). The fixed clock makes a run's frames independent of the machine's load in most runs
# (level 3 walking: 10 runs at once matched a run alone, 0 pixels), so the runs need not be kept apart. Not
# always: with a build (-j 16) and a Windows game run going at the same time, 2 of 38 runs diverged (millions
# of pixels; a rerun matched). So don't build while it runs. A level whose A and B differ is run again, A and B
# alone, and both results are printed: believe the rerun. Both settings get the fixed clock, so compare A with
# B, not with runs made without it. All 19 levels take about 10 minutes.
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=${RAC1_ISO:-$HOME/rac1.iso}
a=$1; b=$2; levels=$3; va=$4; vb=$5
JOBS=${JOBS:-10}
for lv in $levels; do
    for n in "$a" "$b"; do
        v=$va; [ "$n" = "$b" ] && v=$vb
        ./run.sh walk "$n" "$lv" UYA_FIXEDTIME=1 $v > /dev/null &
        while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 1; done
    done
done
wait
for lv in $levels; do
    r=$(python3 cmp_frames.py out/walk_${a}_$lv/d out/walk_${b}_$lv/d | tail -1)
    case "$r" in
    *"differing pixels 0 "*|*"differing pixels 0") echo "level $lv: $r" ;;
    *)  ./run.sh walk "$a" "$lv" UYA_FIXEDTIME=1 $va > /dev/null &
        ./run.sh walk "$b" "$lv" UYA_FIXEDTIME=1 $vb > /dev/null
        wait
        echo "level $lv: $r; rerun alone: $(python3 cmp_frames.py out/walk_${a}_$lv/d out/walk_${b}_$lv/d | tail -1)" ;;
    esac
done
