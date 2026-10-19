/* dev/vu_harness: replays a UYA_VUCAPSEQ capture through a translated VU1 microprogram
 * and through its native replacement, and compares what they draw.
 * build: zig cc -target x86-linux-gnu -O1 -I host/include harness.c <gen>/vup_N_vu1.c native_X.c -o h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "vu.h"
#include "harness.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
static u32 cur_top, cur_itop;
u32 vu_vif_top(int vu) { (void)vu; return cur_top; }
u32 vu_vif_itop(int vu) { (void)vu; return cur_itop; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }

/* kicks: as host/src/gfx/vu.c (a kick goes when the next comes or the program ends) */
static u32 kick_pending = 0xFFFFFFFFu;
packet_t got[MAXPK]; int ngot;
static void kick_send(u32 qaddr)
{
    const u32 *mem = (const u32 *)(uintptr_t)0x1100C000u;
    u32 q = qaddr & 0x3FF, n = 0;
    packet_t *p = &got[ngot < MAXPK ? ngot++ : MAXPK - 1];
    while (n < 0x400) {
        const u32 *t = mem + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        for (u32 k = 0; k < len && n < 0x400; k++) { memcpy(p->d + n * 4, mem + ((q + k) & 0x3FF) * 4, 16); n++; }
        q = (q + len) & 0x3FF;
        if (eop) break;
    }
    p->n = n;
}
void vu_xgkick(u32 qaddr) { if (kick_pending != 0xFFFFFFFFu) kick_send(kick_pending); kick_pending = qaddr & 0x3FF; }
static void kick_flush(void) { if (kick_pending != 0xFFFFFFFFu) { u32 q = kick_pending; kick_pending = 0xFFFFFFFFu; kick_send(q); } }

typedef struct { u32 pc; RcVu0 before, after; u8 mem_before[0x4000], mem_after[0x4000]; u32 tops[2]; int nk; packet_t *k; } rec_t;

static int read_state(FILE *f, RcVu0 *v, u8 *mem, u32 *tops)
{
    return fread(v, sizeof *v, 1, f) == 1 && fread(mem, 1, 0x4000, f) == 0x4000 && fread(tops, 4, 2, f) == 2;
}

void prog_run(RcVu0 *v, u32 pc);   /* the translated program (harness.h maps it) */

extern int trace_on;
int main(int argc, char **argv)
{
    int trace_rec = getenv("TRACE_REC") ? atoi(getenv("TRACE_REC")) : -1;
    if (argc < 2) { fprintf(stderr, "usage: h vucapseq.bin [verbose]\n"); return 2; }
    int verbose = argc > 2 ? atoi(argv[2]) : 0;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);   /* round toward zero, as the game (host/src/rac1/boot.c) */
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static rec_t r; static u32 t2[2];
    int nrec = 0, same_prog = 0, same_native = 0, tried_native = 0;
    char magic[4];
    native_reset();
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        if (fread(&r.pc, 4, 1, f) != 1 || !read_state(f, &r.before, r.mem_before, r.tops)) break;
        if (fread(&r.nk, 4, 1, f) != 1) break;
        static packet_t kk[MAXPK]; r.k = kk;
        for (int i = 0; i < r.nk; i++) { packet_t *p = &kk[i < MAXPK ? i : MAXPK - 1]; if (fread(&p->n, 4, 1, f) != 1 || fread(p->d, 16, p->n, f) != p->n) return 1; }
        if (!read_state(f, &r.after, r.mem_after, t2)) break;
        nrec++;
        /* 1. the translated program, as a check of the harness itself */
        memcpy((void *)0x1100C000u, r.mem_before, 0x4000);
        rc_vu1 = r.before; cur_top = r.tops[0]; cur_itop = r.tops[1]; ngot = 0; kick_pending = 0xFFFFFFFFu;
        trace_on = (nrec - 1) == trace_rec;
        prog_run(&rc_vu1, r.pc); kick_flush();
        trace_on = 0;
        int ok = ngot == r.nk;
        for (int i = 0; ok && i < ngot; i++) ok = got[i].n == r.k[i].n && !memcmp(got[i].d, r.k[i].d, 16 * got[i].n);
        same_prog += ok;
        if (!ok && verbose > 1) {
            fprintf(stderr, "rec %d pc %04X: kicks %d vs %d\n", nrec - 1, r.pc, ngot, r.nk);
            for (int i = 0; i < ngot && i < r.nk; i++) {
                if (got[i].n != r.k[i].n) { fprintf(stderr, "  kick %d: %u vs %u qw\n", i, got[i].n, r.k[i].n); continue; }
                for (u32 q = 0; q < got[i].n * 4; q++) if (got[i].d[q] != r.k[i].d[q]) { fprintf(stderr, "  kick %d word %u: %08X vs %08X\n", i, q, got[i].d[q], r.k[i].d[q]); break; }
            }
            if (verbose > 2) exit(1);
        }
        /* 2. the native replacement, from the memory as the program found it */
        memcpy((void *)0x1100C000u, r.mem_before, 0x4000);
        int nn = native_run(r.pc, &r.before, r.tops[0], (const u8 *)0x1100C000u);
        if (nn >= 0) {
            tried_native++;
            int c = native_compare(r.k, r.nk, verbose);
            same_native += c;
            if (!c && verbose) fprintf(stderr, "record %d (pc %04X): native differs\n", nrec - 1, r.pc);
        }
    }
    printf("%d runs; translated program reproduces %d; native handled %d, matches %d\n", nrec, same_prog, tried_native, same_native);
    return 0;
}
