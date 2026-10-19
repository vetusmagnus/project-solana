#include "vu.h"
void fz_cov(unsigned pc);
extern int fz_nan, fz_stale;
static inline f32 fz_fc(f32 x) { u32 b = vu_u(x); if ((b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu)) fz_nan = 1; return vu_fc(x); }
static inline f32 fz_q(RcVu0 *v, u32 cyc) { if (v->q_pend && (s32)(cyc - v->q_rdy) < 0) fz_stale |= 1; return vu_q(v, cyc); }
static inline u32 fz_clip(RcVu0 *v, u32 cyc) { vu_cl_sync(v, cyc); if (v->cp_n) fz_stale |= 2; return v->clip; }
