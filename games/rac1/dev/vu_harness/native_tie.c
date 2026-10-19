/* Native ties (VU1 image 13507), harness version: the class from the data the game unpacked for
 * the setup run (0x0030), each instance from the instance block (0x0000), drawn into a GIF packet
 * laid out as the program lays it out, so native_compare() can check it word for word. */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "harness.h"
#ifndef USE_FAT
#define USE_FAT 0
#endif
#ifndef FAT_IDX_SKIP
#define FAT_IDX_SKIP 0
#endif
#ifndef FAT_I0
#define FAT_I0 0
#define FAT_I1 1
#define FAT_I2 2
#endif

typedef struct { s32 p[4]; s32 st[4]; } Dinky;            /* x y z slot | s t q slot2 */
typedef struct { s32 a[4]; s32 b[4]; s32 st[4]; } Fat;     /* delta | base | s t q slot */
typedef struct {
    int nad; u32 ad_dest[4]; u32 ad[4][5][4];
    u32 hb[12];
    int nstrip; u32 strip_count[64], strip_off[64];
    int ndinky, nfat; Dinky dinky[256]; Fat fat[128];
    u32 tag_ad[4], tag_strip[4];
    int valid;
} TieClass;
static TieClass cls;
static const u32 *Q(const u8 *mem, u32 a) { return (const u32 *)(mem + (a & 0x3FF) * 16); }

void native_reset(void) { memset(&cls, 0, sizeof cls); }

static void setup(const u8 *mem)
{
    TieClass *c = &cls;
    memset(c, 0, sizeof *c);
    const u32 *d0 = Q(mem, 0);
    c->ad_dest[0] = 0; c->nad = 1;
    for (int k = 0; k < 4 && (s32)d0[k] > 0; k++) c->ad_dest[c->nad++] = d0[k];
    for (int a = 0; a < c->nad; a++) for (int r = 0; r < 5; r++) memcpy(c->ad[a][r], Q(mem, 1 + a * 5 + r), 16);
    u32 h = 1 + c->nad * 5;                       /* the unpack header: 12 bytes, one per word */
    u32 hb[12]; for (int k = 0; k < 12; k++) hb[k] = Q(mem, h + k / 4)[k % 4];
    memcpy(c->hb, hb, sizeof hb);
    c->nstrip = hb[3];
    for (int s = 0; s < c->nstrip && s < 64; s++) { const u32 *q = Q(mem, h + 3 + s); c->strip_count[s] = q[0]; c->strip_off[s] = q[2]; }
    c->ndinky = ((s32)hb[8] - 4) / 2; if (c->ndinky < 0) c->ndinky = 0; if (c->ndinky > 256) c->ndinky = 256;
    for (int j = 0; j < c->ndinky && j < 256; j++) { memcpy(c->dinky[j].p, Q(mem, 50 + 2 * j), 16); memcpy(c->dinky[j].st, Q(mem, 51 + 2 * j), 16); }
    c->nfat = hb[11]; if (c->nfat > 128) c->nfat = 128;
    u32 f0 = 50 + hb[8] - 4;
    for (int j = 0; j < c->nfat && j < 128; j++) { memcpy(c->fat[j].a, Q(mem, f0 + 3 * j), 16); memcpy(c->fat[j].b, Q(mem, f0 + 3 * j + 1), 16); memcpy(c->fat[j].st, Q(mem, f0 + 3 * j + 2), 16); }
    memcpy(c->tag_ad, Q(mem, 966), 16); memcpy(c->tag_strip, Q(mem, 967), 16);
    c->valid = 1;
}

static u32 out[0x400 * 4], out_n; static u8 ign3[0x400];   /* ST qwords: word 3 is not read by the GS */
static int ftoi4(float f) { return (int)(f * 16.0f); }
static float itof12(s32 i) { return (float)i * (1.0f / 4096.0f); }

static void put_vertex(u32 slot, const float st[3], const u32 rgba[4], const s32 xyz[3], u32 fog)
{
    slot &= 0x3FF; u32 *o = out + slot * 4;
    memcpy(o, st, 12); o[3] = 0; ign3[slot & 0x3FF] = 1;
    o[4] = rgba[0]; o[5] = rgba[1]; o[6] = rgba[2]; o[7] = rgba[3];
    o[8] = xyz[0]; o[9] = xyz[1]; o[10] = xyz[2]; o[11] = fog;
    if (slot * 1 + 3 > out_n) out_n = slot + 3;
}

static void draw(const u8 *mem, u32 top)
{
    TieClass *c = &cls;
    float R[6][4]; for (int k = 0; k < 6; k++) memcpy(R[k], Q(mem, 0xC6 + top + k), 16);
    const u32 *idx = Q(mem, 0xCC + top);       /* color indexes, one word each */
    memset(out, 0, sizeof out); memset(ign3, 0, sizeof ign3); out_n = 0;
    for (int a = 0; a < c->nad; a++) {
        u32 *o = out + c->ad_dest[a] * 4; memcpy(o, c->tag_ad, 16); o[3] = 0;
        for (int r = 0; r < 5; r++) memcpy(o + 4 + r * 4, c->ad[a][r], 16);
    }
    for (int s = 0; s < c->nstrip; s++) {
        u32 *o = out + c->strip_off[s] * 4; memcpy(o, c->tag_strip, 16);
        o[0] = c->strip_count[s] | (s == c->nstrip - 1 ? 0x8000 : 0);
        u32 end = c->strip_off[s] + 1 + 3 * c->strip_count[s]; if (end > out_n) out_n = end;
    }
    s32 fog = ftoi4(R[5][3]);
    for (int j = 0; j < c->ndinky; j++) {
        const Dinky *v = &c->dinky[j];
        float x = (float)v->p[0], y = (float)v->p[1], z = (float)v->p[2];
        float p[4];
        for (int k = 0; k < 4; k++) { float acc = R[3][k] * 1.0f; acc = acc + R[0][k] * x; acc = acc + R[1][k] * y; p[k] = R[2][k] * z + acc; }
        float q = 1.0f / p[3];
        s32 xyz[3] = { ftoi4(p[0] * q), ftoi4(p[1] * q), ftoi4(p[2] * q) };
        float st[3] = { itof12(v->st[0]) * q, itof12(v->st[1]) * q, itof12(v->st[2]) * q };
        const u32 *col = Q(mem, 838 + idx[j]);
        put_vertex(v->p[3], st, col, xyz, fog);
        if (v->st[3] && v->st[3] != v->p[3]) put_vertex(v->st[3], st, col, xyz, fog);
    }
    /* fat vertices: a position morphed between two (base + delta * k) and a color blended
     * from three of the instance's colors (one, and the mean of two others) */
    u32 fq = (c->ndinky + 3) / 4 + FAT_IDX_SKIP;
    for (int j = 0; j < (USE_FAT ? c->nfat : 0); j++) {
        const Fat *v = &c->fat[j];
        float x = (float)v->a[0] * R[4][0] + (float)v->b[0];
        float y = (float)v->a[1] * R[4][0] + (float)v->b[1];
        float z = (float)v->a[2] * R[4][0] + (float)v->b[2];
        float p[4];
        for (int k = 0; k < 4; k++) { float acc = R[3][k] * 1.0f; acc = acc + R[0][k] * x; acc = acc + R[1][k] * y; p[k] = R[2][k] * z + acc; }
        float q = 1.0f / p[3];
        s32 xyz[3] = { ftoi4(p[0] * q), ftoi4(p[1] * q), ftoi4(p[2] * q) };
        float st[3] = { itof12(v->st[0]) * q, itof12(v->st[1]) * q, itof12(v->st[2]) * q };
        const u32 *iq = Q(mem, 0xCC + top + fq + j);
        const float *c9 = (const float *)Q(mem, 838 + iq[FAT_I0]), *c10 = (const float *)Q(mem, 838 + iq[FAT_I1]), *c11 = (const float *)Q(mem, 838 + iq[FAT_I2]);
        u32 col[4];
        for (int k = 0; k < 4; k++) {
            float acc = c10[k] * 0.5f; float m = c11[k] * 0.5f + acc;
            acc = m * R[4][3]; float f = c9[k] * R[4][2] + acc;
            memcpy(&col[k], &f, 4);
        }
        put_vertex(v->a[3], st, col, xyz, fog);
        if (v->st[3] && v->st[3] != v->a[3]) put_vertex(v->st[3], st, col, xyz, fog);
    }
}

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    (void)v;
    if (pc == 0x30) { setup(mem); return -1; }
    if (pc == 0x00 && cls.valid) { draw(mem, top); return 0; }
    return -1;
}

/* compare against the program's packets: the last kick holds this instance's packet */
static int cmp_words(const u32 *a, const u32 *b, u32 nq, int verbose)
{
    int bad = 0;
    for (u32 q = 0; q < nq; q++) {
        const u32 *x = a + q * 4, *y = b + q * 4;
        for (int k = 0; k < 4; k++) {
            if (x[k] == y[k] || (k == 3 && ign3[q])) continue;
            float fx, fy; memcpy(&fx, &x[k], 4); memcpy(&fy, &y[k], 4);
            if (fabsf(fx - fy) <= 1e-6f * fabsf(fy) + 1e-30f) continue;
            if (verbose > 1 && bad < 6) fprintf(stderr, "    qw %u word %d: native %08X program %08X\n", q, k, x[k], y[k]);
            bad++;
        }
    }
    return bad;
}
int native_compare(const packet_t *k, int nk, int verbose)
{
    if (nk < 1) return 0;
    const packet_t *p = &k[nk - 1];
    int bad = cmp_words(out, p->d, p->n < out_n ? p->n : out_n, verbose);
    if (p->n != out_n && verbose > 1) fprintf(stderr, "    size: native %u program %u\n", out_n, p->n);
    if ((bad || p->n != out_n) && verbose) { fprintf(stderr, "    class hb"); for (int i = 0; i < 12; i++) fprintf(stderr, " %d", cls.hb[i]); fprintf(stderr, " nad %d dinky %d\n", cls.nad, cls.ndinky); }
    return bad == 0 && p->n == out_n;
}
