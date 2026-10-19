#!/bin/bash
# run.sh MODE NAME LEVEL [VAR=value ...]: one headless run of one level.
#
# MODE
#   still   1300 vsyncs, standing still, frame dumped at vsync 1250 (software renderer)
#   walk    walks and turns, a frame every 20 vsyncs from 1420 to 1760 (software renderer)
#   glrec   GL renderer under Xvfb, walking as above, GS input recorded for vsyncs 1560..1639
#           to OUT/glrec_NAME_LEVEL.bin (compare two recordings with gl_compare.sh)
# NAME      a label for the run; output goes to OUT/MODE_NAME_LEVEL/ (out.txt = the log, d/ = frames)
# LEVEL     0..18 (3 Kerwan, 13 Gemlik)
# VAR=value extra environment for the game, e.g. UYA_NTFPROC=0 or UYA_SRCSTATS=1000
#           or UYA_VUCAPSEQ=13507,1250 UYA_NATIVE=0 (writes vucapseq.bin into the run's folder for
#           dev/vu_harness; UYA_NATIVE=0 because it records runs of the translated program)
#
# Example: ./run.sh still on 3 && ./run.sh still off 3 UYA_NTFPROC=0 &&
#          python3 cmp_frames.py out/still_on_3/d out/still_off_3/d
source "$(dirname "$0")/common.sh"
mode=$1; name=$2; lv=$3; shift 3 || { sed -n '2,18p' "$0"; exit 1; }
d=$OUT/${mode}_${name}_$lv; rm -rf "$d"; mkdir -p "$d/d"; cd "$d"
base=(RAC1_ISO="$RAC1_ISO" UYA_LEVEL=$lv@300 UYA_SKIPMOVIES=10)
case $mode in
  still) env "${base[@]}" UYA_HEADLESS=1 UYA_MTGS=0 UYA_KEYS="$STILL_KEYS" UYA_DUMP="$d/d" UYA_DUMP_EVERY=50 UYA_DUMP_FROM=1250 UYA_VSYNCS=1300 \
             "$@" timeout 900 "$WRAPPER" > out.txt 2>&1 ;;
  walk)  env "${base[@]}" UYA_HEADLESS=1 UYA_MTGS=0 UYA_KEYS="$WALK_KEYS" UYA_DUMP="$d/d" UYA_DUMP_EVERY=20 UYA_DUMP_FROM=1420 UYA_VSYNCS=1762 \
             "$@" timeout 900 "$WRAPPER" > out.txt 2>&1 ;;
  glrec) X=(); [ -z "$DISPLAY" ] && X=(xvfb-run -a -s "-screen 0 1280x1024x24")
         env "${base[@]}" UYA_RENDERER=gl UYA_KEYS="$WALK_KEYS" UYA_GSREC="$OUT/glrec_${name}_$lv.bin,1560,80" UYA_VSYNCS=1642 \
             "$@" timeout 900 "${X[@]}" "$WRAPPER" > out.txt 2>&1 ;;
  *) sed -n '2,18p' "$0"; exit 1 ;;
esac
rc=$?
rm -f d/*.wav d/*.raw
what="frames $(ls d/*.ppm 2>/dev/null | wc -l)"; [ "$mode" = glrec ] && what="recording $OUT/glrec_${name}_$lv.bin"
echo "$mode $name level $lv: exit $rc, problem lines $(problems out.txt), $what"
