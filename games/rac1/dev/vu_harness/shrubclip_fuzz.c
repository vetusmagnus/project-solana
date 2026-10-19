/* dev/vu_harness/shrubclip_fuzz.c: the shrub clip program (VU1 image 912339) against
 * native_shrubclip.c on shrub classes and instances the captures never had. From the first
 * captured state (before its 0x0000 run) it runs sequences of starts: 0x0000 (now and then with
 * new constants), a class (0x0088 or 0x00A8: random strips, texture blocks, vertices) and one to
 * three instance groups (1..5 instances with transforms that put triangles across every clip
 * plane, behind the eye, on w = 0, and now and then inf / NaN / huge values), plus rare degenerate
 * cases (zero counts of A+D blocks, vertices or a strip's vertices, which make the program's 16-bit
 * loops wrap). Program and native code run from
 * the same VU memory; the kicked packets (every word), the whole VU memory after the run and the
 * carried registers (through the runs that follow) are compared. Prints which of the program's instruction addresses ran.
 * build: see shrubclip_fuzz.sh; run: ./shrubclip_fuzz vucapseq.bin [cases] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <time.h>
#include "vu.h"
#include "native_shrubclip.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
unsigned char cov_hits[0x4000 / 8];
/* branch outcomes: what ran after each delay slot (the next address or not) */
static unsigned char after_seq[0x4000 / 8], after_jump[0x4000 / 8];
static u32 cov_last = 0xFFFFFFFFu;
void cov_step(unsigned pc)
{
    cov_hits[pc >> 3] = 1;
    if (cov_last != 0xFFFFFFFFu) { if (pc == cov_last + 8) after_seq[cov_last >> 3] = 1; else after_jump[cov_last >> 3] = 1; }
    cov_last = pc;
}
static const u16 cond_branch[] = { 0x0110,0x0158,0x0198,0x0250,0x02E0,0x02F8,0x0470,0x04B0,0x0590,0x05B0,0x05C0,0x0600,0x0628,0x0640,0x0658,0x0670,0x0688,0x06A0,0x0878,0x0890,0x08A8,0x08E0,0x0928,0x09E8,0x0A00,0x0A18,0x0A50,0x0A98,0x0B60,0x0B78,0x0B90,0x0BC8,0x0C10,0x0CD0,0x0CE8,0x0D00,0x0D38,0x0D80,0x0E48,0x0E60,0x0E78,0x0EB0,0x0EF8,0x0FA8,0x0FB8,0x10A8 };
u32 vu_vif_top(int vu) { (void)vu; return 0; }
u32 vu_vif_itop(int vu) { (void)vu; return 0; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
void prog_run(RcVu0 *v, u32 pc);

#define VUMEM ((u8 *)(uintptr_t)0x1100C000u)
#define MAXK 8192
#define MAXQ 0x80000
typedef struct { u32 n, off[MAXK], len[MAXK], nq; u32 *d; } kicks_t;
static kicks_t KP, KN;
static void kadd(kicks_t *k, const u32 *src_mem, u32 qaddr, const u32 *lin, u32 nlin)
{
    if (!k->d) k->d = malloc(MAXQ * 16);
    if (k->n >= MAXK) return;
    u32 n = 0, q = qaddr & 0x3FF, base = k->nq;
    if (lin) { if (base + nlin > MAXQ) return; memcpy(k->d + base * 4, lin, nlin * 16); n = nlin; }
    else while (n < 0x400) {
        const u32 *t = src_mem + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        for (u32 j = 0; j < len && n < 0x400; j++) { if (base + n >= MAXQ) return; memcpy(k->d + (base + n) * 4, src_mem + ((q + j) & 0x3FF) * 4, 16); n++; }
        q = (q + len) & 0x3FF;
        if (eop) break;
    }
    k->off[k->n] = base; k->len[k->n] = n; k->nq += n; k->n++;
}
static u32 kick_pending = 0xFFFFFFFFu;
void vu_xgkick(u32 qaddr) { if (kick_pending != 0xFFFFFFFFu) kadd(&KP, (const u32 *)VUMEM, kick_pending, 0, 0); kick_pending = qaddr & 0x3FF; }
static void kick_flush(void) { if (kick_pending != 0xFFFFFFFFu) { u32 q = kick_pending; kick_pending = 0xFFFFFFFFu; kadd(&KP, (const u32 *)VUMEM, q, 0, 0); } }
static void nemit(const u32 *pk, u32 n) { kadd(&KN, 0, 0, pk, n); }

static u32 rs = 12345;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
/* a random word that is not an inf or NaN as a float (the program reads most words as floats;
   two NaN inputs to one FMAC operation give a result whose sign depends on the host compiler) */
static u32 rndw(void) { u32 x = rnd(); if ((x & 0x7F800000u) == 0x7F800000u) x &= ~0x00800000u; return x; }
static float frnd(float a, float b) { return a + (b - a) * (rnd() % 1000001) / 1000000.0f; }
static u32 *QW(u8 *m, u32 a) { return (u32 *)(m + (a & 0x3FF) * 16); }
static u32 Ul(float f) { u32 u; memcpy(&u, &f, 4); return u; }

static RcVu0 V;              /* the program's registers, carried from run to run */
static u8 NM[0x4000];        /* the native code's VU memory */
static long runs, bad_runs, kicks_total, clip_kicks;
static long by_pc[4];

/* one start of both; returns 1 if they agree */
static int run_both(u32 pc, int verbose)
{
    static u8 before[0x4000]; static RcVu0 Vb;
    memcpy(before, VUMEM, 0x4000); Vb = V;
    memcpy(NM, before, 0x4000);
    KP.n = KP.nq = 0; KN.n = KN.nq = 0;
    if (verbose > 2) fprintf(stderr, " run %04X\n", pc);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    cov_last = 0xFFFFFFFFu;
    prog_run(&V, pc); kick_flush();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    if (verbose > 1 && dt > 0.05) fprintf(stderr, "  pc %04X: the program took %.2f s (%u kicks)\n", pc, dt, KP.n);
    if (verbose > 2) fprintf(stderr, " native\n");
    native_shrubclip_run(pc, NM, 0, nemit);
    runs++;
    by_pc[pc == 0 ? 0 : pc == 0x88 ? 1 : pc == 0xA8 ? 2 : 3]++;
    kicks_total += KP.n;
    for (u32 i = 0; i < KP.n; i++) clip_kicks += KP.len[i] == 1;
    int ok = 1;
    if (KP.n != KN.n) { ok = 0; if (verbose) fprintf(stderr, "  pc %04X: kicks program %u native %u\n", pc, KP.n, KN.n); }
    for (u32 i = 0; ok && i < KP.n; i++) {
        if (KP.len[i] != KN.len[i]) { ok = 0; if (verbose) fprintf(stderr, "  pc %04X kick %u: %u qw, native %u\n", pc, i, KP.len[i], KN.len[i]); break; }
        for (u32 w = 0; w < KP.len[i] * 4; w++) if (KP.d[KP.off[i] * 4 + w] != KN.d[KN.off[i] * 4 + w]) {
            ok = 0; if (verbose) fprintf(stderr, "  pc %04X kick %u qw %u word %u: native %08X program %08X\n", pc, i, w / 4, w % 4, KN.d[KN.off[i] * 4 + w], KP.d[KP.off[i] * 4 + w]);
            break;
        }
    }
    if (memcmp(NM, VUMEM, 0x4000)) {
        ok = 0;
        if (verbose) for (int q = 0; q < 0x400; q++) if (memcmp(NM + q * 16, VUMEM + q * 16, 16)) {
            const u32 *a = (const u32 *)(NM + q * 16), *b = (const u32 *)(VUMEM + q * 16);
            fprintf(stderr, "  pc %04X: memory qword %d: native %08X %08X %08X %08X program %08X %08X %08X %08X\n", pc, q, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            break;
        }
    }
    /* (the carried registers are checked through the runs that follow; on a failure the native
       code takes the program's) */
    if (!ok && getenv("DUMP")) {   /* the failing run as a UYA_VUCAPSEQ record, for h_912339 */
        static FILE *df; if (!df) df = fopen(getenv("DUMP"), "wb");
        u32 z[2] = { 0, 0 };
        fwrite("VCAP", 1, 4, df); fwrite(&pc, 4, 1, df); fwrite(&Vb, sizeof Vb, 1, df); fwrite(before, 1, 0x4000, df); fwrite(z, 4, 2, df);
        fwrite(&KP.n, 4, 1, df);
        for (u32 i = 0; i < KP.n; i++) { fwrite(&KP.len[i], 4, 1, df); fwrite(KP.d + KP.off[i] * 4, 16, KP.len[i], df); }
        fwrite(&V, sizeof V, 1, df); fwrite(VUMEM, 1, 0x4000, df); fwrite(z, 4, 2, df); fflush(df);
    }
    if (!ok) {
        bad_runs++;
        native_shrubclip_seed((const u32 (*)[4])V.vf, V.vi);
    }
    return ok;
}

/* the frame's constants (qwords 0..5) */
static u32 cap_const[6][4];
/* (not mutated: the integer bias in qword 1 y; another value puts every buffer address in odd
   float bits, and the program then reads and writes garbage everywhere, with millions of kicks,
   and FMAC operations with two NaN inputs, whose result in the translated program depends on the
   host compiler's operand order) */
static void put_consts(int mutate)
{
    for (int a = 0; a < 6; a++) memcpy(QW(VUMEM, a), cap_const[a], 16);
    if (!mutate) return;
    u32 *c1 = QW(VUMEM, 1), *c2 = QW(VUMEM, 2), *c3 = QW(VUMEM, 3), *c4 = QW(VUMEM, 4), *c5 = QW(VUMEM, 5);
    c1[2] = Ul(frnd(-0.2f, 0.2f)); c1[3] = Ul(frnd(0, 300));          /* fog scale, offset */
    c2[0] = Ul(frnd(-10, 200)); c2[1] = Ul(frnd(50, 300));            /* fog clamp */
    if (rnd() % 4 == 0) { u32 x = c2[0]; c2[0] = c2[1]; c2[1] = x; }
    c3[0] = Ul(frnd(100, 2000)); c3[1] = Ul(frnd(100, 2000)); c3[2] = Ul(frnd(-9e6f, 9e6f));
    c4[0] = Ul(frnd(1000, 3000)); c4[1] = Ul(frnd(1000, 3000)); c4[2] = Ul(frnd(0, 9e6f));
    c5[0] = rnd() % 3 == 0 ? 0x8000u : rnd() % 2 ? 0x7FF0u + rnd() % 16 : rndw();   /* the clipped polygon's GIF tag */
    c5[1] = rnd() % 2 ? 0x303EC000u : rndw();
    if (mutate > 1) c1[0] = Ul(3 * 8388608.0f + 2064 + 2 * (s32)(rnd() % 32) - 32);   /* buffers moved (they drift) */
    if (rnd() % 8 == 0) { u32 *c0 = QW(VUMEM, 0); c0[2] = rnd() % 2 ? Ul((float)(rnd() % 8)) : rndw(); }   /* vf31.z (added to vf28.y) */
    if (rnd() % 10 == 0) c1[2] = 0x7FC00000u;                          /* NaN fog scale */
    if (rnd() % 10 == 0) c2[1] = 0x7F800000u;                          /* inf clamp */
}

/* a shrub class at base cb (header, strip tags, A+D blocks, positions, ST) */
static int cls_nvert;
static void put_class(u32 cb, int chaos)
{
    u8 *m = VUMEM;
    int nstrip = 1 + rnd() % 8, len[16], nad = 0, adoff[16], toff[16];
    int budget = 50;
    for (int s = 0; s < nstrip; s++) {
        int r = rnd() % 10;
        len[s] = r == 0 ? 1 + rnd() % 2 : 3 + rnd() % 9;
        if (len[s] > budget) len[s] = budget < 1 ? 1 : budget;
        budget -= len[s];
        if (budget <= 0) { nstrip = s + 1; break; }
    }
    int off = 0, first_late = rnd() % 8 == 0;
    for (int s = 0; s < nstrip; s++) {
        if (((s == 0 && !first_late) || (s > 0 && rnd() % 4 == 0) || (s == 1 && first_late)) && nad < 4) { adoff[nad++] = off; off += 5; }
        toff[s] = off; off += 1 + 3 * len[s];
    }
    if (!nad) { adoff[nad++] = off; off += 5; }
    int nv = 0; for (int s = 0; s < nstrip; s++) nv += len[s];
    int voff = 1 + nstrip + 4 * nad;
    if (voff + 2 * nv > 117) { nstrip = 1; len[0] = 3 + rnd() % 8; nv = len[0]; toff[0] = 5; nad = 1; adoff[0] = 0; voff = 1 + 1 + 4; }
    u32 *h = QW(m, cb);
    h[0] = nad; h[1] = nstrip; h[2] = nv; h[3] = voff;
    /* one degenerate thing at a time (not: a strip count of 0, a vertex count of 0 or vertices
       elsewhere, which end in 65536 strips of whatever lies there: too slow to run) */
    switch (chaos) {
    case 1: h[0] = 0; break;                   /* no A+D blocks: the copy loop wraps over all memory (the
                                                  counts after it are garbage: too slow to run; not used) */
    case 2: h[0] |= (rnd() & 0x7F00) << 16; h[1] |= (rnd() & 0x7F00) << 16; h[2] |= (rnd() & 0x7F00) << 16; break;   /* high bits ignored */
    }
    for (int s = 0; s < nstrip; s++) {
        u32 *t = QW(m, cb + 1 + s);
        t[0] = len[s] | (s == nstrip - 1 ? 0x8000 : 0) | (rnd() % 8 == 0 ? ((rnd() & 0x7F00) << 16) : 0);
        if (chaos == 5 && rnd() % 3 == 0) t[0] = (rnd() & 0x8000) | (1 + rnd() % len[s]);   /* fewer vertices than the strip has (not more: see GARBAGE) */
        if (chaos == 8 && s == nstrip - 1) t[0] = 0x8000;   /* 0: 65535 vertices, through all memory */
        t[1] = 0x303E4000u; t[2] = 0x412; t[3] = toff[s];
    }
    for (int a = 0; a < nad; a++) for (int r = 0; r < 4; r++) {
        u32 *q = QW(m, cb + 1 + nstrip + a * 4 + r);
        q[0] = rndw(); q[1] = rnd() & 0xFFFF; q[2] = rnd() % 0x63; q[3] = r == 0 ? (u32)adoff[a] : (rnd() % 4 == 0 ? rndw() : 0);
    }
    int v = 0;
    for (int s = 0; s < nstrip; s++) for (int k = 0; k < len[s]; k++, v++) {
        u32 *p = QW(m, cb + voff + v), *st = QW(m, cb + voff + nv + v);
        p[0] = (u32)(s32)(rnd() % 4001) - 2000; p[1] = (u32)(s32)(rnd() % 4001) - 2000; p[2] = (u32)(s32)(rnd() % 4001) - 2000;
        p[3] = toff[s] + 1 + 3 * k;
        if (chaos == 6 && rnd() % 4 == 0) p[3] = rnd() % 168;   /* slots elsewhere */
        st[0] = rnd() % 16384; st[1] = rnd() % 16384; st[2] = rnd() % 4 ? 4096 : rnd() % 8192;
        st[3] = rnd() % 24 | (rnd() % 4 == 0 ? 0x8000u : 0) | (rnd() % 8 == 0 ? (rnd() & 0x7F00) << 16 : 0);
    }
    cls_nvert = nv;
}

/* an instance group at grp */
static void put_group(u32 grp, int one)
{
    u8 *m = VUMEM;
    int n = one ? 1 : 1 + rnd() % 5;
    QW(m, grp)[0] = n | (rnd() % 4 == 0 ? (rnd() & 0x7F00) << 16 : 0);
    for (int i = 0; i < n; i++) {
        u32 b = grp + 1 + 28 * i;
        float R[4][4];
        int mode = rnd() % 10;
        float s = mode < 2 ? 0.02f : mode < 8 ? 0.15f : 1.0f;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) R[r][c] = frnd(-s, s);
        for (int r = 0; r < 3; r++) R[r][3] = frnd(-s, s) * (mode == 9 ? 2.0f : 0.3f);
        float w = mode == 8 ? frnd(-50, 50) : frnd(30, 400);
        R[3][0] = frnd(-1.5f, 1.5f) * w; R[3][1] = frnd(-1.5f, 1.5f) * w; R[3][2] = frnd(-1.8f, 1.0f) * w; R[3][3] = w;
        if (mode == 7) { for (int c = 0; c < 4; c++) R[0][c] = R[1][c] = R[2][c] = 0; R[3][2] = -2 * w; }   /* all on one point */
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) QW(m, b + r)[c] = Ul(R[r][c]);
        if (rnd() % 30 == 0) QW(m, b + rnd() % 4)[rnd() % 3] = rnd() % 2 ? 0x7F800000u : 0x7FC00001u | (rnd() & 0x80000000u);
        if (rnd() % 30 == 0) QW(m, b + rnd() % 4)[rnd() % 4] = Ul(frnd(-1, 1) * 3e38f);
        if (rnd() % 40 == 0) QW(m, b + 3)[3] = 0;               /* w = 0 at the origin */
        if (rnd() % 40 == 0) QW(m, b + rnd() % 3)[rnd() % 4] = rnd() & 0x807FFFFFu;   /* denormal */
        for (int c = 0; c < 24; c++) {
            u32 *q = QW(m, b + 4 + c);
            q[0] = rnd() % 256; q[1] = rnd() % 256; q[2] = rnd() % 256; q[3] = c == 0 ? 0x80 : rndw();
            if (rnd() % 20 == 0) q[rnd() % 3] = rndw();
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: shrubclip_fuzz vucapseq.bin [cases] [seed]\n"); return 2; }
    long ncases = argc > 2 ? atol(argv[2]) : 2000;
    if (argc > 3) rs = (u32)strtoul(argv[3], 0, 0) | 1;
    int verbose = getenv("V") ? atoi(getenv("V")) : 1, garbage = getenv("GARBAGE") != NULL;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    char magic[4]; u32 pc;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "VCAP", 4) || fread(&pc, 4, 1, f) != 1 || fread(&V, sizeof V, 1, f) != 1 || fread(VUMEM, 1, 0x4000, f) != 0x4000) { fprintf(stderr, "bad capture\n"); return 1; }
    fclose(f);
    for (int a = 0; a < 6; a++) memcpy(cap_const[a], QW(VUMEM, a), 16);
    native_shrubclip_reset();
    native_shrubclip_seed((const u32 (*)[4])V.vf, V.vi);
    long fails = 0;
    put_consts(0);
    fails += !run_both(0x0000, verbose);
    for (long c = 0; c < ncases; c++) {
        /* degenerate cases: 2 high bits in the counts, 5 strips shorter than their data, 7 buffers
           moved; with GARBAGE=1 instead 8 a strip of 0 (65535 vertices), 6 vertex slots elsewhere,
           where the program reads its own integer output (XYZ, ADC words: NaNs as floats) as
           positions and ST, see rndw() */
        int chaos = rnd() % 25 == 0 ? (garbage ? (int[]){ 6, 8 }[rnd() % 2] : (int[]){ 2, 5, 7 }[rnd() % 3]) : 0;
        if (verbose > 2) fprintf(stderr, "case %ld chaos %d\n", c, chaos);
        if (chaos == 7) { put_consts(2); fails += !run_both(0x0000, verbose); }
        else if (rnd() % 8 == 0) { put_consts(rnd() % 3 != 0); fails += !run_both(0x0000, verbose); }
        u32 cb = (122 - (V.vi[11] & 0xFFFF)) & 0xFFFF;
        put_class(cb, chaos);
        fails += !run_both(rnd() % 2 ? 0x0088 : 0x00A8, verbose);
        int ng = chaos == 8 ? 1 : 1 + rnd() % 3;
        for (int g = 0; g < ng; g++) {
            u32 grp = (617 - (QW(VUMEM, 1)[1] & 0xFFFF)) & 0xFFFF;
            put_group(grp, chaos == 8);
            if (!run_both(0x0318, verbose)) { fails++; if (verbose) fprintf(stderr, "case %ld group %d (class %u verts, chaos %d)\n", c, g, cls_nvert, chaos); }
        }
        if (chaos) {   /* after a degenerate case: start again from the program's state */
            native_shrubclip_seed((const u32 (*)[4])V.vf, V.vi);
            put_consts(0); fails += !run_both(0x0000, verbose);
        }
    }
    int hit = 0, total = 0, miss_shown = 0;
    for (u32 a = 0; a < 0x1268; a += 8) { total++; hit += cov_hits[a / 8]; }
    printf("%ld runs (0x0000 %ld, 0x0088 %ld, 0x00A8 %ld, 0x0318 %ld), %ld kicks (%ld one-qword clip tags), %ld differ; program addresses run: %d of %d\n",
           runs, by_pc[0], by_pc[1], by_pc[2], by_pc[3], kicks_total, clip_kicks, bad_runs, hit, total);
    for (u32 a = 0; a < 0x1268; a += 8) if (!cov_hits[a / 8] && miss_shown++ < 40) printf(" not run: %04X\n", a);
    int nb = sizeof cond_branch / sizeof cond_branch[0], both = 0;
    for (int i = 0; i < nb; i++) {
        u32 d = (cond_branch[i] + 8) >> 3;
        both += after_seq[d] && after_jump[d];
        if (!(after_seq[d] && after_jump[d])) printf(" branch at %04X: %s only\n", cond_branch[i], after_jump[d] ? "taken" : after_seq[d] ? "not taken" : "never run, neither");
    }
    printf("conditional branches seen both taken and not taken: %d of %d\n", both, nb);
    return fails != 0;
}
