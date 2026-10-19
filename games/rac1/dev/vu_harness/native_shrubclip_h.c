/* harness side of host/src/native/native_shrubclip.c (VU1 image 912339, the shrub clip program).
 * build: ./build.sh 912339 "native_shrubclip_h.c ../../host/src/native/native_shrubclip.c"
 * run:   EXACT=1 ./h_912339 vucapseq.bin [verbose]   (or STRICT=1)
 * Every kicked packet is compared word for word. EXACT=1: every word the GS reads must be
 * identical (the fourth word of an ST qword and the high REGS word of a GIF tag with at most 8
 * registers may differ, counted apart); STRICT=1: every word. Neither: floats within 1e-6.
 * The whole VU data memory after the native run is also compared with the program's (the program
 * is run again here on a copy of the state), and must be identical in every case. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "harness.h"
#include "native_shrubclip.h"

void prog_run(RcVu0 *v, u32 pc);

#define NPK 4096
static u32 *pkd; static u32 pkoff[NPK], pklen[NPK], npk, nq;
static void emit(const u32 *pk, u32 n)
{
    if (!pkd) pkd = malloc(0x40000 * 16);
    if (npk >= NPK || nq + n > 0x40000) return;
    pkoff[npk] = nq; pklen[npk] = n; memcpy(pkd + nq * 4, pk, n * 16); nq += n; npk++;
}
static long n_unread, n_strict_ok, n_mem_bad;
static void report(void) { fprintf(stderr, "native: %ld runs identical in every word; %ld words differing that the GS does not read; %ld runs with VU memory differing\n", n_strict_ok, n_unread, n_mem_bad); }

void native_reset(void) { native_shrubclip_reset(); }

static int mem_bad;
int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    static int seeded;
    static u8 before[0x4000], prog_after[0x4000];
    if (!seeded) { native_shrubclip_seed((const u32 (*)[4])v->vf, v->vi); seeded = 1; atexit(report); }
    /* the program's memory after the run, for the memory check */
    memcpy(before, mem, 0x4000);
    { static RcVu0 c; c = *v; prog_run(&c, pc); }
    memcpy(prog_after, mem, 0x4000);
    memcpy((u8 *)mem, before, 0x4000);
    npk = nq = 0;
    int r = native_shrubclip_run(pc, (u8 *)mem, top, emit);
    if (r < 0) return -1;
    mem_bad = memcmp(mem, prog_after, 0x4000) != 0;
    if (mem_bad) {
        n_mem_bad++;
        if (getenv("VERBOSE_MEM")) for (int q = 0; q < 0x400; q++) if (memcmp(mem + q * 16, prog_after + q * 16, 16)) { fprintf(stderr, "   mem qword %d differs\n", q); break; }
    }
    return 0;
}

/* is word w of qword q of packet p one the GS does not read? */
static int unread_word(const u32 *p, u32 n, u32 q, int w)
{
    u32 t = 0;
    while (t < n) {
        const u32 *g = p + t * 4;
        u32 nloop = g[0] & 0x7FFF, flg = (g[1] >> 26) & 3, nreg = g[1] >> 28;
        u64 regs = ((u64)g[3] << 32) | g[2];
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if (q == t) return w == 3 && nreg <= 8;
        if (q < t + len) {
            if (flg != 0) return 0;
            u32 j = q - t - 1, reg = (u32)(regs >> (4 * (j % nreg))) & 15;
            return reg == 2 && w == 3;
        }
        t += len;
        if (g[0] & 0x8000) break;
    }
    return 0;
}

int native_compare(const packet_t *k, int nk, int verbose)
{
    static int strict = -1, exact;
    if (strict < 0) { strict = getenv("STRICT") != NULL; exact = strict || getenv("EXACT") != NULL; }
    if ((int)npk != nk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %u\n", nk, npk); return 0; }
    int bad = mem_bad, unread = 0;
    for (int i = 0; i < nk; i++) {
        const u32 *a = pkd + pkoff[i] * 4;
        if (pklen[i] != k[i].n) { if (verbose) fprintf(stderr, "   kick %d: %u qwords, program %u\n", i, pklen[i], k[i].n); bad++; continue; }
        for (u32 q = 0; q < k[i].n; q++) for (int w = 0; w < 4; w++) {
            u32 x = a[q * 4 + w], y = k[i].d[q * 4 + w];
            if (x == y) continue;
            if (!strict && unread_word(k[i].d, k[i].n, q, w)) { unread++; continue; }
            if (!exact) { float fx, fy; memcpy(&fx, &x, 4); memcpy(&fy, &y, 4); if (fabsf(fx - fy) <= 1e-6f * fabsf(fy)) continue; }
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %d: %08X, program %08X\n", i, q, w, x, y);
            bad++;
        }
    }
    n_unread += unread;
    if (!bad && !unread) n_strict_ok++;
    return !bad;
}
