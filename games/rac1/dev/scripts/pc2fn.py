#!/usr/bin/env python3
# pc2fn.py LEVEL PC...: the routine holding each PC on that level (config/overlays/functions.tsv's places,
# then the executable's functions below the levels' code)
import sys, os, bisect, re
R = os.path.expanduser("~/solana-dev/kit/decomp")
lv = "%02d" % int(sys.argv[1]); fs = []
for l in open(os.path.join(R, "config", "overlays", "functions.tsv")):
    if l.startswith("#") or not l.strip(): continue
    f = l.rstrip("\n").split("\t")
    for p in f[5].split(","):
        if p.split(":")[0] == lv: fs.append((int(p.split(":")[1], 16), int(f[2]), f[0]))
gen = os.path.expanduser("~/solana-dev/rac1/build/linux/gen/rc_table.c")   # (the executable's: its translated functions and the C ones' thunks)
if os.path.exists(gen):
    for a, n in re.findall(r"\{0x([0-9A-F]{8}), rc_(?:thunk_)?(func_[0-9A-F]{8})\w*\}", open(gen).read()):
        if int(a, 16) < 0x1E9080 or not any(x[0] == int(a, 16) for x in fs): fs.append((int(a, 16), 0, n))
fs.sort(); ks = [a for a, _, _ in fs]
for p in sys.argv[2:]:
    a = int(p, 16); i = bisect.bisect_right(ks, a) - 1
    print(p, "%s+%X%s" % (fs[i][2], a - fs[i][0], "" if not fs[i][1] or a - fs[i][0] < fs[i][1] else " (past its end)") if i >= 0 else "?")
