/* harness side of host/src/native/native_tfragclip.c (tfrag clip program, image 903379)
 * build: ZIG=... ./build.sh 903379 "native_tfragclip_h.c ../../host/src/native/native_tfragclip.c"
 * run:   EXACT=1 ./h_903379 vucapseq.bin [verbose]   (or STRICT=1)
 * Every kicked packet is compared word for word. EXACT=1: every word the GS reads must be
 * equal (the fourth word of ST may differ, counted apart); STRICT=1: every word. Neither:
 * floats within 1e-6. Besides the packets, all of VU1 data memory after the run and the
 * registers the native code carries (vf3, clip flags, vi15) are compared with the program's
 * (it is run again here from the same state). NOSEED=1: the carried registers are taken from
 * the capture before the first run only, then kept by the native code from run to run. */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "harness.h"
#include "native_tfragclip.h"

#define OUTCAP (0x400 * MAXPK)
static u32 out[OUTCAP * 4];
static NativeTfragclipPacket pk[MAXPK];
static int npk;
static long n_mem_bad, n_state_bad, n_carry_bad, n_strict_ok, n_unread, n_runs;
static void report(void);

void native_reset(void) { native_tfragclip_reset(); atexit(report); }

void prog_run(RcVu0 *v, u32 pc);

static void regs_of(const RcVu0 *v, NativeTfragclipRegs *r)
{
    memcpy(r->vf3, v->vf[3], 16);
    r->clip = v->clip & 0xFFFFFF; r->vi15 = v->vi[15] & 0xFFFF;
    memcpy(&r->vf26w, &v->vf[26][3], 4); memcpy(&r->vf27w, &v->vf[27][3], 4);
}

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    static u8 nm[0x4000];
    static RcVu0 pv;
    int verbose = getenv("VERBOSE") ? atoi(getenv("VERBOSE")) : 0;
    NativeTfragclipRegs cr, pr;
    static int first = 1;
    regs_of(v, &pr);
    native_tfragclip_state(&cr);
    if (!first && memcmp(&cr, &pr, sizeof cr)) {
        if (getenv("NOSEED") && n_carry_bad < 5) fprintf(stderr, "  carried registers differ: clip %06X/%06X vi15 %X/%X vf3 %X/%X vf26w %X/%X vf27w %X/%X\n", cr.clip, pr.clip, cr.vi15, pr.vi15, cr.vf3[0], pr.vf3[0], cr.vf26w, pr.vf26w, cr.vf27w, pr.vf27w);
        n_carry_bad++;
    }
    if (first || !getenv("NOSEED")) native_tfragclip_seed(&pr);
    first = 0;
    memcpy(nm, mem, 0x4000);
    npk = native_tfragclip_run(pc, nm, top, out, OUTCAP, pk, MAXPK);
    if (npk < 0) return -1;
    n_runs++;
    /* the program again, for its memory and registers after the run */
    pv = *v;
    prog_run(&pv, pc);
    const u8 *pm = (const u8 *)(uintptr_t)0x1100C000u;
    if (memcmp(pm, nm, 0x4000)) {
        n_mem_bad++;
        int shown = 0;
        for (u32 a = 0; a < 0x1000 && shown < 8; a++) {
            u32 x, y; memcpy(&x, nm + a * 4, 4); memcpy(&y, pm + a * 4, 4);
            if (x != y) { if (verbose || getenv("MEMV")) fprintf(stderr, "   mem qw %u word %u: native %08X program %08X\n", a / 4, a % 4, x, y); shown++; }
        }
    }
    native_tfragclip_state(&cr); regs_of(&pv, &pr);
    if (memcmp(&cr, &pr, sizeof cr)) {
        if (verbose || getenv("MEMV")) fprintf(stderr, "   state after: clip %06X/%06X vi15 %X/%X vf3 %X/%X\n", cr.clip, pr.clip, cr.vi15, pr.vi15, cr.vf3[0], pr.vf3[0]);
        n_state_bad++;
    }
    return 0;
}

/* is word w of qword q of packet p one the GS does not read (the fourth word of ST)? */
static int unread_word(const u32 *p, u32 n, u32 q, int w)
{
    u32 t = 0;
    while (t < n) {
        const u32 *g = p + t * 4;
        u32 nloop = g[0] & 0x7FFF, flg = (g[1] >> 26) & 3, nreg = g[1] >> 28;
        u64 regs = ((u64)g[3] << 32) | g[2];
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if (q == t) return 0;
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
    if (nk != npk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, npk); return 0; }
    int bad = 0, unread = 0;
    for (int i = 0; i < nk; i++) {
        const packet_t *p = &k[i];
        const u32 *d = out + pk[i].off * 4;
        if (p->n != pk[i].n) { if (verbose) fprintf(stderr, "   kick %d: %u vs %u qwords\n", i, p->n, pk[i].n); bad++; continue; }
        for (u32 q = 0; q < p->n; q++) for (int w = 0; w < 4; w++) {
            u32 a = d[q * 4 + w], b = p->d[q * 4 + w];
            if (a == b) continue;
            if (!strict && unread_word(p->d, p->n, q, w)) { unread++; continue; }
            float fa, fb; memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
            if (!exact && fabsf(fa - fb) <= 1e-6f * fabsf(fb)) continue;
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %d: native %08X program %08X\n", i, q, w, a, b);
            bad++;
        }
    }
    n_unread += unread;
    if (!bad && !unread) n_strict_ok++;
    return !bad;
}

static void report(void)
{
    fprintf(stderr, "native: %ld runs, %ld identical in every word, %ld ST fourth words differing; memory after differs in %ld runs, carried registers after in %ld; carried registers before differ from the capture in %ld\n",
            n_runs, n_strict_ok, n_unread, n_mem_bad, n_state_bad, n_carry_bad);
}
