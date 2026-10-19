#!/bin/bash
# build.sh IMAGE NATIVE.c [GEN_DIR]: the harness for one VU1 program image
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
img=$1; nat=$2; gen=${3:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
sed "s/vup_IMG_vu1/vup_${img}_vu1/" > "$H/prog_$img.c" <<'X'
#include "vu.h"
void vup_IMG_vu1(RcVu0 *v, u32 pc);
void prog_run(RcVu0 *v, u32 pc) { vup_IMG_vu1(v, pc); }
X
EXTRA=${EXTRA:-}
$ZIG cc $EXTRA -target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I "$R/host/include" -I "$H" -I "$R/host/src/native" \
    "$H/harness.c" "$H/trace.c" "$H/mesh_stub.c" "$H/prog_$img.c" "$gen/vup_${img}_vu1.c" $nat -o "$H/h_$img" -lm
