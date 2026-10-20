#!/bin/bash
# hl_census.sh "LEVELS": the host draw list check in the chain-watch tree (software, walking, UYA_HLFROM=1450): the
# items the list does not hold as the walker reads them, by writer (routine names from pc2fn.py)
cd ~/solana-dev/rac1_cw/dev/tests; export RAC1_ISO=$HOME/rac1.iso
S=$(dirname "$(readlink -f "$0")")
for lv in $1; do ./run.sh walk hl "$lv" UYA_FIXEDTIME=1 UYA_CWATCH=0 UYA_HLCHECK=1 UYA_HLFROM=1450 > /dev/null & while [ "$(jobs -rp | wc -l)" -ge 10 ]; do sleep 1; done; done; wait
for lv in $1; do
  f=out/walk_hl_$lv/out.txt; echo "=== level $lv: $(grep "host draw list:.*sends," $f | tail -1 | sed "s/.*sends that differ/differ/")"
  grep -A80 "host draw list census" $f | grep "written by" | python3 -c '
import sys, re, subprocess
lv = sys.argv[1]; rows = [l for l in sys.stdin]
pcs = set()
for l in rows:
    m = re.search(r"written by ([0-9A-F]{8}) from ([0-9A-F]{8})", l); pcs |= {m[1], m[2]}
names = dict(x.split(" ", 1) for x in subprocess.run(["python3", sys.argv[2], lv] + sorted(pcs), capture_output=True, text=True).stdout.splitlines())
for l in rows[:30]:
    m = re.search(r"(\d+)\s+(not in the list|changed since taken)\s+written by ([0-9A-F]{8}) from ([0-9A-F]{8}) \(e.g. ([0-9A-F]{8})\)", l)
    print("%7s  %-20s %s from %s (e.g. %s)" % (m[1], m[2], names.get(m[3], m[3]), names.get(m[4], m[4]), m[5]))
' "$lv" "$S/pc2fn.py"
done
