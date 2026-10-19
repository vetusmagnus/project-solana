#!/bin/bash
# gldl.sh LEVEL: one GL run on the GPU with UYA_GLDLLOG=1: the targets' syncs with GS memory, summarised
S=/mnt/c/Users/Nathan/Desktop/project-solana/games/rac1/dev/scripts
log=$(bash $S/win_run.sh $1 "UYA_GLDLLOG=1" gldl)
echo "log: $log"
python3 - "$log" <<'PY'
import re, sys, collections
c = collections.Counter(); first = {}; last = {}
for l in open(sys.argv[1], errors="replace"):
    m = re.search(r"(download|upload) v(\d+): target (\w+)/(\d+) cls (\d+), (\d+) pages from (\d+)(?: \((.*)\))?", l)
    if not m: continue
    kind, v, tb, bw, cls, n, p0, why = m.groups(); v = int(v)
    why = re.sub(r"0x[0-9a-fA-F]+|[0-9a-fA-F]{8,}", "*", why or "")
    k = (kind, tb, bw, cls, why[:70], "boot" if v < 300 else "load" if v < 1000 else "play")
    c[k] += int(n); first.setdefault(k, v); last[k] = v
for k, n in sorted(c.items(), key=lambda x: first[x[0]]):
    print("%-8s target %s/%s cls %s %-5s pages %5d vsyncs %d-%d  %s" % (k[0], k[1], k[2], k[3], k[5], n, first[k], last[k], k[4]))
PY
