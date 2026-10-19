/* harness side of host/src/native/native_fx57843.c (effect strips, image 57843).
 * Every kicked packet is compared word for word with the program's, and so are the whole of
 * VU1 data memory after the run and the whole VU1 register state (RcVu0: vf, vi, acc, q, i, p,
 * r, clip, status, mac, the internal st_int / cl_int, tpc, cmsar0, the pipeline counters and
 * q_new; not the pipeline queues' contents fp_rdy fp_mac fp_st cp_rdy cp_val and q_rdy, which are
 * cycle bookkeeping and empty after a run). The program is run again here, from the same state and
 * memory, for that. The native code is seeded from each run's VU state (as the game does: other
 * programs share the registers).
 * build: ./build.sh 57843 "native_fx57843_h.c ../../host/src/native/native_fx57843.c" */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "harness.h"
#include "native_fx57843.h"

#define OUTCAP (0x400 * MAXPK)
static u32 out[OUTCAP * 4];
static NativeFx57843Packet pk[MAXPK];
static int npk;
static u8 nat_mem[0x4000];
static int mem_same, regs_same;
static long n_runs, n_strict, n_mem_ok, n_regs_ok;
void prog_run(RcVu0 *v, u32 pc);

static void report(void)
{
    fprintf(stderr, "native: %ld runs compared; %ld identical in every word; VU memory after the run identical in %ld; whole VU register state identical in %ld\n",
            n_runs, n_strict, n_mem_ok, n_regs_ok);
}

/* the RcVu0 fields compared (all but the pipeline queues' contents) */
#define FLD(f) { offsetof(RcVu0, f), sizeof(((RcVu0 *)0)->f), #f }
static const struct { size_t off, len; const char *name; } fld[] = {
    FLD(vf), FLD(acc), FLD(q), FLD(i), FLD(p), FLD(r), FLD(vi), FLD(clip), FLD(status), FLD(mac),
    FLD(cmsar0), FLD(tpc), FLD(susp), FLD(st_int), FLD(cl_int), FLD(fp_n), FLD(fp_h), FLD(cp_n), FLD(cp_h),
    FLD(q_pend), FLD(q_new) };
int fx57843_regs_differ(const RcVu0 *a, const RcVu0 *b, int show)
{
    int bad = 0;
    for (unsigned k = 0; k < sizeof fld / sizeof *fld; k++) {
        const u8 *x = (const u8 *)a + fld[k].off, *y = (const u8 *)b + fld[k].off;
        for (size_t w = 0; w < fld[k].len; w += 4) if (memcmp(x + w, y + w, 4)) {
            u32 u, v; memcpy(&u, x + w, 4); memcpy(&v, y + w, 4);
            if (show && bad < 8) fprintf(stderr, "   %s word %u: native %08X program %08X\n", fld[k].name, (unsigned)(w / 4), u, v);
            bad++;
        }
    }
    return bad;
}

void native_reset(void) { native_fx57843_reset(); atexit(report); }

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    NativeFx57843Regs r;
    native_fx57843_regs_from_vu(&r, v);
    native_fx57843_seed(&r);
    static u8 before[0x4000];
    memcpy(before, mem, 0x4000);
    npk = native_fx57843_run(pc, (u8 *)mem, top, out, OUTCAP, pk, MAXPK);
    if (npk < 0) return -1;
    static RcVu0 nv; nv = *v;
    native_fx57843_state(&r);
    native_fx57843_regs_to_vu(&r, &nv);
    memcpy(nat_mem, mem, 0x4000);
    /* the program's memory and registers after the run, for the comparison */
    static RcVu0 pv; pv = *v;
    memcpy((void *)mem, before, 0x4000);
    prog_run(&pv, pc);
    mem_same = !memcmp(nat_mem, mem, 0x4000);
    regs_same = !fx57843_regs_differ(&nv, &pv, getenv("VERBOSE_MEM") != NULL);
    if (!mem_same && getenv("VERBOSE_MEM")) {
        int shown = 0;
        for (u32 q = 0; q < 0x400 && shown < 8; q++) for (int w = 0; w < 4; w++) {
            u32 a = ((const u32 *)nat_mem)[q * 4 + w], b = ((const u32 *)mem)[q * 4 + w];
            if (a != b && shown++ < 8) fprintf(stderr, "   mem qw %u word %d: native %08X program %08X\n", q, w, a, b);
        }
    }
    return 0;
}

int native_compare(const packet_t *k, int nk, int verbose)
{
    n_runs++;
    if (nk != npk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, npk); return 0; }
    int bad = 0;
    for (int i = 0; i < nk; i++) {
        const packet_t *p = &k[i];
        const u32 *d = out + pk[i].off * 4;
        if (p->n != pk[i].n) { if (verbose) fprintf(stderr, "   kick %d: native %u qwords, program %u\n", i, pk[i].n, p->n); bad++; continue; }
        for (u32 q = 0; q < p->n * 4; q++) if (d[q] != p->d[q]) {
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %u: native %08X program %08X\n", i, q / 4, q % 4, d[q], p->d[q]);
            bad++;
        }
    }
    if (!mem_same) { if (verbose) fprintf(stderr, "   VU memory after the run differs\n"); bad++; }
    else n_mem_ok++;
    if (!regs_same) { if (verbose) fprintf(stderr, "   VU registers after the run differ\n"); bad++; }
    else n_regs_ok++;
    if (!bad) n_strict++;
    return !bad;
}
