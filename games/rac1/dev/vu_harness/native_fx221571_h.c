/* harness side of host/src/native/native_fx221571.c (the glint / sparkle effect program, image
 * 221571). Every kicked packet is compared word for word with the program's, and so are the
 * whole of VU1 data memory and the whole VU1 register file (RcVu0: vf, vi, ACC, Q, I, P, R,
 * flags, st_int / cl_int, TPC, the pipeline queues; all but q_rdy, a cycle count) after the run
 * (the program is run again here, from the same state and memory, for those). The native code
 * takes its registers from the state before each run, as the game's hook does (CARRY=1: only
 * before the first, then its own).
 * build: ./build.sh 221571 "native_fx221571_h.c ../../host/src/native/native_fx221571.c" */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "harness.h"
#include "native_fx221571.h"

#define OUTCAP (0x400 * MAXPK)
static u32 out[OUTCAP * 4];
static NativeFx221571Packet pk[MAXPK];
static int npk;
static u8 nat_mem[0x4000];
static int mem_same, regs_same;
static long n_runs, n_pk_ok, n_mem_ok, n_regs_ok, n_all_ok;
void prog_run(RcVu0 *v, u32 pc);

static void report(void)
{
    fprintf(stderr, "native: %ld runs compared; kicked packets identical in every word in %ld; VU memory after the run identical in %ld; whole register file (RcVu0) identical in %ld; all three in %ld\n",
            n_runs, n_pk_ok, n_mem_ok, n_regs_ok, n_all_ok);
}

void native_reset(void) { native_fx221571_reset(); }

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    static int seeded;
    if (!seeded) {               /* the registers the game had when the capture started */
        seeded = 1;
        atexit(report);
        NativeFx221571Regs r; native_fx221571_regs_from_vu(&r, v); native_fx221571_seed(&r);
    }
    /* as the game's hook: the registers from rc_vu1 before every run (CARRY=1: only before the
     * first, the native code's own registers carried on) */
    if (!getenv("CARRY")) { NativeFx221571Regs r; native_fx221571_regs_from_vu(&r, v); native_fx221571_seed(&r); }
    static u8 before[0x4000];
    memcpy(before, mem, 0x4000);
    npk = native_fx221571_run(pc, (u8 *)mem, top, out, OUTCAP, pk, MAXPK);
    if (npk < 0) return -1;
    memcpy(nat_mem, mem, 0x4000);
    /* the program's memory and registers after the run, for the comparison */
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
    /* the whole register file: the program's against the state before the run with the native
     * code's registers put back (q_rdy, a cycle count only read while a DIV is in flight, left out) */
    static RcVu0 nv; nv = *v;
    NativeFx221571Regs a; native_fx221571_state(&a); native_fx221571_regs_to_vu(&a, &nv);
    nv.q_rdy = pv.q_rdy = 0;
    regs_same = !memcmp(&nv, &pv, sizeof nv);
    if (!regs_same && getenv("VERBOSE_REGS")) {
        const u32 *x = (const u32 *)&nv, *y = (const u32 *)&pv;
        for (u32 k = 0; k < sizeof nv / 4; k++) if (x[k] != y[k]) fprintf(stderr, "   RcVu0 word %u: native %08X program %08X\n", k, x[k], y[k]);
    }
    return 0;
}

int native_compare(const packet_t *k, int nk, int verbose)
{
    n_runs++;
    int bad = 0;
    if (nk != npk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %d\n", nk, npk); bad++; }
    else for (int i = 0; i < nk; i++) {
        const packet_t *p = &k[i];
        const u32 *d = out + pk[i].off * 4;
        if (p->n != pk[i].n) { if (verbose) fprintf(stderr, "   kick %d: native %u qwords, program %u\n", i, pk[i].n, p->n); bad++; continue; }
        for (u32 q = 0; q < p->n * 4; q++) if (d[q] != p->d[q]) {
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %u: native %08X program %08X\n", i, q / 4, q % 4, d[q], p->d[q]);
            bad++;
        }
    }
    if (!bad) n_pk_ok++;
    if (mem_same) n_mem_ok++; else { if (verbose) fprintf(stderr, "   VU memory after the run differs\n"); bad++; }
    if (regs_same) n_regs_ok++; else { if (verbose) fprintf(stderr, "   registers differ\n"); bad++; }
    if (!bad) n_all_ok++;
    return !bad;
}
