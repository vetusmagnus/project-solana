#!/bin/bash
# peek.sh RUN PATTERN LEVEL...: lines matching PATTERN in each level's log of RUN (e.g. still_schk), and its last lines
cd ~/solana-dev/rac1/dev/tests/out
run=$1; pat=$2; shift 2
for l in "$@"; do
    echo "== level $l"
    grep -hE "$pat" "${run}_$l/out.txt" | head -6 | cut -c1-260
    tail -2 "${run}_$l/out.txt" | cut -c1-200
done
