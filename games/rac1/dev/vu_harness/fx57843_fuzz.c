/* dev/vu_harness/fx57843_fuzz.c: the effect strip program (VU1 image 57843) against
 * host/src/native/native_fx57843.c on runs the captures never had. Each iteration takes a
 * captured run's VU state and memory (UYA_VUCAPSEQ) and:
 *   - (clipped strips, 0x0070 / 0x0068) makes up a strip in clip space that wanders across the
 *     near plane, the four sides and the clipper's x3 test (whole runs of vertices outside one
 *     plane, points on the planes, w <= 0), with a clip space matrix of its own (or the
 *     captured one, the captured vertices moved), random s t q, colors, carried clip flags,
 *     vf26 / vf27, STQ w registers (vf6.w, vf13.w), fog clamps, screen scale, fan GIF tag;
 *   - (plain strips, 0x0060 / 0x0078) random vertices through the captured screen matrix or
 *     a random one, random Q numerators (vf31.w, vf29.w);
 *   - (setup, 0x0000) random 932..943;
 *   - counts of 0..3 (the 16 bit counters run 65536 more vertices) now and then, TOP anywhere
 *     (the strip over the clipper's buffers, VU memory wrapping).
 * Both start from the same VU state and memory; after each run the kicked packets, the whole of
 * VU1 data memory and the whole VU1 register state (RcVu0 but the pipeline queues' contents) are
 * compared word for word. Runs where an FMAC result is a NaN (NaN bit patterns read as floats)
 * are counted apart. It reports the program addresses the runs reached.
 * build: see fx57843_fuzz.sh. run: ./fx57843_fuzz vucapseq.bin [iterations] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "vu.h"
#include "native_fx57843.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
static u32 cur_top;
u32 vu_vif_top(int vu) { (void)vu; return cur_top; }
u32 vu_vif_itop(int vu) { (void)vu; return 0; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
void vup_57843_vu1(RcVu0 *v, u32 pc);

#define VUMEM ((u8 *)(uintptr_t)0x1100C000u)
#define MAXPK 512
typedef struct { u32 n; u32 d[0x400 * 4]; } packet_t;
static packet_t got[MAXPK]; static int ngot; static long nkicks;
static u32 kick_pending = 0xFFFFFFFFu;
static void kick_send(u32 qaddr)
{
    const u32 *mem = (const u32 *)VUMEM;
    u32 q = qaddr & 0x3FF, n = 0;
    nkicks++;
    if (ngot >= MAXPK) return;
    packet_t *p = &got[ngot++];
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

/* coverage, and the hooks' flags (fx57843_cov.h) */
#define NPAIR (0x1260 / 8)
static u8 cov[NPAIR], cov_run[NPAIR];
int fz_nan, fz_stale;
/* the conditional branches (address, target): each way counted (from the delay slot) */
static const u32 br[][2] = {
    {0x0208, 0x0388}, {0x0268, 0x01B8}, {0x0650, 0x0710}, {0x0660, 0x06F0}, {0x06D0, 0x0778}, {0x06E0, 0x05F0}, {0x0718, 0x0660}, {0x0728, 0x0660},
    {0x0738, 0x0660}, {0x0748, 0x0660}, {0x0758, 0x0660}, {0x0780, 0x06E0}, {0x0790, 0x06E0}, {0x07A0, 0x06E0}, {0x07B0, 0x06E0}, {0x07C0, 0x06E0},
    {0x07E0, 0x0800}, {0x0818, 0x0838}, {0x08A0, 0x10F0}, {0x08B0, 0x10F0}, {0x08C0, 0x10F0}, {0x08D0, 0x10F0}, {0x0A68, 0x0AA8}, {0x0A78, 0x1158},
    {0x0A88, 0x11E0}, {0x0AC0, 0x09F0}, {0x0AE0, 0x10F0}, {0x0BA8, 0x0BE8}, {0x0BB8, 0x1158}, {0x0BC8, 0x11E0}, {0x0C00, 0x0B30}, {0x0C20, 0x10F0},
    {0x0CF0, 0x0D30}, {0x0D00, 0x1158}, {0x0D10, 0x11E0}, {0x0D48, 0x0C70}, {0x0D68, 0x10F0}, {0x0E30, 0x0E70}, {0x0E40, 0x1158}, {0x0E50, 0x11E0},
    {0x0E88, 0x0DB8}, {0x0EA8, 0x10F0}, {0x0F78, 0x0FB8}, {0x0F88, 0x1158}, {0x0F98, 0x11E0}, {0x0FD0, 0x0EF8}, {0x0FF0, 0x10F0}, {0x10D8, 0x1068} };
#define NBR (int)(sizeof br / sizeof *br)
static u8 br_way[NBR][2];
static unsigned last_pc;
void fz_cov(unsigned pc)
{
    if (pc / 8 < NPAIR) cov_run[pc / 8] = 1;
    for (int i = 0; i < NBR; i++) if (last_pc == br[i][0] + 8) { if (pc == br[i][1]) br_way[i][1] = 1; else if (pc == br[i][0] + 16) br_way[i][0] = 1; }
    last_pc = pc;
}

/* ---- the capture ---- */
typedef struct { u32 pc; RcVu0 before; u8 mb[0x4000]; u32 tops[2]; } rec_t;
static rec_t *recs; static int nrec;
static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    char magic[4]; static RcVu0 after; static u8 ma[0x4000]; u32 t2[2];
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        recs = realloc(recs, sizeof *recs * (nrec + 1));
        rec_t *r = &recs[nrec];
        if (fread(&r->pc, 4, 1, f) != 1 || fread(&r->before, sizeof r->before, 1, f) != 1 || fread(r->mb, 1, 0x4000, f) != 0x4000 || fread(r->tops, 4, 2, f) != 2) break;
        u32 nk; if (fread(&nk, 4, 1, f) != 1) break;
        for (u32 i = 0; i < nk; i++) { u32 n; if (fread(&n, 4, 1, f) != 1) return 0; fseek(f, 16L * n, SEEK_CUR); }
        if (fread(&after, sizeof after, 1, f) != 1 || fread(ma, 1, 0x4000, f) != 0x4000 || fread(t2, 4, 2, f) != 2) break;
        nrec++;
    }
    fclose(f);
    return nrec;
}

static u32 rs = 12345;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float frand(float a, float b) { return a + (b - a) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static int chance(int pct) { return (int)(rnd() % 100) < pct; }
static u32 *QW(u8 *m, u32 a) { return (u32 *)(m + (a & 0x3FF) * 16); }
static u32 Ul(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static float Fl(u32 u) { float f; memcpy(&f, &u, 4); return f; }
static void setf(u32 *w, float a, float b, float c, float d) { w[0] = Ul(a); w[1] = Ul(b); w[2] = Ul(c); w[3] = Ul(d); }
/* no NaN / inf bit patterns in VU memory or the float registers to start with */
static void sanitize(u32 *w, u32 n) { for (u32 i = 0; i < n; i++) if ((w[i] & 0x7F800000u) == 0x7F800000u) w[i] ^= 0x40000000u; }
static float some_float(void)
{
    switch (rnd() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return frand(-1e-3f, 1e-3f);
    case 3: return frand(-1e6f, 1e6f);
    default: return frand(-300.0f, 300.0f);
    }
}

/* ---- a made-up clipped strip ---- */
static void make_clipped(u8 *m, RcVu0 *v, u32 top, u32 n)
{
    u32 (*vf)[4] = (u32 (*)[4])v->vf;
    float tx = 0, ty = 0, d = frand(0.2f, 4.0f);
    int own = !chance(15);
    if (own) {                                  /* P = (x + tx, y + ty, z - d, z) */
        tx = chance(50) ? 0.0f : frand(-5, 5); ty = chance(50) ? 0.0f : frand(-5, 5);
        setf(vf[22], 1, 0, 0, 0); setf(vf[23], 0, 1, 0, 0); setf(vf[24], 0, 0, 1, 1); setf(vf[25], tx, ty, -d, 0);
        if (chance(20)) for (int r = 22; r <= 25; r++) for (int k = 0; k < 4; k++) vf[r][k] = Ul(Fl(vf[r][k]) + frand(-0.05f, 0.05f));
    }
    float W = frand(0.1f, 6), u = frand(-1, 1), w2 = frand(-1, 1);
    int jumpy = chance(30);
    for (u32 k = 0; k < n + 3; k++) {
        u32 b = top + 1 + 3 * k;
        /* the next point: a walk with jumps (runs of vertices outside one plane) */
        if (jumpy || chance(25)) {
            switch (rnd() % 6) {
            case 0: W = frand(0.05f, d); break;                       /* near the near plane */
            case 1: W = frand(-2, 0.3f); break;                       /* behind */
            default: W = frand(0.1f, 8); break;
            }
            int r = rnd() % 10;
            u = r < 4 ? frand(-0.3f, 0.3f) : r < 8 ? frand(-3, 3) : (rnd() & 1 ? 1.0f : -1.0f) * (r == 8 ? 1.0f : 1.0f / 3.0f);
            r = rnd() % 10;
            w2 = r < 4 ? frand(-0.3f, 0.3f) : r < 8 ? frand(-3, 3) : (rnd() & 1 ? 1.0f : -1.0f) * (r == 8 ? 1.0f : 1.0f / 3.0f);
        } else {
            W += frand(-0.5f, 0.5f); u += frand(-0.3f, 0.3f); w2 += frand(-0.3f, 0.3f);
        }
        if (chance(3)) W = d * 0.5f;                                  /* on the near plane */
        if (chance(2)) W = 0.0f;
        if (own) setf(QW(m, b + 2), W * u - tx, W * w2 - ty, W, some_float());
        else for (int j = 0; j < 3; j++) QW(m, b + 2)[j] = Ul(Fl(QW(m, b + 2)[j]) + frand(-20, 20));
        setf(QW(m, b), frand(0, 1), frand(0, 1), chance(80) ? 1.0f : frand(0.5f, 2), chance(50) ? 1.0f : some_float());
        for (int j = 0; j < 4; j++) QW(m, b + 1)[j] = chance(80) ? rnd() % 256 : rnd();
    }
}

/* ---- the comparison ---- */
static u32 out[0x400 * MAXPK * 4];
static NativeFx57843Packet pk[MAXPK];
/* the RcVu0 fields compared (all but the pipeline queues' contents: fp_rdy fp_mac fp_st cp_rdy
 * cp_val q_rdy, cycle bookkeeping, empty after a run) */
#include <stddef.h>
#define FLD(f) { offsetof(RcVu0, f), sizeof(((RcVu0 *)0)->f), #f }
static const struct { size_t off, len; const char *name; } fld[] = {
    FLD(vf), FLD(acc), FLD(q), FLD(i), FLD(p), FLD(r), FLD(vi), FLD(clip), FLD(status), FLD(mac),
    FLD(cmsar0), FLD(tpc), FLD(susp), FLD(st_int), FLD(cl_int), FLD(fp_n), FLD(fp_h), FLD(cp_n), FLD(cp_h),
    FLD(q_pend), FLD(q_new) };
static int regs_differ(const RcVu0 *a, const RcVu0 *b, int it, u32 pc, int shown)
{
    int bad = 0;
    for (unsigned k = 0; k < sizeof fld / sizeof *fld; k++) {
        const u8 *x = (const u8 *)a + fld[k].off, *y = (const u8 *)b + fld[k].off;
        for (size_t w = 0; w < fld[k].len; w += 4) if (memcmp(x + w, y + w, 4)) {
            u32 u, v; memcpy(&u, x + w, 4); memcpy(&v, y + w, 4);
            if (shown + bad < 16) fprintf(stderr, "iter %d (pc %04X): register %s word %u: program %08X native %08X\n", it, pc, fld[k].name, (unsigned)(w / 4), v, u);
            bad++;
        }
    }
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: fx57843_fuzz vucapseq.bin [iterations] [seed]\n"); return 2; }
    int iters = argc > 2 ? atoi(argv[2]) : 20000;
    if (argc > 3) rs = (u32)atoi(argv[3]) | 1;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);   /* round toward zero, as the game */
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    if (!load(argv[1])) { fprintf(stderr, "no runs in %s\n", argv[1]); return 1; }
    int bstrip[64], nb = 0;
    for (int i = 0; i < nrec && nb < 64; i++) if (recs[i].pc == 0x70) bstrip[nb++] = i;
    static u8 pm[0x4000], nm[0x4000], before[0x4000];
    static RcVu0 pv, v0;
    long runs = 0, runs_ok = 0, nanruns = 0, nanruns_ok = 0, stale = 0, clipcalls = 0, wraps = 0, byk[4] = { 0 }, kicks = 0;
    int failures = 0, cov0 = 0;
    for (int it = 0; it < iters && failures < 5; it++) {
        int mutate = it >= nrec;                     /* the first iterations: the captured runs as they are */
        const rec_t *r = mutate ? &recs[nb ? bstrip[rnd() % nb] : rnd() % nrec] : &recs[it];
        v0 = r->before;
        memcpy(before, r->mb, 0x4000);
        u32 pc = r->pc, top = r->tops[0];
        int kind = 0;
        if (mutate) {
            u32 (*vf)[4] = (u32 (*)[4])v0.vf;
            sanitize((u32 *)before, 0x1000);
            for (int k = 1; k < 32; k++) sanitize(vf[k], 4);
            if (chance(50)) {                                    /* every other register too */
                for (int k = 1; k < 18; k++) if (k != 6 && k != 13) setf(vf[k], some_float(), some_float(), some_float(), some_float());
                for (int k = 1; k < 16; k++) v0.vi[k] = rnd() & 0xFFFF;
                for (int k = 0; k < 4; k++) v0.acc[k] = some_float();
                v0.q = some_float(); v0.q_new = some_float(); v0.i = some_float();
                setf(vf[6], some_float(), some_float(), some_float(), Fl(vf[6][3])); setf(vf[13], some_float(), some_float(), some_float(), Fl(vf[13][3]));
            }
            int s = rnd() % 20;
            kind = s == 0 ? 0 : s < 5 ? 1 : 2;
            top = chance(60) ? (chance(50) ? 0 : 466) : rnd() % 1024;
            if (chance(2)) top |= (rnd() % 64) << 10;          /* TOP over 1023: only its low bits address */
            u32 n = chance(70) ? 1 + rnd() % 30 : chance(85) ? 31 + rnd() % 130 : rnd() % 4;
            int big = n < (kind == 2 ? 1u : 4u);
            if (big && !chance(15)) n += 4;                       /* (65536 vertices: now and then) */
            big = n < (kind == 2 ? 1u : 4u);
            wraps += big;
            if (chance(30)) vf[6][3] = Ul(some_float());
            if (chance(30)) vf[13][3] = Ul(some_float());
            if (chance(30)) setf(vf[7], frand(-50, 200), frand(100, 300), some_float(), some_float());
            if (chance(5)) vf[7][1] = vf[7][0];
            if (kind == 0) {                                     /* the setup */
                pc = 0;
                for (u32 a = 932; a < 944; a++) if (chance(50)) setf(QW(before, a), some_float(), some_float(), some_float(), some_float());
            } else if (kind == 1) {                              /* a plain strip */
                pc = chance(80) ? 0x60 : 0x78;
                if (chance(40)) for (int rr = 18; rr <= 21; rr++) setf(vf[rr], some_float(), some_float(), some_float(), some_float());
                else for (int rr = 18; rr <= 21; rr++) memcpy(vf[rr], QW(before, 932 + rr - 18), 16);
                if (chance(50)) vf[31][3] = Ul(some_float());
                vf[29][3] = chance(50) ? vf[31][3] : Ul(some_float());
                if (chance(30)) setf(vf[30], some_float(), some_float(), some_float(), some_float());
                setf(QW(before, top), 0, 0, 0, 0);
                QW(before, top)[0] = n | (chance(80) ? 0x8000 : rnd() << 15); QW(before, top)[1] = 0x303e4000; QW(before, top)[2] = 0x412;
                for (u32 k = 0; k < n + 3 && k < 400; k++) {
                    u32 b = top + 1 + 3 * k;
                    setf(QW(before, b), some_float(), some_float(), some_float(), some_float());
                    for (int j = 0; j < 4; j++) QW(before, b + 1)[j] = rnd() % 256;
                    float ww = chance(5) ? 0.0f : frand(-20, 300);
                    setf(QW(before, b + 2), frand(-1000, 1000), frand(-1000, 1000), ww, some_float());
                }
            } else {                                             /* a clipped strip */
                pc = chance(80) ? 0x70 : 0x68;
                if (r->pc == 0x70 && !chance(10)) { /* keep its own memory layout */ }
                setf(QW(before, top), 0, 0, 0, 0);
                QW(before, top)[0] = n | (chance(80) ? 0x8000 : rnd() << 15); QW(before, top)[1] = 0x303e4000; QW(before, top)[2] = 0x412;
                make_clipped(before, &v0, top, n < 400 ? n : 400);
                v0.clip = rnd() & 0xFFFFFF;
                if (chance(50)) { setf(vf[26], some_float(), some_float(), some_float(), some_float()); setf(vf[27], some_float(), some_float(), some_float(), some_float()); }
                if (chance(30)) setf(vf[29], frand(-2000, 2000), frand(-2000, 2000), some_float(), some_float());
                if (chance(30)) setf(vf[30], some_float(), some_float(), some_float(), some_float());
                if (chance(15)) { QW(before, 940)[0] = rnd() % 4 ? (rnd() & 0xFFFF) : rnd(); if (chance(30)) QW(before, 940)[1] = rnd(); }
                if (chance(10)) setf(QW(before, 947), some_float(), some_float(), some_float(), some_float());
            }
            sanitize((u32 *)before, 0x1000);
            v0.cl_int = v0.clip;
        }
        /* the program */
        memcpy(VUMEM, before, 0x4000);
        rc_vu1 = v0; cur_top = top; ngot = 0; nkicks = 0; kick_pending = 0xFFFFFFFFu; fz_nan = 0; fz_stale = 0;
        memset(cov_run, 0, sizeof cov_run);
        vup_57843_vu1(&rc_vu1, pc); kick_flush();
        pv = rc_vu1;
        memcpy(pm, VUMEM, 0x4000);
        for (int a = 0; a < NPAIR; a++) cov[a] |= cov_run[a];
        if (it == nrec - 1) for (int a = 0; a < NPAIR; a++) cov0 += cov[a];
        if (cov_run[0x0838 / 8]) clipcalls++;
        /* the native code */
        memcpy(nm, before, 0x4000);
        NativeFx57843Regs sr; native_fx57843_regs_from_vu(&sr, &v0);
        native_fx57843_seed(&sr);
        int npk = native_fx57843_run(pc, nm, top, out, sizeof out / 16, pk, MAXPK);
        static RcVu0 nv; nv = v0;
        native_fx57843_state(&sr); native_fx57843_regs_to_vu(&sr, &nv);
        runs++; byk[kind]++; kicks += nkicks;
        stale += fz_stale != 0;
        if (fz_stale) fprintf(stderr, "iter %d (pc %04X): the program reads Q or clip flags before they land (%d)\n", it, pc, fz_stale);
        int bad = 0;
        if (npk != ngot) { fprintf(stderr, "iter %d (pc %04X): %d kicks, native %d\n", it, pc, ngot, npk); bad++; }
        for (int i = 0; i < ngot && i < npk; i++) {
            if (got[i].n != pk[i].n) { fprintf(stderr, "iter %d (pc %04X) kick %d: %u qwords, native %u\n", it, pc, i, got[i].n, pk[i].n); bad++; continue; }
            const u32 *dn = out + pk[i].off * 4;
            for (u32 q = 0; q < got[i].n * 4; q++) if (dn[q] != got[i].d[q]) {
                if (bad < 8) fprintf(stderr, "iter %d (pc %04X) kick %d qw %u word %u: program %08X native %08X\n", it, pc, i, q / 4, q % 4, got[i].d[q], dn[q]);
                bad++;
            }
        }
        for (u32 w = 0; w < 0x1000; w++) if (((u32 *)pm)[w] != ((u32 *)nm)[w]) {
            if (bad < 12) fprintf(stderr, "iter %d (pc %04X, top %u): VU memory qw %u word %u: program %08X native %08X\n", it, pc, top, w / 4, w % 4, ((u32 *)pm)[w], ((u32 *)nm)[w]);
            bad++;
        }
        bad += regs_differ(&nv, &pv, it, pc, bad);
        if (fz_nan) { nanruns++; if (!bad) { nanruns_ok++; runs_ok++; } continue; }
        if (bad) { failures++; fprintf(stderr, "iter %d (pc %04X top %u): %d words differ\n", it, pc, top, bad); continue; }
        runs_ok++;
    }
    int n = 0;
    for (int a = 0; a < NPAIR; a++) n += cov[a];
    printf("%ld runs compared (%ld setup, %ld plain strips, %ld clipped strips; %ld with the clipper; %ld of 65536+ vertices), %ld identical in every word (kicked packets, all of VU memory, all VU registers); %ld kicks\n",
           runs, byk[0], byk[1], byk[2], clipcalls, wraps, runs_ok, kicks);
    printf("  of them %ld with a NaN FMAC result (NaN bit patterns read as floats), %ld identical; %ld with a Q or clip flag read before it lands\n", nanruns, nanruns_ok, stale);
    printf("program coverage: %d of %d instruction pairs (the capture alone: %d)\n", n, NPAIR, cov0);
    int nw = 0;
    for (int i = 0; i < NBR; i++) nw += br_way[i][0] + br_way[i][1];
    printf("conditional branches: %d of %d ways taken\n", nw, 2 * NBR);
    if (getenv("UNCOVERED")) for (int i = 0; i < NBR; i++) for (int w = 0; w < 2; w++) if (!br_way[i][w]) printf("  branch at %04X never %s\n", br[i][0], w ? "taken" : "falls through");
    if (getenv("UNCOVERED")) {
        for (int a = 0; a < NPAIR;) {
            if (cov[a]) { a++; continue; }
            int b = a; while (b < NPAIR && !cov[b]) b++;
            printf("  not reached: %04X-%04X\n", a * 8, b * 8 - 8);
            a = b;
        }
    }
    return failures != 0;
}
