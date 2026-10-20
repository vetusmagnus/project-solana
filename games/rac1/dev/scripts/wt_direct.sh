#!/bin/bash
# wt_direct.sh "LEVELS" [VSYNC]: a GPU walking run per level with UYA_WALKTRACE (default vsync 1450); prints
# each level's DIRECT packets still in the chain (VIF word 0x50......), by writer, and the run's problems
S=$(dirname "$(readlink -f "$0")")
W=$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")/solana-test
v=${2:-1450}
for lv in $1; do
  rm -f "$W/walk_$v.txt"
  log=$("$S/win_run.sh" "$lv" "UYA_WALKTRACE=$v $WTV" wt)
  mv -f "$W/walk_$v.txt" "$W/walk_${v}_l$lv.txt" 2>/dev/null
  f="$W/walk_${v}_l$lv.txt"
  echo "=== level $lv: $(grep -c ' vif [0-9A-F]* 5' "$f" 2>/dev/null) DIRECT packets; problems $(grep -ciE 'rc_bad|crash|segmentation' "$log")"
  grep ' vif [0-9A-F]* 5' "$f" 2>/dev/null | sed 's/.* tag \([0-9]*\) qwc \([0-9]*\).* by \([0-9A-F]*\).*/tag \1 qwc \2 by \3/' | sort | uniq -c | sort -rn | head -8
done
