#!/bin/bash
# build dev/vu_harness/fx57843_fuzz (the effect strip program, image 57843, against
# native_fx57843.c on mutated and made-up strips). The program is built from the translation
# with hooks: VU_TRACE counting the addresses reached, and checks that no FMAC result is a NaN
# (NaN bit patterns read as floats: which NaN an op keeps is up to the compiler) and that no Q
# or clip flag read comes before its result lands (the native code has no pipeline).
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../.." && pwd)
gen=${1:-$R/build/linux/gen}
ZIG=${ZIG:-$(command -v zig || echo "python3 -m ziglang")}
CF="-target x86-linux-gnu -O2 -g -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv -I $R/host/include -I $H -I $R/host/src/native"
cat > "$H/fx57843_cov.h" <<'X'
#include "vu.h"
void fz_cov(unsigned pc);
extern int fz_nan, fz_stale;
static inline f32 fz_fc(f32 x) { u32 b = vu_u(x); if ((b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu)) fz_nan = 1; return vu_fc(x); }
static inline f32 fz_q(RcVu0 *v, u32 cyc) { if (v->q_pend && (s32)(cyc - v->q_rdy) < 0) fz_stale |= 1; return vu_q(v, cyc); }
static inline u32 fz_clip(RcVu0 *v, u32 cyc) { vu_cl_sync(v, cyc); if (v->cp_n) fz_stale |= 2; return v->clip; }
X
sed -e 's/vu_fc(/fz_fc(/g' -e 's/vu_q(v, cyc)/fz_q(v, cyc)/g' -e 's/vu_clip(v, cyc)/fz_clip(v, cyc)/g' "$gen/vup_57843_vu1.c" |
$ZIG cc $CF '-DVU_TRACE(pc)=fz_cov(pc)' -include "$H/fx57843_cov.h" -x c -c - -o "$H/fx57843_prog.o" &&
$ZIG cc ${EXTRA:-} $CF "$H/fx57843_fuzz.c" "$H/fx57843_prog.o" "$R/host/src/native/native_fx57843.c" -o "$H/${OUT:-fx57843_fuzz}" -lm
