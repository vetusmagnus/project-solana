#!/bin/bash
# clean_build_time.sh DIR LOG [JOBS]: a clean build of all 19 levels (Linux) in the dev tree copy DIR,
# with what the launcher deletes after a build deleted first (gen, gen.new, obj, build/levels, build/disc,
# zig-cache; zig's global cache too, kept per tree); prints the wall time of the whole build and of its phases (from the [progress] lines)
d=$1; log=$2; j=${3:-20}
cd "$d" || exit 1
rm -rf build/linux/gen build/linux/gen.new build/linux/obj build/linux/region build/levels build/levels_us build/disc zig-cache zig-gcache
export ZIG_GLOBAL_CACHE_DIR=$PWD/zig-gcache ZIG_LOCAL_CACHE_DIR=$PWD/zig-cache   # (empty caches: a clean build)
source ~/solana-dev/venv/bin/activate
python3 -u build.py --iso ~/rac1.iso --repo ~/solana-dev/kit/decomp --platform linux --levels 00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18 -j $j 2>&1 \
  | while IFS= read -r l; do printf '%s %s\n' "$(date +%s.%N)" "$l"; done > "$log"
s=$(head -1 "$log" | cut -d" " -f1); e=$(tail -1 "$log" | cut -d" " -f1)
t() { grep -m1 " \[progress\] $1" "$log" | cut -d" " -f1; }
echo "total $(echo "$e - $s" | bc) s; to levels $(echo "$(t levels) - $s" | bc) s; levels..compile $(echo "$(t compile) - $(t levels)" | bc) s; compile..link $(echo "$(t link) - $(t compile)" | bc) s; $(tail -1 "$log" | cut -d" " -f2-)"
