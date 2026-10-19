#!/bin/bash
# build dev/vu_harness/fx221571_fuzz (the glint / sparkle program, image 221571, against
# native_fx221571.c on made-up particle buffers; the program built with VU_TRACE counting the
# addresses reached)
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
gen=${1:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
CF="-target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I $R/host/include -I $H -I $R/host/src/native"
echo 'void fx_cov(unsigned pc);' > "$H/fx221571_cov.h"
$ZIG cc $CF '-DVU_TRACE(pc)=fx_cov(pc)' -include "$H/fx221571_cov.h" -c "$gen/vup_221571_vu1.c" -o "$H/fx221571_prog.o" &&
$ZIG cc ${EXTRA:-} $CF "$H/fx221571_fuzz.c" "$H/fx221571_prog.o" "$R/host/src/native/native_fx221571.c" -o "$H/${OUT:-fx221571_fuzz}" -lm
