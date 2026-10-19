#!/bin/bash
# the chain-watch test tree: ~/solana-dev/rac1_cw, built with UYA_RC_CWATCH=1 (linux)
set -e
src=/mnt/c/Users/Nathan/Desktop/project-solana/games/rac1
dst=~/solana-dev/rac1_cw
if [ ! -d $dst ]; then
  mkdir -p $dst
  cd ~/solana-dev/rac1
  rsync -a --exclude dev/tests/out --exclude build/windows ./ $dst/
fi
rsync -a --checksum "$src/host/" "$dst/host/"
rsync -a --checksum "$src/tools/" "$dst/tools/"
cd $dst
source ../venv/bin/activate
UYA_RC_CWATCH=1 python3 build.py --iso ~/rac1.iso --repo ~/solana-dev/kit/decomp --levels 00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18 -j 16 > ~/solana-dev/cw_build.log 2>&1 && echo "build ok" || echo "build FAILED"
grep -n "error" ~/solana-dev/cw_build.log | head -20 || true
tail -1 ~/solana-dev/cw_build.log
