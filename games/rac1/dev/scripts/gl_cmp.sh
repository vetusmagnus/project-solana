#!/bin/bash
# gl_cmp.sh LEVEL...: GL recordings, native TieProc (direct) against the game's, per level
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=~/rac1.iso
unset DISPLAY WAYLAND_DISPLAY   # (run.sh then uses Xvfb: WSLg's display gives the game no GL)
[ -x out/gs_replay_gl ] || ./build_replay.sh || exit 1
for lv in "$@"; do
    ./run.sh glrec dir "$lv"
    ./run.sh glrec off "$lv" UYA_NTIEPROC=0
    grep -h "GS: drawing\|clip part goes\|clip parts direct" out/glrec_dir_$lv/out.txt | tail -2
    echo "level $lv: $(./gl_compare.sh out/glrec_dir_$lv.bin out/glrec_off_$lv.bin | tail -1)"
done
