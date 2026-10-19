#!/bin/bash
# gl_ab.sh A B "LEVELS" "VARS_A" "VARS_B": GL recordings (the game on its GL renderer, under Xvfb) of two
# settings per level, replayed and compared. WRAPPER picks the build (default: the tree's).
cd ~/solana-dev/rac1/dev/tests
export WRAPPER=${WRAPPER:-$HOME/solana-dev/rac1/build/linux/rac1_wrapper}
export RAC1_ISO=~/rac1.iso
unset DISPLAY WAYLAND_DISPLAY   # (run.sh then uses Xvfb: WSLg's display gives the game no GL)
[ -x out/gs_replay_gl ] || ./build_replay.sh || exit 1
a=$1; b=$2; levels=$3; va=$4; vb=$5
for lv in $levels; do
    ./run.sh glrec "$a" "$lv" $va > /dev/null
    ./run.sh glrec "$b" "$lv" $vb > /dev/null
    echo "level $lv ($(grep -h 'GS: drawing' out/glrec_${a}_$lv/out.txt | head -1)): $(./gl_compare.sh out/glrec_${a}_$lv.bin out/glrec_${b}_$lv.bin | tail -1)"
    rm -f out/glrec_${a}_$lv.bin out/glrec_${b}_$lv.bin
done
