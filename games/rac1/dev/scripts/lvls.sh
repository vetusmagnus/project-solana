#!/bin/bash
# lvls.sh MODE NAME "LEVELS" PATTERN [VAR=...]: run.sh MODE NAME for each level (JOBS at once), then
# the last line matching PATTERN in each level's log
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=~/rac1.iso
mode=$1; name=$2; levels=$3; pat=$4; shift 4
JOBS=${JOBS:-6}
for lv in $levels; do
    ./run.sh "$mode" "$name" "$lv" "$@" > /dev/null &
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 2; done
done
wait
for lv in $levels; do
    f=out/${mode}_${name}_$lv/out.txt
    echo "L$lv: problems $(grep -v '^ALSA lib' "$f" | grep -ciE 'rc_bad|crash|SIGSEGV|segmentation|[^e]fault'): $(grep -E "$pat" "$f" | tail -1 | cut -c1-230)"
    grep -E "check: run" "$f" | head -2 | cut -c1-230
done
