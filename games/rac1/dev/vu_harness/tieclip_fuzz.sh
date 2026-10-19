#!/bin/bash
# build dev/vu_harness/tieclip_fuzz (the tie clip program against native_tieclip.c on made-up
# classes and moved instances; the program built with VU_TRACE counting the addresses reached)
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
gen=${1:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
CF="-target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I $R/host/include -I $H -I $R/host/src/native"
echo 'void tc_cov(unsigned pc);' > "$H/tieclip_cov.h"
$ZIG cc $CF '-DVU_TRACE(pc)=tc_cov(pc)' -include "$H/tieclip_cov.h" -c "$gen/vup_224979_vu1.c" -o "$H/tieclip_prog.o" &&
$ZIG cc ${EXTRA:-} $CF "$H/tieclip_fuzz.c" "$H/tieclip_prog.o" "$R/host/src/native/native_tieclip.c" -o "$H/${OUT:-tieclip_fuzz}" -lm
