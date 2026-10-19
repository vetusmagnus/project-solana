#!/bin/bash
# n2d_check.sh "LEVELS" [EXTRA]: walking runs with UYA_N2D=1 (the 2D ports checked against the game's
# routines); prints each check's last line per level, and the native 2D lines of a default run
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=$HOME/rac1.iso
for lv in $1; do
  ./run.sh walk chk "$lv" UYA_FIXEDTIME=1 UYA_N2D=1 $2 > /dev/null &
  ./run.sh walk n2d "$lv" UYA_FIXEDTIME=1 $2 > /dev/null &
  while [ "$(jobs -rp | wc -l)" -ge 10 ]; do sleep 1; done
done
wait
for lv in $1; do
  echo "=== level $lv"
  f=out/walk_chk_$lv/out.txt
  for k in "quad check" "sky check" "sky (textured) check" "shadow volumes check" "darkening pass check" "GS settings check" "HUD sprite check" "stencil clear check"; do grep "native 2D $k" $f | tail -1; done
  grep -i "differ\|not known\|the game's routine\|GIF data\|not the" $f | grep -v " 0 differ" | sort | uniq -c | head -12
  g=out/walk_n2d_$lv/out.txt
  grep "native 2D" $g | grep -v check | sort | uniq | tail -12
  echo "problems: $(grep -ciE 'rc_bad|crash|SIGSEGV|segmentation' $f) $(grep -ciE 'rc_bad|crash|SIGSEGV|segmentation' $g)"
done
