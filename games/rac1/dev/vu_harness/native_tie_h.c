/* harness side of host/src/native/native_tie.c */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "harness.h"
#include "native_tie.h"
static u32 out[0x400 * 4], out_n, sout[8], sout_n;
void native_reset(void) {}
int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    (void)v;
    if (pc == 0x30) { native_tie_setup(mem); return -1; }
    if (pc == 0x00) { sout_n = native_tie_state((u8 *)mem, top, sout); memset(out, 0, sizeof out); out_n = native_tie_draw(mem, top, out); return out_n ? 0 : -1; }
    return -1;
}
int native_compare(const packet_t *k, int nk, int verbose)
{
    if (nk < 1) return 0;
    if (nk != 1 + (sout_n != 0)) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, 1 + (sout_n != 0)); return 0; }
    if (sout_n && (k[0].n != sout_n || memcmp(k[0].d, sout, sout_n * 16))) { if (verbose) fprintf(stderr, "   state packet differs\n"); return 0; }
    const packet_t *p = &k[nk - 1];
    int bad = p->n != out_n, exact = 1;
    for (u32 q = 0; q < out_n && q < p->n; q++) for (int w = 0; w < 4; w++) {
        u32 a = out[q * 4 + w], b = p->d[q * 4 + w];
        if (a == b) continue;
        if (w == 3 && out[q * 4] == 0 && 0) continue;
        float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
        /* the fourth word of an ST qword: whatever the program left there (the GS does not read it) */
        if (w == 3 && (b >> 24) == 0x4B) continue;
        exact = 0;
        if (!getenv("EXACT") && fabsf(fa - fb) <= 1e-6f * fabsf(fb)) continue;
        if (verbose > 1 && bad < 4) fprintf(stderr, "   qw %u word %d: %08X vs %08X\n", q, w, a, b);
        bad++;
    }
    static int nexact; nexact += exact && !bad;
    if (verbose && !bad) {}
    return !bad;
}
