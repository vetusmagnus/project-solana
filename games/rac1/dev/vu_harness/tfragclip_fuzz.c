/* dev/vu_harness/tfragclip_fuzz.c: the tfrag clip program (VU1 image 903379) against
 * native_tfragclip.c on input the captures never had. For each captured run at 0x10 it makes
 * variants: the guard band (662) and frustum (665) scales moved in so that vertices fall
 * outside and triangles get clipped against each plane (z included) or trivially rejected,
 * vertices thrown far out or behind the eye, the index list reshuffled over the packet's own
 * index entries (repeated vertices, every vertex-mod-4 copy of the loop), new strip tables
 * (strip lengths, same-texture strips, texture switches, packet flushes with and without a
 * texture block, packets near the buffer size), random carried registers (clip flags, vi15,
 * vf26.w/vf27.w, either buffer first) and now and then stale clipper stage records. Both run
 * from the same VU state and memory; the kicked packets, the whole VU memory after the run
 * and the carried registers are compared word for word.
 * build (from dev/vu_harness):
 *   sed "s/vup_IMG_vu1/vup_903379_vu1/" ... (build.sh writes prog_903379.c; run it once first)
 *   $ZIG cc -target x86-linux-gnu -O2 -march=nehalem -mfpmath=sse -malign-double -fno-strict-aliasing -fwrapv \
 *       '-DVU_TRACE(pc)=do{extern unsigned char tfc_cov[];tfc_cov[(pc)>>3]=1;}while(0)' \
 *       -I ../../host/include -I . -I ../../host/src/native tfragclip_fuzz.c prog_903379.c \
 *       ../../build/linux/gen/vup_903379_vu1.c ../../host/src/native/native_tfragclip.c -o tfragclip_fuzz -lm
 * run: ./tfragclip_fuzz vucapseq.bin [variants per run] [verbose]   (COV=1: list the program
 *   addresses no run reached; captures given after -- add to the coverage without fuzzing;
 *   SEED=n: another set of variants) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "vu.h"
#include "harness.h"
#include "native_tfragclip.h"

RcVu0 rc_vu1;
u32 vu_nonfinite;
int trace_on;
unsigned char tfc_cov[0x800];
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
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() % 10000) / 10000.0f; }

typedef struct { u32 pc; RcVu0 v; u8 mem[0x4000]; u32 top; } rec_t;

#define OUTCAP (0x400 * MAXPK)
static u32 nout[OUTCAP * 4];
static NativeTfragclipPacket npk[MAXPK];
static long n_kicks, n_clipped;

static void regs_of(const RcVu0 *v, NativeTfragclipRegs *r)
{
    memcpy(r->vf3, v->vf[3], 16);
    r->clip = v->clip & 0xFFFFFF; r->vi15 = v->vi[15] & 0xFFFF;
    memcpy(&r->vf26w, &v->vf[26][3], 4); memcpy(&r->vf27w, &v->vf[27][3], 4);
}

static int cmp_run(rec_t *r, int verbose, const char *what)
{
    static u8 mem_prog[0x4000];
    static RcVu0 pv;
    memcpy(VUMEM, r->mem, 0x4000);
    pv = r->v; cur_top = r->top; ngot = 0; kick_pending = 0xFFFFFFFFu;
    prog_run(&pv, r->pc); kick_flush();
    memcpy(mem_prog, VUMEM, 0x4000);
    memcpy(VUMEM, r->mem, 0x4000);
    NativeTfragclipRegs nr, pr;
    regs_of(&r->v, &nr);
    native_tfragclip_seed(&nr);
    int n = native_tfragclip_run(r->pc, VUMEM, r->top, nout, OUTCAP, npk, MAXPK);
    if (n < 0) return -1;
    n_kicks += ngot;
    for (int i = 0; i < ngot; i++) if (got[i].n <= 28 && (got[i].d[1] & 0xFFFFC000u) == 0x303EC000u) n_clipped++;
    int bad = 0;
    if (n != ngot) { if (verbose) fprintf(stderr, "%s: kicks %d, program %d\n", what, n, ngot); return 0; }
    for (int i = 0; i < ngot; i++) {
        if (npk[i].n != got[i].n) { if (verbose) fprintf(stderr, "%s: kick %d %u qw, program %u\n", what, i, npk[i].n, got[i].n); bad++; continue; }
        for (u32 q = 0; q < got[i].n * 4; q++) if (nout[npk[i].off * 4 + q] != got[i].d[q]) {
            if (verbose && bad < 4) fprintf(stderr, "%s: kick %d qw %u word %u: %08X, program %08X\n", what, i, q / 4, q % 4, nout[npk[i].off * 4 + q], got[i].d[q]);
            bad++;
        }
    }
    for (u32 q = 0; q < 0x1000; q++) {
        u32 a = ((u32 *)VUMEM)[q], b = ((u32 *)mem_prog)[q];
        if (a != b) { if (verbose && (bad < 8 || getenv("MEMALL"))) fprintf(stderr, "%s: memory qw %u word %u: %08X, program %08X\n", what, q / 4, q % 4, a, b); bad++; }
    }
    native_tfragclip_state(&nr); regs_of(&pv, &pr);
    if (memcmp(&nr, &pr, sizeof nr)) {
        if (verbose) fprintf(stderr, "%s: registers after: clip %06X/%06X vi15 %X/%X vf3 %X/%X\n", what, nr.clip, pr.clip, nr.vi15, pr.vi15, nr.vf3[0], pr.vf3[0]);
        bad++;
    }
    return !bad;
}

/* a new strip table for the packet's nv vertices; blocks: the texture block offsets in use */
static void new_table(u8 *m, u32 B, int nv, const int *blocks, int nblocks)
{
    u32 tbl = B + (QW(m, B + 4)[2] & 0xFFFF);
    u32 lim = (B ? 656 : 328) - 1;
    int left = nv, pq = 0, ne = 0, maxlen = 1 + rnd() % (rnd() % 3 ? 12 : 40);
    int big = rnd() % 4 == 0;                       /* packets up to the buffer size */
    while (left > 0 && tbl + ne + 2 < lim) {
        int n = 1 + rnd() % maxlen; if (n > left) n = left;
        if (n > 40) n = 40;
        s32 code, y = 0, z = -1;
        int kind = ne == 0 ? 1 : rnd() % 4;          /* 0 same texture, 1 new texture, 2/3 flush */
        int need = 1 + 3 * n + (kind == 1 ? 6 : 0);
        if (kind == 0 && pq + need > (big ? 151 : 120)) kind = 2;
        if (kind == 1 && ne && pq + need > (big ? 151 : 120)) kind = 2;
        if (kind >= 2) { y = -128; z = rnd() % 2 ? blocks[rnd() % nblocks] : -1; pq = 0; }
        if (kind == 1) z = blocks[rnd() % nblocks];
        if (kind == 0) code = n; else code = n - 128;
        pq += 1 + 3 * n + (z >= 0 && kind ? 6 : 0);
        u32 *q = QW(m, tbl + ne);
        q[0] = (u32)code; q[1] = (u32)y; q[2] = (u32)z;
        ne++; left -= n;
    }
    u32 *q = QW(m, tbl + ne);
    q[0] = 0; q[1] = (u32)-1; q[2] = (u32)-1;
}

static int nverts(u8 *m, u32 B)
{
    u32 tbl = B + (QW(m, B + 4)[2] & 0xFFFF);
    int nv = 0;
    for (int t = 0; t < 64; t++) {
        s16 c = (s16)QW(m, tbl + t)[0];
        if (t == 0) nv += c + 128;
        else if (c > 0) nv += c;
        else if (c < 0) nv += c + 128;
        else break;
    }
    return nv;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: tfragclip_fuzz vucapseq.bin [variants per run] [verbose] [-- more captures for coverage]\n"); return 2; }
    int nvar = argc > 2 ? atoi(argv[2]) : 200, verbose = argc > 3 ? atoi(argv[3]) : 1;
    u32 seed = getenv("SEED") ? (u32)strtoul(getenv("SEED"), 0, 0) : 0;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    long tried = 0, same = 0, skipped = 0, plain = 0, plain_same = 0;
    int nrec = 0;
    for (int fi = 1; fi < argc; fi++) {
        if (fi == 2 || fi == 3) continue;
        if (!strcmp(argv[fi], "--")) continue;
        int fuzz = fi == 1;
        FILE *f = fopen(argv[fi], "rb");
        if (!f) { perror(argv[fi]); return 1; }
        static rec_t r, base; static u8 junk[0x4000]; static RcVu0 vj;
        char magic[4];
        while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
            u32 tops[2], t2[2]; int nk;
            if (fread(&base.pc, 4, 1, f) != 1 || fread(&base.v, sizeof base.v, 1, f) != 1 || fread(base.mem, 1, 0x4000, f) != 0x4000 || fread(tops, 4, 2, f) != 2) break;
            if (fread(&nk, 4, 1, f) != 1) break;
            for (int i = 0; i < nk; i++) { u32 n; if (fread(&n, 4, 1, f) != 1 || fread(junk, 16, n, f) != n) return 1; }
            if (fread(&vj, sizeof vj, 1, f) != 1 || fread(junk, 1, 0x4000, f) != 0x4000 || fread(t2, 4, 2, f) != 2) break;
            nrec++;
            base.top = tops[0];
            /* the run as captured */
            char what[64]; snprintf(what, sizeof what, "%s rec %d", argv[fi], nrec - 1);
            int c0 = cmp_run(&base, verbose, what);
            if (c0 >= 0) { plain++; plain_same += c0; }
            for (u32 alt = 0x20; alt <= 0x30; alt += 0x10) {   /* the starts that only reload vf4 */
                r = base; r.pc = alt;
                snprintf(what, sizeof what, "%s rec %d at %02X", argv[fi], nrec - 1, alt);
                int c1 = cmp_run(&r, verbose, what);
                if (c1 >= 0) { plain++; plain_same += c1; }
            }
            if (!fuzz || base.pc != 0x10) continue;
            u32 B = base.top & 0xFFFF, Bd = B ? 328 : 0;
            int nv = nverts(base.mem, B);
            u32 idx = QW(base.mem, B + 3)[1] & 0xFFFF;
            /* the index entries and vertices the packet uses, the texture blocks */
            u32 ents[512]; int nents = 0;
            for (int k = 0; k < nv && nents < 512; k++) ents[nents++] = QW(base.mem, Bd + idx + k / 4)[k % 4] & 0xFFFF;
            int blocks[64], nblocks = 0;
            {
                u32 tbl = B + (QW(base.mem, B + 4)[2] & 0xFFFF);
                for (int t = 0; t < 64 && nblocks < 64; t++) {
                    u32 *q = QW(base.mem, tbl + t);
                    s16 c = (s16)q[0], z = (s16)q[2];
                    if ((t == 0 || c < 0) && z >= 0) blocks[nblocks++] = z;
                    if (t && !c) break;
                }
                if (!nblocks) blocks[nblocks++] = 0;
            }
            for (int t = 0; t < nvar; t++) {
                r = base;
                u8 *m = r.mem;
                rs = 0x9E3779B9u ^ (nrec * 7919u) ^ (t * 104729u) ^ seed; if (!rs) rs = 1; rnd(); rnd();
                snprintf(what, sizeof what, "rec %d var %d", nrec - 1, t);
                /* carried registers */
                if (rnd() % 2) r.v.clip = rnd() % 3 ? rnd() & 0xFFFFFF : 0;
                if (rnd() % 2) r.v.vi[15] = rnd() & 0xFFFF;
                { u32 w = rnd(); memcpy(&r.v.vf[26][3], &w, 4); w = rnd(); memcpy(&r.v.vf[27][3], &w, 4); }
                if (rnd() % 2) { u32 b[4] = { 823, 672, 823, 672 }; memcpy(r.v.vf[3], b, 16); }
                /* guard band and frustum */
                float *gb = (float *)QW(m, 662), *fr = (float *)QW(m, 665);
                int g = rnd() % 8;
                if (g >= 2) { gb[0] *= (float)(1u << (rnd() % 8)) * frand(1, 2); gb[1] *= (float)(1u << (rnd() % 8)) * frand(1, 2); }
                if (g >= 5) gb[2] *= (float)(1u << (rnd() % 31)) * frand(1, 2) * (rnd() % 2 ? 1 : -1);
                if (g == 7) gb[3] *= frand(0.05f, 1.0f);
                if (rnd() % 3 == 0) { fr[0] = frand(0.2f, 8); fr[1] = frand(0.2f, 8); fr[2] = frand(0.2f, 8); }
                /* the index list over the packet's own entries */
                if (rnd() % 2) {
                    int rep = rnd() % 4 == 0;
                    for (int k = 0; k < nv; k++) {
                        u32 e = ents[rnd() % nents];
                        if (rep && k && rnd() % 3 == 0) e = QW(m, Bd + idx + (k - 1) / 4)[(k - 1) % 4] & 0xFFFF;
                        QW(m, Bd + idx + k / 4)[k % 4] = e;
                    }
                }
                /* vertices far out or behind */
                if (rnd() % 2) {
                    int nmv = 1 + rnd() % 6;
                    for (int j = 0; j < nmv; j++) {
                        u32 va = QW(m, Bd + ents[rnd() % nents])[3] & 0xFFFF;
                        s32 *p = (s32 *)QW(m, Bd + va);
                        int how = rnd() % 4;
                        for (int k = 0; k < 3; k++) {
                            if (how == 0) p[k] = (s32)(p[k] * (s32)(1 + rnd() % 8)) * (rnd() % 2 ? 1 : -1);
                            else if (how == 1) p[k] += (s32)(rnd() % 400000) - 200000;
                            else if (how == 2) p[k] = -p[k];
                            else p[k] += (s32)(rnd() % 20000) - 10000;
                        }
                    }
                }
                /* the whole fragment moved */
                if (rnd() % 6 == 0) {
                    float *tr = (float *)QW(m, B + 8);
                    tr[rnd() % 4] *= frand(-4, 4);
                }
                /* new strips */
                if (rnd() % 2) new_table(m, B, nv, blocks, nblocks);
                /* stale clipper stages (not from the game: a check of the memory semantics) */
                if (rnd() % 16 == 0) {
                    int s = rnd() % 6;
                    QW(m, 974 + 7 * s)[rnd() % 2] = 823 + 28 + 3 * (rnd() % 3);
                }
                if (getenv("FUZZ_REC") && (atoi(getenv("FUZZ_REC")) != nrec - 1 || atoi(getenv("FUZZ_VAR")) != t)) continue;
                int c = cmp_run(&r, verbose, what);
                if (c < 0) { skipped++; continue; }
                tried++; same += c;
                if (!c && verbose > 1) exit(1);
            }
        }
        fclose(f);
    }
    printf("%d runs read, %ld compared as captured (%ld identical); %ld variants compared, %ld identical (kicks, VU memory, registers); %ld not handled; %ld kicks, %ld clipped-triangle packets\n",
           nrec, plain, plain_same, tried, same, skipped, n_kicks, n_clipped);
    {
        int hit = 0, tot = 0x1DC8 / 8 + 1;
        for (int i = 0; i < tot; i++) hit += tfc_cov[i];
        printf("coverage: %d of %d program addresses\n", hit, tot);
        if (getenv("COV")) for (int i = 0; i < tot; i++) if (!tfc_cov[i]) {
            int j = i; while (j + 1 < tot && !tfc_cov[j + 1]) j++;
            printf("  not reached: %04X-%04X\n", i * 8, j * 8); i = j;
        }
    }
    return 0;
}
