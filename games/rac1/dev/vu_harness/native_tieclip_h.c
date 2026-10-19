/* harness side of host/src/native/native_tieclip.c (tie clip program, image 224979).
 * Every kicked packet is compared word for word with the program's, and so is the whole of
 * VU1 data memory after the run (the program is run again here, from the same state and
 * memory, for that). Without EXACT a float word may differ by 1e-6 relative; without STRICT a
 * word the GS never reads (the fourth word of an ST qword) may differ (counted apart).
 * build: ./build.sh 224979 "native_tieclip_h.c ../../host/src/native/native_tieclip.c" */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "harness.h"
#include "native_tieclip.h"

#define OUTCAP (0x400 * MAXPK)
static u32 out[OUTCAP * 4];
static NativeTieclipPacket pk[MAXPK];
static int npk;
static u8 nat_mem[0x4000];
static int mem_same;
static long n_runs, n_strict, n_mem_ok, n_unread;
void prog_run(RcVu0 *v, u32 pc);

static void report(void)
{
    fprintf(stderr, "native: %ld runs compared; %ld identical in every word; VU memory after the run identical in %ld; %ld unread words differing\n",
            n_runs, n_strict, n_mem_ok, n_unread);
}

static void regs_of(const RcVu0 *v, NativeTieclipRegs *r)
{
    for (int k = 0; k < 16; k++) r->vi[k] = (u16)v->vi[k];
    memcpy(r->vf21, v->vf[21], 16); memcpy(r->vf22, v->vf[22], 16); memcpy(r->vf23, v->vf[23], 16);
    memcpy(r->vf29, v->vf[29], 16); memcpy(r->vf30, v->vf[30], 16); memcpy(r->vf31, v->vf[31], 16);
    r->vf1w = ((const u32 *)v->vf[1])[3]; r->vf25w = ((const u32 *)v->vf[25])[3]; r->vf26w = ((const u32 *)v->vf[26])[3];
}

void native_reset(void) { native_tieclip_reset(); }

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    static int seeded;
    if (!seeded) {               /* the registers the game had when the capture started */
        seeded = 1;
        atexit(report);
        NativeTieclipRegs r; regs_of(v, &r); native_tieclip_seed(&r);
    }
    static u8 before[0x4000];
    memcpy(before, mem, 0x4000);
    npk = native_tieclip_run(pc, (u8 *)mem, top, out, OUTCAP, pk, MAXPK);
    if (npk < 0) return -1;
    memcpy(nat_mem, mem, 0x4000);
    /* the program's memory after the run, for the comparison */
    static RcVu0 pv; pv = *v;
    memcpy((void *)mem, before, 0x4000);
    prog_run(&pv, pc);
    mem_same = !memcmp(nat_mem, mem, 0x4000);
    if (!mem_same && getenv("VERBOSE_MEM")) {
        int shown = 0;
        for (u32 q = 0; q < 0x400 && shown < 8; q++) for (int w = 0; w < 4; w++) {
            u32 a = ((const u32 *)nat_mem)[q * 4 + w], b = ((const u32 *)mem)[q * 4 + w];
            if (a != b && shown++ < 8) fprintf(stderr, "   mem qw %u word %d: native %08X program %08X\n", q, w, a, b);
        }
    }
    return 0;
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
    static int strict = -1, exact = -1;
    if (strict < 0) { strict = getenv("STRICT") != NULL; exact = strict || getenv("EXACT") != NULL; }
    n_runs++;
    if (nk != npk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, npk); return 0; }
    int bad = 0, unread = 0;
    for (int i = 0; i < nk; i++) {
        const packet_t *p = &k[i];
        const u32 *d = out + pk[i].off * 4;
        if (p->n != pk[i].n) { if (verbose) fprintf(stderr, "   kick %d: native %u qwords, program %u\n", i, pk[i].n, p->n); bad++; continue; }
        static u8 isst[0x400];
        st_slots(p->d, p->n, isst);
        for (u32 q = 0; q < p->n; q++) for (int w = 0; w < 4; w++) {
            u32 a = d[q * 4 + w], b = p->d[q * 4 + w];
            if (a == b) continue;
            if (!strict && w == 3 && isst[q]) { unread++; continue; }
            float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
            if (!exact && fabsf(fa - fb) <= 1e-6f * fabsf(fb)) continue;
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %d: native %08X program %08X\n", i, q, w, a, b);
            bad++;
        }
    }
    n_unread += unread;
    if (!mem_same) { if (verbose) fprintf(stderr, "   VU memory after the run differs\n"); bad++; }
    else n_mem_ok++;
    if (!bad && !unread) n_strict++;
    return !bad;
}
