/* harness side of host/src/native/native_tfrag.c (tfrag program, image 55907) */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "harness.h"
#include "native_tfrag.h"

#define OUTCAP (0x400 * MAXPK)
static u32 out[OUTCAP * 4];
static NativeTfragPacket pk[MAXPK];
static int npk;
static int nst_bad;

void native_reset(void) { native_tfrag_reset(); }

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    /* the registers the native code carries between runs must be what the program had */
    if (pc != 0x00) {
        u32 b[4]; memcpy(b, v->vf[3], 16);
        u32 nb[4]; float nc[4]; native_tfrag_state(nb, nc);
        if ((memcmp(nb, b, 16) || memcmp(nc, v->vf[4], 16)) && nst_bad++ < 5) fprintf(stderr, "  carried registers differ (vf3 %u %u, vf4 %g)\n", b[0], b[1], v->vf[4][0]);
    }
    npk = native_tfrag_run(pc, (u8 *)mem, top, out, OUTCAP, pk, MAXPK);
    return npk < 0 ? -1 : 0;
}

/* the qwords of a packet that are ST data in PACKED mode (the GS does not read their 4th word) */
static void st_slots(const u32 *d, u32 n, u8 *isst)
{
    memset(isst, 0, n);
    for (u32 q = 0; q < n;) {
        const u32 *t = d + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0];
        u64 regs = ((u64)t[3] << 32) | t[2];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        if (flg == 0) for (u32 k = 0; k < len && q + 1 + k < n; k++) if (((regs >> 4 * (k % nreg)) & 15) == 2) isst[q + 1 + k] = 1;
        q += 1 + len;
        if (eop) break;
    }
}

int native_compare(const packet_t *k, int nk, int verbose)
{
    static int exact_runs, st_only;
    if (nk != npk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, npk); return 0; }
    int bad = 0, exact = 1;
    for (int i = 0; i < nk; i++) {
        const packet_t *p = &k[i];
        const u32 *d = out + pk[i].off * 4;
        if (p->n != pk[i].n) { if (verbose) fprintf(stderr, "   kick %d: %u vs %u qwords\n", i, p->n, pk[i].n); bad++; continue; }
        static u8 isst[0x400];
        st_slots(p->d, p->n, isst);
        for (u32 q = 0; q < p->n; q++) for (int w = 0; w < 4; w++) {
            u32 a = d[q * 4 + w], b = p->d[q * 4 + w];
            if (a == b) continue;
            if (w == 3 && isst[q]) { st_only = 1; continue; }   /* the unused 4th word of ST */
            exact = 0;
            float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
            if (!getenv("EXACT") && fabsf(fa - fb) <= 1e-6f * fabsf(fb)) continue;
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %d: native %08X program %08X\n", i, q, w, a, b);
            bad++;
        }
    }
    exact_runs += exact && !bad;
    (void)st_only;
    return !bad;
}
