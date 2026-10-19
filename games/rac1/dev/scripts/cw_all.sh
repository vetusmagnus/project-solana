#!/bin/bash
# cw_all.sh "LEVELS" [FROM,TO]: the chain-watch census on several levels at once (10 runs at a time)
cd ~/solana-dev/rac1_cw/dev/tests
export RAC1_ISO=$HOME/rac1.iso
for lv in $1; do
  ./run.sh walk cw "$lv" UYA_FIXEDTIME=1 UYA_CWATCH=${CW:-0} UYA_CWATCH_AT=${2:-1450,1750} > /dev/null &
  while [ "$(jobs -rp | wc -l)" -ge 10 ]; do sleep 1; done
done
wait
for lv in $1; do
  echo "=== level $lv"
  grep -A400 "chain watch census" out/walk_cw_$lv/out.txt | grep -v " 0.0 kicks" | head -40
done
