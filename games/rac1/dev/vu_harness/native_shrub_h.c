/* harness side of host/src/native/native_shrub.c */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "harness.h"
#include "native_shrub.h"
#include "gifst.h"
static packet_t mine[MAXPK]; static int nmine;
static void emit(const u32 *pk, u32 n) { if (nmine < MAXPK) { mine[nmine].n = n; memcpy(mine[nmine].d, pk, n * 16); nmine++; } }
void native_reset(void) {}
int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    (void)v; nmine = 0;
    static int rec; if (getenv("DBG") && rec >= 166 && rec <= 168) { const s32 *h = (const s32 *)(mem + top * 16); fprintf(stderr, "rec %d pc %04X top %u hdr %d %d %d %d st2w %d\n", rec, pc, top, h[0], h[1], h[2], h[3], ((const s32 *)(mem + (top + h[3] + h[2] + 2) * 16))[3]); } rec++;
    return native_shrub_run((u8 *)mem, pc, top, emit) ? 0 : -1;
}
int native_compare(const packet_t *k, int nk, int verbose)
{
    if (nk != nmine) { if (verbose) fprintf(stderr, "   packets: program %d native %d\n", nk, nmine); return 0; }
    int bad = 0;
    for (int i = 0; i < nk; i++) {
        if (k[i].n != mine[i].n) { if (verbose) fprintf(stderr, "   packet %d: program %u qw native %u\n", i, k[i].n, mine[i].n); bad++; continue; }
        static unsigned char stm[0x400];
        gif_st_mask(k[i].d, k[i].n, stm);
        for (u32 q = 0; q < k[i].n; q++) for (int w = 0; w < 4; w++) {
            u32 a = mine[i].d[q * 4 + w], b = k[i].d[q * 4 + w];
            if (a == b || (w == 3 && stm[q])) continue;
            float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
            if (!getenv("EXACT") && fabsf(fa - fb) <= 1e-6f * fabsf(fb)) continue;
            if (verbose > 1 && bad < 64) fprintf(stderr, "   packet %d qw %u word %d: native %08X program %08X\n", i, q, w, a, b);
            bad++;
        }
    }
    return !bad;
}
