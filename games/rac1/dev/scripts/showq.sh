#!/bin/bash
# showq.sh PATTERN BEFORE AFTER: the translated ShrubProc (level 13) around the first line matching PATTERN
F=~/solana-dev/rac1/build/linux/gen/l13/rc_funcs_082.c
n=$(grep -nF "$1" "$F" | head -1 | cut -d: -f1)
sed -n "$((n-$2)),$((n+$3))p" "$F" | grep -v '^L_' | cut -c1-300
