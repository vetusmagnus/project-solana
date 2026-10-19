#!/bin/bash
# build dev/vu_harness/moby_fuzz (the moby program against native_moby.c on made-up strips)
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
gen=${1:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
sed "s/vup_IMG_vu1/vup_13859_vu1/" > "$H/moby_prog.c" <<'X'
#include "vu.h"
void vup_IMG_vu1(RcVu0 *v, u32 pc);
void prog_run(RcVu0 *v, u32 pc) { vup_IMG_vu1(v, pc); }
X
$ZIG cc ${EXTRA:-} -target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I "$R/host/include" -I "$H" -I "$R/host/src/native" \
    "$H/moby_fuzz.c" "$H/moby_prog.c" "$gen/vup_13859_vu1.c" "$R/host/src/native/native_moby.c" "$H/mesh_stub.c" ${MOBY_EXTRA_SRC:-} -o "$H/${MOBY_OUT:-moby_fuzz}" -lm
