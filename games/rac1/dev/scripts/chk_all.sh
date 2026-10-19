#!/bin/bash
# chk_all.sh MODE NAME [VAR=...]: every level, then each level's last TieProc check summary
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=~/rac1.iso
mode=$1; name=$2; shift 2
JOBS=${JOBS:-6}
for lv in $(seq 0 18); do
    ./run.sh "$mode" "$name" "$lv" "$@" &
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 2; done
done | sort -k4 -n
wait
for lv in $(seq 0 18); do
    f=out/${mode}_${name}_$lv/out.txt
    echo "L$lv: $(grep 'ShrubProc check:' "$f" | tail -1 | sed 's/.*check: //')"
    grep 'ShrubProc check: run' "$f" | head -3
    grep 'ShrubProc: the level' "$f"
done
