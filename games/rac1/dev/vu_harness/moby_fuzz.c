/* dev/vu_harness/moby_fuzz.c: the moby program (VU1 image 13859) against native_moby.c on
 * strips the captures never had. For each captured run at 0x0050 / 0x0070 it makes variants:
 * new random strips over the run's own vertices (strip starts, texture switches with their
 * A+D blocks, negative counts that flip the winding, culling on either winding), and moved
 * clip planes so that triangles get clipped. Both run from the same VU state and memory; the
 * kicked packets and the whole VU memory after the run are compared word for word.
 * build: see moby_fuzz.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "vu.h"
#include "harness.h"
#include "native_moby.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
int trace_on;
static u32 cur_top;
u32 vu_vif_top(int vu) { (void)vu; return cur_top; }
u32 vu_vif_itop(int vu) { (void)vu; return 0; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
static u32 kick_pending = 0xFFFFFFFFu;
static packet_t got[MAXPK]; static int ngot;
#define VUMEM ((u8 *)(uintptr_t)0x1100C000u)
static void kick_send(u32 qaddr)
{
    const u32 *mem = (const u32 *)VUMEM;
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
void prog_run(RcVu0 *v, u32 pc);

static u32 rs = 12345;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 *QW(u8 *m, u32 a) { return (u32 *)(m + (a & 0x3FF) * 16); }
static float Fl(u32 u) { float f; memcpy(&f, &u, 4); return f; }
static u32 Ul(float f) { u32 u; memcpy(&u, &f, 4); return u; }

/* a new strip over vertices 1..nv in the packet at top, with its texture table */
static void make_strip(u8 *m, u32 top, int nv, int clipping)
{
    u32 P = top + 0x12F;
    int room = 0x164 - 0x12F - 2;                  /* qwords for the strip and the table */
    int nsw = rnd() % 8, L = 8 + rnd() % 120, blk[10], nblk = 0;
    int tabq = 0;                                  /* table qwords */
    for (int j = 0; j < nsw + 1; j++) {
        int n = clipping ? 1 + rnd() % 5 : 4;
        blk[nblk++] = n; tabq += n;
    }
    if (tabq < nsw + 2) tabq = nsw + 2;
    int maxe = (room - tabq) * 4 - 4;
    if (L > maxe - 4) L = maxe - 4;
    if (L < 8) L = 8;
    s32 e[600]; int n = 0;
    int first_switch = rnd() % 2;
    e[n++] = first_switch ? 0 : -(s32)(128 - (1 + rnd() % nv));
    e[n++] = -(s32)(128 - (1 + rnd() % nv));
    e[n++] = 1 + rnd() % nv;
    int sw_used = first_switch;
    while (n < L) {
        u32 r = rnd() % 100;
        if (r < 14 && sw_used < nsw) { e[n++] = 0; sw_used++; }
        else if (r < 20) e[n++] = -(s32)(128 - (1 + rnd() % nv));
        else e[n++] = 1 + rnd() % nv;
    }
    if (n < 4) e[n++] = 1;
    e[n++] = 1; e[n++] = 1; e[n++] = 1; e[n++] = 0;  /* padding, the end */
    /* the words: four in 0x12E, the rest from 0x12F */
    for (int k = 0; k < n; k++) { u32 a = k < 4 ? P - 1 : P + (k - 4) / 4; QW(m, a)[k < 4 ? k : (k - 4) & 3] = (u32)e[k]; }
    int sq = (n - 4 + 3) / 4;
    for (int k = n; k < 4 + sq * 4; k++) QW(m, P + (k - 4) / 4)[(k - 4) & 3] = 1;
    u32 T = P + sq;                                /* the texture table */
    u32 h0y = T - (top + 0x12D);
    /* the switch vertices: the first in the header, the others in the table's w words */
    u32 swv[10]; for (int j = 0; j < 10; j++) swv[j] = 1 + rnd() % nv;
    u32 *h0 = QW(m, top + 0x12D);
    h0[1] = h0y;
    u32 a = T;
    for (int j = 0; j < nblk; j++) {
        for (int k = 0; k < blk[j]; k++) {
            u32 *q = QW(m, a + k);
            q[0] = rnd(); q[1] = rnd();
            float reg = (float)(0x06 + (rnd() % 0x30));
            u32 z = Ul(reg);
            if (k == 0) {
                if (clipping) {
                    u32 cnt = blk[j];
                    if (rnd() % 3 == 0) cnt = (cnt - 256) & 0xFFFF;   /* flips the winding */
                    z = (z & 0xFFFF0000u) | cnt;
                } else z = (z & 0xFFFF0000u) | 4;
            }
            q[2] = z;
        }
        a += blk[j];
    }
    /* the switch vertices in turn: the header's z, then the table's w words; a 0 ends */
    u32 val[12]; for (int j = 0; j < sw_used; j++) val[j] = swv[j]; val[sw_used] = 0;
    h0[2] = val[0];
    for (int j = 0; j < sw_used; j++) QW(m, T + j)[3] = val[j + 1];
}

typedef struct { u32 pc; RcVu0 v; u8 mem[0x4000]; u32 top; } rec_t;

static int cmp_run(rec_t *r, int verbose, const char *what)
{
    static u8 mem_prog[0x4000];
    static NativeMobyKicks K;
    /* the program */
    memcpy(VUMEM, r->mem, 0x4000);
    rc_vu1 = r->v; cur_top = r->top; ngot = 0; kick_pending = 0xFFFFFFFFu;
    prog_run(&rc_vu1, r->pc); kick_flush();
    trace_on = 0;
    memcpy(mem_prog, VUMEM, 0x4000);
    /* native */
    memcpy(VUMEM, r->mem, 0x4000);
    u32 vf27[4]; memcpy(vf27, r->v.vf[27], 16);
    native_moby_seed(vf27, ((const u32 *)r->v.vf[12])[3]);
    int rc = native_moby_run(r->pc, VUMEM, r->top, &K);
    if (rc < 0) return -1;
    int bad = 0;
    if ((int)K.npk != ngot) { if (verbose) fprintf(stderr, "%s: kicks %u, program %d\n", what, K.npk, ngot); return 0; }
    for (int i = 0; i < ngot; i++) {
        if (K.len[i] != got[i].n) { if (verbose) fprintf(stderr, "%s: kick %d %u qw, program %u\n", what, i, K.len[i], got[i].n); bad++; continue; }
        for (u32 q = 0; q < got[i].n * 4; q++) if (K.d[K.off[i] * 4 + q] != got[i].d[q]) {
            if (verbose && bad < 4) fprintf(stderr, "%s: kick %d qw %u word %u: %08X, program %08X\n", what, i, q / 4, q % 4, K.d[K.off[i] * 4 + q], got[i].d[q]);
            bad++;
        }
    }
    for (u32 q = 0; q < 0x1000; q++) {
        if (q / 4 >= 968 && q / 4 <= 978) continue;   /* the clipper's register save area */
        u32 a = ((u32 *)VUMEM)[q], b = ((u32 *)mem_prog)[q];
        if (a != b) { if (verbose && bad < 6) fprintf(stderr, "%s: memory qw %03X word %u: %08X, program %08X\n", what, q / 4, q % 4, a, b); bad++; }
    }
    return !bad;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: moby_fuzz vucapseq.bin [variants per run] [mode bits] [verbose]\n"); return 2; }
    int only50 = getenv("ONLY50") != NULL;
    int nvar = argc > 2 ? atoi(argv[2]) : 20, mode = argc > 3 ? strtol(argv[3], 0, 0) : 0xF, verbose = argc > 4 ? atoi(argv[4]) : 1;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static rec_t r, base; static u8 junk[0x4000]; static RcVu0 vj;
    char magic[4]; int nrec = 0;
    long tried = 0, same = 0, skipped = 0;
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        u32 tops[2], t2[2]; int nk;
        if (fread(&base.pc, 4, 1, f) != 1 || fread(&base.v, sizeof base.v, 1, f) != 1 || fread(base.mem, 1, 0x4000, f) != 0x4000 || fread(tops, 4, 2, f) != 2) break;
        if (fread(&nk, 4, 1, f) != 1) break;
        for (int i = 0; i < nk; i++) { u32 n; if (fread(&n, 4, 1, f) != 1 || fread(junk, 16, n, f) != n) return 1; }
        if (fread(&vj, sizeof vj, 1, f) != 1 || fread(junk, 1, 0x4000, f) != 0x4000 || fread(t2, 4, 2, f) != 2) break;
        nrec++;
        base.top = tops[0];
        if (base.pc != 0x50 && base.pc != 0x70) continue;
        if (only50 && base.pc != 0x50) continue;
        int clipping = base.pc == 0x50;
        /* the vertices the packet has: the highest the strip names */
        int nv = 1;
        {
            u32 P = base.top + 0x12F;
            for (int k = 0; k < 120; k++) {
                s32 w = (s32)QW(base.mem, k < 4 ? P - 1 : P + (k - 4) / 4)[k < 4 ? k : (k - 4) & 3];
                s32 i = w > 0 ? w : w < 0 ? w + 128 : 0;
                if (i > nv && i < 96) nv = i;
            }
        }
        for (int t = 0; t < nvar; t++) {
            r = base;
            rs = 0x9E3779B9u ^ (nrec * 7919u) ^ (t * 104729u); rnd(); rnd();
            char what[64]; snprintf(what, sizeof what, "rec %d pc %02X var %d", nrec - 1, r.pc, t);
            u32 B = r.top - 356;
            if ((mode & 1) && t % 2 == 1) make_strip(r.mem, r.top, nv, clipping);
            if (clipping && (mode & 2)) {          /* culling: either winding, or none */
                u32 c = rnd() % 3; QW(r.mem, r.top + 0x12D)[0] = c == 0 ? 0 : c == 1 ? 2 : 0xFFFFFFFEu;
            }
            if (clipping && (mode & 4) && t % 4 >= 2) {   /* clip planes moved in: triangles to clip */
                float *cs = (float *)QW(r.mem, B + 650);
                float kx = (float)(1u << (rnd() % 7)) * (1.0f + (float)(rnd() % 100) / 100.0f);
                float ky = (float)(1u << (rnd() % 7)) * (1.0f + (float)(rnd() % 100) / 100.0f);
                cs[0] *= kx; cs[1] *= ky;
                if (rnd() % 2 == 0) {   /* near / far planes through the moby: z against w */
                    const float *M = (const float *)QW(r.mem, B + 653);
                    const s32 *pv = (const s32 *)QW(r.mem, r.top + (rnd() % nv));
                    float pz = M[2] * pv[0] + M[6] * pv[1] + M[10] * pv[2] + M[14];
                    float pw = M[3] * pv[0] + M[7] * pv[1] + M[11] * pv[2] + M[15];
                    if (pz != 0.0f) {
                        float ratio = pw * cs[3] / pz; if (ratio < 0) ratio = -ratio;
                        cs[2] = ratio * (0.5f + (float)(rnd() % 100) / 100.0f) * (rnd() % 2 ? 1.0f : -1.0f);
                    }
                }
            }
            if (clipping && (mode & 8) && t % 8 == 7) { /* the guard band too */
                float *g = (float *)QW(r.mem, B + 649);
                g[0] = g[1] = 1.0f + (float)(rnd() % 4);
            }
            if (getenv("FUZZ_REC") && (atoi(getenv("FUZZ_REC")) != nrec - 1 || atoi(getenv("FUZZ_VAR")) != t)) continue;
            trace_on = getenv("FUZZ_REC") != NULL;
            int c = cmp_run(&r, verbose, what);
            if (c < 0) { skipped++; continue; }
            tried++; same += c;
            if (!c && verbose > 1) exit(1);
        }
    }
    printf("%d runs read; %ld variants compared, %ld identical (kicks and VU memory); %ld not handled natively\n", nrec, tried, same, skipped);
    (void)Fl;
    return 0;
}
