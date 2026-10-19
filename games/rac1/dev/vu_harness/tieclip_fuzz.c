/* dev/vu_harness/tieclip_fuzz.c: the tie clip program (VU1 image 224979) against
 * host/src/native/native_tieclip.c on runs the captures never had. It replays a capture
 * (UYA_VUCAPSEQ) run by run, the game's uploads between runs taken from the capture, and:
 *   - moves the instances (new object to clip space matrices, morph, color weights, fog, the
 *     screen scale at 975) so that triangles cross the guard band and the screen's planes,
 *     behind the camera too, and get clipped;
 *   - (with probability CLS, default 0.5 per class packet) replaces the class with a made-up
 *     one: random strips (lengths 1.., restarts, texture switches with their A+D blocks, the
 *     walk's odd cases: a two-vertex first strip, a restart right after a restart), random
 *     dinky / fat vertices on one or two places, all four header cases (hb[0], hb[1]) and every
 *     position of the header's end vertices in the program's four-way unrolled loops;
 *   - now and then makes the program's first test of an instance (a stale register against
 *     hb[4]) hit, so that the instance draws no vertices.
 * Both start from the same VU state and memory; after each run the kicked packets and the
 * whole of VU1 data memory are compared word for word. It reports the program addresses the
 * runs reached (the translated program is built with VU_TRACE counting them).
 * build: see tieclip_fuzz.sh. run: ./tieclip_fuzz vucapseq.bin [iterations] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "vu.h"
#include "native_tieclip.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
static u32 cur_top;
u32 vu_vif_top(int vu) { (void)vu; return cur_top; }
u32 vu_vif_itop(int vu) { (void)vu; return 0; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
void vup_224979_vu1(RcVu0 *v, u32 pc);

#define VUMEM ((u8 *)(uintptr_t)0x1100C000u)
#define MAXPK 256
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

/* coverage of the program, a watchdog, and the runs the comparison leaves out:
 *   hazard 1: the clipper's stack of pending vertices (1018..) reaches 1022, its return address
 *   hazard 2: a clip call's saved registers (44..46 of the clip buffer) overwritten
 *   hazard 3: a plane stage's test address (976 + 7k, z) overwritten
 * (all three send the program to a wild address; native code cannot follow it there), and
 *   hazard 4: a fan of more than 9 vertices, over the clipper's own inputs (34..)
 *   naninput: NaN / inf bit patterns read as clip space floats (integers read back): which NaN
 *             a sum keeps then depends on the order the compiler gives its operands
 * The last two are compared, and counted apart. */
static u8 cov[0x4000 / 8];
static long steps; static unsigned last_pc; static int hazard, naninput, flow;
static long haz_kind[5], haz_nan[5];
static jmp_buf runaway;
static int bad_bits(int r0, int r1)
{
    for (int r = r0; r <= r1; r++) for (int k = 0; k < 4; k++) if ((((u32 *)rc_vu1.vf[r])[k] & 0x7F800000u) == 0x7F800000u) return 1;
    return 0;
}
void tc_cov(unsigned pc)
{
    if (flow && pc != last_pc + 8) fprintf(stderr, " %04X->%04X", last_pc, pc);
    cov[(pc & 0x3FFF) / 8] = 1; last_pc = pc;
    switch (pc) {
    case 0x3448: if ((rc_vu1.vi[7] & 0xFFFF) >= 4) hazard = 1; break;
    case 0x31C8: {
        static const u32 ok[] = { 1046, 1054, 1030, 1038, 1117, 1144, 1063, 1090, 1133, 1161, 1079, 1106 };
        int good = 0; for (unsigned q = 0; q < 12; q++) good |= (rc_vu1.vi[15] & 0xFFFF) == ok[q];
        if (!good) hazard = 2;
        break;
    }
    case 0x3320: case 0x3370: case 0x3568: {
        u32 f = rc_vu1.vi[3] & 0xFFFF;
        if (f < 1751 || f > 1761 || !(f & 1)) hazard = 3;
        break;
    }
    case 0x3158: if ((rc_vu1.vi[2] & 0x7FFF) > 9 && !hazard) hazard = 4; break;
    case 0x1F80: case 0x1FC0: case 0x2000: case 0x2040: case 0x2080: case 0x20C0:
    case 0x2108: case 0x21E0: case 0x22B8: case 0x2390: case 0x2470: naninput |= bad_bits(5, 8); break;
    }
    if (pc >= 0x36B8 && pc <= 0x3710) naninput |= bad_bits(18, 19);
    if (++steps > 4000000) longjmp(runaway, 1);
}

/* ---- the capture ---- */
typedef struct { u32 pc; RcVu0 before; u8 mb[0x4000], ma[0x4000]; u32 tops[2]; } rec_t;
static rec_t *recs; static int nrec;
static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    char magic[4]; static RcVu0 after; u32 t2[2];
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        recs = realloc(recs, sizeof *recs * (nrec + 1));
        rec_t *r = &recs[nrec];
        if (fread(&r->pc, 4, 1, f) != 1 || fread(&r->before, sizeof r->before, 1, f) != 1 || fread(r->mb, 1, 0x4000, f) != 0x4000 || fread(r->tops, 4, 2, f) != 2) break;
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
static float Fl(u32 u) { float f; memcpy(&f, &u, 4); return f; }
static float fcf(float x) { u32 b = Ul(x); if ((b & 0x7F800000u) == 0x7F800000u) x = Fl((b & 0x80000000u) | 0x7F7FFFFFu); return x; }

/* ---- a made-up class ---- */
typedef struct { int nd, nf; int n1, n2, n3, n4; } ClassInfo;
static ClassInfo ci; static int ci_valid; static int cl_slot[200], cl_nslot;

static void make_class(u8 *m, int stale_base)
{
    for (u32 q = 0; q < 198; q++) memset(QW(m, q), 0, 16);
    int nad = 1 + rnd() % 3;
    /* the vertex groups */
    int n1 = 4 + rnd() % 9, n2 = (rnd() % 3) ? 3 + rnd() % 8 : 0, fat = rnd() % 4 != 0;
    int n3 = fat ? 1 + rnd() % 6 : 0, n4 = fat ? 1 + rnd() % 6 : 0;
    int nd = n1 + n2, nf = n3 + n4;
    int nslots = n1 + 2 * n2 + n3 + 2 * n4;
    /* the strips over the slots */
    int cnt[40], ns = 0, left = nslots, maxns = 26 - 5 * nad - 3;
    if (maxns > 30) maxns = 30;
    int odd = rnd() % 8 == 0;                            /* strips of one or two vertices */
    while (left > 0) {
        int c = odd ? 1 + rnd() % 4 : 3 + rnd() % 10;
        if (ns == maxns - 1 || c > left) c = left;
        cnt[ns++] = c; left -= c;
    }
    int sw[40] = { 0 }, nsw = 0;                         /* texture switches before strips 1.. */
    for (int k = 1; k < ns && nsw < nad - 1; k++) if (rnd() % 3 == 0) { sw[k] = 1; nsw++; }
    /* packet layout */
    int pos = 0, slot[200], nsl = 0, stag[40], adpos[4] = { 0 }, nadp = 1;
    pos = 6;
    for (int k = 0; k < ns; k++) {
        if (sw[k]) { adpos[nadp++] = pos; pos += 6; }
        stag[k] = pos++;
        for (int j = 0; j < cnt[k]; j++) { slot[nsl++] = pos; pos += 3; }
    }
    nad = nadp;
    cl_nslot = nsl; memcpy(cl_slot, slot, sizeof slot);
    /* class header and blocks */
    u32 *d0 = QW(m, 0);
    for (int a = 1; a < nad; a++) d0[a - 1] = adpos[a];
    for (int a = nad - 1; a < 4; a++) d0[a] = (rnd() % 4 == 0) ? (u32)-(int)(rnd() % 50) : 0;
    for (int a = 0; a < nad; a++) for (int r = 0; r < 5; r++) for (int w = 0; w < 4; w++) QW(m, 1 + 5 * a + r)[w] = rnd();
    u32 h = 1 + 5 * nad;
    u32 *hb0 = QW(m, h), *hb1 = QW(m, h + 1), *hb2 = QW(m, h + 2);
    hb0[0] = n2 ? 0 : 1 + rnd() % 3; hb0[1] = fat ? 0 : 1 + rnd() % 3; hb0[2] = rnd() % 8; hb0[3] = ns;
    hb2[0] = 4 + 2 * nd; hb2[1] = 3 * (nf + 2); hb2[2] = rnd() % 8; hb2[3] = nf;
    for (int k = 0; k < ns; k++) { u32 *q = QW(m, h + 3 + k); q[0] = cnt[k]; q[1] = rnd() % 4; q[2] = stag[k]; q[3] = 0; }
    /* the walk */
    int o[100], no = 0, si = 0;
    for (int k = 0; k < ns; k++) for (int j = 0; j < cnt[k]; j++) {
        int prev = si ? slot[si - 1] : 0;
        int d = slot[si] - prev;
        if (si == 0) o[no++] = d;
        else if (j == 0) o[no++] = -d;
        else o[no++] = d;
        si++;
    }
    o[no++] = -10; o[no++] = 0; o[no++] = rnd() % 7 - 3; o[no++] = rnd() % 7 - 3;
    for (int k = 0; k < no && k < 80; k++) QW(m, 30 + k / 4)[k % 4] = (u32)o[k];
    /* the vertices: each to one or two slots */
    int perm[200];
    for (int k = 0; k < nsl; k++) perm[k] = k;
    for (int k = nsl - 1; k > 0; k--) { int j = rnd() % (k + 1), t = perm[k]; perm[k] = perm[j]; perm[j] = t; }
    int pi = 0, place[100], place2[100];
    int twice[100];
    for (int v = 0; v < nd + nf; v++) {
        int dbl = (v >= n1 && v < nd) || v >= nd + n3;
        twice[v] = dbl;
        place[v] = slot[perm[pi++]];
        place2[v] = dbl ? slot[perm[pi++]] : (int)(rnd() % 3 == 0 ? rnd() % 300 : 0);
    }
    for (int v = 0; v < nd; v++) {
        u32 *p = QW(m, 50 + 2 * v), *t = QW(m, 51 + 2 * v);
        p[0] = (u32)(int)(rnd() % 2001) - 1000; p[1] = (u32)(int)(rnd() % 2001) - 1000; p[2] = (u32)(int)(rnd() % 2001) - 1000; p[3] = place[v];
        t[0] = rnd() % 8192; t[1] = rnd() % 8192; t[2] = rnd() % 4 ? 4096 : 2048 + rnd() % 4096; t[3] = place2[v];
    }
    u32 f0 = 50 + 2 * nd;
    for (int v = 0; v < nf; v++) {
        u32 *a = QW(m, f0 + 3 * v), *b = QW(m, f0 + 3 * v + 1), *t = QW(m, f0 + 3 * v + 2);
        for (int k = 0; k < 3; k++) { a[k] = (u32)(int)(rnd() % 401) - 200; b[k] = (u32)(int)(rnd() % 2001) - 1000; }
        a[3] = place[nd + v]; b[3] = rnd();
        t[0] = rnd() % 8192; t[1] = rnd() % 8192; t[2] = 4096; t[3] = place2[nd + v];
    }
    hb1[0] = place[n1 - 4];
    hb1[1] = n2 ? place[n1 + n2 - 3] : rnd() % 300;
    hb1[2] = fat ? place[nd + n3 - 1] : rnd() % 300;
    hb1[3] = fat ? place[nd + n3 + n4 - 1] : rnd() % 300;
    if (stale_base >= 0) hb1[0] = (u32)((50 + hb2[0] - stale_base) & 0xFFFF);
    (void)twice;
    ci.nd = nd; ci.nf = nf; ci.n1 = n1; ci.n2 = n2; ci.n3 = n3; ci.n4 = n4; ci_valid = 1;
}

/* a moved instance (and its color indexes, for a made-up class) */
static void move_instance(u8 *m, u32 inst)
{
    float sc = frand(0.0002f, 0.003f);
    float R[4][4];
    for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++) R[r][k] = frand(-sc, sc);
    R[3][0] = frand(-2.5f, 2.5f); R[3][1] = frand(-2.5f, 2.5f); R[3][2] = frand(-1.2f, 1.2f);
    R[3][3] = rnd() % 8 == 0 ? frand(-1.0f, 0.3f) : frand(0.3f, 3.0f);
    if (rnd() % 16 == 0) R[3][3] = 0.0f;
    if (rnd() % 4 == 0) for (int k = 0; k < 3; k++) R[3][k] *= 0.1f;     /* mostly inside */
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) QW(m, inst + r)[k] = Ul(R[r][k]);
    u32 *r4 = QW(m, inst + 4);
    r4[0] = Ul(frand(-1.0f, 2.0f)); r4[2] = Ul(frand(0.0f, 1.0f)); r4[3] = Ul(frand(0.0f, 1.0f));
    QW(m, inst + 5)[3] = Ul(rnd() % 32 == 0 ? frand(-1e6f, 1e9f) : frand(0.0f, 255.0f));
    if (ci_valid) {
        int nq = (ci.nd + 3) / 4;
        for (int q = 0; q < nq + ci.nf + 2 && q < 38; q++) for (int k = 0; k < 4; k++) QW(m, inst + 6 + q)[k] = rnd() % 128;
    }
    if (rnd() % 6 == 0) { u32 *g = QW(m, 975); g[0] = g[1] = Ul(frand(0.5f, 8.0f)); g[2] = Ul(frand(0.5f, 2.0f)); g[3] = Ul(1.0f); }
}

static void regs_of(const RcVu0 *v, NativeTieclipRegs *r)
{
    for (int k = 0; k < 16; k++) r->vi[k] = (u16)v->vi[k];
    memcpy(r->vf21, v->vf[21], 16); memcpy(r->vf22, v->vf[22], 16); memcpy(r->vf23, v->vf[23], 16);
    memcpy(r->vf29, v->vf[29], 16); memcpy(r->vf30, v->vf[30], 16); memcpy(r->vf31, v->vf[31], 16);
    r->vf1w = ((const u32 *)v->vf[1])[3]; r->vf25w = ((const u32 *)v->vf[25])[3]; r->vf26w = ((const u32 *)v->vf[26])[3];
}

static int compare(int it, int i, u32 pc, const packet_t *k, int nk, const u32 *out, const NativeTieclipPacket *pk, int npk, const u8 *pm, const u8 *nm)
{
    int bad = 0;
    if (nk != npk) { fprintf(stderr, "iter %d run %d (pc %04X): kicks: program %d, native %d\n", it, i, pc, nk, npk); bad++; }
    for (int j = 0; j < nk && j < npk; j++) {
        if (k[j].n != pk[j].n) { fprintf(stderr, "iter %d run %d: kick %d: program %u qw, native %u\n", it, i, j, k[j].n, pk[j].n); bad++; continue; }
        for (u32 q = 0; q < k[j].n * 4; q++) if (k[j].d[q] != out[pk[j].off * 4 + q]) {
            if (bad < 8) fprintf(stderr, "iter %d run %d: kick %d qw %u word %u: program %08X native %08X\n", it, i, j, q / 4, q % 4, k[j].d[q], out[pk[j].off * 4 + q]);
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
    if (argc < 2) { fprintf(stderr, "usage: tieclip_fuzz vucapseq.bin [iterations] [seed]\n"); return 2; }
    int iters = argc > 2 ? atoi(argv[2]) : 100;
    rs = argc > 3 ? (u32)strtoul(argv[3], 0, 0) : 12345;
    if (!rs) rs = 1;
    float pcls = getenv("CLS") ? (float)atof(getenv("CLS")) : 0.5f;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);   /* round toward zero */
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    if (!load(argv[1])) return 1;
    static u8 pm[0x4000], nm[0x4000];
    static u32 out[0x400 * MAXPK * 4];
    static NativeTieclipPacket pk[MAXPK];
    static RcVu0 pv;
    long runs = 0, runs_ok = 0, clipruns = 0, fans = 0, runaways = 0, hazards = 0, overruns = 0, overruns_ok = 0, nanruns = 0, nanruns_ok = 0;
    int failures = 0, cov0 = 0;
    long nsynth = 0, nstale = 0, nskip = 0;
    for (int it = 0; it < iters && failures < 3; it++) {
        int mutate = it > 0;                         /* iteration 0: the capture as it is */
        pv = recs[0].before;
        memcpy(pm, recs[0].mb, 0x4000); memcpy(nm, recs[0].mb, 0x4000);
        native_tieclip_reset();
        { NativeTieclipRegs sr; regs_of(&pv, &sr); native_tieclip_seed(&sr); }
        ci_valid = 0;
        int synthetic = 0, stale_next = -1, prev_inst = 0;
        for (int i = 0; i < nrec; i++) {
            rec_t *r = &recs[i];
            if (i > 0) {                              /* the game's uploads since the last run */
                const u32 *b = (const u32 *)r->mb, *a = (const u32 *)recs[i - 1].ma;
                for (u32 w = 0; w < 0x1000; w++) if (b[w] != a[w]) { ((u32 *)pm)[w] = b[w]; ((u32 *)nm)[w] = b[w]; }
            }
            if (r->pc == 0x30) { memcpy(pm, r->mb, 198 * 16); memcpy(nm, r->mb, 198 * 16); }   /* the class as the game sent it */
            if (r->pc == 0x00) { memcpy(pm + 198 * 16, r->mb + 198 * 16, 88 * 16); memcpy(nm + 198 * 16, r->mb + 198 * 16, 88 * 16); }
            if (mutate && r->pc == 0x30) {
                synthetic = frand(0, 1) < pcls;
                if (synthetic) {
                    int stale = rnd() % 10 == 0;
                    int base = -1;
                    if (stale) {                       /* the next instance's buffer, as the program will have it */
                        float x = pv.vf[21][0];
                        float vx = fcf(pv.vf[23][0] - x);
                        vx = fcf(vx - pv.vf[21][1]);
                        base = (int)(Ul(vx) & 0xFFFF);
                    }
                    make_class(pm, base); memcpy(nm, pm, 198 * 16);
                    nsynth++;
                    stale_next = stale ? 1 : -1;
                } else { ci_valid = 0; stale_next = -1; }
            }
            if (mutate && r->pc == 0x00 && rnd() % 8 != 0) {
                u32 inst = pv.vi[3] & 0xFFFF;
                move_instance(pm, inst);
                memcpy(nm + inst * 16, pm + inst * 16, 44 * 16);
                memcpy(nm + 975 * 16, pm + 975 * 16, 16);
            }
            u32 pc = r->pc;
            int stale_run = 0;
            if (mutate && pc == 0x00) {                /* after a stale hit the class's markers are no good: skip */
                if (stale_next == 0 || (stale_next < 0 && prev_inst && rnd() % 16 == 0)) pc = 0x10;
                if (stale_next == 1) { stale_next = 0; stale_run = 1; }
            }
            prev_inst = pc == 0x00 || pc == 0x10;
            nstale += stale_run; nskip += pc == 0x10;
            if (stale_run) {   /* the stale hit: the walk reads what the buffer has */
                u32 base = ((u32 *)pv.vf[21])[1] & 0xFFFF;
                for (int q = 0; q < cl_nslot; q++) for (int k = 0; k < 3; k++) for (int w = 0; w < 4; w++) {
                    u32 a = base + cl_slot[q] + k;
                    ((u32 *)pm)[(a & 0x3FF) * 4 + w] = ((u32 *)nm)[(a & 0x3FF) * 4 + w] = Ul(k == 1 ? (float)(8388608 + rnd() % 256) : frand(-2.0f, 2.0f));
                }
            }
            /* the program */
            { const char *e = getenv("FLOW"); int di, dr; flow = e && sscanf(e, "%d,%d", &di, &dr) == 2 && di == it && dr == i; }
            memcpy(VUMEM, pm, 0x4000);
            rc_vu1 = pv; cur_top = r->tops[0]; ngot = 0; kick_pending = 0xFFFFFFFFu; steps = 0; hazard = 0; naninput = 0;
            if (setjmp(runaway)) { runaways++; if (hazard) { hazards++; haz_kind[hazard]++; haz_nan[hazard] += naninput; goto next_iter; } failures++; fprintf(stderr, "iter %d run %d: the program runs away (pc %04X, at %04X; class %s: nd %d nf %d n1 %d n2 %d n3 %d n4 %d)\n", it, i, pc, last_pc, synthetic ? "made up" : "captured", ci.nd, ci.nf, ci.n1, ci.n2, ci.n3, ci.n4); goto next_iter; }
            vup_224979_vu1(&rc_vu1, pc); kick_flush();
            pv = rc_vu1;
            memcpy(pm, VUMEM, 0x4000);
            /* the native code */
            int npk = native_tieclip_run(pc, nm, r->tops[0], out, sizeof out / 16, pk, MAXPK);
            runs++;
            if (hazard && hazard != 4) { hazards++; haz_kind[hazard]++; haz_nan[hazard] += naninput; goto next_iter; }   /* the program jumped somewhere wild */
            if (ngot > 1 && pc == 0) { clipruns++; fans += (ngot - 1) / 2; }
            int bad = compare(it, i, pc, got, ngot, out, pk, npk, pm, nm);
            {   /* the registers carried to the next run */
                NativeTieclipRegs nr; native_tieclip_state(&nr);
                u16 *nvi = nr.vi; u32 *a = nr.vf21, *b = nr.vf22, *c = nr.vf23, w1 = nr.vf1w, w25 = nr.vf25w;
                static const int chk[] = { 1, 2, 3, 4, 5, 6, 7, 8, 12, 14, 15 };
                for (unsigned q = 0; q < sizeof chk / sizeof *chk; q++) if (nvi[chk[q]] != (pv.vi[chk[q]] & 0xFFFF)) { if (bad < 12) fprintf(stderr, "iter %d run %d: vi%d: program %04X native %04X\n", it, i, chk[q], pv.vi[chk[q]] & 0xFFFF, nvi[chk[q]]); bad++; }
                if (memcmp(a, pv.vf[21], 16) || memcmp(b, pv.vf[22], 16) || memcmp(c, pv.vf[23], 16)) { fprintf(stderr, "iter %d run %d: vf21..vf23 differ\n", it, i); bad++; }
                if (w1 != ((u32 *)pv.vf[1])[3]) { fprintf(stderr, "iter %d run %d: vf1.w program %08X native %08X\n", it, i, ((u32 *)pv.vf[1])[3], w1); bad++; }
                if (w25 != ((u32 *)pv.vf[25])[3]) { fprintf(stderr, "iter %d run %d: vf25.w program %08X native %08X\n", it, i, ((u32 *)pv.vf[25])[3], w25); bad++; }
            }
            if (hazard == 4) { overruns++; if (!bad) overruns_ok++; goto next_iter; }
            if (naninput) { nanruns++; if (!bad) { nanruns_ok++; runs_ok++; continue; } goto next_iter; }
            if (bad) {
                failures++;
                fprintf(stderr, "iter %d run %d (pc %04X): %d words differ (class %s", it, i, pc, bad, synthetic ? "made up" : "captured");
                if (synthetic) fprintf(stderr, ": nd %d nf %d n1 %d n2 %d n3 %d n4 %d", ci.nd, ci.nf, ci.n1, ci.n2, ci.n3, ci.n4);
                fprintf(stderr, ")\n");
                goto next_iter;
            }
            runs_ok++;
        }
    next_iter:;
        if (it == 0) for (int a = 0; a < 0x3760 / 8; a++) cov0 += cov[a];
    }
    int n = 0, total = 0;
    for (int a = 0; a < 0x3760 / 8; a++) { total++; n += cov[a]; }
    printf("%ld runs compared, %ld identical in every word (kicked packets, all of VU memory, the registers carried on); %ld with clipped fans (%ld fans)\n", runs - (hazards - runaways), runs_ok, clipruns, fans);
    printf("  of them %ld with NaN / inf bit patterns read as clip space (integers read back), %ld identical; %ld with a fan of over 9 vertices (over the clipper's inputs), %ld identical\n", nanruns, nanruns_ok, overruns, overruns_ok);
    printf("left out: %ld runs where the program jumps to an address from overwritten VU memory (%ld run away): its clip stack over its return address %ld, a clip call's saved registers %ld, a plane stage's test %ld; %ld of them with NaN inputs\n",
           hazards, runaways, haz_kind[1], haz_kind[2], haz_kind[3], haz_nan[1] + haz_nan[2] + haz_nan[3]);
    printf("%ld made-up classes, %ld instances on the stale-register path, %ld runs at 0x0010\n", nsynth, nstale, nskip);
    printf("program coverage: %d of %d instruction pairs (the capture alone: %d)\n", n, total, cov0);
    if (getenv("UNCOVERED")) {
        for (int a = 0; a < 0x3760 / 8;) {
            if (cov[a]) { a++; continue; }
            int b = a; while (b < 0x3760 / 8 && !cov[b]) b++;
            printf("  not reached: %04X-%04X\n", a * 8, b * 8 - 8);
            a = b;
        }
    }
    return failures != 0;
}
