/* Shrubs (small plants, grass, reeds), drawn by native code from the game's shrub data
 * instead of by the PS2's VU1 shrub microprogram (image 56467).
 *
 * Runs: 0x0000 once a frame (constants: the A+D tag, fog scale/offset, fog clamp), 0x0088 or
 * 0x00A8 per shrub class (its packet: texture registers, strip tags, vertices), 0x0318 per group
 * of up to five instances (a 4x4 transform and a color table each). Each instance becomes one
 * GS packet, built here word for word as the program built it (checked against the program
 * by dev/vu_harness on every shrub run of whole frames). */
#include "native_shrub.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "native_dlist.h"
static void sm_class(void);
static void dec_check(const u8 *mem, u32 top);
/* the shrub programs' memory for the direct draws (below): VU1's, which the other VU1 programs still
 * share (the quirk reads what they leave: a tie clip packet's strip tag, for one), or with
 * UYA_SHRUBPRIV=1 a private image of it (exact only while nothing else writes there) */
static u8 pimg_own[1024 * 16], *pimg; static int in_seg;
static int sm_draw(const float R[4][4], s32 fogi, const u8 *rgba);
static void sm_class(void);

static const u32 *Q(const u8 *mem, u32 a) { return (const u32 *)(mem + (a & 0x3FF) * 16); }
static float fc(float x) { u32 b; memcpy(&b, &x, 4); if ((b & 0x7F800000u) == 0x7F800000u) { b = (b & 0x80000000u) | 0x7F7FFFFFu; memcpy(&x, &b, 4); } return x; }
static s32 ftoi4(float f) { float x = f * 16.0f; return x >= 2147483647.0f ? 0x7FFFFFFF : x <= -2147483648.0f ? (s32)0x80000000u : (s32)x; }
static float itof12(s32 i) { return (float)i * (1.0f / 4096.0f); }

typedef struct ShState {
    u32 tag_ad[4];          /* qword 0 at the frame's start: the A+D tag */
    float fogk[4], clamp[4];/* qwords 1 and 2: z, w: fog = row3.w * z + w; clamp: x = max, y = min */
    int have_consts;
    u32 bufx, bufy;         /* the program's output buffers: instances go to y, then x and y swap;
                               a class's packet template goes to the third buffer (x) and y */
    int valid;
    u32 top;                /* the class's place in VU memory */
    u32 nstrip, nvert, voff, nad;
    u32 tags[16][4], ad[4][4][4];
    s32 pos[256][4], st[256][4];   /* the class's vertices as the game unpacked them (the program
                                      converts them in place; nothing else reads them) */
} ShState;
static ShState sh;


/* a class packet at VU memory top into sh */
static void load_class(const u8 *mem, u32 top)
{
    const u32 *h = Q(mem, top);
    sh.nad = h[0]; sh.nstrip = h[1] < 16 ? h[1] : 16; sh.nvert = h[2] < 256 ? h[2] : 256; sh.voff = h[3];
    for (u32 s = 0; s < sh.nstrip; s++) memcpy(sh.tags[s], Q(mem, top + 1 + s), 16);
    if (sh.nad > 4) sh.nad = 4;
    for (u32 b = 0; b < sh.nad; b++) for (u32 a = 0; a < 4; a++) memcpy(sh.ad[b][a], Q(mem, top + 1 + sh.nstrip + b * 4 + a), 16);
    for (u32 j = 0; j < sh.nvert; j++) { memcpy(sh.pos[j], Q(mem, top + sh.voff + j), 16); memcpy(sh.st[j], Q(mem, top + sh.voff + h[2] + j), 16); }
    sh.top = top; sh.valid = 1;
}

int native_shrub_run(u8 *mem, u32 pc, u32 top, void (*emit)(const u32 *, u32))
{
    switch (pc) {
    case 0x0000:
        memcpy(sh.tag_ad, Q(mem, 0), 16); memcpy(sh.fogk, Q(mem, 1), 16); memcpy(sh.clamp, Q(mem, 2), 16);
        sh.bufx = 856; sh.bufy = 520;
        sh.have_consts = 1;
        return 1;
    case 0x0088: case 0x00A8: {
        /* the next instance group goes to 238 or 379 (617 minus what is kept at qword 1, y) */
        u32 *g = (u32 *)(mem + 1 * 16);
        g[1] = (g[1] & 0xFFFF0000u) | (pc == 0x0088 ? 379u : 238u);
        { static int dc = -1; if (dc < 0) dc = getenv("UYA_SHRUBDEC") != 0; if (dc && !in_seg) dec_check(mem, top); }
        load_class(mem, top);
        sm_class();
        sh.bufx = 2064 - sh.bufx - sh.bufy;        /* the buffer neither of the last two used */
        u32 bufs[2] = { sh.bufx, sh.bufy };
        for (int k = 0; k < 2; k++) {
            u8 *b = mem + bufs[k] * 16;
            for (u32 a = 0; a < sh.nad; a++) {
                u32 d = sh.ad[a][0][3] & 0x3FF;
                if (bufs[k] + d + 5 > 0x400) continue;
                memcpy(b + d * 16, sh.tag_ad, 16);
                for (u32 r = 0; r < 4; r++) memcpy(b + (d + 1 + r) * 16, sh.ad[a][r], 16);
            }
            for (u32 t = 0; t < sh.nstrip; t++) { u32 off = sh.tags[t][3] & 0x3FF; if (bufs[k] + off < 0x400) memcpy(b + off * 16, sh.tags[t], 16); }
        }
        return 1;
    }
    case 0x0318: {
        if (!sh.valid || !sh.have_consts) return 0;
        u32 *g = (u32 *)(mem + 1 * 16);
        u32 grp = (617u - (g[1] & 0xFFFF)) & 0xFFFF;
        g[1] = (g[1] & 0xFFFF0000u) | grp;
        u32 n = Q(mem, grp)[0];
        if (!in_seg) { extern void native_shrub_gchk(const u8 *, u32, u32); native_shrub_gchk(mem, grp, n); }
        static u32 pk[0x400 * 4];
        for (u32 i = 0; i < n && i < 8; i++) {
            u32 base = grp + 1 + i * 28;
            float R[4][4]; for (int k = 0; k < 4; k++) memcpy(R[k], Q(mem, base + k), 16);
            u32 cols = base + 4;
            u32 buf = sh.bufy;
            u8 *bm = mem + buf * 16;
            u32 size = 0;
            for (u32 b = 0; b < sh.nad; b++) { u32 d = sh.ad[b][0][3] & 0x3FF; if (d + 5 > size) size = d + 5; }
            for (u32 t = 0; t < sh.nstrip; t++) { u32 end = (sh.tags[t][3] & 0x3FF) + 1 + 3 * (sh.tags[t][0] & 0x7FFF); if (end > size) size = end; }
            float fog = fc(fc(R[3][3] * sh.fogk[2]) + sh.fogk[3]);
            if (fog < sh.clamp[0]) fog = sh.clamp[0];
            if (fog > sh.clamp[1]) fog = sh.clamp[1];
            s32 fogi = ftoi4(fog);
            u32 alpha = Q(mem, cols)[3];
            static u8 rgba[256 * 4];
            /* the program's quirk: when the third vertex ends the list early, the fourth vertex's
               color is read with the instance's color base added twice (whatever lies there) */
            int quirk = sh.nvert > 3 && (sh.st[2][3] & 0x8000);
            for (u32 j = 0; j < sh.nvert; j++) {
                const s32 *p = sh.pos[j], *st = sh.st[j];
                float x = (float)p[0], y = (float)p[1], z = (float)p[2], v[4];
                for (int k = 0; k < 4; k++) { float acc = fc(R[3][k]); acc = fc(acc + fc(R[0][k] * x)); acc = fc(acc + fc(R[1][k] * y)); v[k] = fc(fc(R[2][k] * z) + acc); }
                float q = v[3] == 0.0f ? 3.4028235e38f : 1.0f / v[3];
                u32 slot = (u32)p[3] & 0x7FFF;
                if (buf + slot + 3 > 0x400) continue;
                u32 *o = (u32 *)(bm + slot * 16);
                float sq[3] = { fc(itof12(st[0]) * q), fc(itof12(st[1]) * q), fc(itof12(st[2]) * q) };
                memcpy(o, sq, 12);
                u32 ci = ((u32)st[3] & 0x7FFF) + cols;
                if (quirk && j == 3) ci += cols;
                const u32 *c = Q(mem, ci);
                o[4] = c[0]; o[5] = c[1]; o[6] = c[2]; o[7] = alpha;
                rgba[j * 4] = (u8)c[0]; rgba[j * 4 + 1] = (u8)c[1]; rgba[j * 4 + 2] = (u8)c[2]; rgba[j * 4 + 3] = (u8)alpha;
                o[8] = (u32)ftoi4(fc(v[0] * q)); o[9] = (u32)ftoi4(fc(v[1] * q)); o[10] = (u32)ftoi4(fc(v[2] * q)); o[11] = (u32)fogi;
            }
            if (buf + size > 0x400) size = 0x400 - buf;
            memcpy(pk, bm, size * 16);
            { extern void native_shrub_pchk(int, const u32 *, u32, const u8 *, u32); native_shrub_pchk(in_seg, pk, size, rgba, sh.nvert); }
            { u32 t = sh.bufx; sh.bufx = sh.bufy; sh.bufy = t; }
            if (!sm_draw((const float (*)[4])R, fogi, rgba)) emit(pk, size);
        }
        return 1;
    }
    }
    return 0;
}

/* ---- shrubs as native meshes ----
 * As ties (native_tie.c): a class's vertices, texture coordinates and strips become a mesh the
 * first time its packet is seen (found again by its contents, the texture registers' values
 * aside); an instance is that mesh with its transform, colours (its table, its alpha) and fog,
 * in place of its GS packet. The program's output buffers in VU memory are still written as
 * the program wrote them. The packet's shape is checked when the mesh is made (items one after
 * another from qword 0 to the end-of-packet tag, every strip vertex a vertex's place, q 1, the
 * packet within its buffer); else the class goes as packets. UYA_NSHRUBMESH=0: the packets. */
#define SM_ID0 20480u
#define SM_N 4096u
typedef struct { int kind; u32 blk, first, count; u64 prim; int pre; } SmOp;   /* kind 0: a texture block; 1: strips */
typedef struct { u32 *key; u32 nkey; int ok; u32 nv; u8 *rgba; SmOp *op; int nop; } ShrubMesh;
static ShrubMesh *smt; static u32 nsm;
static int sm_on = -1, sm_cur = -1;
static unsigned long long sm_n_mesh, sm_n_pk;
#define SH_H 8192
static u64 sh_k[SH_H]; static int sh_v[SH_H];
static u32 skey[8 + 16 * 4 + 4 * 2 + 256 * 8];

static u32 sm_key(void)
{
    u32 n = 0;
    skey[n++] = sh.nad; skey[n++] = sh.nstrip; skey[n++] = sh.nvert;
    memcpy(skey + n, sh.tag_ad, 16); n += 4;
    for (u32 t = 0; t < sh.nstrip; t++) { memcpy(skey + n, sh.tags[t], 16); n += 4; }
    for (u32 b = 0; b < sh.nad; b++) for (u32 a = 0; a < 4; a++) skey[n++] = (sh.ad[b][a][2] & 0xFF) | (a ? 0 : (sh.ad[b][0][3] & 0x3FF) << 8);
    for (u32 j = 0; j < sh.nvert; j++) { memcpy(skey + n, sh.pos[j], 16); n += 4; memcpy(skey + n, sh.st[j], 16); n += 4; }
    return n;
}
static void sm_drop(void)
{
    for (u32 i = 0; i < nsm; i++) { if (smt[i].ok) native_mesh_free(SM_ID0 + i); free(smt[i].key); free(smt[i].rgba); free(smt[i].op); }
    nsm = 0; memset(sh_k, 0, sizeof sh_k);
}
static int sm_fail(int line) { static int n; if (n++ < 8) uya_log("native shrub meshes: a class goes as packets (native_shrub.c:%d)", line); return 0; }
static int sm_build(ShrubMesh *m, u32 id)
{
    u64 ta = ((u64)sh.tag_ad[1] << 32) | sh.tag_ad[0];
    int ad1 = (ta & 0x7FFF) == 4 && ((ta >> 60) & 15) == 1 && (sh.tag_ad[2] & 15) == 0xE, ad4 = (ta & 0x7FFF) == 1 && ((ta >> 60) & 15) == 4 && (sh.tag_ad[2] & 0xFFFF) == 0xEEEE;   /* (four A+D qwords, either way) */
    if (!(ad1 || ad4) || ((ta >> 58) & 3) || (ta & 0x8000)) { static int t; if (!t++) uya_log("native shrub meshes: A+D tag %08X %08X %08X %08X", sh.tag_ad[0], sh.tag_ad[1], sh.tag_ad[2], sh.tag_ad[3]); return sm_fail(__LINE__); }
    static s8 item[0x400]; static s16 itemk[0x400], start[0x400], vtx[0x400];
    memset(item, 0, sizeof item); memset(start, 0xFF, sizeof start); memset(vtx, 0xFF, sizeof vtx);
    const u32 lim = 1024 - 856;                    /* (the last buffer's room: a packet within it is within every buffer) */
    for (u32 b = 0; b < sh.nad; b++) {
        u32 d = sh.ad[b][0][3] & 0x3FF;
        if (d + 5 > lim) return sm_fail(__LINE__);
        for (u32 q = d; q < d + 5; q++) if (item[q]) return sm_fail(__LINE__);
        item[d] = 1; itemk[d] = (s16)b; for (u32 q = d + 1; q < d + 5; q++) item[q] = 3;
    }
    for (u32 t = 0; t < sh.nstrip; t++) {
        u64 ts = ((u64)sh.tags[t][1] << 32) | sh.tags[t][0];
        if (((ts >> 58) & 3) || ((ts >> 60) & 15) != 3 || (sh.tags[t][2] & 0xFFF) != 0x412) return sm_fail(__LINE__);
        u32 d = sh.tags[t][3] & 0x3FF;
        if (d + 1 + 3 * (sh.tags[t][0] & 0x7FFF) > lim || item[d]) return sm_fail(__LINE__);
        item[d] = 2; itemk[d] = (s16)t;
    }
    for (u32 j = 0; j < sh.nvert; j++) {
        if (sh.st[j][2] != 4096) return sm_fail(__LINE__);
        u32 sl = (u32)sh.pos[j][3] & 0x7FFF;
        if (sl + 3 > lim) return sm_fail(__LINE__);
        for (u32 q = sl; q < sl + 3; q++) {
            if (item[q]) return sm_fail(__LINE__);
            if (start[q] >= 0 && (u32)start[q] != sl) return sm_fail(__LINE__);
            start[q] = (s16)sl;
        }
        vtx[sl] = (s16)j;
    }
    static float v6[256 * 6]; static u32 idx[0x400 * 3]; static SmOp op[64];
    u32 ni = 0; int nop = 0, eop = 0, blk = 0;
    for (u32 q = 0; q < lim && !eop;) {
        if (item[q] == 1) {
            if (nop == 64) return sm_fail(__LINE__);
            op[nop].kind = 0; op[nop].blk = (u32)itemk[q]; nop++; blk = 1;
            q += 5;
        } else if (item[q] == 2) {
            const u32 *tg = sh.tags[itemk[q]];
            u64 ts = ((u64)tg[1] << 32) | tg[0];
            u32 n = tg[0] & 0x7FFF; int pre = (int)((ts >> 46) & 1); u64 prim = (ts >> 47) & 0x7FF;
            if (!blk) return sm_fail(__LINE__);
            if (!(nop && op[nop - 1].kind == 1 && op[nop - 1].pre == pre && op[nop - 1].prim == prim)) {
                if (nop == 64) return sm_fail(__LINE__);
                op[nop].kind = 1; op[nop].first = ni; op[nop].count = 0; op[nop].pre = pre; op[nop].prim = prim; nop++;
            }
            u32 w[3] = { 0, 0, 0 };
            for (u32 k = 0; k < n; k++) {
                u32 sl = q + 1 + 3 * k;
                if (vtx[sl] < 0) return sm_fail(__LINE__);
                w[0] = w[1]; w[1] = w[2]; w[2] = (u32)vtx[sl];
                if (k >= 2) { idx[ni++] = w[0]; idx[ni++] = w[1]; idx[ni++] = w[2]; op[nop - 1].count += 3; }
            }
            if (tg[0] & 0x8000) eop = 1;
            q += 1 + 3 * n;
        } else return sm_fail(__LINE__);
    }
    if (!eop || !ni) return sm_fail(__LINE__);
    for (u32 j = 0; j < sh.nvert; j++) {
        float *o = v6 + j * 6;
        o[0] = (float)sh.pos[j][0]; o[1] = (float)sh.pos[j][1]; o[2] = (float)sh.pos[j][2]; o[3] = 1.0f;
        o[4] = itof12(sh.st[j][0]); o[5] = itof12(sh.st[j][1]);
    }
    m->nv = sh.nvert; m->rgba = calloc(sh.nvert, 4);
    m->op = malloc(nop * sizeof *op); memcpy(m->op, op, nop * sizeof *op); m->nop = nop;
    native_mesh_create(SM_ID0 + id, v6, m->rgba, m->nv, idx, ni);
    return 1;
}
static void sm_class(void)
{
    sm_cur = -1;
    if (sm_on < 0) { const char *e = getenv("UYA_NSHRUBMESH"), *n = getenv("UYA_NATIVE"); sm_on = !(e && !atoi(e)) && !(n && !atoi(n)); }
    if (!sm_on) return;
    u32 nk = sm_key();
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < nk; i++) h = (h ^ skey[i]) * 1099511628211ull;
    if (!h) h = 1;
    u32 s = (u32)(h >> 51) & (SH_H - 1);
    while (sh_k[s]) {
        if (sh_k[s] == h) { ShrubMesh *m = &smt[sh_v[s]]; if (m->nkey == nk && !memcmp(m->key, skey, nk * 4)) { sm_cur = sh_v[s]; return; } }
        s = (s + 1) & (SH_H - 1);
    }
    if (!smt) smt = calloc(SM_N, sizeof *smt);
    if (nsm == SM_N) { sm_drop(); sm_class(); return; }
    ShrubMesh *m = &smt[nsm];
    memset(m, 0, sizeof *m);
    m->key = malloc(nk * 4); memcpy(m->key, skey, nk * 4); m->nkey = nk;
    m->ok = sm_build(m, nsm);
    sh_k[s] = h; sh_v[s] = (int)nsm;
    sm_cur = (int)nsm++;
}
/* an instance as mesh mi: texture blocks ad (the class's), transform R, then screen scale sc and
 * offset of (clip: the GPU cuts at the near plane) */
static int sm_draw_x(int sm_cur, const u32 (*ad)[4][4], const float R[4][4], s32 fogi, const u8 *rgba, const float sc[3], const float of[3], int clip)
{
    if (sm_cur < 0 || !smt[sm_cur].ok || (fogi & 0x8000)) { if (++sm_n_pk % 100000 == 1) uya_log("native shrub meshes: %llu instances as packets (class %d, fog %08X)", sm_n_pk, sm_cur, (u32)fogi); return 0; }
    ShrubMesh *m = &smt[sm_cur];
    u32 id = SM_ID0 + (u32)sm_cur;
    if (memcmp(rgba, m->rgba, m->nv * 4)) { memcpy(m->rgba, rgba, m->nv * 4); native_mesh_colors(id, m->rgba, m->nv); }
    float xf[32];
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) xf[r * 4 + k] = k < 3 ? R[r][k] * sc[k] : R[r][k];
    float F = (float)((fogi >> 4) & 0xFF);
    xf[16] = 1.0f; xf[17] = F; xf[18] = F; xf[19] = 0.0f;
    xf[20] = of[0]; xf[21] = of[1]; xf[22] = of[2]; xf[23] = -3.4028235e38f;   /* (no near limit: these shrubs are inside the view) */
    xf[24] = xf[25] = xf[26] = xf[27] = 0.0f;
    xf[28] = 0.0f; xf[29] = 0.0f; xf[30] = 1e-30f; xf[31] = 1.0f;   /* (the GPU: no near or far cut but w > 0) */
    if (clip) { if (sc[2] == 0.0f) return 0; xf[28] = 1.0f; xf[30] = 1.0f / sc[2]; }   /* (the clip program's: z >= -w; x y: the screen's edges) */
    extern const char *uya_gs_src; uya_gs_src = clip ? "native shrub clip" : "native shrub";
    for (int k = 0; k < m->nop; k++) {
        const SmOp *o = &m->op[k];
        if (o->kind == 0) {
            u32 a[4]; u64 v[4];
            for (int j = 0; j < 4; j++) { const u32 *q = ad[o->blk][j]; a[j] = q[2] & 0xFF; v[j] = ((u64)q[1] << 32) | q[0]; }
            native_regs(a, v, 4);
        } else {
            if (o->pre) { u32 a = 0; u64 v = o->prim; native_regs(&a, &v, 1); }
            native_mesh_draw(id, o->first, o->count, xf);
        }
    }
    if (++sm_n_mesh % 100000 == 0) { u32 bad = 0; for (u32 i = 0; i < nsm; i++) bad += !smt[i].ok; uya_log("native shrub meshes: %u classes (%u as packets), instances %llu as meshes, %llu as packets", nsm, bad, sm_n_mesh, sm_n_pk); }
    return 1;
}
static int sm_draw(const float R[4][4], s32 fogi, const u8 *rgba)
{
    static const float one[3] = { 1, 1, 1 }, zero[3] = { 0, 0, 0 };
    return sm_draw_x(sm_cur, (const u32 (*)[4][4])sh.ad, R, fogi, rgba, one, zero, 0);
}

/* the shrub clip program (912339): its class packets are the main program's (at base); decoded
 * before the program converts them in place, into a class of their own */
static ShState csh; static int sm_clip_cur = -1;
void native_shrub_clip_class(const u8 *mem, u32 base)
{
    static ShState keep; int kc = sm_cur;
    keep = sh;
    load_class(mem, base); sm_class();
    csh = sh; sm_clip_cur = sm_cur;
    sh = keep; sm_cur = kc;
}
/* a group of instances (at VU memory grp) as meshes the GPU cuts at the near plane; c22, c23: the
 * screen scale and offset, c29 the fog clamp, c30 z w the fog scale and offset. 1: all drawn */
int native_shrub_clip_group(const u8 *mem, u32 grp, const float c22[4], const float c23[4], const float c29[4], const float c30[4])
{
    if (sm_clip_cur < 0 || !smt[sm_clip_cur].ok || !csh.valid) { static int t; if (t++ < 3) uya_log("native shrub clip meshes: no class mesh (valid %d, mesh %d)", csh.valid, sm_clip_cur); return 0; }
    u32 n = Q(mem, grp)[0];
    if (n > 8) return 0;
    static u8 rgba[8][256 * 4]; float R[8][4][4]; s32 fogi[8];
    for (u32 i = 0; i < n; i++) {
        u32 base = grp + 1 + i * 28, cols = base + 4;
        for (int k = 0; k < 4; k++) memcpy(R[i][k], Q(mem, base + k), 16);
        float fog = fc(fc(R[i][3][3] * c30[2]) + c30[3]);
        fog = fmaxf(fog, c29[0]); fog = fminf(fog, c29[1]);
        fogi[i] = ftoi4(fog);
        if (fogi[i] & 0x8000) return 0;
        u32 alpha = Q(mem, cols)[3];
        int quirk = csh.nvert > 3 && (csh.st[2][3] & 0x8000);
        for (u32 j = 0; j < csh.nvert; j++) {
            u32 ci = ((u32)csh.st[j][3] & 0x7FFF) + cols;
            if (quirk && j == 3) ci += cols;
            const u32 *c = Q(mem, ci);
            u8 *o = rgba[i] + j * 4; o[0] = (u8)c[0]; o[1] = (u8)c[1]; o[2] = (u8)c[2]; o[3] = (u8)alpha;
        }
    }
    for (u32 i = 0; i < n; i++) sm_draw_x(sm_clip_cur, (const u32 (*)[4][4])csh.ad, (const float (*)[4])R[i], fogi[i], rgba[i], c22, c23, 1);
    return 1;
}

/* ---- shrubs straight from the native ShrubProc (native_shrubproc.c) ----
 * ShrubProc's main and fading parts, when every packet of the frame can come here: in place of
 * their REFs and MSCALs the chain has markers, each naming a run of records drawn where the chain
 * walker meets it (the chain's order, its one-frame lag). The records are what the chain would have
 * given the program: a class packet (its slot; its VU place and start), an instance group (its VU
 * place, its count) and its instances (their block in the chain, their colour table, in the order
 * the REFs had: the fading part's colours first). The program's own code (above) then runs on a
 * private image of its memory, as it ran on VU1 memory: the class's templates and the instances' GS
 * packets go into the image's buffers, from which an instance with no mesh is sent, and from which
 * the program's quirk (the fourth vertex's colour, read from where earlier packets lie) takes its
 * colour, as on the PS2. A class packet is read from the level data once, through its own VIF codes
 * (the formats the shrub packets use), into the image's layout; its texture qwords are read again
 * from the level data at each draw (the game patches them). The image persists from frame to
 * frame, as VU memory does. */
#define EE(a) ((const u8 *)(uintptr_t)(a))
int uya_vif1_op_mpg_packet(const u32 *w, u32 qwc);
static u32 rd32(u32 a) { u32 v; memcpy(&v, EE(a), 4); return v; }
typedef struct { u32 p, qwc; u8 *img; u32 *src; int ok; u32 data, nq; u8 *raw, *vol; } DPk;   /* img: qwords 0..qwc (from TOPS); raw: the packet's
                                                    stream as read, vol: its qwords the game patches (the texture qwords) */
#define DPN 4096
#define DPH 8192
static DPk *dpk; static u32 ndpk, dp_sig = ~0u;
static u32 dph_k[DPH]; static int dph_v[DPH];
static u32 dconsts[12];
static unsigned long long d_pk, d_pkbad, d_inst;

void native_shrub_direct_frame(u32 sig, const u32 consts[12])
{
    memcpy(dconsts, consts, sizeof dconsts);
    if (sig == dp_sig) return;
    for (u32 i = 0; i < ndpk; i++) { free(dpk[i].img); free(dpk[i].src); free(dpk[i].raw); free(dpk[i].vol); }
    ndpk = 0; memset(dph_k, 0, sizeof dph_k);
    dp_sig = sig;
}

/* a packet's VIF codes (as the shrub packets have them) into img from qword 0 (TOPS 0); src: the
 * level data's address of each qword written whole by a V4-32 unpack (0: none) */
static int vif_class(const u8 *buf, u32 nbytes, u32 *img, u32 *src, u32 base, u32 *maxq)
{
    static const u8 SZ[16] = { 4, 2, 1, 0, 8, 4, 2, 0, 12, 6, 3, 0, 16, 8, 4, 2 };
    u32 cl = 4, wl = 4, mode = 0, row[4] = { 0, 0, 0, 0 }, o = 0;
    *maxq = 0;
    while (o + 4 <= nbytes) {
        u32 c; memcpy(&c, buf + o, 4); o += 4;
        u32 cmd = (c >> 24) & 0x7F, imm = c & 0xFFFF, num = (c >> 16) & 0xFF;
        if (cmd >= 0x60) {
            u32 k = cmd & 0xF, n = num ? num : 256, usn = (imm >> 14) & 1, addr = imm & 0x3FF;
            if (cmd & 0x10) return -1;                       /* (masked writes) */
            if (!(imm & 0x8000)) return -2;                  /* (an address not from TOPS) */
            if (!SZ[k] || k == 0xF || cl < wl || !wl) return -3;
            u32 vn = (k >> 2) & 3, vl = k & 3, nc = vn + 1;
            if (o + SZ[k] * n > nbytes) return -4;
            for (u32 i = 0; i < n; i++) {
                u32 qa = addr + (i / wl) * cl + i % wl;
                if (qa >= 0x400) return -5;
                const u8 *p = buf + o + i * SZ[k];
                u32 v[4];
                for (u32 j = 0; j < nc; j++) {
                    if (vl == 0) memcpy(&v[j], p + 4 * j, 4);
                    else if (vl == 1) { u16 h; memcpy(&h, p + 2 * j, 2); v[j] = usn ? h : (u32)(s32)(s16)h; }
                    else v[j] = usn ? p[j] : (u32)(s32)(s8)p[j];
                }
                if (nc == 1) v[1] = v[2] = v[3] = v[0];
                u32 m = nc == 1 ? 4 : nc;
                for (u32 j = 0; j < m; j++) img[qa * 4 + j] = mode == 1 ? v[j] + row[j] : v[j];
                src[qa] = (vl == 0 && nc == 4 && mode == 0) ? base + o + i * 16 : 0;
                if (qa + 1 > *maxq) *maxq = qa + 1;
            }
            o += (SZ[k] * n + 3) & ~3u;
        } else switch (cmd) {
        case 0x00: case 0x02: case 0x03: case 0x04: case 0x06: case 0x07: case 0x10: case 0x11: case 0x13: break;
        case 0x01: cl = imm & 0xFF; wl = imm >> 8; break;
        case 0x05: mode = imm & 3; if (mode > 1) return -6; break;
        case 0x30: if (o + 16 > nbytes) return -7; memcpy(row, buf + o, 16); o += 16; break;
        case 0x31: o += 16; break;
        case 0x20: o += 4; break;
        default: return -8;                                  /* (MSCAL, MPG, DIRECT...) */
        }
    }
    return 0;
}

/* the packet at table entry p (its data, its qwords << 4): its slot, or -1 (the frame goes into the chain) */
int native_shrub_direct_class(u32 p)
{
    if (sm_on < 0) { const char *e = getenv("UYA_NSHRUBMESH"), *n = getenv("UYA_NATIVE"); sm_on = !(e && !atoi(e)) && !(n && !atoi(n)); }
    u32 h = (p * 2654435761u) >> 19, data = rd32(p), nq = rd32(p + 4) >> 4;
    int at = -1;
    while (dph_k[h]) { if (dph_k[h] == p) { at = dph_v[h]; break; } h = (h + 1) & (DPH - 1); }
    if (at >= 0) {
        /* the same packet still (the level data can change under the same table: a level loading) */
        DPk *d = &dpk[at];
        int same = d->data == data && d->nq == nq;
        for (u32 q = 0; same && q < nq; q++) if (!d->vol[q] && memcmp(d->raw + q * 16, EE(data + q * 16), 16)) same = 0;
        if (same) return d->ok ? at : -1;
        free(d->img); free(d->src); free(d->raw); free(d->vol);
    } else {
        if (!dpk) dpk = calloc(DPN, sizeof *dpk);
        if (!dpk || ndpk == DPN) return -1;
        at = (int)ndpk++;
        dph_k[h] = p; dph_v[h] = at;
    }
    DPk *d = &dpk[at];
    memset(d, 0, sizeof *d);
    d->p = p; d->data = data; d->nq = nq;
    static u32 img[1024 * 4], src[1024];
    memset(img, 0, sizeof img); memset(src, 0, sizeof src);
    u32 maxq = 0;
    int r = nq <= 0x400 ? vif_class(EE(data), nq * 16, img, src, data, &maxq) : -9;
    d->raw = malloc(nq * 16 + 16); d->vol = calloc(nq + 1, 1);
    if (nq <= 0x400) memcpy(d->raw, EE(data), nq * 16);
    if (r == 0 && maxq) {
        d->qwc = maxq;
        d->img = malloc(maxq * 16); memcpy(d->img, img, maxq * 16);
        d->src = malloc(maxq * 4); memcpy(d->src, src, maxq * 4);
        /* its class as the program reads it: every texture qword from a source */
        static ShState keep;
        keep = sh;
        load_class(d->img, 0);
        int ok = sh.nad <= 4 && sh.nstrip <= 16;
        for (u32 b = 0; ok && b < sh.nad; b++) for (u32 a = 0; a < 4; a++) {
            u32 q = 1 + sh.nstrip + b * 4 + a;
            if (q >= maxq || !d->src[q]) { ok = 0; break; }
            u32 o = d->src[q] - data;                    /* (its bytes in the stream: patched by the game) */
            for (u32 k = o >> 4; k <= (o + 15) >> 4 && k < nq; k++) d->vol[k] = 1;
        }
        sh = keep;
        d->ok = ok;
        if (!ok) r = -10;
    }
    if (d->ok) d_pk++;
    else { d_pkbad++; static int told; if (told++ < 8) uya_log("native shrub direct: packet %08X (data %08X, %u qwords) stays in the chain (%d)", p, data, nq, r); }
    return d->ok ? at : -1;
}

/* segments: runs of records { kind, a, b, c } */
enum { R_START, R_PACKET, R_GROUP, R_INST, R_INSTF, R_CSTART, R_CPACKET, R_CGROUP, R_GIF };   /* (R_C*: the clip program's) */
#define DSEG 4096
#define DREC (1 << 17)
static struct { u32 first, count; } dseg[DSEG];
static u32 drec[DREC][4], dseg_next, drec_next, dseg_cur;
u32 native_shrub_seg_begin(void)
{
    u32 id = dseg_next++ % DSEG;
    dseg[id].first = drec_next; dseg[id].count = 0; dseg_cur = id;
    return id;
}
static void rec4(u32 k, u32 a, u32 b, u32 c) { u32 *r = drec[drec_next++ % DREC]; r[0] = k; r[1] = a; r[2] = b; r[3] = c; dseg[dseg_cur].count++; }
void native_shrub_seg_start(void) { rec4(R_START, 0, 0, 0); }
void native_shrub_seg_packet(int slot, u32 top, u32 pc, int clip) { rec4(clip ? R_CPACKET : R_PACKET, (u32)slot, top, pc); }
void native_shrub_seg_group(u32 at, u32 n, int clip) { rec4(clip ? R_CGROUP : R_GROUP, at, n, 0); }
void native_shrub_seg_cstart(u32 consts, u32 mpg_addr, u32 mpg_qwc) { rec4(R_CSTART, consts, mpg_addr, mpg_qwc); }
/* GS packets ShrubProc built itself (its billboards): kept here, each sent as VIF1 DIRECT sent it */
#define GRING (1u << 18)
static u32 (*gring)[4]; static u32 gring_next;
void native_shrub_seg_gif(const u32 (*q)[4], u32 n)
{
    if (!gring) gring = malloc(GRING * 16);
    if (!gring || n > GRING / 4) return;
    rec4(R_GIF, gring_next, n, 0);
    for (u32 k = 0; k < n; k++) memcpy(gring[(gring_next + k) % GRING], q[k], 16);
    gring_next = (gring_next + n) % GRING;
}
void native_shrub_seg_inst(u32 at, u32 blk, u32 col, int fading) { rec4(fading ? R_INSTF : R_INST, at, blk, col); }

static int dchk = -1;   /* (testing, UYA_SHRUBDCHK=1: the records run beside the chain's draws, compared) */
static u32 *gq[256]; static u32 gq_n[256], gq_h, gq_t;
int uya_shrubclip_run(u8 *mem, u32 pc);   /* (host/src/gfx/vu.c) */
static void d_emit(const u32 *pk, u32 qwc) { if (dchk > 0) return; extern const char *uya_gs_src; uya_gs_src = "PATH1 native shrub"; native_kick(pk, qwc); }
static u32 *pq[2048]; static u32 pq_n[2048], pq_h, pq_t;
void native_shrub_pchk(int priv, const u32 *pk, u32 n, const u8 *rgba, u32 nv)
{
    if (dchk <= 0) return;
    if (priv) {
        u32 k = pq_t++ % 2048;
        if (!pq[k]) pq[k] = malloc(0x400 * 16 + 1024);
        memcpy(pq[k], pk, n * 16); memcpy((u8 *)pq[k] + 0x400 * 16, rgba, nv * 4); pq_n[k] = n | nv << 16;
        return;
    }
    if (pq_h == pq_t) return;
    static unsigned long long np, bad;
    u32 k = pq_h++ % 2048;
    np++;
    if ((pq_n[k] & 0xFFFF) != n) { if (bad++ < 10) uya_log("shrub direct check: packet of %u qwords, the records' %u", n, pq_n[k] & 0xFFFF); return; }
    for (u32 q = 0; q < n; q++) if (memcmp(pk + 4 * q, pq[k] + 4 * q, 12)) {
        const u32 *a = pk + 4 * q, *b = pq[k] + 4 * q;
        if (bad++ < 10) uya_log("shrub direct check: packet qword %u of %u: VU %08X %08X %08X %08X, records %08X %08X %08X %08X", q, n, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
        return;
    }
    if (memcmp(rgba, (u8 *)pq[k] + 0x400 * 16, nv * 4)) {
        const u8 *b = (u8 *)pq[k] + 0x400 * 16; u32 j = 0; while (j < nv && !memcmp(rgba + 4 * j, b + 4 * j, 4)) j++;
        if (bad++ < 10) uya_log("shrub direct check: the mesh colours differ: vertex %u of %u: VU %02X%02X%02X%02X, records %02X%02X%02X%02X", j, nv, rgba[4*j], rgba[4*j+1], rgba[4*j+2], rgba[4*j+3], b[4*j], b[4*j+1], b[4*j+2], b[4*j+3]);
    }
    if (np % 20000 == 0) uya_log("shrub direct check: %llu packets, %llu differ", np, bad);
}
void native_shrub_gchk(const u8 *mem, u32 grp, u32 n)
{
    if (dchk <= 0 || gq_h == gq_t) return;
    static unsigned long long ng, bad;
    u32 k = gq_h++ % 256, m = 1 + 28 * (n < 8 ? n : 8);
    ng++;
    if (gq_n[k] != m) { if (bad++ < 10) uya_log("shrub direct check: group %u has %u qwords, the records %u", grp, m, gq_n[k]); return; }
    for (u32 q = 0; q < m; q++) {
        const u32 *a = (const u32 *)(mem + ((grp + q) & 0x3FF) * 16), *b = gq[k] + q * 4;
        if (memcmp(a, b, 16)) { if (bad++ < 10) uya_log("shrub direct check: group %u qword %u: VU %08X %08X %08X %08X, records %08X %08X %08X %08X", grp, q, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]); break; }
    }
    if (ng % 5000 == 0) uya_log("shrub direct check: %llu groups, %llu differ", ng, bad);
}
static void put32(u32 q, u32 src, u32 n) { for (u32 i = 0; i < n; i++) memcpy(pimg + ((q + i) & 0x3FF) * 16, EE(src + 16 * i), 16); }
static void put8(u32 q, u32 src, u32 n) { for (u32 i = 0; i < n; i++) { u32 *w = (u32 *)(pimg + ((q + i) & 0x3FF) * 16); for (int j = 0; j < 4; j++) w[j] = EE(src)[4 * i + (u32)j]; } }
static void seg_draw(u32 id)
{
    if (dchk < 0) dchk = getenv("UYA_SHRUBDCHK") != 0;
    if (!pimg) pimg = getenv("UYA_SHRUBPRIV") || dchk > 0 ? pimg_own : (u8 *)(uintptr_t)0x1100C000u;
    in_seg = 1;
    id %= DSEG;
    for (u32 k = 0; k < dseg[id].count; k++) {
        const u32 *r = drec[(dseg[id].first + k) % DREC];
        switch (r[0]) {
        case R_START:                                   /* the chain's constants (VU 0..2), the program's start */
            memcpy(pimg, dconsts, sizeof dconsts);
            native_shrub_run(pimg, 0x0000, 0, d_emit);
            break;
        case R_GIF: {                                   /* GS packets, as VIF1 DIRECT sent them (PATH2) */
            static u32 (*t)[4]; static u32 tn;
            if (tn < r[2]) { free(t); t = malloc(r[2] * 16); tn = t ? r[2] : 0; }
            if (!t) break;
            for (u32 q = 0; q < r[2]; q++) memcpy(t[q], gring[(r[1] + q) % GRING], 16);
            extern const char *uya_gs_src; uya_gs_src = "PATH2 native shrub billboards";
            native_gif(1, t[0], r[2]);
            break;
        }
        case R_CSTART:                                  /* (the clip program's MPG, when the record has it), its constants (VU 0..5), its start */
            if (r[2] && !uya_vif1_op_mpg_packet((const u32 *)EE(r[2]), r[3])) { static int told; if (!told++) uya_log("native shrub direct: the clip program's MPG packet at %08X has other codes", r[2]); }
            for (u32 q = 0; q < 6; q++) memcpy(pimg + q * 16, EE(r[1] + 16 * q), 16);
            uya_shrubclip_run(pimg, 0x0000);
            break;
        case R_CPACKET:
        case R_PACKET: {                                /* a class packet at its place, then its start */
            const DPk *d = r[1] < ndpk ? &dpk[r[1]] : 0;
            if (!d || !d->ok) break;
            u32 top = r[2];
            for (u32 q = 0; q < d->qwc; q++) {
                u8 *o = pimg + ((top + q) & 0x3FF) * 16;
                if (d->src[q]) memcpy(o, EE(d->src[q]), 16);
                else memcpy(o, d->img + q * 16, 16);
            }
            if (r[0] == R_CPACKET) uya_shrubclip_run(pimg, r[3]);
            else native_shrub_run(pimg, r[3], top, d_emit);
            break;
        }
        case R_CGROUP:
        case R_GROUP: {                                 /* the count, the instances, then the group's start */
            u32 *w = (u32 *)(pimg + (r[1] & 0x3FF) * 16);
            w[0] = w[1] = w[2] = w[3] = r[2];
            u32 n = r[2];
            for (u32 i = 0; i < n && k + 1 < dseg[id].count; i++) {
                const u32 *q = drec[(dseg[id].first + ++k) % DREC];
                if (q[0] == R_INST) { put32(q[1], q[2], 5); put8(q[1] + 4, q[3], 24); }
                else { put8(q[1] + 4, q[3], 24); put32(q[1], q[2], 5); }
                d_inst++;
            }
            if (r[0] == R_CGROUP) { uya_shrubclip_run(pimg, 0x0318); break; }
            if (dchk > 0) {
                u32 g = r[1] & 0x3FF, m = 1 + 28 * (n < 8 ? n : 8), k2 = gq_t++ % 256;
                if (!gq[k2]) gq[k2] = malloc(1024 * 16);
                for (u32 q = 0; q < m; q++) memcpy(gq[k2] + q * 4, pimg + ((g + q) & 0x3FF) * 16, 16);
                gq_n[k2] = m;
            }
            native_shrub_run(pimg, 0x0318, 0, d_emit);
            break;
        }
        }
    }
    in_seg = 0;
    { static unsigned long long nl; if (d_inst / 200000 != nl) { nl = d_inst / 200000; uya_log("native shrub direct: %llu instances; packets read %llu, staying in the chain %llu", d_inst, d_pk, d_pkbad); } }
}
int native_shrub_tag(const u32 *q)
{
    if ((q[0] & 0x7000FFFFu) != 0x10000000u || q[2] != NATIVE_SHRUB_MARK) return 0;
    seg_draw(q[3]);
    return 1;
}

/* testing (UYA_SHRUBDEC=1, the draws in the chain): each class packet the chain gives the program,
 * against the one read from the level data, in the order ShrubProc sent them */
static u32 chk_q[4096], chk_h, chk_t;
void native_shrub_dec_push(int slot) { chk_q[chk_t++ % 4096] = (u32)slot; }
static void dec_check(const u8 *mem, u32 top)
{
    static unsigned long long n, bad;
    if (chk_h == chk_t) return;
    u32 slot = chk_q[chk_h++ % 4096];
    if (slot >= ndpk || !dpk[slot].ok) return;
    const DPk *d = &dpk[slot];
    n++;
    for (u32 q = 0; q < d->qwc; q++) {
        const u8 *v = mem + ((top + q) & 0x3FF) * 16, *e = d->src[q] ? EE(d->src[q]) : d->img + q * 16;
        if (memcmp(v, e, 16)) {
            const u32 *a = (const u32 *)v, *b = (const u32 *)e;
            const u32 *im = (const u32 *)(d->img + q * 16);
            if (bad++ < 10) uya_log("native shrub direct check: packet %08X (at %u) qword %u differs: VU %08X %08X %08X %08X, read %08X %08X %08X %08X (src %08X; decoded %08X %08X %08X %08X; now data %08X)", d->p, top, q, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], d->src[q], im[0], im[1], im[2], im[3], rd32(d->p));
            break;
        }
    }
    if (n % 2000 == 0) uya_log("native shrub direct check: %llu class packets, %llu differ", n, bad);
}
