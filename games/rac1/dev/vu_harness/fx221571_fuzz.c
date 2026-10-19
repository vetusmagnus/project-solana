/* dev/vu_harness/fx221571_fuzz.c: the glint / sparkle effect program (VU1 image 221571) against
 * host/src/native/native_fx221571.c on runs the captures never had. It replays a capture
 * (UYA_VUCAPSEQ) run by run, the game's uploads between runs taken from the capture, and:
 *   - (setup runs) changes the setup's inputs: the matrix, vf25.x (the projection's numerator,
 *     0 too), the screen offset, the size scale, the GIF tags' / A+D qwords' words, and the
 *     registers vf12.zw / vf13.zw it multiplies by 0 (negative, inf, denormal);
 *   - (particle runs) replaces the particle buffer with a made-up one (probability PART, default
 *     0.8): 1..250 particles of every mode (0 screen quad, > 0 world quad, < 0 line, vi8 streak:
 *     the mode words' high bits random, 0x7FFF, 0x8000, -1, -3...), positions in front of and
 *     behind the camera, on w = 0, huge; sizes, colors, A+D data random; float edge values
 *     (+-0, denormals, +-MAX, +-inf; never a NaN bit pattern where the program reads a float);
 *     long buffers whose output (12 / 6 / 13 qwords a particle, from the buffer + 128) runs over
 *     input not read yet; now and then a count the walk does not hit exactly (it then reads
 *     on through VU memory, wrapping, until it does: runs the native code gives up on, after
 *     65536 particles, are not run on the program and are counted apart);
 *   - now and then moves the double buffer (vi15, vi14) and the streak marker (vi8) as if
 *     another program had left them (the native code seeded with the same registers).
 *   - now and then scrambles the registers the program does not read first (vf1..vf15, ACC,
 *     scratch vi, I, P, flags, st_int / cl_int; flags and a Q still in flight).
 * Both start from the same VU state and memory; after each run the kicked packets, the whole of
 * VU1 data memory and the whole register file (RcVu0, all but q_rdy, a cycle count) are compared
 * word for word, the native code's registers put back with native_fx221571_regs_to_vu(). It reports the
 * program addresses the runs reached (the translated program is built with VU_TRACE counting
 * them).
 * build: see fx221571_fuzz.sh. run: ./fx221571_fuzz vucapseq.bin [iterations] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "vu.h"
#include "native_fx221571.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
u32 vu_vif_top(int vu) { (void)vu; return 0; }
u32 vu_vif_itop(int vu) { (void)vu; return 0; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
void vup_221571_vu1(RcVu0 *v, u32 pc);

#define VUMEM ((u8 *)(uintptr_t)0x1100C000u)
#define MAXPK 16
typedef struct { u32 n; u32 d[0x400 * 4]; } packet_t;
static packet_t got[MAXPK]; static int ngot;
static u32 kick_pending = 0xFFFFFFFFu;
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

/* coverage of the program, a watchdog, and runs where a NaN bit pattern is read as a float
 * (only when a particle's output has run over input not read yet): the NaN a sum or product
 * keeps then depends on the order the compiler gives its operands (compared, counted apart) */
#define PROG_END 0x06C8
static u8 cov[PROG_END / 8];
static long steps; static int naninput;
static jmp_buf runaway;
static int isnan_u(u32 u) { return (u & 0x7F800000u) == 0x7F800000u && (u & 0x7FFFFFu); }
static int nan3(int r) { for (int k = 0; k < 3; k++) if (isnan_u(((u32 *)rc_vu1.vf[r])[k])) return 1; return 0; }
void fx_cov(unsigned pc)
{
    cov[(pc & 0x3FFF) / 8] = 1;
    switch (pc) {           /* where a particle's floats are first used */
    case 0x00F8: case 0x0218: case 0x03E8: case 0x0508: naninput |= nan3(1) | nan3(15); break;
    case 0x0518: naninput |= nan3(14) | isnan_u(((u32 *)rc_vu1.vf[14])[3]); break;
    }
    if (++steps > 8000000) longjmp(runaway, 1);
}

/* ---- the capture ---- */
typedef struct { u32 pc; RcVu0 before; u8 mb[0x4000], ma[0x4000]; } rec_t;
static rec_t *recs; static int nrec;
static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    char magic[4]; static RcVu0 after; u32 t2[2];
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        recs = realloc(recs, sizeof *recs * (nrec + 1));
        rec_t *r = &recs[nrec];
        if (fread(&r->pc, 4, 1, f) != 1 || fread(&r->before, sizeof r->before, 1, f) != 1 || fread(r->mb, 1, 0x4000, f) != 0x4000 || fread(t2, 4, 2, f) != 2) break;
        u32 nk; if (fread(&nk, 4, 1, f) != 1) break;
        for (u32 i = 0; i < nk; i++) { u32 n; if (fread(&n, 4, 1, f) != 1) return 0; fseek(f, 16L * n, SEEK_CUR); }
        if (fread(&after, sizeof after, 1, f) != 1 || fread(r->ma, 1, 0x4000, f) != 0x4000 || fread(t2, 4, 2, f) != 2) break;
        nrec++;
    }
    fclose(f);
    return nrec;
}

static u32 rs = 12345;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float frand(float a, float b) { return a + (b - a) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static u32 *QW(u8 *m, u32 a) { return (u32 *)(m + (a & 0x3FF) * 16); }
static u32 Ul(float f) { u32 u; memcpy(&u, &f, 4); return u; }

/* a float the program will compute with: mostly in range, sometimes an edge value */
static u32 fv(float lo, float hi)
{
    if (rnd() % 24) return Ul(frand(lo, hi));
    static const u32 edge[] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x80400000u, 0x007FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu,
                                0x7F800000u, 0xFF800000u, 0x7F000000u, 0xFE800000u, 0x3F800000u, 0x0D000000u, 0x72000000u };
    return edge[rnd() % (sizeof edge / sizeof *edge)];
}
static u32 rbits(void) { return rnd(); }
/* random bits, but not a NaN */
static u32 rnn(void) { u32 u = rnd(); if ((u & 0x7F800000u) == 0x7F800000u) u &= ~0x00800000u; return u; }

/* the setup's inputs (VU memory 0..15) */
static void mutate_setup(u8 *m, RcVu0 *pv)
{
    int kind = rnd() % 4;
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) {
        float s = kind == 0 ? 1.0f : kind == 1 ? 1e3f : kind == 2 ? 1e-3f : 1e30f;
        if (rnd() % 3) QW(m, r)[k] = rnd() % 16 ? Ul(frand(-s, s)) : fv(-1, 1);
    }
    if (rnd() % 2) QW(m, 4)[0] = rnd() % 8 ? Ul(frand(-20000, 20000)) : fv(-1, 1);
    if (rnd() % 2) for (int k = 0; k < 4; k++) QW(m, 5)[k] = rnd() % 8 ? Ul(frand(-8192, 8192)) : fv(-1, 1);
    if (rnd() % 2) for (int k = 0; k < 2; k++) QW(m, 6)[k] = rnd() % 8 ? Ul(frand(-4, 4)) : fv(-1, 1);
    for (int q = 7; q < 16; q++) for (int k = 0; k < 4; k++) if (rnd() % 8 == 0) QW(m, q)[k] = rnd() % 2 ? rnn() : rbits();
    if (rnd() % 8 == 0) { for (int q = 11; q < 14; q++) QW(m, q)[0] = 0x8001; }    /* tags with EOP: a short packet */
    /* the words the setup multiplies by 0 */
    for (int k = 2; k < 4; k++) {
        if (rnd() % 2) ((u32 *)pv->vf[12])[k] = rnd() % 3 ? fv(-5, 5) : rnn();
        if (rnd() % 2) ((u32 *)pv->vf[13])[k] = rnd() % 3 ? fv(-5, 5) : rnn();
    }
}

static long st_modes[4], n_overrun, n_inexact;
/* a made-up particle buffer at base (as the program will find it: (vi15 + 512) & vi14) */
static void make_particles(u8 *m, u32 base, u16 vi8)
{
    int np = rnd() % 4 == 0 ? 1 + rnd() % 250 : 1 + rnd() % 40;
    int style = rnd() % 6;                     /* 0..3 one mode, else mixed */
    int behind = rnd() % 5 == 0;
    u32 a = base, total = 0;
    for (int i = 0; i < np; i++) {
        int md = style < 4 ? style : (int)(rnd() % 4);
        u32 mw;
        switch (md) {
        case 0: mw = (rnd() & 0xFFFF0000u); break;
        case 1: { static const u16 pos[] = { 1, 2, 0x7FFF, 0x1234, 4 }; mw = (rnd() & 0xFFFF0000u) | (rnd() % 2 ? pos[rnd() % 5] : 1 + rnd() % 0x7FFF); break; }
        case 2: { static const u16 neg[] = { 0xFFFF, 0xFFFD, 0x8000, 0xFFFC, 0x9000 }; u16 v = rnd() % 2 ? neg[rnd() % 5] : (u16)(0x8000 | rnd()); if (v == vi8) v ^= 1; if (!(v & 0x8000)) v |= 0x8000; mw = (rnd() & 0xFFFF0000u) | v; break; }
        default: mw = (rnd() & 0xFFFF0000u) | vi8; break;
        }
        u16 m16 = (u16)mw;
        int real = m16 == 0 ? 0 : !(m16 & 0x8000) ? 1 : m16 == vi8 ? 3 : 2;
        st_modes[real]++;
        int n = real == 3 ? 6 : 4;
        for (int k = 0; k < n; k++) for (int j = 0; j < 4; j++) QW(m, a + k)[j] = rnn();
        float sc = rnd() % 3 ? 50.0f : 5000.0f;
        float zl = behind ? -sc : -sc * 0.1f, zh = behind ? sc : sc;
        u32 *p = QW(m, a);
        p[0] = fv(-sc, sc); p[1] = fv(-sc, sc); p[2] = fv(zl, zh); p[3] = mw;
        if (real == 0 || real == 1) {
            u32 *s = QW(m, a + 2); s[0] = fv(-30, 30); s[1] = fv(-30, 30); s[2] = rbits();
            if (rnd() % 3 == 0) s[1] = s[0];
        } else if (real == 2) {
            u32 *s = QW(m, a + 2); s[0] = fv(-sc, sc); s[1] = fv(-sc, sc); s[2] = fv(zl, zh);
        } else {
            u32 *e = QW(m, a + 1); e[0] = fv(-sc, sc); e[1] = fv(-sc, sc); e[2] = fv(zl, zh); e[3] = fv(0, 4);
            u32 *s = QW(m, a + 2); s[0] = fv(-10, 10); s[1] = fv(-10, 10); s[2] = fv(-10, 10);
        }
        a += n; total += n;
    }
    /* the count: the first particle's qword 2, w */
    u32 cnt = total;
    if (rnd() % 16 == 0) { cnt = total + 4 * (1 + rnd() % 8); n_inexact++; }   /* the walk reads on */
    else if (rnd() % 64 == 0) { cnt = total + 2; n_inexact++; }
    QW(m, base + 2)[3] = (rnd() & 0xFFFF0000u) | (cnt & 0xFFFF);
    if (128 + 13 * (u32)np > 1024 - 4) n_overrun++;
}

/* registers the program does not read first, as other programs might leave them: scratch
 * vf / vi / ACC, flags, and now and then flags or a Q still in flight */
static long n_scrambled, n_inflight;
static void scramble(RcVu0 *v)
{
    static int mask = -1; if (mask < 0) mask = getenv("SCR") ? atoi(getenv("SCR")) : 255;
    RcVu0 o = *v;
    for (int r = 1; r < 16; r++) for (int k = 0; k < 4; k++) ((u32 *)v->vf[r])[k] = rnd() % 4 ? rnn() : fv(-1, 1);
    for (int k = 0; k < 4; k++) ((u32 *)v->acc)[k] = rnn();
    for (int k = 1; k < 16; k++) if (k != 8 && k != 14 && k != 15) v->vi[k] = rnd() & 0xFFFF;
    v->status = rnd() & 0xFFF; v->mac = rnd() & 0xFFFF; v->clip = rnd() & 0xFFFFFF;
    v->st_int = rnd() & 0xFFF; v->cl_int = rnd() & 0xFFFFFF;
    { u32 x = rnn(); memcpy(&v->i, &x, 4); x = rnn(); memcpy(&v->p, &x, 4); }
    n_scrambled++;
    if (rnd() % 4 == 0) {
        n_inflight++;
        v->fp_h = rnd() & 3; v->fp_n = rnd() % 5; v->cp_h = rnd() & 3; v->cp_n = rnd() % 5;
        for (int k = 0; k < 4; k++) { v->fp_mac[k] = rnd() & 0xFFFF; v->fp_st[k] = rnd() & 0xFFF; v->cp_val[k] = rnd() & 0xFFFFFF; v->fp_rdy[k] = rnd(); v->cp_rdy[k] = rnd(); }
        if (rnd() % 2) { u32 x = rnn(); memcpy(&v->q_new, &x, 4); v->q_pend = 1; v->q_rdy = rnd() % 16; }
    }
    if (!(mask & 1)) memcpy(v->vf, o.vf, sizeof o.vf);
    if (!(mask & 2)) memcpy(v->acc, o.acc, 16);
    if (!(mask & 4)) memcpy(v->vi, o.vi, sizeof o.vi);
    if (!(mask & 8)) { v->status = o.status; v->mac = o.mac; v->clip = o.clip; v->st_int = o.st_int; v->cl_int = o.cl_int; v->i = o.i; v->p = o.p; }
    if (!(mask & 16)) { v->fp_h = o.fp_h; v->fp_n = o.fp_n; v->cp_h = o.cp_h; v->cp_n = o.cp_n; memcpy(v->fp_mac, o.fp_mac, 16); memcpy(v->fp_st, o.fp_st, 16); memcpy(v->cp_val, o.cp_val, 16); memcpy(v->fp_rdy, o.fp_rdy, 16); memcpy(v->cp_rdy, o.cp_rdy, 16); v->q_new = o.q_new; v->q_pend = o.q_pend; v->q_rdy = o.q_rdy; }
}

static int compare(int it, int i, u32 pc, const packet_t *k, int nk, const u32 *out, const NativeFx221571Packet *pk, int npk, const u8 *pm, const u8 *nm)
{
    int bad = 0;
    if (nk != npk) { fprintf(stderr, "iter %d run %d (pc %04X): kicks: program %d, native %d\n", it, i, pc, nk, npk); return 1; }
    for (int p = 0; p < nk; p++) {
        if (k[p].n != pk[p].n) { fprintf(stderr, "iter %d run %d: kick %d: program %u qwords, native %u\n", it, i, p, k[p].n, pk[p].n); bad++; continue; }
        const u32 *d = out + pk[p].off * 4;
        for (u32 q = 0; q < k[p].n * 4; q++) if (d[q] != k[p].d[q]) {
            if (bad < 12) fprintf(stderr, "iter %d run %d (pc %04X): kick %d qw %u word %u: program %08X native %08X\n", it, i, pc, p, q / 4, q % 4, k[p].d[q], d[q]);
            bad++;
        }
    }
    for (u32 q = 0; q < 0x1000; q++) if (((const u32 *)pm)[q] != ((const u32 *)nm)[q]) {
        if (bad < 12) fprintf(stderr, "iter %d run %d (pc %04X): mem qw %u word %u: program %08X native %08X\n", it, i, pc, q / 4, q % 4, ((const u32 *)pm)[q], ((const u32 *)nm)[q]);
        bad++;
    }
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: fx221571_fuzz vucapseq.bin [iterations] [seed]\n"); return 2; }
    int iters = argc > 2 ? atoi(argv[2]) : 100;
    rs = argc > 3 ? (u32)strtoul(argv[3], 0, 0) : 12345;
    if (!rs) rs = 1;
    float ppart = getenv("PART") ? (float)atof(getenv("PART")) : 0.8f;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);   /* round toward zero, as the game */
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    if (!load(argv[1])) return 1;
    static u8 pm[0x4000], nm[0x4000];
    static u32 out[0x400 * MAXPK * 4];
    static NativeFx221571Packet pk[MAXPK];
    static RcVu0 pv;
    int check_giveup = getenv("CHECK_GIVEUP") != NULL, carry = getenv("CARRY") != NULL; long giveup_ended = 0;
    long runs = 0, runs_ok = 0, nanruns = 0, nanruns_ok = 0, gaveup = 0, runaways = 0, nsynth = 0, nsetup = 0, nmoved = 0, pkq = 0;
    int failures = 0, cov0 = 0;
    for (int it = 0; it < iters && failures < 3; it++) {
        int mutate = it > 0;                         /* iteration 0: the capture as it is */
        pv = recs[0].before;
        memcpy(pm, recs[0].mb, 0x4000);
        native_fx221571_reset();
        for (int i = 0; i < nrec; i++) {
            rec_t *r = &recs[i];
            if (i > 0) {                              /* the game's uploads since the last run */
                const u32 *b = (const u32 *)r->mb, *a = (const u32 *)recs[i - 1].ma;
                for (u32 w = 0; w < 0x1000; w++) if (b[w] != a[w]) ((u32 *)pm)[w] = b[w];
            }
            u32 pc = r->pc;
            int reseed = i == 0;
            if (mutate && pc == 0x0000 && rnd() % 4) { mutate_setup(pm, &pv); nsetup++; reseed = 1; }
            if (mutate && pc == 0x00A0) {
                if (rnd() % 10 == 0) {                /* registers another program left */
                    pv.vi[15] = rnd() % 4 ? (rnd() & 0x3FF) : (rnd() & 0xFFFF);
                    if (rnd() % 3 == 0) pv.vi[14] = rnd() % 2 ? 0x3FF : (rnd() & 0xFFFF);
                    if (rnd() % 3 == 0) { static const u16 m8[] = { 0xFFFE, 0xFFFF, 0x8000, 0xFFFD, 0 }; pv.vi[8] = m8[rnd() % 5]; }
                    reseed = 1; nmoved++;
                }
                if (frand(0, 1) < ppart) {
                    u32 base = ((pv.vi[15] + 512) & pv.vi[14]) & 0xFFFF;
                    make_particles(pm, base, (u16)pv.vi[8]);
                    nsynth++;
                }
            }
            if (mutate && rnd() % 4 == 0) { scramble(&pv); reseed = 1; }
            /* as the game's hook: the native code takes its registers from rc_vu1 before each run
             * (CARRY=1: only when the fuzzer changed them, its own carried on otherwise) */
            if (reseed || !carry) { NativeFx221571Regs sr; native_fx221571_regs_from_vu(&sr, &pv); native_fx221571_seed(&sr); }
            RcVu0 before = pv;
            {   /* SAVE=iter,run,file: this run's registers and memory (an RcVu0, then 16 KB), to replay it */
                const char *e = getenv("SAVE"); int di, dr; char fn[512];
                if (e && sscanf(e, "%d,%d,%511s", &di, &dr, fn) == 3 && di == it && dr == i) { FILE *f = fopen(fn, "wb"); if (f) { fwrite(&pv, sizeof pv, 1, f); fwrite(pm, 1, 0x4000, f); fclose(f); } }
            }
            memcpy(nm, pm, 0x4000);
            /* the native code first: a walk it gives up on is not run on the program */
            int npk = native_fx221571_run(pc, nm, 0, out, sizeof out / 16, pk, MAXPK);
            if (npk == 0 && pc == 0x00A0) {          /* skipped: both go on from the state before it */
                gaveup++;
                if (check_giveup) {                  /* does the program end after all? (it should not) */
                    memcpy(VUMEM, pm, 0x4000);
                    rc_vu1 = pv; ngot = 0; kick_pending = 0xFFFFFFFFu; steps = -40000000L;
                    if (!setjmp(runaway)) { vup_221571_vu1(&rc_vu1, pc); kick_flush(); giveup_ended++; }
                }
                NativeFx221571Regs sr; native_fx221571_regs_from_vu(&sr, &pv); native_fx221571_seed(&sr);
                continue;
            }
            /* the program */
            memcpy(VUMEM, pm, 0x4000);
            rc_vu1 = pv; ngot = 0; kick_pending = 0xFFFFFFFFu; steps = 0; naninput = 0;
            if (setjmp(runaway)) { runaways++; failures++; fprintf(stderr, "iter %d run %d: the program runs away\n", it, i); goto next_iter; }
            vup_221571_vu1(&rc_vu1, pc); kick_flush();
            pv = rc_vu1;
            memcpy(pm, VUMEM, 0x4000);
            runs++;
            for (int p = 0; p < ngot; p++) pkq += got[p].n;
            int bad = compare(it, i, pc, got, ngot, out, pk, npk, pm, nm);
            {   /* the registers carried to the next run */
                static RcVu0 nv; nv = before;
                NativeFx221571Regs a; native_fx221571_state(&a); native_fx221571_regs_to_vu(&a, &nv);
                RcVu0 pp = pv; nv.q_rdy = pp.q_rdy = 0;      /* (a cycle count, read only while a DIV is in flight) */
                if (memcmp(&nv, &pp, sizeof nv)) {
                    const u32 *x = (const u32 *)&nv, *y = (const u32 *)&pp; int shown = 0;
                    for (u32 k = 0; k < sizeof nv / 4; k++) if (x[k] != y[k] && shown++ < 8) fprintf(stderr, "iter %d run %d: RcVu0 word %u: program %08X native %08X\n", it, i, k, y[k], x[k]);
                    bad++;
                }
            }
            if (naninput) { nanruns++; if (!bad) { nanruns_ok++; runs_ok++; continue; } fprintf(stderr, "  (NaN read as a float)\n"); goto next_iter; }
            if (bad) { failures++; fprintf(stderr, "iter %d run %d (pc %04X): %d words differ\n", it, i, pc, bad); goto next_iter; }
            runs_ok++;
        }
    next_iter:;
        if (it == 0) for (int a = 0; a < PROG_END / 8; a++) cov0 += cov[a];
    }
    int n = 0, total = 0;
    for (int a = 0; a < PROG_END / 8; a++) { total++; n += cov[a]; }
    printf("%ld runs compared, %ld identical in every word (kicked packets, all of VU memory, the whole register file); %ld packet qwords\n", runs, runs_ok, pkq);
    printf("  of them %ld with NaN bit patterns read as floats (output over input), %ld identical\n", nanruns, nanruns_ok);
    printf("%ld made-up particle buffers (particles: %ld screen quads, %ld world quads, %ld lines, %ld streaks; %ld buffers whose output runs over the input; %ld with a count the walk misses), %ld changed setups, %ld runs with moved registers\n",
           nsynth, st_modes[0], st_modes[1], st_modes[2], st_modes[3], n_overrun, n_inexact, nsetup, nmoved);
    printf("%ld runs from scrambled scratch registers / flags (%ld with flags or Q in flight)\n", n_scrambled, n_inflight);
    printf("left out: %ld walks that never end (the native code gives up after 65536 particles), %ld program runaways\n", gaveup, runaways);
    if (check_giveup) printf("  of the walks left out, %ld end on the program within 40M more instruction pairs\n", giveup_ended);
    printf("program coverage: %d of %d instruction pairs (the capture alone: %d)\n", n, total, cov0);
    if (getenv("UNCOVERED")) {
        for (int a = 0; a < PROG_END / 8;) {
            if (cov[a]) { a++; continue; }
            int b = a; while (b < PROG_END / 8 && !cov[b]) b++;
            printf("  not reached: %04X-%04X\n", a * 8, b * 8 - 8);
            a = b;
        }
    }
    return failures != 0;
}
