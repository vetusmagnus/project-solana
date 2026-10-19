#!/bin/bash
# gl_compare.sh REC_A REC_B: replays two GS recordings (run.sh glrec) through the GL renderer
# with gs_replay_gl and compares the frames. Build the replay tool first: ./build_replay.sh
#
# Example: ./run.sh glrec on 3 && ./run.sh glrec off 3 UYA_NTFPROC=0 &&
#          ./gl_compare.sh out/glrec_on_3.bin out/glrec_off_3.bin
# Exact changes give 0 pixels. GPU rounding can move a few tens of pixels a frame; decide the
# tolerance before the change, not after.
source "$(dirname "$0")/common.sh"
a=$1; b=$2; [ -n "$b" ] || { sed -n '2,8p' "$0"; exit 1; }
RP=${REPLAY:-$OUT/gs_replay_gl}
[ -x "$RP" ] || { echo "no replay tool at $RP: run ./build_replay.sh"; exit 1; }
rm -rf "$OUT/gl_a" "$OUT/gl_b"; mkdir -p "$OUT/gl_a" "$OUT/gl_b"
REPLAY_QUIET=1 "$RP" "$a" "$OUT/gl_a" > /dev/null 2>&1
REPLAY_QUIET=1 "$RP" "$b" "$OUT/gl_b" > /dev/null 2>&1
python3 "$T/cmp_frames.py" "$OUT/gl_a" "$OUT/gl_b"
