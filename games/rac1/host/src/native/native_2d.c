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
typedef struct { u32 at, end, q0[4]; u32 off, nq; u32 snap_addr, snap_off, snap_n; u32 upl; } Rec;   /* upl: ops with op 22 (an upload's data, read at the marker) */   /* snap: memory the record was made from, checked again where the chain meets it */
static Rec *rec; static u32 *ring;
static u32 rec_next, ring_at;
static unsigned long long n_made, n_drawn, n_unknown, n_back;
int uya_hl_on(void);
void uya_hl_put(u32 a);
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
    if (uya_hl_on()) uya_hl_put(at);   /* (the marker into the host draw list as it is made) */
    n_made++;
}
/* a record of several op 19 blocks (a producer whose material changes inside its packet) */
static u32 rb_start, rb_ok, rb_snap_addr, rb_snap_off, rb_snap_n, rb_upl;
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
    r->snap_addr = rb_snap_addr; r->snap_off = rb_snap_off; r->snap_n = rb_snap_n; r->upl = rb_upl; rb_upl = 0;
    for (int k = 0; k < 4; k++) r->q0[k] = M32(at + 4 * (u32)k);
    rc_sw(at, 0x20000000u); rc_sw(at + 4, end); rc_sw(at + 8, NATIVE_2D_MARK); rc_sw(at + 12, i);
    if (uya_hl_on()) uya_hl_put(at);   /* (the marker into the host draw list as it is made) */
    n_made++;
    return 1;
}
int native_2d_tag(const u32 *q, u32 t)
{
    u32 i = q[3] % R_N;
    if (!rec || rec[i].at != t) { if (n_unknown++ < 3) uya_log("native 2D: marker %u at %08X not known: skipped", q[3], t); return 0; }
    Rec *r = &rec[i];
    for (int k = 0; k < 4; k++) rc_sw(t + 4 * (u32)k, r->q0[k]);
    if (uya_hl_on()) uya_hl_put(t);   /* (the packet put back: the host draw list has it too, for a chain sent again) */
    r->at = 0;
    if (r->snap_n && memcmp(ring + (size_t)r->snap_off * 4, (const void *)RC_A(r->snap_addr), (size_t)r->snap_n * 16)) { n_back++; return 2; }   /* (changed since: the packet as GIF data) */
    extern const char *uya_gs_src; uya_gs_src = "native 2D";
    if (r->upl) {   /* an upload: its data read now, where the DMA would have read it (op 22 {22, qwords, address} -> op 21 + the data) */
        static u32 *x; static u32 xcap;
        const u32 *o = ring + (size_t)r->off * 4;
        u32 need = r->nq;
        for (u32 k = 0; k < r->nq; k++) if (o[k * 4] == 22) need += o[k * 4 + 1];
        if (need > xcap) { free(x); xcap = need + 4096; x = malloc((size_t)xcap * 16); if (!x) { xcap = 0; return 2; } }
        u32 n = 0;
        for (u32 k = 0; k < r->nq; k++) {
            const u32 *c = o + k * 4;
            if (c[0] != 22) { memcpy(x + n * 4, c, 16); n++; continue; }
            x[n * 4] = 21; x[n * 4 + 1] = c[1]; x[n * 4 + 2] = 0; x[n * 4 + 3] = 0; n++;
            memcpy(x + n * 4, (const void *)RC_A(c[2]), (size_t)c[1] * 16); n += c[1];
        }
        uya_gs_src = "native texture upload";
        uya_gs_dlist(x, n);
        n_drawn++;
        return 1;
    }
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
u32 uya_chain_word_seen;   /* (testing: the chain watch's chain word, UYA_CWATCH=auto) */
static int chain_word_ok(u32 cw)
{
    if ((cw & 0xFFFFF000u) != 0x161000u) return 0;
    u32 p = M32(cw);
    if (p >= 0x100000u && p < 0x2000000u && !(p & 15)) { uya_chain_word_seen = cw; return 1; }
    return 0;
}
/* the chain word a routine reaches through gp: the executable's at gp - 0x5D00, the levels' at
 * gp - 0x5AC0 or gp - 0x5A80 (their data moved), each the word chain_word_ok names */
static u32 gp_chain_word(RcCtx *c, s32 g)
{
    if (g != -0x5D00 && g != -0x5AC0 && g != -0x5A80) return 0;
    u32 cw = (u32)c->r[28].d[0] + (u32)g;
    return chain_word_ok(cw) ? cw : 0;
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
    u32 cwd = gp_chain_word(c, g[0]), T = cwd ? M32(cwd) : 0;   /* (the chain pointer's gp word: checked, see chain_word_ok) */
    if (!cwd) return 0;
    if (T < 0x100000u || T >= 0x2000000u || (T & 15)) return 0;
    if (!sg_build(n, (u32)c->r[5].d[0], (u32)c->r[6].d[0], (u32)c->r[7].d[0], T)) return 0;
    sg_runs++;
    if (md == 1) { sg_chk_at = T; sg_chk = 1; return 0; }
    if (sg_n == 8) return 1;               /* (no triangle: nothing written) */
    for (u32 k = 0; k < sg_n; k++) rc_sw(T + 4 * k, sg_pk[k]);
    rc_sw(cwd, T + sg_n * 4);
    nd_material m; memset(&m, 0, sizeof m);
    record(T, T + sg_n * 4, &m, sg_wv, sg_nv, 1 | 4);
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
            if (nv) { if (write) rec_op19(&mt, wv, nv, 4); nv = 0; }
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
    if (nv && write) rec_op19(&mt, wv, nv, 4);
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
    /* (checked, see chain_word_ok): the executable's layout or the levels' (their sky table and upload
     * count moved with their data; the upload list is each level's own) */
    if (!gp_chain_word(c, g[0])) return 0;
    if (!(g[0] == -0x5D00 && tblo == -0x67A4 && allo == -0x7D8C && cnto == -0x77A8 && a[0] == 0x18D140u) &&
        !(g[0] != -0x5D00 && tblo == g[0] - 0xCA4 && allo == -0x7D8C && cnto == -0x77E8 && (a[0] & 0xFFFF007Fu) == 0x160000u)) return 0;
    s32 G[3] = { tblo, allo, cnto };
    u32 n = (u32)c->r[4].d[0];
    if (!n || n > 0x10000) return 0;
    u32 cwd = (u32)c->r[28].d[0] + (u32)g[0], T = M32(cwd);
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
        if (write) rec_op19(&mq, wv, 4, 4);
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
        if (write) { rec_begin(); rec_op19(&mc, wv, nv, 4); if (!rec_end(T2, T2 + (nv + 4) * 16)) return 0; }
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
        rec_op19(&m, wv, (u32)sc * 2, 1 | 4);
        rec_rgbaq((u32)col, (u32)(col >> 32));
        if (!rec_end(T, T + e * 4)) return 0;
    }
    return 1;
}
static int darkpass_enter_(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
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
static void darkpass_exit_(RcCtx *c)
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
        if (tag == 0x20000000u && M32(q + 8) == NATIVE_2D_MARK && M32(q + 4) > q && M32(q + 4) <= end) { q = M32(q + 4); continue; }   /* (a packet already a record: VU1_addGSregister's) */
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
            } else { bb_other++; static int t; if (t < 4) { t++; uya_log("native 2D billboards: a material not taken: CLAMP %016llX TEX0 %016llX PRIM %016llX", (unsigned long long)w[0], (unsigned long long)w[1], (unsigned long long)w[2]); } }
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
        rec_op19(&ml, lw, 2, 1 | 4);
    }
    return 1;
}
int uya_texdma_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na);
void uya_texdma_exit(RcCtx *c);
static int bl_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na, u32 qwc)
{
    uya_texdma_enter(c, g, ng, a, na);   /* (the look-alikes the levels catalogue as this routine, VU1_texFlush among them: their REFs at their return) */
    int md = n2d_mode();
    if (md != 2) return 0;
    /* the chain word and the table (absolute), the second chain pointer (gp: the routine leaves it the new pointer too) */
    if (na != 6 || ng != 1) return 0;
    u32 cw = a[0], tb = a[1];
    if (gp_chain_word(c, g[0]) != cw || a[2] != cw || a[3] != cw || a[4] != cw || a[5] != cw || (tb != 0x151A00u && tb != 0x151C60u)) return 0;
    /* (the levels name the twin as a copy of the first, sharing its body: the size goes with the table) */
    qwc = tb == 0x151A00u ? 0x26 : 0x29;
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
void uya_blurpass_exit(RcCtx *c) { uya_texdma_exit(c); }
void uya_blurpass2_exit(RcCtx *c) { uya_texdma_exit(c); }   /* (bl_enter opened the return scan for both) */

/* ---- the GS state helpers: VU1_addGSregister func_00234C98, VU1_setScissor func_00234D58 ----
 * Each writes one register as a packet (a CNT 2 with DIRECT 2, an A+D GIF tag, the value and the
 * register's address): VU1_addGSregister the register a0 with the value a1; VU1_setScissor SCISSOR_1
 * from x0 x1 y0 y1 (a0-a3) clamped to the screen (the view's width and height, D_0013E600[0], [1]).
 * Natively: the packet written as the game writes it, its place a marker, and the record the write
 * as a pass setting or material (ops 17, 18); a register that is neither stays the game's packet. */
static unsigned long long gr_made, gr_ok, gr_bad, gr_other;
static u32 gr_pk[12], gr_chk_at, gr_chk;
static int rec_regs(u32 addr, u64 val)
{
    if (!rb_ok) return 0;
    u32 o = ring_at;
    if (!nd_pack_regs(&addr, &val, 1, ring, &o, R_QW)) return 0;
    ring_at = o;
    return 1;
}
static void gr_build(u32 reg, u64 val)
{
    u32 *p = gr_pk;
    p[0] = 0x10000002u; p[1] = 0; p[2] = 0; p[3] = 0x50000002u;
    p[4] = 0x8001; p[5] = 0x10000000u; p[6] = 0xE; p[7] = 0;
    p[8] = (u32)val; p[9] = (u32)(val >> 32); p[10] = reg; p[11] = 0;
}
static int gr_emit(u32 cw, u32 reg, u64 val, int md)
{
    u32 T = M32(cw);
    gr_build(reg, val);
    if (md == 1) { gr_chk_at = T; gr_chk = 1; return 0; }
    if (ring_at + 64 > R_QW) ring_at = 0;
    rec_begin();
    if (!rec_regs(reg, val)) { gr_other++; return 0; }
    for (int k = 0; k < 12; k++) rc_sw(T + 4 * (u32)k, gr_pk[k]);
    rc_sw(cw, T + 0x30);
    if (rec_end(T, T + 0x30)) gr_made++;
    if ((gr_made & 0xFFFF) == 1) uya_log("native 2D GS settings: %llu as host settings, %llu registers left as packets", gr_made, gr_other);
    return 1;
}
/* the chain word: every absolute reference the routine has is it */
static u32 refs_one(const u32 *a, int na, int n0)
{
    if (na < n0) return 0;
    for (int i = 1; i < n0; i++) if (a[i] != a[0]) return 0;
    return chain_word_ok(a[0]) ? a[0] : 0;
}
int uya_addgsreg_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    int md = n2d_mode();
    if (!md) return 0;
    u32 cw = refs_one(a, na, na);
    if (!cw || ng != 1 || gp_chain_word(c, g[0]) != cw) { static int t; if (!t++) uya_log("native 2D GS settings: %d references, %d through gp, not the chain word's: the game's routine", na, ng); return 0; }
    return gr_emit(cw, (u32)c->r[4].d[0], c->r[5].d[0], md);
}
void uya_addgsreg_exit(RcCtx *c)
{
    (void)c;
    if (!gr_chk) return;
    gr_chk = 0;
    if (!memcmp((const void *)RC_A(gr_chk_at), gr_pk, 48)) gr_ok++;
    else if (gr_bad++ < 5) { const u32 *q = (const u32 *)RC_A(gr_chk_at); uya_log("native 2D GS settings check: game %08X %08X %08X %08X / %08X %08X %08X %08X, native %08X %08X %08X %08X", q[0], q[3], q[8], q[9], q[10], q[11], q[4], q[6], gr_pk[0], gr_pk[3], gr_pk[8], gr_pk[9]); }
    if ((gr_ok + gr_bad) == 50 || (gr_ok + gr_bad) % 20000 == 0) uya_log("native 2D GS settings check: %llu compared, %llu identical, %llu differ", gr_ok + gr_bad, gr_ok, gr_bad);
}
int uya_setscissor_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    int md = n2d_mode();
    if (!md) return 0;
    /* the references: the view, then the chain word (12 times) */
    if (na != 13 || a[0] != 0x13E600u) { static int t; if (!t++) uya_log("native 2D scissor: %d references (not the routine's): the game's routine", na); return 0; }
    u32 cw = refs_one(a + 1, 12, 12);
    if (!cw || ng != 1 || gp_chain_word(c, g[0]) != cw) return 0;
    s32 x0 = (s32)c->r[4].d[0], x1 = (s32)c->r[5].d[0], y0 = (s32)c->r[6].d[0], y1 = (s32)c->r[7].d[0];
    s32 W = (s32)M32(0x13E600u), H = (s32)M32(0x13E604u);
    x0 = x0 < 0 ? 0 : x0; x1 = x1 > W - 1 ? W - 1 : x1;
    y0 = y0 < 0 ? 0 : y0; y1 = y1 > H - 1 ? H - 1 : y1;
    u64 v = (u64)(s64)x0 | ((u64)(s64)x1 << 16) | ((u64)(s64)y0 << 32) | ((u64)(s64)y1 << 48);
    return gr_emit(cw, 0x40, v, md);
}
void uya_setscissor_exit(RcCtx *c) { uya_addgsreg_exit(c); }

/* ---- HudSprite func_00200468 and its quarter-turned strip func_00200650 ----
 * A level carries its own copies, the same but for the addresses in them (the HUD's data, the chain
 * word, GetFrameTex): the hook's references give them (the recompiler adds the routine's calls after
 * its data references, HOST_HOOKS_CALLS), and must have the routine's shape: the HUD data, the chain
 * word six times, the view, the chain word twice, then GetFrameTex.
 * a0 the HUD texture, a1-a3 x y w, t0 h, t1 alpha. The texture's entry (the HUD data's +0x24 table,
 * by the index at +0x20) gives its size (1 << its +6, +7 bytes); GetFrameTex (the game's) gives its
 * TEX0, placing it and queueing its upload the first time. The sprite: a CNT 5 with DIRECT 5, a REGLIST
 * tag (TEX0, PRIM 0x156, RGBAQ 0x7F7F7F with the alpha, UV XYZ2 twice), the corners (x, y), (x+w, y+h)
 * in 1/16 pixel with the view's offset less 8, Z the HUD data's +0xC; UV (0, 0), (tw * 16, th * 16).
 * The strip: CNT 7, PRIM 0x154, four corners (x, y+h), (x, y), (x+w, y+h), (x+w, y) with UVs (tw, 0),
 * (tw, th), (0, 0), (0, th). Natively: the packet written as the game writes it, its place a marker,
 * the record the material (TEX0, PRIM) and the vertices. */
static unsigned long long hs_made, hs_ok, hs_bad, hs_plain;
static u32 hs_pk[32], hs_n, hs_chk_at, hs_chk;
static u64 hud_xyz(s32 x, s32 y, s32 z)
{
    s32 ox = (s32)M32(0x13E610u), oy = (s32)M32(0x13E614u);
    return (u64)(s64)(s32)((u32)x * 16u + (u32)ox - 8u) | ((u64)(s64)(s32)((u32)y * 16u + (u32)oy - 8u) << 16) | ((u64)(u32)z << 32);
}
static int hud_enter(RcCtx *c, int ng, const u32 *a, int na, int flip)
{
    int md = n2d_mode();
    if (!md) return 0;
    u32 cw = na == 11 ? a[1] : 0, ok = na == 11 && ng == 0 && a[7] == 0x13E600u && a[0] >= 0x100000u && a[0] < 0x2000000u;
    for (int k = 1; ok && k < 10; k++) if (k != 7 && a[k] != cw) ok = 0;
    if (!ok || !chain_word_ok(cw) || a[10] < 0x100000u || a[10] >= 0x2000000u || (a[10] & 3)) {
        static int t[2]; if (t[flip] < 3) { t[flip]++; char b[300]; int o = 0; for (int k = 0; k < na && o < 280; k++) o += snprintf(b + o, sizeof b - o, " %08X", a[k]);
            uya_log("native 2D HUD sprite%s: references%s, %d through gp (not the routine's): the game's routine", flip ? " (strip)" : "", b, ng); }
        return 0;
    }
    u32 arena = a[0], gft = a[10];
    { static int t; if (!t++) uya_log("native 2D HUD sprite: the HUD data %08X, the chain word %08X, GetFrameTex %08X", arena, cw, gft); }
    s32 tex = (s32)c->r[4].d[0], x = (s32)c->r[5].d[0], y = (s32)c->r[6].d[0], w = (s32)c->r[7].d[0], h = (s32)c->r[8].d[0];
    u64 al = c->r[9].d[0];
    u32 e = M32(arena + 0x24) + (u32)((s32)(s16)M16(M32(arena + 0x20) + (u32)tex * 4 + 2) * 8);
    u32 tw = 1u << (M8(e + 6) & 31), th = 1u << (M8(e + 7) & 31);
    s32 z = (s32)M32(arena + 0xC);
    /* the texture (the game's GetFrameTex: it places it and queues its upload) */
    static R128 keep[32];   /* (the registers as the call found them: the game's routine runs after it in the check) */
    memcpy(keep, c->r, sizeof keep);
    rc_dispatch(c, gft);
    u64 tex0 = c->r[2].d[0];
    memcpy(c->r, keep, sizeof keep);
    u64 rgba = (al << 24) | 0x7F7F7F;
    u32 T = M32(cw);
    u64 p[14]; u32 nq;
    p[2] = tex0; p[4] = rgba;
    u32 wv[4 * 8]; u32 nv;
    if (!flip) {
        p[0] = 0x7400000000008001ull; p[1] = 0x5353106; p[3] = 0x156;
        p[5] = 0; p[6] = hud_xyz(x, y, z);
        p[7] = (u64)(s64)(s32)((th << 20) + tw * 16); p[8] = hud_xyz(x + w, y + h, z); p[9] = 0;
        nq = 5; nv = 2;
        vtx(wv, p[6], rgba, p[5]); vtx(wv + 8, p[8], rgba, p[7]);
    } else {
        p[0] = 0xB400000000008001ull; p[1] = 0x53535353106ull; p[3] = 0x154;
        p[5] = (u64)(s64)(s32)(tw * 16); p[6] = hud_xyz(x, y + h, z);
        p[7] = (u64)(s64)(s32)((th << 20) + tw * 16); p[8] = hud_xyz(x, y, z);
        p[9] = 0; p[10] = hud_xyz(x + w, y + h, z);
        p[11] = (u64)(s64)(s32)(th << 20); p[12] = hud_xyz(x + w, y, z); p[13] = 0;
        nq = 7; nv = 4;
        for (int k = 0; k < 4; k++) vtx(wv + k * 8, p[6 + 2 * k], rgba, p[5 + 2 * k]);
    }
    hs_pk[0] = 0x10000000u | nq; hs_pk[1] = 0; hs_pk[2] = 0; hs_pk[3] = 0x50000000u | nq;
    memcpy(hs_pk + 4, p, nq * 16);
    hs_n = (nq + 1) * 4;
    if (md == 1) {   /* check: the packet built aside; the game's routine writes its own (GetFrameTex has run: the routine's call finds the texture placed) */
        hs_chk_at = T; hs_chk = 1;
        return 0;
    }
    for (u32 k = 0; k < hs_n; k++) rc_sw(T + 4 * k, hs_pk[k]);
    rc_sw(cw, T + hs_n * 4);
    nd_material m; u32 ra2[2] = { 0x06, 0x00 }; u64 rv[2] = { tex0, p[3] };
    if (!nd_material_from_regs(&m, ra2, rv, 2)) { hs_plain++; return 1; }   /* (the packet as GIF data) */
    record(T, T + hs_n * 4, &m, wv, nv, 1);
    if ((++hs_made & 0x3FFF) == 1) uya_log("native 2D HUD sprites: %llu as host draws, %llu as GIF data", hs_made, hs_plain);
    return 1;
}
int uya_hudsprite_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { (void)g; return hud_enter(c, ng, a, na, 0); }
int uya_hudflip_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { (void)g; return hud_enter(c, ng, a, na, 1); }
void uya_hudsprite_exit(RcCtx *c)
{
    (void)c;
    if (!hs_chk) return;
    hs_chk = 0;
    if (!memcmp((const void *)RC_A(hs_chk_at), hs_pk, hs_n * 4)) hs_ok++;
    else if (hs_bad++ < 5) {
        const u32 *g = (const u32 *)RC_A(hs_chk_at);
        for (u32 k = 0; k < hs_n / 4; k++) if (memcmp(g + 4 * k, hs_pk + 4 * k, 16)) { uya_log("native 2D HUD sprite check: qword %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, g[4 * k], g[4 * k + 1], g[4 * k + 2], g[4 * k + 3], hs_pk[4 * k], hs_pk[4 * k + 1], hs_pk[4 * k + 2], hs_pk[4 * k + 3]); break; }
    }
    if ((hs_ok + hs_bad) == 50 || (hs_ok + hs_bad) % 2000 == 0) uya_log("native 2D HUD sprite check: %llu compared, %llu identical, %llu differ", hs_ok + hs_bad, hs_ok, hs_bad);
}
void uya_hudflip_exit(RcCtx *c) { uya_hudsprite_exit(c); }

/* ---- func_002284E8: the shadows' stencil clear (asm) ----
 * Before the volumes: ALPHA_1 0x64 (VU1_addGSregister's packet), then TEST_1 0x31001 (the alpha test
 * passing nothing, failing pixels write the frame only: the target's alpha cleared), PRIM 0x146 and
 * RGBAQ 0x7F808080, and full-height sprites one per 32 pixels of the display's width, as the darkening
 * pass draws them. As host draws: the blend setting, the test setting (op 18), the sprites (op 19). */
static unsigned long long sc_ok, sc_bad;
static u32 sc_pk[4 * 170], sc_n, sc_chk_at, sc_chk;
static int sc_build(u32 dims, u32 T, int write)
{
    s32 w = (s16)M16(dims + 0x150), h = (s16)M16(dims + 0x152);
    s32 sc = (w > -1 ? w : w + 0x1F) >> 5;
    if (sc <= 0 || sc > 150) return 0;
    const u64 col = 0x7F808080ull;
    u32 *p = sc_pk;
    gr_build(0x42, 0x64); memcpy(p, gr_pk, 48); p += 12;
    p[0] = ((u32)sc + 5) | 0x10000000u; p[1] = 0; p[2] = 0; p[3] = ((u32)sc + 5) | 0x50000000u;
    p[4] = 1; p[5] = 0x10000000u; p[6] = 0xE; p[7] = 0;
    p[8] = 0x31001; p[9] = 0; p[10] = 0x47; p[11] = 0;
    p[12] = 0x8001; p[13] = 0x24000000u; p[14] = 0x10; p[15] = 0;
    p[16] = 0x146; p[17] = 0; p[18] = (u32)col; p[19] = 0;
    p[20] = (u32)sc | 0x8000; p[21] = 0x24000000u; p[22] = 0x44; p[23] = 0;
    static u32 wv[150 * 2 * 8];
    s32 by = h * 8 + 0x7FF0, ty = 0x8000 - h * 8, nh = -(w * 8);
    s64 pty = (s64)ty << 16, pby = (s64)by << 16;
    s32 lx = nh + 0x8000, rx = nh + 0x8200;
    for (s32 i = 0; i < sc; i++) {
        u64 a = (u64)((s64)lx | pty), b = (u64)((s64)rx | pby);
        p[24 + i * 4] = (u32)a; p[25 + i * 4] = (u32)(a >> 32); p[26 + i * 4] = (u32)b; p[27 + i * 4] = (u32)(b >> 32);
        vtx(wv + (i * 2) * 8, a, col, 0); vtx(wv + (i * 2 + 1) * 8, b, col, 0);
        lx += 0x200; rx += 0x200;
    }
    sc_n = 12 + 24 + (u32)sc * 4;
    if (write) {
        if (ring_at + 256 > R_QW) ring_at = 0;
        rec_begin();
        if (!rec_regs(0x42, 0x64)) return 0;
        if (!rec_end(T, T + 48)) return 0;
        rec_begin();
        nd_pass q; u32 ta = 0x47; u64 tv = 0x31001;
        if (!nd_pass_from_regs(&q, &ta, &tv, 1)) return 0;
        rec_pass(&q);
        nd_material m; u32 pa = 0x00; u64 pv = 0x146;
        if (!nd_material_from_regs(&m, &pa, &pv, 1)) return 0;
        rec_op19(&m, wv, (u32)sc * 2, 1 | 4);
        if (!rec_end(T + 48, T + sc_n * 4)) return 0;
    }
    return 1;
}
int uya_stencilclr_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)c; (void)g; (void)ng;
    int md = n2d_mode();
    if (!md) return 0;
    /* the references: the display's block, then the chain word (8 times) */
    if (na != 9 || a[0] != 0x151880u) { static int t; if (!t++) uya_log("native 2D stencil clear: %d references (not the routine's): the game's routine", na); return 0; }
    u32 cw = refs_one(a + 1, 8, 8);
    if (!cw) return 0;
    u32 T = M32(cw);
    if (!sc_build(a[0], T, 0)) return 0;
    if (md == 1) { sc_chk_at = T; sc_chk = 1; return 0; }
    /* (the bytes first: the records put their markers on them) */
    for (u32 k = 0; k < sc_n; k++) rc_sw(T + 4 * k, sc_pk[k]);
    rc_sw(cw, T + sc_n * 4);
    if (!sc_build(a[0], T, 1)) { static int t; if (!t++) uya_log("native 2D stencil clear: records not made: the packets go as GIF data"); }
    return 1;
}
void uya_stencilclr_exit(RcCtx *c)
{
    (void)c;
    if (!sc_chk) return;
    sc_chk = 0;
    if (!memcmp((const void *)RC_A(sc_chk_at), sc_pk, sc_n * 4)) sc_ok++;
    else if (sc_bad++ < 5) {
        const u32 *gm = (const u32 *)RC_A(sc_chk_at);
        for (u32 k = 0; k < sc_n / 4; k++) if (memcmp(gm + 4 * k, sc_pk + 4 * k, 16)) { uya_log("native 2D stencil clear check: qword %u of %u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, sc_n / 4, gm[4 * k], gm[4 * k + 1], gm[4 * k + 2], gm[4 * k + 3], sc_pk[4 * k], sc_pk[4 * k + 1], sc_pk[4 * k + 2], sc_pk[4 * k + 3]); break; }
    }
    if ((sc_ok + sc_bad) == 50 || (sc_ok + sc_bad) % 2000 == 0) uya_log("native 2D stencil clear check: %llu calls compared, %llu identical, %llu differ", sc_ok + sc_bad, sc_ok, sc_bad);
}

/* ---- func_L00_001FD428: a fogged, textured quad in perspective (asm, VU0; the levels' shared code) ----
 * Its math is the game's own (VU0's divide, its clip test): the routine runs. When it draws (its clip
 * test passed), it has written, through writeGSHeader's 12 qwords at `at`: an A+D tag with TEX1_1 and
 * ALPHA_1, a REGLIST tag (CLAMP_1, TEX0_1, PRIM 0x7C: a fogged, blended, textured Gouraud strip, then
 * ST, RGBAQ, XYZF2 for each of four vertices). At its return that packet, its fixed layout checked word
 * for word, becomes a record: the settings (ops 17, 18) and the strip as host primitives. */
static unsigned long long fq_made, fq_other;
int uya_fogquad_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { (void)c; (void)g; (void)ng; (void)a; (void)na; return 0; }
void uya_fogquad_exit(RcCtx *c)
{
    if (n2d_mode() != 2) return;
    u32 at = (u32)c->r[1].d[0];
    if (at < 0x100010u || at >= 0x2000000u || (at & 15)) return;
    u32 T = at - 16, end = at + 0xC0, cw = 0;
    static const u32 cws[3] = { 0x161000u, 0x161240u, 0x161280u };
    for (int k = 0; k < 3; k++) if (M32(cws[k]) == end && chain_word_ok(cws[k])) cw = cws[k];
    if (!cw || M32(T) != 0x1000000Cu || M32(T + 4) || M32(T + 8) || M32(T + 12) != 0x5000000Cu) { fq_other++; return; }
    if (M64(at) != 0x2000000000000001ull || M64(at + 8) != 0xEE || M64(at + 0x18) != 0x14 || M64(at + 0x28) != 0x42 ||
        M64(at + 0x30) != 0xF400000000008001ull || M64(at + 0x38) != 0x0412412412412068ull) { fq_other++; return; }
    u32 ga[2] = { 0x14, 0x42 }; u64 gv[2] = { M64(at + 0x10), M64(at + 0x20) };
    u32 ma[3] = { 0x08, 0x06, 0x00 }; u64 mv[3] = { M64(at + 0x40), M64(at + 0x48), M64(at + 0x50) };
    nd_material m;
    if (!nd_material_from_regs(&m, ma, mv, 3)) { fq_other++; return; }
    u32 wv[4 * 8];
    for (int k = 0; k < 4; k++) {
        u32 q = at + 0x58 + 24 * (u32)k;
        u64 st = M64(q), rgbaq = M64(q + 8), xyzf = M64(q + 16);
        u32 *w = wv + k * 8;
        w[0] = (u32)(xyzf & 0xFFFF) | (u32)((xyzf >> 16) & 0xFFFF) << 16; w[1] = (u32)(xyzf >> 32) & 0xFFFFFF; w[2] = (u32)(xyzf >> 56); w[3] = (u32)rgbaq;
        w[4] = (u32)st; w[5] = (u32)(st >> 32); w[6] = (u32)(rgbaq >> 32); w[7] = 0;
    }
    if (ring_at + 64 > R_QW) ring_at = 0;
    rec_begin();
    u32 o = ring_at;
    if (!nd_pack_regs(ga, gv, 2, ring, &o, R_QW)) { fq_other++; return; }
    ring_at = o;
    rec_op19(&m, wv, 4, 4);
    if (rec_end(T, end)) fq_made++;
    if ((fq_made & 0x3FFF) == 1) uya_log("native 2D fogged quads: %llu as host draws, %llu other packets", fq_made, fq_other);
}

/* ---- func_001FB530: the screen clear (a REF to the table at 0x152140, 21 qwords) ----
 * The table (built once by func_001FAB40, its colour set by func_001FB448): an A+D tag with TEST_1, a
 * PACKED tag with PRIM (a sprite) and RGBAQ (the clear colour), a REGLIST tag of sprites (two XYZ2 a
 * qword: one per 32-pixel strip of the screen). Natively as the frame copy: the REF written as the
 * game writes it, its place a marker; the record the test setting, the material and the sprites in
 * the table's colour; the table kept with the record and compared again where the chain meets the
 * marker (changed: GIF data). A table of any other shape stays GIF data. */
static unsigned long long cl_made;
static int cl_parse(u32 tb, u32 qwc, int write)
{
    if (M64(tb) != 0x1000000000000001ull || M64(tb + 8) != 0xE || M64(tb + 0x18) != 0x47) return 0;
    if (M64(tb + 0x20) != 0x2400000000008001ull || M64(tb + 0x28) != 0x10) return 0;
    u64 t3 = M64(tb + 0x40);
    u32 n = (u32)(t3 & 0x7FFF);
    if ((t3 & ~0x7FFFull) != 0x2400000000008000ull || M64(tb + 0x48) != 0x44 || n == 0 || 5 + n != qwc) return 0;
    if (!write) return 1;
    nd_pass ps; u32 ta = 0x47; u64 tv = M64(tb + 0x10);
    if (!nd_pass_from_regs(&ps, &ta, &tv, 1)) return 0;
    nd_material m; u32 pa = 0x00; u64 pv = M64(tb + 0x30);
    if (!nd_material_from_regs(&m, &pa, &pv, 1)) return 0;
    u64 col = M64(tb + 0x38);
    static u32 wv[64 * 2 * 8];
    if (n > 64) return 0;
    for (u32 i = 0; i < n; i++) { vtx(wv + (2 * i) * 8, M64(tb + 0x50 + 16 * i), col, 0); vtx(wv + (2 * i + 1) * 8, M64(tb + 0x58 + 16 * i), col, 0); }
    rec_pass(&ps);
    rec_op19(&m, wv, 2 * n, 1 | 4);
    return 1;
}
int uya_clearref_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    int md = n2d_mode();
    if (md != 2) return 0;
    /* the references: the chain word and the table (absolute; in some layouts the chain word through gp at the end) */
    u32 cw = 0, tb = 0;
    for (int i = 0; i < na; i++) {
        if (a[i] == 0x152140u) { tb = a[i]; continue; }
        if (!cw) cw = a[i];
        else if (a[i] != cw) return 0;
    }
    if (!cw || !tb || ng > 1 || (ng == 1 && gp_chain_word(c, g[0]) != cw) || !chain_word_ok(cw)) {
        static int t; if (t < 3) { t++; char b[300]; int o = 0; for (int k = 0; k < ng && o < 200; k++) o += snprintf(b + o, sizeof b - o, " %d", g[k]); for (int k = 0; k < na && o < 280; k++) o += snprintf(b + o, sizeof b - o, " %08X", a[k]);
            uya_log("native 2D screen clear: references%s (not the routine's): the game's routine", b); }
        return 0;
    }
    if (!cl_parse(tb, 0x15, 0)) { static int t; if (!t++) uya_log("native 2D screen clear: the table at %08X is not the expected shape: GIF data", tb); return 0; }
    u32 T = M32(cw);
    rc_sw(T, 0x30000015u); rc_sw(T + 4, tb); rc_sw(T + 8, 0); rc_sw(T + 12, 0x50000015u);
    rc_sw(cw, T + 16);
    if (ring_at + 256 > R_QW) ring_at = 0;
    rec_begin();
    if (!cl_parse(tb, 0x15, 1)) { rb_ok = 0; return 1; }
    rec_snapshot(tb, 0x15);
    if (rec_end(T, T + 16)) cl_made++;
    return 1;
}
void uya_clearref_exit(RcCtx *c) { (void)c; }

/* ---- func_001F856C: the fogged fans (asm, VU0; writeGSHeader's packets) ----
 * Among what it draws, through writeGSHeader: a fan in perspective, its vertices projected with VU0
 * (the routine runs). The packet: a REGLIST tag (PRIM, then ST RGBAQ XYZF2 of the first vertex), a
 * REGLIST tag of the others (ST RGBAQ XYZF2 each). At its return the packets it wrote (from the chain
 * pointer as it found it) are looked at: each of that layout, checked word for word, becomes a record
 * (the primitive, the vertices); any other stays as it is. */
static u32 fn_t0[3];
static unsigned long long fn_made, fn_other;
static const u32 fn_cws[3] = { 0x161000u, 0x161240u, 0x161280u };
int uya_fogfan_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)c; (void)g; (void)ng; (void)a; (void)na;
    for (int k = 0; k < 3; k++) fn_t0[k] = M32(fn_cws[k]);
    return 0;
}
void uya_fogfan_exit(RcCtx *c)
{
    (void)c;
    if (n2d_mode() != 2) return;
    u32 cw = 0;
    static unsigned long long calls, nomove;
    calls++;
    for (int k = 0; k < 3; k++) { u32 e = M32(fn_cws[k]); if (e > fn_t0[k] && e - fn_t0[k] < 0x10000u && !(fn_t0[k] & 15) && chain_word_ok(fn_cws[k])) { if (cw) return; cw = fn_cws[k]; } }
    if (!cw) { if (++nomove == 1000) uya_log("native 2D fogged fans: %llu returns, %llu with no packet", calls, nomove); return; }
    u32 T = fn_t0[cw == 0x161000u ? 0 : cw == 0x161240u ? 1 : 2], E = M32(cw);
    while (T < E) {
        u32 tag = M32(T), qwc = tag & 0xFFFF;
        if (tag == 0x20000000u && M32(T + 8) == NATIVE_2D_MARK && M32(T + 4) > T && M32(T + 4) <= E) { T = M32(T + 4); continue; }   /* (a packet already a record) */
        if ((tag & 0xFFFF0000u) != 0x10000000u || M32(T + 4) || M32(T + 8) || M32(T + 12) != (0x50000000u | qwc) || T + 16 * (qwc + 1) > E) {
            static int t; if (t < 3) { t++; uya_log("native 2D fogged fans: tag %08X %08X %08X %08X at %08X (from %08X to %08X): stopped", tag, M32(T + 4), M32(T + 8), M32(T + 12), T, fn_t0[cw == 0x161000u ? 0 : cw == 0x161240u ? 1 : 2], E); }
            return;
        }
        u32 D = T + 16, nx = T + 16 * (qwc + 1);
        { static int t; if (t < 3 && M64(D) == 0x4400000000000001ull) { t++; uya_log("native 2D fogged fans: a packet of %u qwords: %016llX %016llX, PRIM %016llX, tag %016llX %016llX", qwc, (unsigned long long)M64(D), (unsigned long long)M64(D + 8), (unsigned long long)M64(D + 0x10), (unsigned long long)M64(D + 0x30), (unsigned long long)M64(D + 0x38)); } }
        u64 t2 = M64(D + 0x30);
        u32 nv = (u32)(t2 & 0x7FFF) + 1;
        if (M64(D) == 0x4400000000000001ull && M64(D + 8) == 0x4120 && (t2 & ~0x7FFFull) == 0x3400000000008000ull && M64(D + 0x38) == 0x412 &&
            qwc == 3 + (3 * nv >> 1) && nv >= 3 && nv <= 256) {
            nd_material m; u32 pa = 0x00; u64 pv = M64(D + 0x10);
            static u32 wv[256 * 8];
            if (nd_material_from_regs(&m, &pa, &pv, 1)) {
                for (u32 k = 0; k < nv; k++) {
                    u32 q = k ? D + 0x40 + 0x18 * (k - 1) : D + 0x18;
                    u64 st = M64(q), rgbaq = M64(q + 8), xyzf = M64(q + 16);
                    u32 *w = wv + k * 8;
                    w[0] = (u32)(xyzf & 0xFFFF) | (u32)((xyzf >> 16) & 0xFFFF) << 16; w[1] = (u32)(xyzf >> 32) & 0xFFFFFF; w[2] = (u32)(xyzf >> 56); w[3] = (u32)rgbaq;
                    w[4] = (u32)st; w[5] = (u32)(st >> 32); w[6] = (u32)(rgbaq >> 32); w[7] = 0;
                }
                if (ring_at + 1024 > R_QW) ring_at = 0;
                rec_begin();
                rec_op19(&m, wv, nv, 4);
                if (rec_end(T, nx)) fn_made++;
                if ((fn_made & 0x3FFF) == 1) uya_log("native 2D fogged fans: %llu as host draws, %llu other packets", fn_made, fn_other);
            } else fn_other++;
        }
        T = nx;
    }
}

/* ---- func_001F55C0: a full-screen fade (VU1_addGSregister RGBAQ, then a REF to the table at 0x13CD90) ----
 * The table (20 qwords, built once): an A+D tag with TEST_1 and PRIM (a sprite), a REGLIST tag of
 * sprites (two XYZ2 a qword); the colour the RGBAQ write before it left (Q 1.0 after the tags). At the
 * routine's return its REF (the chain's last qword) becomes a marker; the record is the test setting
 * and the sprites in the colour the GS holds; the table kept and compared again at the marker. */
static unsigned long long fd_made;
static int fade_parse(u32 tb, u32 qwc, int write)
{
    if (M64(tb) != 0x2000000000000001ull || M64(tb + 8) != 0xEE || M64(tb + 0x18) != 0x47 || M64(tb + 0x28) != 0x00) return 0;
    u64 t2 = M64(tb + 0x30);
    u32 n = (u32)(t2 & 0x7FFF);
    if ((t2 & ~0x7FFFull) != 0x2400000000008000ull || M64(tb + 0x38) != 0x44 || n == 0 || n > 64 || 4 + n != qwc) return 0;
    if (!write) return 1;
    nd_pass ps; u32 ta = 0x47; u64 tv = M64(tb + 0x10);
    if (!nd_pass_from_regs(&ps, &ta, &tv, 1)) return 0;
    nd_material m; u32 pa = 0x00; u64 pv = M64(tb + 0x20);
    if (!nd_material_from_regs(&m, &pa, &pv, 1)) return 0;
    static u32 wv[64 * 2 * 8];
    for (u32 i = 0; i < n; i++) { vtx(wv + (2 * i) * 8, M64(tb + 0x40 + 16 * i), 0, 0); vtx(wv + (2 * i + 1) * 8, M64(tb + 0x48 + 16 * i), 0, 0); }
    rec_pass(&ps);
    rec_op19(&m, wv, 2 * n, 1 | 2 | 4);
    return 1;
}
int uya_fadeset_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { (void)c; (void)g; (void)ng; (void)a; (void)na; return 0; }
void uya_fadeset_exit(RcCtx *c)
{
    (void)c;
    if (n2d_mode() != 2) return;
    static const u32 cws[3] = { 0x161000u, 0x161240u, 0x161280u };
    for (int k = 0; k < 3; k++) {
        if (!chain_word_ok(cws[k])) continue;
        u32 T = M32(cws[k]) - 16;
        if (M32(T) != 0x30000014u || M32(T + 8) || M32(T + 12) != 0x50000014u) continue;
        u32 tb = M32(T + 4);
        if (tb != 0x13CD90u || !fade_parse(tb, 0x14, 0)) continue;
        if (ring_at + 256 > R_QW) ring_at = 0;
        rec_begin();
        if (!fade_parse(tb, 0x14, 1)) { rb_ok = 0; return; }
        rec_snapshot(tb, 0x14);
        if (rec_end(T, T + 16)) fd_made++;
        return;
    }
}

/* ---- func_001F5650: DrawRectOverlay (C in the decomp) ----
 * y0 y1 x0 x1 and the colour: a CNT 5 with DIRECT 5, a REGLIST tag (from the template at D_00160920:
 * PRIM, RGBAQ), PRIM 0x144 and the colour, a REGLIST tag (D_00160930's: four XYZ2), the corners at Z
 * 0xFFFFF0. At the routine's return the packet (the chain's last six qwords), its layout checked,
 * becomes a record: the strip in its colour. */
static unsigned long long ro_made;
int uya_rectovl_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) { (void)c; (void)g; (void)ng; (void)a; (void)na; return 0; }
void uya_rectovl_exit(RcCtx *c)
{
    (void)c;
    if (n2d_mode() != 2) return;
    static const u32 cws[3] = { 0x161000u, 0x161240u, 0x161280u };
    for (int k = 0; k < 3; k++) {
        if (!chain_word_ok(cws[k])) continue;
        u32 T = M32(cws[k]) - 0x60, D = T + 16;
        if (M32(T) != 0x10000005u || M32(T + 4) || M32(T + 8) || M32(T + 12) != 0x50000005u) continue;
        u64 t1 = M64(D), t2 = M64(D + 0x20);
        if ((t1 & 0xFFFF) != 0x8001 || ((t1 >> 58) & 3) != 1 || ((t1 >> 60) & 15) != 2 || M64(D + 8) != 0x10) continue;
        if ((t2 & 0xFFFF) != 0x8004 || ((t2 >> 58) & 3) != 1 || ((t2 >> 60) & 15) != 1 || M64(D + 0x28) != 0x5) continue;
        if ((t1 & 0x03FF7FFF0000ull) || (t2 & 0x03FF7FFF0000ull)) continue;   /* (no PRE/PRIM field, nothing else in the tags) */
        u64 pr = M64(D + 0x10), col = M64(D + 0x18);
        nd_material m; u32 pa = 0x00;
        if (!nd_material_from_regs(&m, &pa, &pr, 1)) continue;
        u32 wv[4 * 8];
        for (int v = 0; v < 4; v++) vtx(wv + v * 8, M64(D + 0x30 + 8 * (u32)v), col, 0);
        if (ring_at + 64 > R_QW) ring_at = 0;
        rec_begin();
        rec_op19(&m, wv, 4, 1 | 4);
        if (rec_end(T, T + 0x60)) ro_made++;
        return;
    }
}

/* ---- the texture uploads (item 4, first step): BuildTieTextureDma func_002383D8, BuildShrubTextureDma
 * func_0022B648, BuildTfragTextureDma func_L00_002963D8, func_00212258 (the mobys'), func_L00_0024E798 (the
 * upload list's; it leaves by tail calls, without its exit hook: its callers func_001F4748 and the
 * sky's func_0022C870 are hooked too, a return finding its entry by the return address) ----
 * Each upload they write: a CNT with DIRECT, an A+D tag with the transfer's registers (BITBLTBUF, TRXREG,
 * TRXDIR; TRXPOS when given), an IMAGE tag, then a REF to the image in the game's memory. At the routine's
 * return each one, its layout checked, becomes a marker and a record: the register writes (op 1, with the
 * GIF tags' Q reset, op 3) and the data's place (op 22); where the chain meets the marker the data is read
 * from the game's memory (as the DMA would read it then) and goes with the registers to the GS side as a
 * host upload (op 21). No upload's data goes through the chain or the GIF. UYA_NUPL=0: the game's packets. */
static int upl_mode = -1;
static int nupl_on(void) { if (upl_mode < 0) { const char *e = getenv("UYA_NUPL"); upl_mode = e ? atoi(e) != 0 : n2d_mode() != 0; uya_log("native texture uploads: %s (UYA_NUPL)", upl_mode ? "host records" : "the game's packets"); } return upl_mode; }
static const u32 up_cws[3] = { 0x161000u, 0x161240u, 0x161280u };
static u32 up_t0[16][3], up_buf[16], up_ra[16]; static int up_depth;
static unsigned long long up_enters, up_exits;
static unsigned long long up_made, up_other;
static void rec_raw(const u32 *op)
{
    if (!rb_ok) return;
    if (ring_at + 1 > R_QW) { rb_ok = 0; return; }
    memcpy(ring + (size_t)ring_at * 4, op, 16);
    ring_at++;
}
int uya_texdma_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)c; (void)g; (void)ng; (void)a; (void)na;
    /* (by the return address: a routine that leaves without its exit hook must not hold the others) */
    if (up_depth == 16) { memmove(up_t0, up_t0 + 1, sizeof up_t0[0] * 15); memmove(up_buf, up_buf + 1, 4 * 15); memmove(up_ra, up_ra + 1, 4 * 15); up_depth = 15; }
    for (int k = 0; k < 3; k++) up_t0[up_depth][k] = M32(up_cws[k]);
    up_buf[up_depth] = (u32)c->r[4].d[0]; up_ra[up_depth] = (u32)c->r[31].d[0];
    up_depth++; up_enters++;
    return 0;
}
/* the upload at T (a CNT with DIRECT): 1 and its end if it has the layout, its record made when write */
static int upl_one(u32 T, u32 E, u32 *next, int write)
{
    u32 tag = M32(T), qwc = tag & 0xFFFF;
    if ((tag & 0xFFFF0000u) != 0x10000000u || M32(T + 4) || M32(T + 8) || M32(T + 12) != (0x50000000u | qwc) || qwc < 3 || qwc > 6) return 0;
    u32 D = T + 16, n = qwc - 2;
    if (M64(D) != (0x1000000000000000ull | n) || M64(D + 8) != 0xE) return 0;
    u32 ra[4]; u64 rv[4]; int dir = 0;
    for (u32 k = 0; k < n; k++) {
        rv[k] = M64(D + 16 + 16 * k); u64 ad = M64(D + 24 + 16 * k);
        if (ad < 0x50 || ad > 0x53) return 0;
        ra[k] = (u32)ad;
        if (ad == 0x53) { if (k != n - 1 || rv[k] != 0) return 0; dir = 1; }
    }
    if (!dir) return 0;
    u64 it = M64(D + 16 + 16 * n), ih = M64(D + 24 + 16 * n);
    u32 nl = (u32)(it & 0x7FFF);
    if ((it & ~0x7FFFull) != 0x0800000000008000ull || ih || !nl) return 0;
    u32 R = T + 16 * (qwc + 1);
    if (R + 16 > E || M32(R) != (0x30000000u | nl) || M32(R + 8) || M32(R + 12) != (0x50000000u | nl)) return 0;
    u32 src = M32(R + 4);
    if (src < 0x100000u || src + nl * 16 > 0x2000000u || (src & 15)) return 0;
    *next = R + 16;
    if (!write) return 1;
    if (ring_at + 16 > R_QW) ring_at = 0;
    rec_begin();
    u32 op[4] = { 3, 0, 0, 0 };
    rec_raw(op);                                   /* (the A+D tag: Q 1.0) */
    for (u32 k = 0; k < n; k++) { op[0] = 1; op[1] = ra[k]; op[2] = (u32)rv[k]; op[3] = (u32)(rv[k] >> 32); rec_raw(op); }
    op[0] = 3; op[1] = 0; op[2] = 0; op[3] = 0; rec_raw(op);   /* (the IMAGE tag) */
    op[0] = 22; op[1] = nl; op[2] = src; op[3] = 0; rec_raw(op);
    rb_upl = 1;
    return rec_end(T, R + 16);
}
/* ---- the GS state tables sent by REF (step A of the host draw list) ----
 * A REF with DIRECT whose table is A+D GIF tags only (the shadows' per-volume setup, the texture flushes,
 * the sky's setup blocks, the draw buffers' setups, the frame's tables): its place a marker, the record
 * the table's register writes as host settings (ops 17 and 18 where they are a material or pass
 * settings, op 1 otherwise; each tag's Q reset, op 3), the table kept with it and compared again where
 * the chain meets the marker (changed: the packet as GIF data). UYA_NSTAB=0: the game's packets. */
static int stab_mode = -1;
static int nstab_on(void) { if (stab_mode < 0) { const char *e = getenv("UYA_NSTAB"); stab_mode = e ? atoi(e) != 0 : nupl_on(); uya_log("native GS state tables: %s (UYA_NSTAB)", stab_mode ? "host records" : "the game's packets"); } return stab_mode; }
static unsigned long long st_made, st_other;
/* the table at tb (qwc qwords): 1 if it is A+D GIF tags only; its record's ops made when write */
static int stab_parse(u32 tb, u32 qwc, int write)
{
    u32 q = 0;
    while (q < qwc) {
        u64 lo = M64(tb + 16 * q), hi = M64(tb + 16 * q + 8);
        u32 nl = (u32)(lo & 0x7FFF), nreg = (u32)(lo >> 60) & 15, flg = (u32)(lo >> 58) & 3;
        if (!nreg) nreg = 16;
        if (flg == 1 && !((lo >> 46) & 1) && !(lo & 0x03FF7FFF0000ull)) {   /* REGLIST: the values two a qword */
            for (u32 k = 0; k < nreg; k++) { u32 r_ = (u32)(hi >> (4 * k)) & 15; if (r_ < 6 || r_ > 9) return 0; }
            u32 n = nl * nreg, nq = (n + 1) / 2;
            if (!n || q + 1 + nq > qwc || n > 64) return 0;
            if (write) {
                u32 op[4] = { 3, 0, 0, 0 }; rec_raw(op);
                for (u32 k = 0; k < n; k++) {
                    u32 ad_ = (u32)(hi >> (4 * (k % nreg))) & 15; u64 va_ = M64(tb + 16 * (q + 1) + 8 * k);
                    u32 o = ring_at;
                    if (ring_at + 64 > R_QW) { rb_ok = 0; return 0; }
                    if (nd_pack_regs(&ad_, &va_, 1, ring, &o, R_QW)) { ring_at = o; continue; }
                    op[0] = 1; op[1] = ad_; op[2] = (u32)va_; op[3] = (u32)(va_ >> 32); rec_raw(op);
                }
            }
            q += 1 + nq; continue;
        }
        if (flg != 0 || ((lo >> 46) & 1) || (lo & 0x03FF7FFF0000ull)) return 0;   /* (PACKED, no PRE, nothing else in the tag) */
        /* A+D, or a register whose PACKED form is its 64-bit value (TEX0, CLAMP of either context) */
        for (u32 k = 0; k < nreg; k++) { u32 r_ = (u32)(hi >> (4 * k)) & 15; if (r_ != 0xE && r_ != 6 && r_ != 7 && r_ != 8 && r_ != 9) return 0; }
        u32 n = nl * nreg;
        if (q + 1 + n > qwc || n > 64) return 0;
        if (write) {
            u32 op[4] = { 3, 0, 0, 0 };
            rec_raw(op);
            u32 ad[64]; u64 va[64];
            for (u32 k = 0; k < n; k++) {
                u32 r_ = (u32)(hi >> (4 * (k % nreg))) & 15;
                va[k] = M64(tb + 16 * (q + 1 + k)); ad[k] = r_ == 0xE ? (u32)M64(tb + 16 * (q + 1 + k) + 8) & 0xFF : r_;
            }
            /* runs of writes as settings where they are; the others as register writes, in order */
            u32 k = 0;
            while (k < n) {
                u32 o = ring_at;
                if (ring_at + 64 > R_QW) { rb_ok = 0; return 0; }
                if (nd_pack_regs(ad + k, va + k, 1, ring, &o, R_QW)) { ring_at = o; k++; continue; }
                op[0] = 1; op[1] = ad[k]; op[2] = (u32)va[k]; op[3] = (u32)(va[k] >> 32); rec_raw(op); k++;
            }
        }
        q += 1 + n;
    }
    return q == qwc;
}
/* the VIF word before the DIRECT: a NOP or a flush (FLUSHE, FLUSH, FLUSHA: waits only, nothing here) */
static int vif_wait_only(u32 w) { return w == 0 || w == 0x10000000u || w == 0x11000000u || w == 0x13000000u; }
/* a REF whose data is an IMAGE packet (its tag, then the image): 1 if so, its record (the tag's Q reset,
 * the image as a host upload's data, read at the marker) made when write */
static int upl_ref_image(u32 T, u32 E, int write)
{
    u32 tag = M32(T), qwc = tag & 0xFFFF;
    if ((tag & 0xFFFF0000u) != 0x30000000u || qwc < 2 || !vif_wait_only(M32(T + 8)) || M32(T + 12) != (0x50000000u | qwc) || T + 16 > E) return 0;
    u32 src = M32(T + 4);
    if (src < 0x100000u || src + qwc * 16 > 0x2000000u || (src & 15)) return 0;
    u64 it = M64(src), ih = M64(src + 8);
    if ((it & ~0x7FFFull & ~0x8000ull) != (2ull << 58) || ih || (u32)(it & 0x7FFF) + 1 != qwc) return 0;
    if (!write) return 1;
    if (ring_at + 16 > R_QW) ring_at = 0;
    rec_begin();
    u32 op[4] = { 3, 0, 0, 0 }; rec_raw(op);
    op[0] = 22; op[1] = qwc - 1; op[2] = src + 16; op[3] = 0; rec_raw(op);
    rb_upl = 1;
    return rec_end(T, T + 16);
}
/* the REF (or CNT) at T: 1 if it sends such a table (its record made when write) */
static int stab_one(u32 T, u32 E, int write)
{
    u32 tag = M32(T), qwc = tag & 0xFFFF;
    if (((tag & 0xFFFF0000u) == 0x10000000u) && !M32(T + 4) && vif_wait_only(M32(T + 8)) && M32(T + 12) == (0x50000000u | qwc) && qwc && qwc <= 64 && T + 16 * (qwc + 1) <= E) {
        /* a CNT: the table follows its tag in the chain */
        if (!stab_parse(T + 16, qwc, 0)) return 0;
        if (!write) return 1;
        if (ring_at + 512 > R_QW) ring_at = 0;
        rec_begin();
        if (!stab_parse(T + 16, qwc, 1)) { rb_ok = 0; return 0; }
        return rec_end(T, T + 16 * (qwc + 1));
    }
    if ((tag & 0xFFFF0000u) != 0x30000000u || !qwc || qwc > 64 || !vif_wait_only(M32(T + 8)) || M32(T + 12) != (0x50000000u | qwc) || T + 16 > E) return 0;
    u32 tb = M32(T + 4);
    if (tb < 0x100000u || tb + qwc * 16 > 0x2000000u || (tb & 15)) return 0;
    if (!stab_parse(tb, qwc, 0)) return 0;
    if (!write) return 1;
    if (ring_at + 512 > R_QW) ring_at = 0;
    rec_begin();
    if (!stab_parse(tb, qwc, 1)) { rb_ok = 0; return 0; }
    rec_snapshot(tb, qwc);
    return rec_end(T, T + 16);
}
/* a CNT table a native module copies into its own packets (ShrubProc's heads): converted where it is, at its return */
void uya_stab_at(u32 T)
{
    if (!nstab_on() || (M32(T) >> 28 & 7) != 1) return;
    u32 E = T + 16 * ((M32(T) & 0xFFFF) + 1);
    if (stab_one(T, E, 0)) { if (stab_one(T, E, 1)) st_made++; else st_other++; }
}
static void texdma_scan(RcCtx *c, int d, u32 ra);
static void texdma_take(RcCtx *c, int d, u32 ra);
int uya_hl_on(void);
void uya_hl_take(u32 T, u32 E);
void uya_texdma_exit(RcCtx *c)
{
    u32 ra = (u32)c->r[31].d[0];
    int d = up_depth - 1;
    while (d >= 0 && up_ra[d] != ra) d--;
    if (d < 0) return;
    /* entries below it with the same return address: routines that left by a tail call into this one
     * (SkyDrawShellGouraud into VU1_addGSregister): they return now too */
    int lo = d;
    while (lo > 0 && up_ra[lo - 1] == ra) lo--;
    up_depth = lo; up_exits++;
    for (int e_ = d; e_ >= lo; e_--) texdma_take(c, e_, ra);
}
static void texdma_take(RcCtx *c, int d, u32 ra)
{
    texdma_scan(c, d, ra);
    /* the host draw list (sdk/hostlist.c): the region this writer moved the chain pointer across, its items as they are now */
    if (uya_hl_on()) {
        int took = 0;
        {   /* testing (UYA_HLDBG): the chain words at a return that meets the range */
            int uya_hl_dbg(u32, u32); int hit = 0;
            for (int k = 0; k < 3; k++) { u32 e = M32(up_cws[k]), b = up_t0[d][k]; if (uya_hl_dbg(b < e ? b : e, b < e ? e : b + 16)) hit = 1; }
            if (hit) uya_log("HLDBG return ra %08X (a0 %08X v0 %08X): words %08X->%08X %08X->%08X %08X->%08X", ra, up_buf[d], (u32)c->r[2].d[0], up_t0[d][0], M32(up_cws[0]), up_t0[d][1], M32(up_cws[1]), up_t0[d][2], M32(up_cws[2]));
        }
        void uya_hl_take_chain(u32 T, u32 E);
        for (int k = 0; k < 3; k++) { u32 e = M32(up_cws[k]), b = up_t0[d][k]; if (e > b && e - b < 0x100000u && !(b & 15) && chain_word_ok(up_cws[k])) { uya_hl_take_chain(b, e); took = 1; } }
        /* (the buffer given in a0, its size returned in v0: only a size that is one, a small buffer) */
        if (!took) { u32 b = up_buf[d], sz = (u32)c->r[2].d[0]; if (b >= 0x100000u && !(b & 15) && sz && !(sz & 15) && sz <= 0x10000u && b + sz <= 0x2000000u) uya_hl_take(b, b + sz); }
    }
}
static void texdma_scan(RcCtx *c, int d, u32 ra)
{
    if (!nupl_on() && !nstab_on()) return;
    static int tdlog = -1; if (tdlog < 0) { const char *e_ = getenv("UYA_TDLOG"); tdlog = e_ ? atoi(e_) : 0; }   /* (testing: UYA_TDLOG=n logs n returns' scans) */
    u32 cw = 0, T = 0;
    for (int k = 0; k < 3; k++) { u32 e = M32(up_cws[k]); if (e > up_t0[d][k] && e - up_t0[d][k] < 0x100000u && !(up_t0[d][k] & 15) && chain_word_ok(up_cws[k])) { if (cw) { if (tdlog > 0) { tdlog--; uya_log("TDLOG ra %08X: two chain words moved", ra); } return; } cw = up_cws[k]; T = up_t0[d][k]; } }
    if (tdlog > 0) { tdlog--; uya_log("TDLOG ra %08X: chain word %08X from %08X to %08X (a0 %08X v0 %08X)", ra, cw, T, cw ? M32(cw) : 0, up_buf[d], (u32)c->r[2].d[0]); }
    u32 E = cw ? M32(cw) : 0;
    if (!cw) {   /* (the chain pointer not moved: the routine built into the buffer given in a0, its size returned) */
        u32 b = up_buf[d], sz = (u32)c->r[2].d[0];
        if (b < 0x100000u || (b & 15) || !sz || sz > 0x400000u || b + sz > 0x2000000u) return;
        T = b; E = b + sz;
    }
    while (T < E) {
        u32 tag = M32(T), nx;
        if (tag == 0x20000000u && M32(T + 8) == NATIVE_2D_MARK && M32(T + 4) > T && M32(T + 4) <= E) { T = M32(T + 4); continue; }   /* (a record already) */
        if (nupl_on() && upl_one(T, E, &nx, 0)) { if (upl_one(T, E, &nx, 1)) up_made++; else up_other++; T = nx; continue; }
        if ((tag >> 28 & 7) == 1 && nstab_on() && stab_one(T, E, 0)) { u32 n_ = T + 16 * ((tag & 0xFFFF) + 1); if (stab_one(T, E, 1)) st_made++; else st_other++; T = n_; continue; }
        if ((tag >> 28 & 7) == 1 && M32(T + 4) == 0) {   /* (a CNT of another kind: as it is) */
            {   /* testing: UYA_NUPLLOG=1 logs a few of these that hold an IMAGE tag */
                static int on = -1, left = 8; if (on < 0) on = getenv("UYA_NUPLLOG") != 0;
                u32 q = tag & 0xFFFF, img = 0;
                for (u32 k = 1; k <= q && k < 16; k++) if (((M64(T + 16 * k) >> 58) & 3) == 2) img = k;
                if (on && left > 0 && (img || (M32(T + 16) == 3 && M32(T + 20) == 0x10000000u))) { left--;
                    uya_log("native texture uploads: not taken at %08X (%u qwords, IMAGE tag at %u):", T, q, img);
                    for (u32 k = 0; k <= q + 2 && k < 10; k++) uya_log("   %08X: %08X %08X %08X %08X", T + 16 * k, M32(T + 16 * k), M32(T + 16 * k + 4), M32(T + 16 * k + 8), M32(T + 16 * k + 12)); }
            }
            T += 16 * ((tag & 0xFFFF) + 1); up_other++; continue;
        }
        if ((tag >> 28 & 7) == 3) {
            if (nstab_on() && stab_one(T, E, 0)) { if (stab_one(T, E, 1)) st_made++; else st_other++; T += 16; continue; }
            if (nupl_on() && upl_ref_image(T, E, 0)) { if (upl_ref_image(T, E, 1)) up_made++; else up_other++; T += 16; continue; }
            T += 16; up_other++; continue;   /* (a REF of another kind) */
        }
        if ((tag >> 28 & 7) == 2 && (tag & 0xFFFF) == 0) { T += 16; continue; }   /* (a NEXT: the packets written after it are converted where they are) */
        { static int t; if (t < 3 || tdlog > 0) { t++; uya_log("native texture uploads: tag %08X %08X %08X %08X at %08X: the rest as it is", tag, M32(T + 4), M32(T + 8), M32(T + 12), T); } }
        break;
    }
    static unsigned long long next = 1, snext = 1;
    if (up_made >= next) { next = next < 100000 ? next * 10 : next + 100000; uya_log("native texture uploads: %llu as host records, %llu other packets", up_made, up_other); }
    if (st_made >= snext) { snext = snext < 100000 ? snext * 10 : snext + 100000; uya_log("native GS state tables: %llu as host records, %llu not made", st_made, st_other); }
}

/* the darkening pass with the return scan around it (its items into the host draw list) */
int uya_texdma_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na);
int uya_darkpass_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    uya_texdma_enter(c, g, ng, a, na);
    int r = darkpass_enter_(c, g, ng, a, na);
    if (r) uya_texdma_exit(c);
    return r;
}
void uya_darkpass_exit(RcCtx *c) { darkpass_exit_(c); uya_texdma_exit(c); }
