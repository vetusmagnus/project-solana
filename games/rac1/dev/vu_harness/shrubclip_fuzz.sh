#!/bin/bash
# build dev/vu_harness/shrubclip_fuzz (the shrub clip program against native_shrubclip.c on made-up
# classes and instances; the program built with an address trace for the coverage count)
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
gen=${1:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
printf 'void cov_step(unsigned pc);\n#define VU_TRACE(pc) cov_step(pc)\n' > "$H/shrubclip_cov.h"
sed "s/vup_IMG_vu1/vup_912339_vu1/" > "$H/shrubclip_prog.c" <<'Y'
#include "vu.h"
void vup_IMG_vu1(RcVu0 *v, u32 pc);
void prog_run(RcVu0 *v, u32 pc) { vup_IMG_vu1(v, pc); }
Y
F="-target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I $R/host/include -I $H -I $R/host/src/native"
$ZIG cc ${EXTRA:-} $F -include "$H/shrubclip_cov.h" -c "$gen/vup_912339_vu1.c" -o "$H/shrubclip_vup.o" &&
$ZIG cc ${EXTRA:-} $F "$H/shrubclip_fuzz.c" "$H/shrubclip_prog.c" "$H/shrubclip_vup.o" "$R/host/src/native/native_shrubclip.c" -o "$H/shrubclip_fuzz" -lm
