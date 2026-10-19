/* TfragProc (the game's terrain culling and level of detail, hand-written assembly in the
 * game: decomp text 0x2352C8, 3112 bytes, one copy in every level) as native C.
 *
 * What the game's routine does, in order:
 *   - setup: saves s0..s7, sp, fp, ra in the scratchpad (0x3FA8..); starts the DMA of the
 *     first 32 tfrag headers (64 bytes each, from the table pointer) into the scratchpad
 *     (0x1000 / 0x1800, double buffered); copies the occlusion bitmap (128 bytes) to 0x3B00;
 *     loads the camera (rotation rows, position * 1024, view-projection matrix), the frustum
 *     constants, the guard band scale, the far distance, the three LOD distances (to 0x3F00) and
 *     the four chain tag templates; writes the chain's start: a REF to the tfrag VU program
 *     (the core's MPG packet) and 17 qwords of VIF constants.
 *   - per tfrag: its bounding sphere to camera space; dropped if its occlusion bits are clear,
 *     if it is behind the near plane or beyond the far plane, or outside a side plane. A sphere
 *     crossing the frustum's edge is tested again with its bounding box's 8 corners in clip
 *     space: dropped if all are outside one plane; drawn normally if none is outside the guard
 *     band; else it goes to the clip list (0x3D00, drawn later by the clip program, mode 2).
 *     A drawn tfrag: its index into the list at 0x3000 (texture use) unless its texture flag
 *     says no; its lighting and texture-patch entries into 0x3400 / 0x3780 when it is within
 *     its lighting distance; its draw: the level of detail by its near and far distances
 *     against the three LOD distances (modes 0x14, 0x10, 0xE: LOD0; 0xA, 8: LOD1; 6: LOD2;
 *     always LOD2 when its byte 0x27 is set): REF tags to its packets and colours, from the tag
 *     templates, and an MSCAL of the mode. Written to the scratchpad in 2 KB halves, DMAed to
 *     the chain (the packet pointer) when a half fills.
 *   - the end: the closing template (4 qwords), the lists closed, the texture-patch list DMAed
 *     to its table (0x1E1200 in the executable); then, if there are clip-list tfrags, a second
 *     chain part for them (the clip program's MPG, the constants, LOD0 draws in mode 2, the
 *     closing template); the packet pointer saved.
 *
 * This file does the same, word for word: every store (scratchpad, chain, DMA registers),
 * every VU0 macro operation with its result kept in the VU0 registers (other code may read
 * what the routine leaves), the VU's float arithmetic as the translated game code has it
 * (each product and sum clamped to +-MAX), in the routine's order. The data addresses differ
 * per level: tools/recomp.py passes the level's own (its gp offsets and its address pairs, read
 * from the level's bytes) to the hook it puts at the routine's start.
 *
 * UYA_NTFPROC: 0 the game's routine (translated); 1 check (this file runs first, its results
 * are put aside, the game's routine runs from the same state, and the two are compared at its
 * end: scratchpad, chain, the patch list, the packet pointer, the VU0 registers); 2 this file
 * (default). */
#include "rc.h"
#include "uya_host.h"
#include "native_terrain.h"
#include "native_moby.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_log(const char *fmt, ...);

#define SPR 0x70000000u
#define VW(n, k) (*(u32 *)&VF(n)[k])
static float fc(float x) { return rc_fc(x); }

/* the level's data, from the hook's arguments */
typedef struct {
    s32 g_tbl, g_cnt, g_pkt, g_vf22, g_far, g_dists, g_t4, g_t5, g_t6, g_t7, g_pkt2;   /* gp offsets */
    u32 occl, cam, frust, mpg_qwc, mpg_addr, tmpl, tmpl_end, chunks, clip_qwc, clip_addr, tmpl2, tmpl_end2;
} TfRefs;

/* the DMA channels the routine uses: toSPR (0x1000D400) for the headers, fromSPR (0x1000D000)
 * for the chain and the patch list; the emulated DMA does a transfer at its CHCR write */
static int busy(u32 ch) { return (M32(ch) & 0x100) != 0; }
static void wait_idle(u32 ch) { for (int k = 0; k < 1000000 && busy(ch); k++) ; }

/* a 2 KB half of chain words filled (or the last part): to the chain at fp */
static void flush(u32 *sp, u32 fp)
{
    u32 at = *sp & 0x7FF;
    u32 base = *sp - at;
    wait_idle(0x1000D000u);
    rc_sw(0x1000D080u, base);
    rc_sw(0x1000D010u, fp - at);
    rc_sw(0x1000D020u, at >> 4);
    rc_sw(0x1000D000u, 0x100);
    *sp = (*sp - at) ^ 0x800;
}

static void sq_(u32 a, const R128 *v) { rc_sq(v, a); }
static void sb_(u32 a, u32 v) { M8(a) = (u8)v; }

/* transform by vf18..vf21 (ACC = 18 x, + 19 y, + 20 z; out = ACC + 21 * vf0.w) */
static void xform(int d, int s)
{
    float a[4], o[4], x = VF(s)[0], y = VF(s)[1], z = VF(s)[2];
    for (int k = 0; k < 4; k++) a[k] = fc(VF(18)[k] * x);
    for (int k = 0; k < 4; k++) a[k] = fc(a[k] + fc(VF(19)[k] * y));
    for (int k = 0; k < 4; k++) a[k] = fc(a[k] + fc(VF(20)[k] * z));
    for (int k = 0; k < 4; k++) VACC[k] = a[k];
    for (int k = 0; k < 4; k++) o[k] = fc(a[k] + fc(VF(21)[k] * VF(0)[3]));
    for (int k = 0; k < 4; k++) VF(d)[k] = o[k];
}
static void clipw(int s)
{
    float w = __builtin_fabsf(VF(s)[3]), x = VF(s)[0], y = VF(s)[1], z = VF(s)[2];
    u32 fl = (x > w) | ((x < -w) << 1) | ((y > w) << 2) | ((y < -w) << 3) | ((z > w) << 4) | ((z < -w) << 5);
    rc_vu0.clip = ((rc_vu0.clip << 6) | fl) & 0xFFFFFF;
}
static void mulxy22(int d) { float x = fc(VF(d)[0] * VF(22)[0]), y = fc(VF(d)[1] * VF(22)[1]); VF(d)[0] = x; VF(d)[1] = y; }
static u32 and4(u32 v) { return v & (v >> 6) & ((v >> 12) & (v >> 18)); }

/* the draw entries (the chain's REF tags and MSCAL, from the templates t4..t7) */
static void emit_lod0(const R128 t[4], u32 s4, u32 t3, u32 mode, u32 *sp, u32 *fp)
{
    u32 a = *sp;
    u32 v0 = M16(s4 + 0x16), v1 = M8(s4 + 0x20), a0 = M16(s4 + 0x1E), a1 = M8(s4 + 0x26), a2 = M8(s4 + 0x2A);
    sq_(a, &t[0]); sb_(a, v1); rc_sw(a + 4, v0 + t3);
    v0 = M16(s4 + 0x1A); v1 = M8(s4 + 0x23);
    sq_(a + 0x10, &t[0]); sb_(a + 0x10, v1); rc_sw(a + 0x14, v0 + t3);
    sq_(a + 0x20, &t[1]); a1 >>= 2; sb_(a + 0x20, a1); rc_sw(a + 0x24, a0 + t3); sb_(a + 0x2E, a1 << 2); sb_(a + 0x2C, a2);
    sq_(a + 0x30, &t[3]); *fp += 0x40; sb_(a + 0x3C, mode); *sp += 0x40;
}
static void emit_lod1(const R128 t[4], u32 s4, u32 t3, u32 mode, u32 *sp, u32 *fp)
{
    u32 a = *sp;
    u32 v0 = M16(s4 + 0x16), v1 = M8(s4 + 0x22), a0 = M16(s4 + 0x1E), a1 = M8(s4 + 0x25), a2 = M8(s4 + 0x2A);
    sq_(a, &t[0]); sb_(a, v1); rc_sw(a + 4, v0 + t3);
    sq_(a + 0x10, &t[2]); a1 >>= 2; sb_(a + 0x10, a1); rc_sw(a + 0x14, a0 + t3); sb_(a + 0x1E, a1 << 2); sb_(a + 0x1C, a2);
    sq_(a + 0x20, &t[3]); *fp += 0x30; sb_(a + 0x2C, mode); *sp += 0x30;
}
static void emit_lod2(const R128 t[4], u32 s4, u32 t3, u32 *sp, u32 *fp)
{
    u32 a = *sp;
    u32 v0 = M16(s4 + 0x14), v1 = M8(s4 + 0x21), a0 = M16(s4 + 0x1E), a1 = M8(s4 + 0x24), a2 = M8(s4 + 0x2A);
    sq_(a, &t[0]); sb_(a, v1); rc_sw(a + 4, v0 + t3);
    sq_(a + 0x10, &t[1]); a1 >>= 2; sb_(a + 0x10, a1); rc_sw(a + 0x14, a0 + t3); sb_(a + 0x1E, a1 << 2); sb_(a + 0x1C, a2);
    sq_(a + 0x20, &t[3]); *fp += 0x30; sb_(a + 0x2C, 6); *sp += 0x30;
}
/* a REF to an MPG packet (qwc at q, data after it) and the 17 constant qwords */
static void emit_start(u32 qwc_at, u32 data, u32 tmpl, u32 *sp, u32 *fp)
{
    R128 z = {{0, 0}};
    sq_(*sp, &z);
    u32 v0 = M32(qwc_at);
    rc_sw(*sp + 4, data); rc_sw(*sp, 0x30000000u); *fp += 0x10; M16(*sp) = (u16)v0; *sp += 0x10;
    for (int k = 0; k < 17; k++) { R128 v; rc_lq(&v, tmpl + 16 * k); *fp += 0x10; sq_(*sp, &v); *sp += 0x10; }
}
static void emit_end(u32 tmpl_end, u32 *sp, u32 *fp)
{
    R128 v[4];
    for (int k = 0; k < 4; k++) rc_lq(&v[k], tmpl_end + 16 * k);
    for (int k = 0; k < 4; k++) sq_(*sp + 16 * k, &v[k]);
    *sp += 0x40; *fp += 0x40;
}

/* the tfrag index list (0x3000.., for the texture use) */
static void index_entry(u32 s4, u32 *s0, u32 s3)
{
    u32 v1 = M8(s4 + 0x35), a0 = M16(s4 + 0x36);
    if (v1 == 0 && a0 == 0xFFFF) return;
    if (*s0 == SPR + 0x33FE) return;
    M16(*s0) = (u16)((s3 - M32(SPR + 0x3F10)) >> 6);
    *s0 += 2;
}
/* the lighting (0x3400..) and texture-patch (0x3780..) entries, within the lighting distance */
static void light_entry(u32 s4, u32 t3, u32 t2, s32 Y, u32 *s2)
{
    if ((s32)(t2 - (u32)Y) < 0) return;
    if (*s2 == SPR + 0x3778) return;
    rc_sw(*s2, M16(s4 + 0x2E) + t3); rc_sw(*s2 + 4, M16(s4 + 0x2C));
    rc_sw(*s2 + 0x380, M16(s4 + 0x1C) + t3); rc_sw(*s2 + 0x384, M8(s4 + 0x28));
    *s2 += 8;
}

static unsigned long long st_seen, st_occl, st_out, st_cross, st_clip, st_mode[32];
/* direct: the draws go to native_terrain.c, a marker in the chain where a run of them starts */
static int direct;
/* DrawTfrag's transform packets as a record (native_terrain.h; UYA_NTFXFORM: 0 they stay in the chain,
 * 1 the default) */
#define TX_N 64
static struct { u32 at, q0[4]; } tx[TX_N];
static u32 tx_next;
static int tx_mode = -1;
static unsigned long long tx_made, tx_done, tx_back, tx_unknown;
static int xform_shape(u32 mt)
{
    return M32(mt) == 0x10000004u && !M32(mt + 4) && M32(mt + 8) == 0x01000404u && M32(mt + 12) == 0x6C040005u &&
           M32(mt + 0x50) == 0x10000004u && !M32(mt + 0x54) && M32(mt + 0x58) == 0x01000404u && M32(mt + 0x5C) == 0x6C04014Du;
}
static void xform_record(u32 mt)
{
    if (tx_mode < 0) { const char *e = getenv("UYA_NTFXFORM"); tx_mode = e ? atoi(e) : 1; uya_log("native TfragProc: DrawTfrag's transform packets %s (UYA_NTFXFORM)", tx_mode ? "as records" : "in the chain"); }
    if (!tx_mode || !xform_shape(mt)) return;
    u32 i = tx_next++ % TX_N;
    tx[i].at = mt;
    for (int k = 0; k < 4; k++) tx[i].q0[k] = M32(mt + 4 * (u32)k);
    rc_sw(mt, 0x20000000u); rc_sw(mt + 4, mt + 0xA0); rc_sw(mt + 8, NATIVE_TXFORM_MARK); rc_sw(mt + 12, i);
    tx_made++;
}
int native_tfrag_xform_tag(const u32 *q, u32 t)
{
    u32 i = q[3] % TX_N;
    if (tx[i].at != t) { if (tx_unknown++ < 3) uya_log("native TfragProc: transform marker %u at %08X not known: skipped", q[3], t); return 0; }
    for (int k = 0; k < 4; k++) rc_sw(t + 4 * (u32)k, tx[i].q0[k]);
    tx[i].at = 0;
    if (!xform_shape(t)) { tx_back++; return 2; }
    for (u32 p = t; p != t + 0xA0; p += 0x50) {
        uya_vif1_op_reg(M32(p + 8));
        uya_vif1_op_unpack(M32(p + 12), (const u32 *)(uintptr_t)(p + 16), 16);
    }
    if (++tx_done % 1000 == 0) uya_log("native TfragProc: %llu transform records done, %llu back to the chain, %llu markers not known; made %llu", tx_done, tx_back, tx_unknown, tx_made);
    return 1;
}

static int seg_open, reset_pending;
static u32 kid = ~0u;
static void mark(u32 *sp, u32 *fp)
{
    u32 id = native_terrain_seg_begin(kid, reset_pending);
    reset_pending = 0;
    u32 w[4] = { 0x10000000u, 0, NATIVE_TERRAIN_MARK, id & 0xFFFF };
    memcpy((void *)(uintptr_t)*sp, w, 16);
    *sp += 0x10; *fp += 0x10;
    seg_open = 1;
}
static void run(RcCtx *c, const TfRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    u64 r1hi = c->r[1].d[1], r2hi = c->r[2].d[1];
    /* the registers it saves */
    rc_sd(SPR + 0x3FF8, c->r[31].d[0]); rc_sd(SPR + 0x3FF0, c->r[30].d[0]); rc_sd(SPR + 0x3FE8, c->r[29].d[0]);
    for (int k = 23; k >= 16; k--) rc_sd(SPR + 0x3FA8 + 8 * (u32)(k - 16), c->r[k].d[0]);
    if (R->g_t5 == R->g_t4 + 16 && R->g_t6 == R->g_t4 + 32 && R->g_t7 == R->g_t4 + 48)
        native_terrain_where(gp + (u32)R->g_tbl, gp + (u32)R->g_cnt, gp + (u32)R->g_t4);
    u32 t8 = M32(gp + (u32)R->g_tbl), t9 = M32(gp + (u32)R->g_cnt);
    rc_sw(SPR + 0x3F10, t8);
    u32 s3 = t8 - 0x40;
    rc_sw(0x1000D480u, 0x1000); rc_sw(0x1000D420u, 0x80); rc_sw(0x1000D410u, t8); t8 += 0x800; rc_sw(0x1000D400u, 0x100);
    u32 s5 = 0x800;
    u32 fp = M32(gp + (u32)R->g_pkt);
    u32 sp = SPR;
    for (u32 k = 0; k < 128; k += 32) { R128 a, b; rc_lq(&a, R->occl + k); rc_lq(&b, R->occl + k + 16); rc_sq(&a, SPR + 0x3B00 + k); rc_sq(&b, SPR + 0x3B10 + k); }
    /* the camera */
    { u32 w[4] = { 0x44800000u, 0, (u32)r2hi, (u32)(r2hi >> 32) }; memcpy(VF(1), w, 16); }
    memcpy(VF(28), (void *)(uintptr_t)(R->cam + 0x140), 16);
    memcpy(VF(29), (void *)(uintptr_t)R->cam, 16); memcpy(VF(30), (void *)(uintptr_t)(R->cam + 0x10), 16); memcpy(VF(31), (void *)(uintptr_t)(R->cam + 0x20), 16);
    { float x = VF(1)[0]; for (int k = 0; k < 4; k++) VF(28)[k] = fc(VF(28)[k] * x); }
    for (int k = 0; k < 4; k++) memcpy(VF(12 + k), (void *)(uintptr_t)(R->cam + 0x100 + 16 * (u32)k), 16);
    for (int k = 0; k < 3; k++) VF(1)[k] = fc(VF(0)[k] - VF(28)[k]);
    for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++) VF(18 + r)[k] = fc(VF(12 + r)[k] * VF(0)[3]);
    {
        float a[4];
        for (int k = 0; k < 4; k++) a[k] = fc(VF(12)[k] * VF(1)[0]);
        for (int k = 0; k < 4; k++) a[k] = fc(a[k] + fc(VF(13)[k] * VF(1)[1]));
        for (int k = 0; k < 4; k++) a[k] = fc(a[k] + fc(VF(14)[k] * VF(1)[2]));
        for (int k = 0; k < 4; k++) VACC[k] = a[k];
        for (int k = 0; k < 4; k++) VF(21)[k] = fc(a[k] + fc(VF(15)[k] * VF(0)[3]));
    }
    memcpy(VF(23), (void *)(uintptr_t)(R->frust + 0xA0), 16); memcpy(VF(24), (void *)(uintptr_t)(R->frust + 0xB0), 16);
    memcpy(VF(25), (void *)(uintptr_t)(R->frust + 0x1D0), 16); memcpy(VF(26), (void *)(uintptr_t)(R->frust + 0x1E0), 16);
    memcpy(VF(27), (void *)(uintptr_t)(R->frust + 0x1F0), 16);
    memcpy(VF(22), (void *)(uintptr_t)(gp + (u32)R->g_vf22), 16);
    { u32 far = M32(gp + (u32)R->g_far); s64 se = (s32)far; u32 w[4] = { (u32)se, (u32)((u64)se >> 32), (u32)r1hi, (u32)(r1hi >> 32) }; memcpy(VF(1), w, 16); }
    VF(23)[1] = fc(VF(0)[1] + VF(1)[0]);
    { R128 d; rc_lq(&d, gp + (u32)R->g_dists); rc_sq(&d, SPR + 0x3F00); }
    R128 T[4];
    rc_lq(&T[0], gp + (u32)R->g_t4); rc_lq(&T[1], gp + (u32)R->g_t5); rc_lq(&T[2], gp + (u32)R->g_t6); rc_lq(&T[3], gp + (u32)R->g_t7);
    /* direct, with every tfrag drawable: the chain gets no template, no VU program, no draws (only
     * the markers); the constants are taken from their sources (the template, and DrawTfrag's
     * transform in the two packets it put in the chain just before) */
    int lazy = 0;
    kid = ~0u;
    if (direct) {
        u32 mt = fp - 0xA0;
        if (M32(mt) == 0x10000004u && M32(mt + 8) == 0x01000404u && M32(mt + 12) == 0x6C040005u &&
            M32(fp - 0x50) == 0x10000004u && M32(fp - 0x44) == 0x6C04014Du &&
            M32(R->tmpl) == 0x10000010u && M32(R->tmpl + 8) == 0x01000404u && M32(R->tmpl + 12) == 0x6C0F0290u) {
            NativeTerrainConsts k;
            memcpy(k.m, (void *)(uintptr_t)(mt + 0x10), 64);
            memcpy(k.c, (void *)(uintptr_t)(R->tmpl + 0x10), 15 * 16);
            kid = native_terrain_consts(&k);
            lazy = native_terrain_ready(M32(SPR + 0x3F10));
            if (lazy) xform_record(mt);
        } else { static int told; if (!told++) uya_log("native TfragProc: the chain's transform or template is not as expected: the draws go into the chain"); }
    }
    lazy = lazy && direct;
    { static unsigned long long nl, nr; nr++; nl += lazy; if (nr % 300 == 0) uya_log("native TfragProc: %llu runs, %llu with every draw direct (no template, no VU program in the chain)", nr, nl); }
    if (!lazy) emit_start(R->mpg_qwc, R->mpg_addr, R->tmpl, &sp, &fp);
    reset_pending = lazy;
    seg_open = 0;
    u32 s0 = SPR + 0x3000, s1 = SPR + 0x3D00, s2 = SPR + 0x3400;

    /* the tfrags, 32 at a time */
    while (t9 != 0) {
        s5 ^= 0x800;
        wait_idle(0x1000D400u);
        u32 s6 = SPR + 0x1000 + s5;
        u32 n = t9 < 0x20 ? t9 : 0x20;
        t9 -= n;
        u32 s7 = s6 + (n << 6);
        if (t9 != 0) {
            rc_sw(0x1000D410u, t8); rc_sw(0x1000D480u, s6 ^ 0x800); rc_sw(0x1000D420u, 0x80); rc_sw(0x1000D400u, 0x100);
            t8 += 0x800;
        }
        for (;;) {
            memcpy(VF(1), (void *)(uintptr_t)RC_QA(s6), 16);
            if (s6 == s7) break;
            u32 s4 = s6;
            st_seen++;
            for (int k = 0; k < 3; k++) VF(2)[k] = fc(VF(1)[k] - VF(28)[k]);
            s6 += 0x40;
            VF(3)[0] = fc(VF(0)[0] + VF(1)[3]);
            s3 += 0x40;
            VF(3)[1] = fc(VF(0)[1] - VF(1)[3]);
            u32 t2 = M16(s4 + 0x3E), t3 = M32(s4 + 0x10);
            {
                float a[3], x = VF(2)[0], y = VF(2)[1], z = VF(2)[2];
                for (int k = 0; k < 3; k++) a[k] = fc(VF(29)[k] * x);
                for (int k = 0; k < 3; k++) a[k] = fc(a[k] + fc(VF(30)[k] * y));
                for (int k = 0; k < 3; k++) VACC[k] = a[k];
                for (int k = 0; k < 3; k++) VF(2)[k] = fc(a[k] + fc(VF(31)[k] * z));
            }
            u32 a2 = M8(SPR + 0x3B00 + M8(s4 + 0x3B)), a3 = M8(s4 + 0x3A);
            { float w = VF(1)[3]; VF(8)[0] = fc(VF(26)[0] * w); VF(8)[1] = fc(VF(26)[1] * w); }
            { float z = VF(2)[2]; VF(1)[0] = fc(VF(3)[0] + z); VF(1)[1] = fc(VF(3)[1] + z); }
            a3 &= a2;
            { float z = VF(2)[2]; VF(5)[0] = fc(VF(24)[0] * z); VF(5)[1] = fc(VF(24)[1] * z); }
            if (a3 == 0) { st_occl++; continue; }                     /* occluded */
            VF(2)[0] = __builtin_fabsf(VF(2)[0]); VF(2)[1] = __builtin_fabsf(VF(2)[1]);
            for (int k = 0; k < 2; k++) VF(3)[k] = fc(VF(23)[k] - VF(1)[k]);
            for (int k = 0; k < 2; k++) VF(6)[k] = fc(VF(2)[k] - VF(8)[k]);
            for (int k = 0; k < 2; k++) VF(8)[k] = fc(VF(2)[k] + VF(8)[k]);
            for (int k = 0; k < 2; k++) VF(4)[k] = fc(VF(1)[k] - VF(27)[k]);
            u32 f3x = VW(3, 0), f3y = VW(3, 1);
            for (int k = 0; k < 2; k++) VF(7)[k] = fc(VF(5)[k] - VF(6)[k]);
            for (int k = 0; k < 2; k++) VF(8)[k] = fc(VF(5)[k] - VF(8)[k]);
            { s32 x = rc_ftoi(VF(1)[0], 0), y = rc_ftoi(VF(1)[1], 0); memcpy(&VF(1)[0], &x, 4); memcpy(&VF(1)[1], &y, 4); }
            if (f3y & 0x80000000u) continue;                           /* beyond the far plane */
            if (!(f3x & 0x80000000u)) continue;                        /* behind the near plane */
            u32 a0x = VW(4, 0) ^ 0x80000000u, a0y = VW(4, 1);
            if (VW(7, 1) & 0x80000000u) continue;                      /* outside a side plane */
            if (VW(7, 0) & 0x80000000u) continue;
            int crossing = ((VW(8, 1) | a0y) & 0x80000000u) || ((VW(8, 0) | a0x) & 0x80000000u);
            s32 X = (s32)VW(1, 0), Y = (s32)VW(1, 1);
            if (crossing) {
                st_cross++;
                /* the bounding box's corners (0x38: 8 x 4 halfwords) */
                u32 at = M16(s4 + 0x38) + t3;
                u32 v0 = SPR + 0x3BE0;
                for (int it = 0; it < 4; it++) {
                    for (int h = 0; h < 2; h++) {
                        u64 q = M64(at + 8 * (u32)h);
                        s32 w4[4];
                        for (int k = 0; k < 4; k++) w4[k] = ((s32)((u32)(u16)(q >> (16 * k)) << 16)) >> 10;
                        int r = 4 + h;
                        memcpy(VF(r), w4, 16);
                    }
                    at += 0x10; v0 += 0x20;
                    for (int h = 0; h < 2; h++) { int r = 4 + h; for (int k = 0; k < 3; k++) VF(r)[k] = rc_itof(*(s32 *)&VF(r)[k], 0); }
                    memcpy((void *)(uintptr_t)v0, VF(4), 16); memcpy((void *)(uintptr_t)(v0 + 16), VF(5), 16);
                }
                for (int k = 0; k < 4; k++) memcpy(VF(5 + k), (void *)(uintptr_t)(SPR + 0x3C00 + 16 * (u32)k), 16);
                xform(5, 5); xform(6, 6); clipw(5); xform(7, 7); clipw(6); xform(8, 8); clipw(7);
                mulxy22(5); mulxy22(6); clipw(8); mulxy22(7); mulxy22(8);
                memcpy(VF(10), (void *)(uintptr_t)(SPR + 0x3C40), 16); memcpy(VF(11), (void *)(uintptr_t)(SPR + 0x3C50), 16);
                u32 raw0 = rc_vu0.clip;
                clipw(5); clipw(6); clipw(7); clipw(8);
                xform(5, 10);
                u32 sc0 = rc_vu0.clip;
                memcpy(VF(7), (void *)(uintptr_t)(SPR + 0x3C60), 16); memcpy(VF(8), (void *)(uintptr_t)(SPR + 0x3C70), 16);
                xform(6, 11); clipw(5); xform(7, 7); clipw(6); xform(8, 8); clipw(7);
                mulxy22(5); mulxy22(6); clipw(8); mulxy22(7); mulxy22(8);
                u32 raw1 = rc_vu0.clip;
                clipw(5); clipw(6); clipw(7); clipw(8);
                u32 sc1 = rc_vu0.clip;
                if (and4(raw0) & and4(raw1) & 0x3F) continue;          /* all eight outside one plane */
                if ((sc0 | sc1) != 0) {
                    /* in the guard band but not inside: the clip list */
                    index_entry(s4, &s0, s3);
                    st_clip++;
                    if (s1 == SPR + 0x3EFC) { rc_sw(s1, s3); continue; }
                    rc_sw(s1, s3); s1 += 4;
                    light_entry(s4, t3, t2, Y, &s2);
                    continue;
                }
            }
            index_entry(s4, &s0, s3);
            u32 d0 = M32(SPR + 0x3F00), d1 = M32(SPR + 0x3F04), d2 = M32(SPR + 0x3F08);
            u32 t1 = M8(s4 + 0x27);
            light_entry(s4, t3, t2, Y, &s2);
            int mode;
            if (t1 != 0 || (s32)(d0 - (u32)Y) <= 0) mode = 6;
            else if ((s32)(d0 - (u32)X) <= 0) mode = 8;
            else if ((s32)(d1 - (u32)Y) <= 0) mode = 0xA;
            else if ((s32)(d1 - (u32)X) <= 0) mode = 0xE;
            else if ((s32)(d2 - (u32)Y) <= 0) mode = 0x10;
            else if ((s32)(d2 - (u32)X) <= 0) mode = 0x10;
            else mode = 0x14;
            st_mode[mode & 31]++;
            u32 idx = (s3 - M32(SPR + 0x3F10)) >> 6;
            if (lazy || (direct && native_terrain_can_draw(M32(SPR + 0x3F10), idx, (u32)mode))) {
                if (!seg_open) { mark(&sp, &fp); if ((sp & 0x700) == 0x700) flush(&sp, fp); }
                native_terrain_seg_add(idx, (u32)mode);
                continue;
            }
            seg_open = 0;
            if (mode == 6) emit_lod2(T, s4, t3, &sp, &fp);
            else if (mode == 8 || mode == 0xA) emit_lod1(T, s4, t3, (u32)mode, &sp, &fp);
            else emit_lod0(T, s4, t3, (u32)mode, &sp, &fp);
            if ((sp & 0x700) == 0x700) flush(&sp, fp);
        }
    }
    /* the end of the main part */
    if (!lazy) emit_end(R->tmpl_end, &sp, &fp);
    if (sp & 0x7FF) flush(&sp, fp);
    rc_sw(s2, 0); rc_sw(s2 + 0x380, 0);
    M16(s0) = 0xFFFF;
    wait_idle(0x1000D000u);
    rc_sw(0x1000D010u, R->chunks); rc_sw(0x1000D080u, 0x3780); rc_sw(0x1000D020u, 0x38); rc_sw(0x1000D000u, 0x100);
    wait_idle(0x1000D000u);
    /* the clip list's part */
    if (s1 != SPR + 0x3D00) {
        u32 q = SPR + 0x3D00;
        int lazy2 = lazy && native_terrain_can_draw(M32(SPR + 0x3F10), (M32(q) - M32(SPR + 0x3F10)) >> 6, 2);
        if (!lazy2) emit_start(R->clip_qwc, R->clip_addr, R->tmpl2, &sp, &fp);
        seg_open = 0;
        for (;;) {
            u32 s4 = M32(q);
            if (q == s1) break;
            q += 4;
            u32 t3 = M32(s4 + 0x10);
            u32 idx = (s4 - M32(SPR + 0x3F10)) >> 6;
            if (lazy2 || (direct && native_terrain_can_draw(M32(SPR + 0x3F10), idx, 2))) {
                if (!seg_open) { mark(&sp, &fp); if ((sp & 0x700) == 0x700) flush(&sp, fp); }
                native_terrain_seg_add(idx, 2);
                continue;
            }
            seg_open = 0;
            emit_lod0(T, s4, t3, 2, &sp, &fp);
            if ((sp & 0x700) == 0x700) flush(&sp, fp);
        }
        if (!lazy2) emit_end(R->tmpl_end2, &sp, &fp);
        if (sp & 0x7FF) flush(&sp, fp);
        wait_idle(0x1000D000u);
    }
    rc_sw(gp + (u32)R->g_pkt2, fp);
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;
static TfRefs refs_of(const s32 *g, const u32 *a)
{
    TfRefs R;
    R.g_tbl = g[0]; R.g_cnt = g[1]; R.g_pkt = g[2]; R.g_vf22 = g[3]; R.g_far = g[4]; R.g_dists = g[5];
    R.g_t4 = g[6]; R.g_t5 = g[7]; R.g_t6 = g[8]; R.g_t7 = g[9]; R.g_pkt2 = g[10];
    R.occl = a[0]; R.cam = a[1]; R.frust = a[2]; R.mpg_qwc = a[3]; R.mpg_addr = a[4]; R.tmpl = a[5]; R.tmpl_end = a[6];
    R.chunks = a[7]; R.clip_qwc = a[8]; R.clip_addr = a[9]; R.tmpl2 = a[10]; R.tmpl_end2 = a[11];
    return R;
}

/* check mode: the state the routine reads and writes, put aside and compared */
#define WIN 0x100000u   /* the chain window compared, from the packet pointer */
static struct { u8 spr[0x4000]; u8 *win; u32 fp0, fp1; u8 chunks[0x380]; u32 chunk_a; RcVu0 vu0; u32 pkt; int valid; } A, N;
static u32 chk_gp_pkt;
static void grab(typeof(A) *s, u32 fp0, u32 chunk_a, u32 gp)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    if (!s->win) s->win = malloc(WIN);
    u32 n = fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - fp0;
    memcpy(s->win, (void *)(uintptr_t)fp0, n);
    s->fp0 = fp0; s->chunk_a = chunk_a;
    memcpy(s->chunks, (void *)(uintptr_t)chunk_a, 0x380);
    s->vu0 = rc_vu0;
    s->pkt = M32(gp);
    s->valid = 1;
}
static void put_back(const typeof(A) *s, u32 gp)
{
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    u32 n = s->fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - s->fp0;
    memcpy((void *)(uintptr_t)s->fp0, s->win, n);
    memcpy((void *)(uintptr_t)s->chunk_a, s->chunks, 0x380);
    rc_vu0 = s->vu0;
    rc_sw(gp, s->pkt);
}

int uya_tfragproc_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (mode_ < 0) { const char *e = getenv("UYA_NTFPROC"), *n = getenv("UYA_NATIVE"); mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2; uya_log("native TfragProc: %s", mode_ == 0 ? "off (the game's routine)" : mode_ == 1 ? "check against the game's routine" : "on"); }
    if (!mode_) return 0;
    if (ng != 11 || na != 12) { static int told; if (!told++) uya_log("native TfragProc: the level's routine has %d / %d data references (not 11 / 12): the game's routine runs", ng, na); return 0; }
    TfRefs R = refs_of(g, a);
    if (mode_ == 2) { direct = 1; run(c, &R); return 1; }
    /* check: run here, keep the results, put the state back for the game's routine */
    const u32 gp = (u32)c->r[28].d[0];
    u32 fp0 = M32(gp + (u32)R.g_pkt);
    chk_gp_pkt = gp + (u32)R.g_pkt2;
    RcCtx c0 = *c;
    grab(&A, fp0, R.chunks, chk_gp_pkt);
    direct = 0;
    run(c, &R);
    grab(&N, fp0, R.chunks, chk_gp_pkt);
    N.fp1 = M32(chk_gp_pkt);
    put_back(&A, chk_gp_pkt);
    *c = c0;
    return 0;
}

void uya_tfragproc_exit(RcCtx *c)
{
    (void)c;
    if (mode_ != 1 || !N.valid) return;
    N.valid = 0;
    n_runs++;
    static typeof(A) G;
    grab(&G, N.fp0, N.chunk_a, chk_gp_pkt);
    G.fp1 = M32(chk_gp_pkt);
    int bad = 0;
    char why[256] = "";
    if (G.fp1 != N.fp1) { bad = 1; snprintf(why, sizeof why, "packet pointer %08X, native %08X", G.fp1, N.fp1); }
    else if (memcmp(G.win, N.win, G.fp1 - G.fp0 <= WIN ? G.fp1 - G.fp0 : WIN)) {
        u32 k = 0; while (G.win[k] == N.win[k]) k++;
        bad = 1; snprintf(why, sizeof why, "chain differs at +0x%X (of 0x%X)", k, G.fp1 - G.fp0);
    } else if (memcmp(G.spr, N.spr, 0x4000)) {
        u32 k = 0; while (G.spr[k] == N.spr[k]) k++;
        bad = 1; snprintf(why, sizeof why, "scratchpad differs at 0x%04X", k);
    } else if (memcmp(G.chunks, N.chunks, 0x380)) { bad = 1; snprintf(why, sizeof why, "texture patch list differs"); }
    else if (memcmp(G.vu0.vf, N.vu0.vf, sizeof G.vu0.vf) || memcmp(G.vu0.acc, N.vu0.acc, sizeof G.vu0.acc) || G.vu0.clip != N.vu0.clip) {
        int r = 0; while (r < 32 && !memcmp(G.vu0.vf[r], N.vu0.vf[r], 16)) r++;
        bad = 1; snprintf(why, sizeof why, "VU0 registers differ (first vf%d; clip %06X / %06X)", r, G.vu0.clip, N.vu0.clip);
    }
    if (bad) { n_bad++; if (n_bad <= 10) uya_log("native TfragProc check: run %llu differs: %s", n_runs, why); }
    else n_ok++;
    if (n_runs % 500 == 0) uya_log("native TfragProc check: %llu runs, %llu identical, %llu differ; tfrags %llu: occluded %llu, crossing %llu, to the clip list %llu; modes 6:%llu 8:%llu A:%llu E:%llu 10:%llu 14:%llu", n_runs, n_ok, n_bad, st_seen, st_occl, st_cross, st_clip, st_mode[6], st_mode[8], st_mode[10], st_mode[14], st_mode[16], st_mode[20]);
}
