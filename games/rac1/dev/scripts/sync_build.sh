#!/bin/bash
# sync_build.sh LOG: the project-solana tree's games/rac1 sources into the WSL dev tree, then build
set -e
src=/mnt/c/Users/Nathan/Desktop/project-solana/games/rac1
dst=~/solana-dev/rac1
rsync -a --checksum "$src/host/" "$dst/host/"
rsync -a --checksum "$src/tools/" "$dst/tools/"
cd "$dst"
source ../venv/bin/activate
python3 build.py --iso ~/rac1.iso --repo ~/solana-dev/kit/decomp --levels 00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18 -j 16 > ~/solana-dev/$1 2>&1 && echo "build ok" || echo "build FAILED"
grep -n "error\|warning:" ~/solana-dev/$1 | grep -v "^.*overlays_" | head -20 || true
tail -1 ~/solana-dev/$1
