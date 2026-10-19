/* native_2d.c: the game's own 2D draws as host primitives (item 2 of the GS model's scope).
 *
 * The game's 2D producers write GIF packets into the chain with VIF1 DIRECT. Here a producer is
 * done natively from its arguments: what it draws (its material, its vertices) becomes a record,
 * and its place in the chain a marker (the packet's first qword a NEXT past the packet, VIF words
 * NOP NATIVE_2D_MARK and the record's index), so the draw happens where the chain meets it, in the
 * same order against everything else. There the record goes to the renderer as host primitives
 * (op 19: the material, then vertices the host puts together into primitives); no GIF data. The
 * packet's bytes are still written as the game wrote them (other code may read the chain, and an
 * unknown marker falls back to them).
 *
 * Producers:
 *  - func_001F5800 (DrawTexturedQuad: a textured quad as a four-vertex strip, PRIM 0x154, UVs in
 *    texels; FontPrint's glyphs, HUD images): x y w h u v uw vh in a0-a3 t0-t3, the colour (RGBAQ)
 *    and the texture (TEX0) on the stack.
 * UYA_N2D: 0 the game's routines; 1 check (the native packet is built aside and compared with the
 * game's, which is drawn); 2 native (the default). */
#include "rc.h"
#include "uya_host.h"
#include "native_draw.h"
#include "native_2d.h"
#include "uya_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_log(const char *fmt, ...);
void uya_gs_dlist(const u32 *d, u32 qwc);
#define ND_QW ((u32)((sizeof(nd_material) + 15) / 16))

static int mode = -1;
static int n2d_mode(void)
{
    if (mode < 0) { const char *e = getenv("UYA_N2D"), *n = getenv("UYA_NATIVE"); mode = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2; uya_log("native 2D draws: %s (UYA_N2D)", mode == 0 ? "the game's routines" : mode == 1 ? "checked against the game's routines" : "native"); }
    return mode;
}

/* ---- records ---- */
#define R_N 16384
#define R_QW (1u << 19)                     /* the records' ops (qwords, a ring) */
typedef struct { u32 at, end, q0[4]; u32 off, nq; u32 snap_addr, snap_off, snap_n; } Rec;   /* snap: memory the record was made from, checked again where the chain meets it */
static Rec *rec; static u32 *ring;
static u32 rec_next, ring_at;
static unsigned long long n_made, n_drawn, n_unknown, n_back;
/* a record for the packet at [at, end): op 19 with material m and nv vertices w (8 words each);
 * flags as op 19's */
static void record(u32 at, u32 end, const nd_material *m, const u32 *w, u32 nv, u32 flags)
{
    if (!rec) { rec = calloc(R_N, sizeof *rec); ring = malloc((size_t)R_QW * 16); if (!rec || !ring) { free(rec); rec = 0; return; } }
    u32 nq = 1 + ND_QW + 2 * nv;
    if (nq > R_QW / 4) return;
    if (ring_at + nq > R_QW) ring_at = 0;
    u32 i = rec_next++ % R_N;
    Rec *r = &rec[i];
    r->at = at; r->end = end; r->off = ring_at; r->nq = nq; r->snap_n = 0;
    for (int k = 0; k < 4; k++) r->q0[k] = M32(at + 4 * (u32)k);
    u32 *op = ring + (size_t)ring_at * 4;
    memset(op, 0, (size_t)(1 + ND_QW) * 16);
    op[0] = 19; op[1] = flags; op[2] = nv;
    memcpy(op + 4, m, sizeof *m);
    memcpy(op + 4 + ND_QW * 4, w, (size_t)nv * 32);
    ring_at += nq;
    rc_sw(at, 0x20000000u); rc_sw(at + 4, end); rc_sw(at + 8, NATIVE_2D_MARK); rc_sw(at + 12, i);
    n_made++;
}
/* a record of several op 19 blocks (a producer whose material changes inside its packet) */
static u32 rb_start, rb_ok, rb_snap_addr, rb_snap_off, rb_snap_n;
static void rec_begin(void)
{
    if (!rec) { rec = calloc(R_N, sizeof *rec); ring = malloc((size_t)R_QW * 16); if (!rec || !ring) { free(rec); rec = 0; rb_ok = 0; return; } }
    rb_ok = 1; rb_start = ring_at; rb_snap_n = 0;
}
/* the memory the record is made from (n qwords at addr), kept after its ops */
static void rec_snapshot(u32 addr, u32 n)
{
    if (!rb_ok) return;
    if (ring_at + n > R_QW) { rb_ok = 0; return; }
    memcpy(ring + (size_t)ring_at * 4, (const void *)RC_A(addr), (size_t)n * 16);
    rb_snap_addr = addr; rb_snap_off = ring_at; rb_snap_n = n;
    ring_at += n;
}
static void rec_op19(const nd_material *m, const u32 *w, u32 nv, u32 flags)
{
    if (!rb_ok || !nv) return;
    u32 nq = 1 + ND_QW + 2 * nv;
    if (ring_at + nq > R_QW) {   /* (to the ring's start: the record's earlier blocks move with it) */
        u32 have = ring_at - rb_start;
        if (have + nq > R_QW / 2) { rb_ok = 0; return; }
        memmove(ring, ring + (size_t)rb_start * 4, (size_t)have * 16); rb_start = 0; ring_at = have;
    }
    u32 *op = ring + (size_t)ring_at * 4;
    memset(op, 0, (size_t)(1 + ND_QW) * 16);
    op[0] = 19; op[1] = flags; op[2] = nv;
    memcpy(op + 4, m, sizeof *m);
    memcpy(op + 4 + ND_QW * 4, w, (size_t)nv * 32);
    ring_at += nq;
}
/* the GS's vertex colour (RGBAQ) as a packet leaves it, with no vertex of its own (op 20) */
static void rec_rgbaq(u32 rgba, u32 q)
{
    if (!rb_ok) return;
    if (ring_at + 1 > R_QW) { rb_ok = 0; return; }
    u32 *op = ring + (size_t)ring_at * 4;
    op[0] = 20; op[1] = rgba; op[2] = q; op[3] = 0;
    ring_at++;
}
static int rec_end(u32 at, u32 end)
{
    if (!rb_ok || ring_at == rb_start) return 0;
    u32 i = rec_next++ % R_N;
    Rec *r = &rec[i];
    r->at = at; r->end = end; r->off = rb_start; r->nq = (rb_snap_n ? rb_snap_off : ring_at) - rb_start;
    r->snap_addr = rb_snap_addr; r->snap_off = rb_snap_off; r->snap_n = rb_snap_n;
    for (int k = 0; k < 4; k++) r->q0[k] = M32(at + 4 * (u32)k);
    rc_sw(at, 0x20000000u); rc_sw(at + 4, end); rc_sw(at + 8, NATIVE_2D_MARK); rc_sw(at + 12, i);
    n_made++;
    return 1;
}
int native_2d_tag(const u32 *q, u32 t)
{
    u32 i = q[3] % R_N;
    if (!rec || rec[i].at != t) { if (n_unknown++ < 3) uya_log("native 2D: marker %u at %08X not known: skipped", q[3], t); return 0; }
    Rec *r = &rec[i];
    for (int k = 0; k < 4; k++) rc_sw(t + 4 * (u32)k, r->q0[k]);
    r->at = 0;
    if (r->snap_n && memcmp(ring + (size_t)r->snap_off * 4, (const void *)RC_A(r->snap_addr), (size_t)r->snap_n * 16)) { n_back++; return 2; }   /* (changed since: the packet as GIF data) */
    extern const char *uya_gs_src; uya_gs_src = "native 2D";
    uya_gs_dlist(ring + (size_t)r->off * 4, r->nq);
    n_drawn++;
    if (n_drawn == 50 || n_drawn % 20000 == 0) uya_log("native 2D: %llu draws (%llu made), %llu markers not known", n_drawn, n_made, n_unknown);
    return 1;
}

/* a chain word as the hooks find it: in the levels a routine's translated body can be shared by
 * look-alike routines (the same bytes but for their addresses), and a hook in it then gets that
 * body's relocations, which name other words in another routine. A chain word is the chain
 * pointer's own (0x161000 in the executable, 0x161240 or 0x161280 in the levels), holding a
 * qword-aligned pointer into EE memory; anything else and the game's routine runs. */
static int chain_word_ok(u32 cw)
{
    if ((cw & 0xFFFFF000u) != 0x161000u) return 0;
    u32 p = M32(cw);
    return p >= 0x100000u && p < 0x2000000u && !(p & 15);
}
/* a vertex: XYZ2 (x | y << 16 | z << 32, as the GS takes it), RGBAQ, UV (u | v << 16 in 1/16 texel) */
static void vtx(u32 *w, u64 xyz, u64 rgbaq, u64 uv)
{
    w[0] = (u32)(xyz & 0xFFFF) | (u32)((xyz >> 16) & 0xFFFF) << 16; w[1] = (u32)(xyz >> 32); w[2] = 0; w[3] = (u32)rgbaq;
    w[4] = 0; w[5] = 0; w[6] = (u32)(rgbaq >> 32); w[7] = (u32)(uv & 0x3FFF) | (u32)((uv >> 16) & 0x3FFF) << 16;
}

/* ---- func_001F5800: a textured quad ---- */
static unsigned long long q_runs, q_ok, q_bad;
static int q_tmpl_ok(u32 tmpl)
{
    /* the template's GIF tag: NLOOP 1, EOP, REGLIST, 11 registers: TEX0_1 PRIM RGBAQ (UV XYZ2) x 4 (and a zero
     * doubleword to the qword) */
    u64 lo = M64(tmpl), hi = M64(tmpl + 8);
    return (lo & 0x7FFF) == 1 && ((lo >> 15) & 1) && ((lo >> 58) & 3) == 1 && ((lo >> 60) & 15) == 11 && hi == 0x53535353106ull;
}
/* the packet the routine writes at chain T (8 qwords), and its vertices */
static void q_build(RcCtx *c, u32 view, u32 tmpl, u32 *pk, u32 *wv, nd_material *m)
{
    s32 x = (s32)c->r[4].d[0], y = (s32)c->r[5].d[0], w = (s32)c->r[6].d[0], h = (s32)c->r[7].d[0];
    s32 u = (s32)c->r[8].d[0], v = (s32)c->r[9].d[0], uw = (s32)c->r[10].d[0], vh = (s32)c->r[11].d[0];
    u32 sp = (u32)c->r[29].d[0];
    u64 rgba = M64(sp), tex = M64(sp + 8);
    s32 ox = (s32)M32(view + 0x10), oy = (s32)M32(view + 0x14);
    s64 x0 = (s32)((u32)x * 16u + (u32)ox - 8u), x1 = (s32)(((u32)x + (u32)w) * 16u + (u32)ox - 8u);
    s64 y0 = (s32)((u32)y * 16u + (u32)oy - 8u), y1 = (s32)(((u32)y + (u32)h) * 16u + (u32)oy - 8u);
    s64 s0 = (s32)((u32)u << 4), s1 = (s32)(((u32)u + (u32)uw) << 4);
    s64 v0 = (s32)((u32)v << 20), vb = (s32)(((u32)v + (u32)vh) << 20);
    u64 Z = 0xFFFFF0ull << 32;
    u64 p[12];
    p[0] = tex; p[1] = 0x154; p[2] = rgba;
    p[3] = (u64)(s64)(s32)(u32)(v0 + s0); p[4] = (u64)x0 | ((u64)y0 << 16) | Z;
    p[5] = (u64)(s64)(s32)(u32)(v0 + s1); p[6] = (u64)x1 | ((u64)y0 << 16) | Z;
    p[7] = (u64)(s64)(s32)(u32)(vb + s0); p[8] = (u64)x0 | ((u64)y1 << 16) | Z;
    p[9] = (u64)(s64)(s32)(u32)(vb + s1); p[10] = (u64)x1 | ((u64)y1 << 16) | Z;
    p[11] = 0;
    pk[0] = 0x10000007u; pk[1] = 0; pk[2] = 0; pk[3] = 0x50000007u;
    memcpy(pk + 4, (const void *)RC_A(tmpl), 16);
    memcpy(pk + 8, p, sizeof p);
    for (int k = 0; k < 4; k++) vtx(wv + k * 8, p[4 + 2 * k], rgba, p[3 + 2 * k]);
    u32 ra[2] = { 0x06, 0x00 }; u64 rv[2] = { tex, 0x154 };
    if (!nd_material_from_regs(m, ra, rv, 2)) m->has = 0xFF;   /* (never: TEX0 and PRIM 0x154 always are one) */
}
int uya_dtquad_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)g; (void)ng;
    int md = n2d_mode();
    if (!md) return 0;
    if (na < 2) { static int t; if (!t++) uya_log("native 2D quad: %d references (not the routine's): the game's routine", na); return 0; }
    /* the template (its GIF tag): before the chain word, 0x6C0 in the executable's layout and 0x8C0
     * in the levels'; found by its exact contents, once per layout */
    u32 view = a[0], cw = a[1];
    if (!chain_word_ok(cw) || view != 0x13E600u) return 0;
    static u32 l_cw, l_t;
    if (cw != l_cw) {
        l_cw = cw; l_t = 0;
        for (u32 t = (cw - 0x1000) & ~15u; t < cw; t += 16) if (q_tmpl_ok(t)) { l_t = t; break; }
        uya_log("native 2D quad: chain word %08X, template %08X", cw, l_t);
    }
    u32 tmpl = l_t;
    if (!tmpl || !q_tmpl_ok(tmpl)) { static int t; if (!t++) uya_log("native 2D quad: no template at %08X (chain word %08X: %08X %08X %08X %08X): the game's routine", tmpl, cw, M32(tmpl), M32(tmpl + 4), M32(tmpl + 8), M32(tmpl + 12)); return 0; }
    u32 pk[32], wv[32]; nd_material m;
    q_build(c, view, tmpl, pk, wv, &m);
    if (m.has == 0xFF) return 0;
    u32 T = M32(cw);
    q_runs++;
    if (md == 1) {   /* check: the game's routine writes the packet; compared at its return (uya_dtquad_exit) */
        static u32 keep[32]; extern u32 uya_2d_chk_at; extern u32 *uya_2d_chk_pk;
        memcpy(keep, pk, sizeof keep); uya_2d_chk_at = T; uya_2d_chk_pk = keep;
        return 0;
    }
    for (int k = 0; k < 32; k++) rc_sw(T + 4 * (u32)k, pk[k]);
    rc_sw(cw, T + 0x80);
    record(T, T + 0x80, &m, wv, 4, 1);
    return 1;
}
u32 uya_2d_chk_at; u32 *uya_2d_chk_pk;
void uya_dtquad_exit(RcCtx *c)
{
    (void)c;
    if (!uya_2d_chk_pk) return;
    int same = !memcmp((const void *)RC_A(uya_2d_chk_at), uya_2d_chk_pk, 128);
    if (same) q_ok++;
    else if (q_bad++ < 5) {
        const u32 *g = (const u32 *)RC_A(uya_2d_chk_at);
        for (int k = 0; k < 8; k++) if (memcmp(g + 4 * k, uya_2d_chk_pk + 4 * k, 16)) uya_log("native 2D quad check: qword %d: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, g[4 * k], g[4 * k + 1], g[4 * k + 2], g[4 * k + 3], uya_2d_chk_pk[4 * k], uya_2d_chk_pk[4 * k + 1], uya_2d_chk_pk[4 * k + 2], uya_2d_chk_pk[4 * k + 3]);
    }
    uya_2d_chk_pk = 0;
    if ((q_ok + q_bad) == 50 || (q_ok + q_bad) % 2000 == 0) uya_log("native 2D quad check: %llu packets compared, %llu identical, %llu differ", q_ok + q_bad, q_ok, q_bad);
}

/* ---- func_0022D3F8: the sky's Gouraud triangles (SkyDrawShellGouraud's emitter, asm) ----
 * a0 triangles, a1 their entries (three vertex indices and a texture byte each), a2 the vertices'
 * colours, a3 the vertices (16 bytes: x, y as words, the clip flags at +15), all in the scratchpad.
 * A triangle all of whose vertices share a clip flag is left out. The packet: a CNT tag with
 * DIRECT, a PACKED A+D GIF tag (EOP), then per vertex RGBAQ (the colour word sign-extended: its
 * top bit becomes Q's bits) and XYZ2 (x, y; Z 0). The primitive is what the sky's setup block
 * (sent before) set. No triangle: nothing written, the chain pointer kept. */
static unsigned long long sg_runs, sg_ok, sg_bad;
static u32 *sg_pk, sg_cap, sg_n;      /* the packet built (words), its length */
static u32 *sg_wv, sg_wcap, sg_nv;    /* the vertices */
static u32 sg_chk_at, sg_chk;
static int sg_build(u32 n, u32 il, u32 col, u32 vx, u32 T)
{
    if (6 * n + 2 > sg_cap / 4) { free(sg_pk); sg_cap = (6 * n + 2) * 4 + 1024; sg_pk = malloc(sg_cap * 4); if (!sg_pk) { sg_cap = 0; return 0; } }
    if (3 * n * 8 > sg_wcap) { free(sg_wv); sg_wcap = 3 * n * 8 + 1024; sg_wv = malloc(sg_wcap * 4); if (!sg_wv) { sg_wcap = 0; return 0; } }
    u32 *p = sg_pk + 8; sg_nv = 0;
    for (u32 i = 0; i < n; i++) {
        u32 e = il + 4 * i;
        u32 f = M8(vx + 0xF + (u32)M8(e) * 16) & M8(vx + 0xF + (u32)M8(e + 1) * 16) & M8(vx + 0xF + (u32)M8(e + 2) * 16);
        if (f) continue;
        for (int k = 0; k < 3; k++) {
            u32 v = M8(e + (u32)k);
            u32 cw = M32(col + v * 4), qb = (cw >> 31) ? 0xFFFFFFFFu : 0;
            u32 xy = (M32(vx + v * 16) & 0xFFFF) | (M32(vx + v * 16 + 4) & 0xFFFF) << 16;
            p[0] = cw; p[1] = qb; p[2] = 1; p[3] = 0;
            p[4] = xy; p[5] = 0; p[6] = 5; p[7] = 0;
            p += 8;
            u32 *w = sg_wv + sg_nv * 8; sg_nv++;
            w[0] = xy; w[1] = 0; w[2] = 0; w[3] = cw; w[4] = 0; w[5] = 0; w[6] = qb; w[7] = 0;
        }
    }
    u32 nq = (u32)(p - sg_pk) / 4;
    sg_n = nq * 4;
    if (nq == 2) return 1;                 /* (no triangle) */
    u32 v0 = nq - 1;
    sg_pk[0] = 0x10000000u | v0; sg_pk[1] = 0; sg_pk[2] = 0; sg_pk[3] = 0x50000000u | v0;
    sg_pk[4] = (v0 - 1) | 0x8000; sg_pk[5] = 0x10000000u; sg_pk[6] = 0xE; sg_pk[7] = 0;
    (void)T;
    return 1;
}
int uya_skyg_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)a; (void)na;
    int md = n2d_mode();
    if (!md) return 0;
    if (ng != 2 || g[0] != g[1]) { static int t; if (!t++) uya_log("native 2D sky: %d gp references (not 2, the chain pointer): the game's routine", ng); return 0; }
    u32 n = (u32)c->r[4].d[0];
    if (!n || n > 0x10000) return 0;
    if (g[0] != -0x5D00) return 0;   /* (the chain pointer's gp word: checked, see chain_word_ok) */
    u32 cwd = (u32)c->r[28].d[0] + (u32)g[0], T = M32(cwd);
    if (T < 0x100000u || T >= 0x2000000u || (T & 15)) return 0;
    if (!sg_build(n, (u32)c->r[5].d[0], (u32)c->r[6].d[0], (u32)c->r[7].d[0], T)) return 0;
    sg_runs++;
    if (md == 1) { sg_chk_at = T; sg_chk = 1; return 0; }
    if (sg_n == 8) return 1;               /* (no triangle: nothing written) */
    for (u32 k = 0; k < sg_n; k++) rc_sw(T + 4 * k, sg_pk[k]);
    rc_sw(cwd, T + sg_n * 4);
    nd_material m; memset(&m, 0, sizeof m);
    record(T, T + sg_n * 4, &m, sg_wv, sg_nv, 1);
    return 1;
}
void uya_skyg_exit(RcCtx *c)
{
    (void)c;
    if (!sg_chk) return;
    sg_chk = 0;
    int same = sg_n == 8 || !memcmp((const void *)RC_A(sg_chk_at), sg_pk, sg_n * 4);
    if (same) sg_ok++;
    else if (sg_bad++ < 5) {
        const u32 *g = (const u32 *)RC_A(sg_chk_at);
        for (u32 k = 0; k < sg_n / 4; k++) if (memcmp(g + 4 * k, sg_pk + 4 * k, 16)) { uya_log("native 2D sky check: qword %u of %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, sg_n / 4, g[4 * k], g[4 * k + 1], g[4 * k + 2], g[4 * k + 3], sg_pk[4 * k], sg_pk[4 * k + 1], sg_pk[4 * k + 2], sg_pk[4 * k + 3]); break; }
    }
    if ((sg_ok + sg_bad) == 50 || (sg_ok + sg_bad) % 2000 == 0) uya_log("native 2D sky check: %llu packets compared, %llu identical, %llu differ", sg_ok + sg_bad, sg_ok, sg_bad);
}

/* ---- func_0022D520: the sky's textured triangles (SkyDrawShellTextured's emitter, asm) ----
 * As the Gouraud one, with a texture byte per triangle: when it changes, a TEX0 write. The texture's
 * TEX0 comes from the sky's texture table (gp-0x67A4's +0x10, 16 bytes an entry: the TEX0 at +0, the
 * image's offset at +8, the palette's at +0xA, log2 width and height at +0xC and +0xE); one not
 * placed yet is placed now by the game's own pager (as func_0022D6B4 does): the palette at the
 * allocation pointer, the image 0x400 bytes after it, the pointer moved on by the image's size, the
 * TEX0 (PSMT8, TBW from the width, TCC, CLD 4) kept in the table, and an upload entry added to the
 * list (its count a gp word; data offsets from gp-0x67A4's +0x14). Each vertex: ST (its two
 * halfwords as 12-bit fixed point), RGBAQ (0x808080 with the vertex's alpha byte at +12; Q 1.0),
 * XYZ2. */
static unsigned long long st_ok, st_bad;
static u32 *tx_pk, tx_cap, tx_n, tx_chk_at, tx_chk;
/* G: the sky table's gp offset, the allocation pointer's, the upload count's; upl: the upload list */
static int tx_build(RcCtx *c, const s32 *G, u32 n, u32 il, u32 stv, u32 vx, u32 upl, int write)
{
    if ((10 * n + 2) * 4 > tx_cap) { free(tx_pk); tx_cap = (10 * n + 2) * 4 + 1024; tx_pk = malloc(tx_cap * 4); if (!tx_pk) { tx_cap = 0; return 0; } }
    const u32 gp = (u32)c->r[28].d[0];
    u32 *p = tx_pk + 8;
    u32 cur = 0xFFFF;
    static u32 *wv; static u32 wcap; u32 nv = 0;
    if (3 * n * 8 > wcap) { free(wv); wcap = 3 * n * 8 + 1024; wv = malloc(wcap * 4); if (!wv) { wcap = 0; return 0; } }
    nd_material mt; memset(&mt, 0, sizeof mt);
    if (write) rec_begin();
    u32 alloc = M32(gp + (u32)G[1]), cnt = M32(gp + (u32)G[2]);
    for (u32 i = 0; i < n; i++) {
        u32 e = il + 4 * i;
        u32 f = M8(vx + 0xF + (u32)M8(e) * 16) & M8(vx + 0xF + (u32)M8(e + 1) * 16) & M8(vx + 0xF + (u32)M8(e + 2) * 16);
        u32 tb = M8(e + 3);
        if (f) continue;
        if (tb != cur) {
            if (nv) { if (write) rec_op19(&mt, wv, nv, 0); nv = 0; }
            u32 tbl = M32(M32(gp + (u32)G[0]) + 0x10), ent = tbl + tb * 16;
            u64 t0 = M64(ent);
            if (!t0) {   /* placed now (func_0022D6B4) */
                u32 t3 = alloc >> 8, t5 = alloc + 0x400, t4 = t5 >> 8;
                s32 lw_ = (s16)M16(ent + 0xC), lh_ = (s16)M16(ent + 0xE);
                t5 += 1u << ((u32)(lw_ + lh_) & 31);
                s32 bw = lw_ - 6; if (bw < 0) bw = 0;
                u64 v = ((u64)(s64)(s32)(1u << ((u32)bw & 31)) << 14) + (u64)(s64)(s32)t4;
                v += 0x13ull << 20; v += (u64)(s64)lw_ << 26; v += (u64)(s64)lh_ << 30; v += 1ull << 34; v += (u64)t3 << 37; v += 4ull << 61;
                t0 = v; alloc = t5;
                if (write) {
                    rc_sd(ent, t0);
                    u32 ue = upl + cnt * 16, base = M32(M32(gp + (u32)G[0]) + 0x14);
                    rc_sw(ue, (u32)((s32)(s16)M16(ent + 0xA) * 16) + base); M16(ue + 4) = 0; M16(ue + 6) = (u16)t3;
                    rc_sw(ue + 8, (u32)((s32)(s16)M16(ent + 0x8) * 16) + base);
                    M8(ue + 0xC) = (u8)lw_; M8(ue + 0xD) = (u8)lh_; M16(ue + 0xE) = (u16)t4;
                }
                cnt++;
            }
            p[0] = (u32)t0; p[1] = (u32)(t0 >> 32); p[2] = 6; p[3] = 0; p += 4;
            u32 ra = 0x06; u64 rv = t0;
            nd_material_from_regs(&mt, &ra, &rv, 1);
            cur = tb;
        }
        for (int k = 0; k < 3; k++) {
            u32 v = M8(e + (u32)k);
            u32 sw_ = M32(stv + v * 4);
            float sf = (float)(sw_ & 0xFFFF) / 4096.0f, tf = (float)(sw_ >> 16) / 4096.0f;
            u32 sb, tb2; memcpy(&sb, &sf, 4); memcpy(&tb2, &tf, 4);
            u32 rg = ((u32)M8(vx + v * 16 + 0xC) << 24) | 0x808080u;
            u32 xy = (M32(vx + v * 16) & 0xFFFF) | (M32(vx + v * 16 + 4) & 0xFFFF) << 16;
            p[0] = sb; p[1] = tb2; p[2] = 2; p[3] = 0;
            p[4] = rg; p[5] = 0x3F800000u; p[6] = 1; p[7] = 0;
            p[8] = xy; p[9] = 0; p[10] = 5; p[11] = 0;
            p += 12;
            u32 *w = wv + nv * 8; nv++;
            w[0] = xy; w[1] = 0; w[2] = 0; w[3] = rg; w[4] = sb; w[5] = tb2; w[6] = 0x3F800000u; w[7] = 0;
        }
    }
    if (nv && write) rec_op19(&mt, wv, nv, 0);
    if (write) { rc_sw(gp + (u32)G[1], alloc); rc_sw(gp + (u32)G[2], cnt); }
    u32 nq = (u32)(p - tx_pk) / 4;
    tx_n = nq * 4;
    if (nq == 2) return 1;
    u32 v0 = nq - 1;
    tx_pk[0] = 0x10000000u | v0; tx_pk[1] = 0; tx_pk[2] = 0; tx_pk[3] = 0x50000000u | v0;
    tx_pk[4] = (v0 - 1) | 0x8000; tx_pk[5] = 0x10000000u; tx_pk[6] = 0xE; tx_pk[7] = 0;
    return 1;
}
int uya_skyt_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    int md = n2d_mode();
    if (!md) return 0;
    {   /* (the routine's references, logged once: gp offsets, absolute addresses) */
        static int t; if (!t++) { char b[300]; int o = 0; for (int k = 0; k < ng && o < 200; k++) o += snprintf(b + o, sizeof b - o, " %d", g[k]); for (int k = 0; k < na && o < 280; k++) o += snprintf(b + o, sizeof b - o, " %08X", a[k]); uya_log("native 2D sky (textured): references%s", b); }
    }
    /* gp words in the routine's order: the chain pointer (read, written), the sky table, the allocation
     * pointer (read, written), the upload count (read, written), the sky table twice more */
    if (ng != 9 || g[0] != g[1] || g[2] != g[7] || g[2] != g[8] || g[3] != g[4] || g[5] != g[6] || na < 1) {
        static int t; if (!t++) uya_log("native 2D sky (textured): not the routine's shape: the game's routine");
        return 0;
    }
    s32 tblo = g[2], allo = g[3], cnto = g[5];
    if (g[0] != -0x5D00 || tblo != -0x67A4) return 0;   /* (checked, see chain_word_ok) */
    s32 G[3] = { tblo, allo, cnto };
    u32 n = (u32)c->r[4].d[0];
    if (!n || n > 0x10000) return 0;
    u32 cwd = (u32)c->r[28].d[0] + (u32)g[0], T = M32(cwd);
    if (T < 0x100000u || T >= 0x2000000u || (T & 15)) return 0;
    if (md == 1) { if (!tx_build(c, G, n, (u32)c->r[5].d[0], (u32)c->r[6].d[0], (u32)c->r[7].d[0], a[0], 0)) return 0; tx_chk_at = T; tx_chk = 1; return 0; }
    if (!tx_build(c, G, n, (u32)c->r[5].d[0], (u32)c->r[6].d[0], (u32)c->r[7].d[0], a[0], 1)) return 0;
    if (tx_n == 8) return 1;
    for (u32 k = 0; k < tx_n; k++) rc_sw(T + 4 * k, tx_pk[k]);
    rc_sw(cwd, T + tx_n * 4);
    rec_end(T, T + tx_n * 4);
    return 1;
}
void uya_skyt_exit(RcCtx *c)
{
    (void)c;
    if (!tx_chk) return;
    tx_chk = 0;
    int same = tx_n == 8 || !memcmp((const void *)RC_A(tx_chk_at), tx_pk, tx_n * 4);
    if (same) st_ok++;
    else if (st_bad++ < 5) {
        const u32 *g = (const u32 *)RC_A(tx_chk_at);
        for (u32 k = 0; k < tx_n / 4; k++) if (memcmp(g + 4 * k, tx_pk + 4 * k, 16)) { uya_log("native 2D sky (textured) check: qword %u of %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, tx_n / 4, g[4 * k], g[4 * k + 1], g[4 * k + 2], g[4 * k + 3], tx_pk[4 * k], tx_pk[4 * k + 1], tx_pk[4 * k + 2], tx_pk[4 * k + 3]); break; }
    }
    if ((st_ok + st_bad) == 50 || (st_ok + st_bad) % 2000 == 0) uya_log("native 2D sky (textured) check: %llu packets compared, %llu identical, %llu differ", st_ok + st_bad, st_ok, st_bad);
}

/* ---- func_002298B0: the shadow volumes' sides and caps (asm, from the scratchpad) ----
 * a0 the pass (1 or 2), a1 the colour. The volume's records are at 0x70000000 (0x20 bytes each: two
 * vertices, each a qword {ST, XYZ2}), their count (and one more) at the count word, the side flags at
 * 0x70003000 (a byte each), the caps' flags at 0x70003800 / 0x70003801 (every other byte), the two
 * cap depths at the depth words. Three packets in the chain:
 *  - the sides: one quad (PRIM 0x54: a textured, blended strip of four) per record whose side flag is
 *    the pass, its vertices the record's two and the next record's two; RGBAQ the colour (Q 1.0) once;
 *  - the front cap, when (pass 2 ? -1 : 1) * the first depth is not negative: a fan (PRIM 0x55) of
 *    each record's first vertex whose flag is 0;
 *  - the back cap, when (pass 2 ? -1 : 1) * the second depth is not positive: the same with each
 *    record's second vertex.
 * A cap with no vertex is not written. With a count of 1 nothing is. VU0's vf1 and vf2 are left as
 * the routine leaves them (vf2 the `at` register's 128 bits after it loads a depth; vf1.x the sign
 * times the depth). */
static unsigned long long sv_ok, sv_bad;
static u32 *sv_pk, sv_cap, sv_n, sv_chk_at, sv_chk;
static u32 sv_vf1[4], sv_vf2[4];
static int sv_build(RcCtx *c, u32 cntw, u32 depw, u32 T0, int write)
{
    u32 cnt = M32(cntw);
    if (cnt <= 1 || cnt > 4096) return cnt == 1 ? 2 : 0;   /* (1: nothing; 0 or more than a scratchpad's worth: the game's routine) */
    s64 pass = (s64)c->r[4].d[0]; u32 col = (u32)c->r[5].d[0];
    if ((cnt + 8) * 6 * 4 * 4 > sv_cap) { free(sv_pk); sv_cap = (cnt + 8) * 6 * 4 * 4 + 1024; sv_pk = malloc(sv_cap * 4); if (!sv_pk) { sv_cap = 0; return 0; } }
    u32 *p = sv_pk;   /* all three packets, one after another from T0 */
    u64 r1hi = c->r[1].d[1];
    memcpy(sv_vf1, VF(1), 16); memcpy(sv_vf2, VF(2), 16);
    nd_material mq; memset(&mq, 0, sizeof mq); mq.has = ND_PRIM; mq.kind = 4; mq.textured = 1; mq.blend = 1;   /* PRIM 0x54 */
    nd_material mc; memset(&mc, 0, sizeof mc); mc.has = ND_PRIM; mc.kind = 5; mc.textured = 1; mc.blend = 1;   /* PRIM 0x55 */
    static u32 *wv; static u32 wcap;
    if (cnt * 4 * 8 > wcap) { free(wv); wcap = cnt * 4 * 8 + 1024; wv = malloc(wcap * 4); if (!wv) { wcap = 0; return 0; } }
    /* the sides */
    u32 T = T0, *h = p; p += 16;
    u32 nq = 0;
    if (write) rec_begin();
    for (u32 j = 0; j + 1 < cnt; j++) {
        if ((s64)(s8)M8(0x70003000u + j) != pass) continue;
        u32 r = 0x70000000u + j * 0x20;
        p[0] = 0; p[1] = 0; p[2] = 0x54; p[3] = 0;
        for (int k = 0; k < 16; k++) p[4 + k] = M32(r + 4 * (u32)k);
        p += 20;
        r1hi = M64(r + 0x38);
        for (int k = 0; k < 4; k++) {
            u32 q = r + 16 * (u32)k, *w = wv + k * 8;
            u64 xyz = M64(q + 8);
            w[0] = (u32)(xyz & 0xFFFF) | (u32)((xyz >> 16) & 0xFFFF) << 16; w[1] = (u32)(xyz >> 32); w[2] = 0; w[3] = col;
            w[4] = M32(q); w[5] = M32(q + 4); w[6] = 0x3F800000u; w[7] = 0;
        }
        if (write) rec_op19(&mq, wv, 4, 0);
        nq++;
    }
    u32 t0 = (u32)(p - h) / 4 - 1;
    h[0] = 0x10000000u | t0; h[1] = 0; h[2] = 0; h[3] = 0x50000000u | t0;
    h[4] = 1; h[5] = 0x14000000u; h[6] = 0xF1; h[7] = 0;
    h[8] = col; h[9] = 0x3F800000u; h[10] = 0; h[11] = 0;
    h[12] = nq | 0x8000; h[13] = 0xA4000000u; h[14] = 0x5252520Fu; h[15] = 0x52;
    if (write) {
        rec_rgbaq(col, 0x3F800000u);   /* (the colour as the packet leaves it, for what follows) */
        if (!rec_end(T, T + (u32)(p - h) * 4)) return 0;
    }
    /* the caps */
    for (int cap = 0; cap < 2; cap++) {
        u32 dv = M32(depw + 4 * (u32)cap);
        u32 v2[4]; u64 lo = (u64)(s64)(s32)dv; memcpy(v2, &lo, 8); memcpy(v2 + 2, &r1hi, 8);
        memcpy(sv_vf2, v2, 16);
        float one = pass == 2 ? -1.0f : 1.0f, d; memcpy(&d, &dv, 4);
        float x = rc_fc(one * d);
        memcpy(&sv_vf1[0], &x, 4);
        { u32 b; memcpy(&b, &x, 4); u64 r1lo = (u64)b << 32; r1hi = ((u64)sv_vf1[3] << 32) | sv_vf1[2];
          if (cap == 0 && (s64)r1lo < 0) continue;
          if (cap == 1 && (s64)r1lo > 0) continue; }
        u32 *hc = p, T2 = T0 + (u32)(p - sv_pk) * 4;
        p += 16;
        u32 nv = 0;
        for (u32 j = 0; j < cnt; j++) {
            if ((s8)M8(0x70003800u + 2 * j + (u32)cap)) continue;
            u32 q = 0x70000000u + j * 0x20 + 0x10 * (u32)cap;
            for (int k = 0; k < 4; k++) p[k] = M32(q + 4 * (u32)k);
            p += 4;
            r1hi = M64(q + 8);
            u32 *w = wv + nv * 8; nv++;
            u64 xyz = M64(q + 8);
            w[0] = (u32)(xyz & 0xFFFF) | (u32)((xyz >> 16) & 0xFFFF) << 16; w[1] = (u32)(xyz >> 32); w[2] = 0; w[3] = col;
            w[4] = M32(q); w[5] = M32(q + 4); w[6] = 0x3F800000u; w[7] = 0;
        }
        if (!nv) { p = hc; continue; }
        hc[0] = 0x10000000u | (nv + 3); hc[1] = 0; hc[2] = 0; hc[3] = 0x50000000u | (nv + 3);
        hc[4] = 1; hc[5] = 0x24000000u; hc[6] = 1; hc[7] = 0;
        hc[8] = col; hc[9] = 0x3F800000u; hc[10] = 0x55; hc[11] = 0;
        hc[12] = nv | 0x8000; hc[13] = 0x24000000u; hc[14] = 0x52; hc[15] = 0;
        if (write) { rec_begin(); rec_op19(&mc, wv, nv, 0); if (!rec_end(T2, T2 + (nv + 4) * 16)) return 0; }
    }
    sv_n = (u32)(p - sv_pk);
    return 1;
}
int uya_shadowvol_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)g; (void)ng;
    int md = n2d_mode();
    if (!md) return 0;
    /* the count, chain and depth words, by their lui/addiu pairs in the routine's order */
    /* (the recompiler also pairs `lui t5; lw t5; addiu t5, 0x40` into a reference, 0x160040, at 1, 5, 9) */
    if (na != 11 || a[4] != a[0] || a[8] != a[0] || a[6] != a[2] || a[10] != a[2] || a[7] != a[3]) { static int t; if (!t++) uya_log("native 2D shadow volumes: %d references (not the routine's shape): the game's routine", na); return 0; }
    u32 cntw = a[0], cw = a[2], depw = a[3], T0 = M32(cw);
    if (!chain_word_ok(cw) || (cntw & 0xFFFFF000u) != 0x160000u || depw != cntw + 8) return 0;
    int r = sv_build(c, cntw, depw, T0, 0);   /* the packets' bytes (and nothing else) */
    if (!r) { static int t; if (!t++) uya_log("native 2D shadow volumes: a volume the records could not hold: the game's routine"); return 0; }
    if (md == 1) { sv_chk_at = T0; sv_chk = r == 1 ? 1 : 2; return 0; }
    if (r == 2) return 1;            /* (a count of 1: the routine returns at once) */
    for (u32 k = 0; k < sv_n; k++) rc_sw(T0 + 4 * k, sv_pk[k]);
    rc_sw(cw, T0 + sv_n * 4);
    if (!sv_build(c, cntw, depw, T0, 1)) { static int t; if (!t++) uya_log("native 2D shadow volumes: records not made: the packets go as GIF data"); }   /* the records, markers on the bytes just written */
    memcpy(VF(1), sv_vf1, 16); memcpy(VF(2), sv_vf2, 16);
    return 1;
}
void uya_shadowvol_exit(RcCtx *c)
{
    (void)c;
    if (!sv_chk) return;
    int same = sv_chk == 2 || !memcmp((const void *)RC_A(sv_chk_at), sv_pk, sv_n * 4);
    sv_chk = 0;
    if (same) sv_ok++;
    else if (sv_bad++ < 5) {
        const u32 *gm = (const u32 *)RC_A(sv_chk_at);
        for (u32 k = 0; k < sv_n / 4; k++) if (memcmp(gm + 4 * k, sv_pk + 4 * k, 16)) { uya_log("native 2D shadow volumes check: qword %u of %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, sv_n / 4, gm[4 * k], gm[4 * k + 1], gm[4 * k + 2], gm[4 * k + 3], sv_pk[4 * k], sv_pk[4 * k + 1], sv_pk[4 * k + 2], sv_pk[4 * k + 3]); break; }
    }
    if ((sv_ok + sv_bad) == 50 || (sv_ok + sv_bad) % 2000 == 0) uya_log("native 2D shadow volumes check: %llu calls compared, %llu identical, %llu differ", sv_ok + sv_bad, sv_ok, sv_bad);
}

/* ---- func_00228690: the shadows' darkening pass (C in the decomp; translated in the levels) ----
 * a0 the colour (RGBAQ). The pass: TEST_1 0x3D801 (the destination alpha test on: only where the
 * shadow volumes set the target's alpha bit; no depth test), then full-height sprites (PRIM 0x146),
 * one per 32 pixels of the display's width, in that colour; then a CNT with FLUSHA (left in the chain).
 * As host draws: the pass setting (op 18), the sprites (op 19). */
static unsigned long long dk_ok, dk_bad;
static u32 dk_pk[4 * 160], dk_n, dk_chk_at, dk_chk;
static void rec_pass(const nd_pass *q)
{
    if (!rb_ok) return;
    u32 nq = 1 + (u32)((sizeof(nd_pass) + 15) / 16);
    if (ring_at + nq > R_QW) { rb_ok = 0; return; }
    u32 *op = ring + (size_t)ring_at * 4;
    memset(op, 0, (size_t)nq * 16);
    op[0] = 18; memcpy(op + 4, q, sizeof *q);
    ring_at += nq;
}
static int dk_build(RcCtx *c, u32 dims, u32 T, int write)
{
    s32 w = (s16)M16(dims + 0x150), h = (s16)M16(dims + 0x152);
    s32 sc = (w > -1 ? w : w + 0x1F) >> 5;
    if (sc <= 0 || sc > 150) return 0;   /* (no strip: the game's routine) */
    u64 col = c->r[4].d[0];
    u32 *p = dk_pk;
    p[0] = ((u32)sc + 5) | 0x10000000u; p[1] = 0; p[2] = 0; p[3] = ((u32)sc + 5) | 0x50000000u;
    p[4] = 1; p[5] = 0x10000000u; p[6] = 0xE; p[7] = 0;
    p[8] = 0x3D801; p[9] = 0; p[10] = 0x47; p[11] = 0;
    p[12] = 1; p[13] = 0x24000000u; p[14] = 0x10; p[15] = 0;
    p[16] = 0x146; p[17] = 0; p[18] = (u32)col; p[19] = (u32)(col >> 32);
    p[20] = (u32)sc | 0x8000; p[21] = 0x24000000u; p[22] = 0x44; p[23] = 0;
    static u32 wv[150 * 2 * 8];
    if (sc > 0) {
        s32 by = h * 8 + 0x7FF0, ty = 0x8000 - h * 8, nh = -(w * 8);
        s64 pty = (s64)ty << 16, pby = (s64)by << 16;
        s32 lx = nh + 0x8000, rx = nh + 0x8200;
        for (s32 i = 0; i < sc; i++) {
            u64 a = (u64)((s64)lx | pty), b = (u64)((s64)rx | pby);
            p[24 + i * 4] = (u32)a; p[25 + i * 4] = (u32)(a >> 32); p[26 + i * 4] = (u32)b; p[27 + i * 4] = (u32)(b >> 32);
            for (int k = 0; k < 2; k++) {
                u64 v = k ? b : a; u32 *o = wv + (i * 2 + k) * 8;
                o[0] = (u32)(v & 0xFFFF) | (u32)((v >> 16) & 0xFFFF) << 16; o[1] = (u32)(v >> 32); o[2] = 0; o[3] = (u32)col;
                o[4] = 0; o[5] = 0; o[6] = (u32)(col >> 32); o[7] = 0;
            }
            lx += 0x200; rx += 0x200;
        }
    }
    u32 e = 24 + (u32)sc * 4;
    p[e] = 0x10000000u; p[e + 1] = 0; p[e + 2] = 0x13000000u; p[e + 3] = 0;
    dk_n = e + 4;
    if (write) {
        rec_begin();
        nd_pass q; u32 ta = 0x47; u64 tv = 0x3D801;
        nd_pass_from_regs(&q, &ta, &tv, 1);
        rec_pass(&q);
        nd_material m; memset(&m, 0, sizeof m); m.has = ND_PRIM; m.kind = 6; m.blend = 1; m.uv = 1;   /* PRIM 0x146 */
        rec_op19(&m, wv, (u32)sc * 2, 1);
        rec_rgbaq((u32)col, (u32)(col >> 32));
        if (!rec_end(T, T + e * 4)) return 0;
    }
    return 1;
}
int uya_darkpass_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)g; (void)ng;
    int md = n2d_mode();
    if (!md) return 0;
    if (na < 3 || a[1] != a[2]) { static int t; if (!t++) uya_log("native 2D darkening pass: %d references (not the routine's shape): the game's routine", na); return 0; }
    u32 dims = a[0], cw = a[1], T = M32(cw);
    if (!chain_word_ok(cw) || dims != 0x151880u) return 0;
    if (!dk_build(c, dims, T, 0)) return 0;
    if (md == 1) { dk_chk_at = T; dk_chk = 1; return 0; }
    for (u32 k = 0; k < dk_n; k++) rc_sw(T + 4 * k, dk_pk[k]);
    rc_sw(cw, T + dk_n * 4);
    dk_build(c, dims, T, 1);
    return 1;
}
void uya_darkpass_exit(RcCtx *c)
{
    (void)c;
    if (!dk_chk) return;
    dk_chk = 0;
    if (!memcmp((const void *)RC_A(dk_chk_at), dk_pk, dk_n * 4)) dk_ok++;
    else if (dk_bad++ < 5) {
        const u32 *gm = (const u32 *)RC_A(dk_chk_at);
        for (u32 k = 0; k < dk_n / 4; k++) if (memcmp(gm + 4 * k, dk_pk + 4 * k, 16)) { uya_log("native 2D darkening pass check: qword %u of %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, dk_n / 4, gm[4 * k], gm[4 * k + 1], gm[4 * k + 2], gm[4 * k + 3], dk_pk[4 * k], dk_pk[4 * k + 1], dk_pk[4 * k + 2], dk_pk[4 * k + 3]); break; }
    }
    if ((dk_ok + dk_bad) == 50 || (dk_ok + dk_bad) % 2000 == 0) uya_log("native 2D darkening pass check: %llu calls compared, %llu identical, %llu differ", dk_ok + dk_bad, dk_ok, dk_bad);
}

/* ---- func_001F9478: the glow billboards (append_billboard_batch, C in the decomp) ----
 * The game's routine runs (its projection and its VU0 sine and cosine are the game's own); at its
 * return each billboard it wrote (a CNT 9 with DIRECT 9: its GIF tag from the template, REGLIST
 * CLAMP_1, TEX0_1, PRIM, then RGBAQ, UV, XYZ2 for each of four vertices) becomes a host draw: its
 * material (CLAMP, TEX0, PRIM 0x154: a textured, blended UV strip) and its four vertices as a
 * record, the packet's place a marker. The blend setting around them (VU1_addGSregister ALPHA_1)
 * stays the game's state packet. Any packet of another shape stays as GIF data. */
static u32 bb_cw, bb_t0, bb_on;
static unsigned long long bb_made, bb_other;
static u64 bb_regs(void)
{
    static const u8 r[15] = { 8, 6, 0, 1, 3, 5, 1, 3, 5, 1, 3, 5, 1, 3, 5 };
    u64 v = 0;
    for (int k = 0; k < 15; k++) v |= (u64)r[k] << (4 * k);
    return v;
}
int uya_billboards_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)c; (void)g; (void)ng;
    bb_on = 0;
    if (!n2d_mode() || n2d_mode() == 1) return 0;
    /* the chain word: the absolute reference the routine uses most */
    u32 best = 0; int bn = 0;
    for (int i = 0; i < na; i++) { int k = 0; for (int j = 0; j < na; j++) k += a[j] == a[i]; if (k > bn) { bn = k; best = a[i]; } }
    if (bn < 3 || !chain_word_ok(best)) { static int t; if (!t++) uya_log("native 2D billboards: no chain word among %d references: GIF data", na); return 0; }
    bb_cw = best; bb_t0 = M32(best); bb_on = 1;
    return 0;
}
void uya_billboards_exit(RcCtx *c)
{
    (void)c;
    {   /* testing: UYA_MEMDUMP=addr,qwords,vsync: memory as qwords, once, at that vsync (from here: called each frame in the levels) */
        static int st = -1; static unsigned ad, nq, vs;
        if (st < 0) { const char *e = getenv("UYA_MEMDUMP"); st = e && sscanf(e, "%x,%u,%u", &ad, &nq, &vs) == 3; }
        if (st == 1 && uya_frame_count() >= vs) { st = 2; for (unsigned k = 0; k < nq; k++) uya_log("MEMDUMP %08X: %08X %08X %08X %08X", ad + 16 * k, M32(ad + 16 * k), M32(ad + 16 * k + 4), M32(ad + 16 * k + 8), M32(ad + 16 * k + 12)); }
    }
    if (!bb_on) return;
    bb_on = 0;
    u32 q = bb_t0, end = M32(bb_cw);
    if (end < q || end - q > 0x10000) { static int t; if (!t++) uya_log("native 2D billboards: chain word %08X: %08X..%08X: not a range", bb_cw, q, end); return; }
    { static int t; if (!t++) uya_log("native 2D billboards: chain word %08X, %08X..%08X", bb_cw, q, end); }
    const u64 regs = bb_regs();
    while (q < end) {
        u32 tag = M32(q), qwc = tag & 0xFFFF, id = (tag >> 28) & 7;
        if (id != 1) { static int t; if (t < 3) { t++; uya_log("native 2D billboards: tag %08X %08X %08X %08X at %08X (not a CNT): stopped", tag, M32(q + 4), M32(q + 8), M32(q + 12), q); } return; }   /* (only CNT tags in its part of the chain) */
        u64 lo = M64(q + 16), hi = M64(q + 24);
        if (tag == 0x10000009u && !M32(q + 4) && !M32(q + 8) && M32(q + 12) == 0x50000009u &&
            (lo & 0x7FFF) == 1 && ((lo >> 15) & 1) && ((lo >> 58) & 3) == 1 && ((lo >> 60) & 15) == 15 && hi == regs) {
            const u64 *w = (const u64 *)RC_A(q + 32);
            u32 ra[3] = { 0x08, 0x06, 0x00 }; u64 rv[3] = { w[0], w[1], w[2] };
            nd_material m;
            if (nd_material_from_regs(&m, ra, rv, 3)) {
                u32 wv[4 * 8];
                for (int k = 0; k < 4; k++) {
                    u64 col = w[3 + 3 * k], uvv = w[4 + 3 * k], xyz = w[5 + 3 * k];
                    u32 *o = wv + k * 8;
                    o[0] = (u32)(xyz & 0xFFFF) | (u32)((xyz >> 16) & 0xFFFF) << 16; o[1] = (u32)(xyz >> 32); o[2] = 0; o[3] = (u32)col;
                    o[4] = 0; o[5] = 0; o[6] = (u32)(col >> 32); o[7] = (u32)(uvv & 0x3FFF) | (u32)((uvv >> 16) & 0x3FFF) << 16;
                }
                rec_begin(); rec_op19(&m, wv, 4, 1);
                if (rec_end(q, q + 16 * (qwc + 1))) bb_made++;
            } else bb_other++;
        } else {
            bb_other++;
            static int t; if (t < 4 && tag != 0x10000002u) { t++; uya_log("native 2D billboards: a packet as GIF data: %08X %08X %08X %08X / %016llX %016llX", tag, M32(q + 4), M32(q + 8), M32(q + 12), (unsigned long long)lo, (unsigned long long)hi); }
        }
        q += 16 * (qwc + 1);
    }
    static unsigned long long next = 1;
    if (bb_made >= next) { next = next < 10000 ? next * 10 : next + 10000; uya_log("native 2D billboards: %llu as host draws, %llu other packets", bb_made, bb_other); }
}

/* ---- func_001FB848 / func_001FB8A8 (AA_BlurPass and its twin): the frame copy ----
 * Each sends a REF to a table the game builds once for the display (func_001FAB40, asm): a PACKED
 * GIF tag with PRIM 0x116 (a textured UV sprite) and four A+D writes (TEST_1, CLAMP_1, TEX1_1, TEX0_1:
 * the frame buffer as the texture), a REGLIST tag of sprites (UV, XYZ2, UV, XYZ2: one per 32-pixel
 * strip of the screen), and in the twin a last REGLIST tag (PRIM 0x181, RGBAQ, two XYZF2: a line).
 * Natively: the REF is written as the game writes it and becomes a marker; the record has the pass
 * setting (TEST), the material (CLAMP, TEX1, TEX0, the PRIM), the sprites as host primitives with the
 * colour the GS holds (the table sets none; Q 1.0 from the tag), and the line. The table is read when
 * the routine runs and again where the chain meets the marker: a table changed in between (the
 * display set up again) goes as GIF data. A table of any other shape stays GIF data. */
static unsigned long long bl_made;
static int bl_parse(u32 tb, u32 qwc, int write)
{
    u64 lo = M64(tb), hi = M64(tb + 8);
    if (lo != 0x408B400000000001ull || hi != 0xEEEEull) return 0;
    static const u32 ad[4] = { 0x47, 0x08, 0x14, 0x06 };
    u32 aa[4]; u64 av[4];
    for (int k = 0; k < 4; k++) { av[k] = M64(tb + 16 + 16 * (u32)k); aa[k] = M32(tb + 24 + 16 * (u32)k); if (aa[k] != ad[k] || M32(tb + 28 + 16 * (u32)k)) return 0; }
    u32 q = 5;
    u64 t2 = M64(tb + 16 * q), r2 = M64(tb + 16 * q + 8);
    u32 n = (u32)(t2 & 0x7FFF);
    if (((t2 >> 15) & 1) != 1 || ((t2 >> 46) & 1) || ((t2 >> 58) & 3) != 1 || ((t2 >> 60) & 15) != 4 || r2 != 0x5353 || (t2 >> 16 & 0x3FFFFFFFull)) return 0;
    if (q + 1 + 2 * n > qwc || n > 64) return 0;
    u32 qe = q + 1 + 2 * n;
    int line = 0;
    if (qe != qwc) {
        if (qe + 3 != qwc) return 0;
        u64 t3 = M64(tb + 16 * qe), r3 = M64(tb + 16 * qe + 8);
        if (t3 != 0x4400000000008001ull || r3 != 0x4410) return 0;
        line = 1;
    }
    if (!write) return 1;
    nd_pass ps; u32 ta = 0x47; u64 tv = av[0];
    if (!nd_pass_from_regs(&ps, &ta, &tv, 1)) return 0;
    nd_material m; u32 ma[4] = { 0x00, 0x08, 0x14, 0x06 }; u64 mv[4] = { 0x116, av[1], av[2], av[3] };
    if (!nd_material_from_regs(&m, ma, mv, 4)) return 0;
    static u32 wv[128 * 8];
    for (u32 i = 0; i < n; i++)
        for (int k = 0; k < 2; k++) {
            u64 uvv = M64(tb + 16 * (q + 1 + 2 * i + (u32)k)), xyz = M64(tb + 16 * (q + 1 + 2 * i + (u32)k) + 8);
            u32 *o = wv + (i * 2 + (u32)k) * 8;
            o[0] = (u32)(xyz & 0xFFFF) | (u32)((xyz >> 16) & 0xFFFF) << 16; o[1] = (u32)(xyz >> 32); o[2] = 0; o[3] = 0;
            o[4] = 0; o[5] = 0; o[6] = 0x3F800000u; o[7] = (u32)(uvv & 0x3FFF) | (u32)((uvv >> 16) & 0x3FFF) << 16;
        }
    rec_pass(&ps);
    rec_op19(&m, wv, 2 * n, 3);            /* (ST and the colour as the GS holds them) */
    if (line) {
        u64 pr = M64(tb + 16 * qe + 16), rg = M64(tb + 16 * qe + 24), x0 = M64(tb + 16 * qe + 32), x1 = M64(tb + 16 * qe + 40);
        nd_material ml; u32 la = 0x00; u64 lv = pr;
        if (!nd_material_from_regs(&ml, &la, &lv, 1)) return 0;
        u32 lw[2 * 8];
        for (int k = 0; k < 2; k++) {
            u64 x = k ? x1 : x0; u32 *o = lw + k * 8;
            o[0] = (u32)(x & 0xFFFF) | (u32)((x >> 16) & 0xFFFF) << 16; o[1] = (u32)((x >> 32) & 0xFFFFFF); o[2] = (u32)(x >> 56); o[3] = (u32)rg;
            o[4] = 0; o[5] = 0; o[6] = (u32)(rg >> 32); o[7] = 0;
        }
        rec_op19(&ml, lw, 2, 1);
    }
    return 1;
}
static int bl_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na, u32 qwc)
{
    int md = n2d_mode();
    if (md != 2) return 0;
    /* the chain word and the table (absolute), the second chain pointer (gp: the routine leaves it the new pointer too) */
    if (na < 6 || ng < 1 || g[0] != -0x5D00) return 0;
    u32 cw = a[0], tb = a[1];
    if (!chain_word_ok(cw) || a[2] != cw || a[3] != cw || a[4] != cw || a[5] != cw || (tb != 0x151A00u && tb != 0x151C60u)) return 0;
    if ((qwc == 0x26) != (tb == 0x151A00u)) return 0;   /* (the twin's table with the other's size: a shared body: the game's routine) */
    if (!bl_parse(tb, qwc, 0)) { static int t; if (!t++) uya_log("native 2D frame copy: the table at %08X (%u qwords) is not the expected shape: GIF data", tb, qwc); return 0; }
    u32 T = M32(cw);
    rc_sw(T, 0x30000000u | qwc); rc_sw(T + 4, tb); rc_sw(T + 8, 0); rc_sw(T + 12, 0x50000000u | qwc);
    rc_sw(cw, T + 16);
    rc_sw((u32)c->r[28].d[0] + (u32)g[0], T + 16);
    rec_begin();
    if (!bl_parse(tb, qwc, 1)) { rb_ok = 0; return 1; }
    rec_snapshot(tb, qwc);
    if (rec_end(T, T + 16)) bl_made++;
    return 1;
}
int uya_blurpass_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { return bl_enter(c, g, ng, a, na, 0x26); }
int uya_blurpass2_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { return bl_enter(c, g, ng, a, na, 0x29); }
void uya_blurpass_exit(RcCtx *c) { (void)c; }
void uya_blurpass2_exit(RcCtx *c) { (void)c; }
