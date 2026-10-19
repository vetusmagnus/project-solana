/* ShrubProc (the game's shrub culling, fading, billboards and chain building, hand-written
 * assembly in the game: decomp text 0x229F00, 5956 bytes, one copy in every level) as native C.
 *
 * What the game's routine does, in order:
 *   - setup: saves s0..s7, sp, fp, ra in the scratchpad (0x3FA8..); keeps its state there: the
 *     class count and the class table (0x3F60 / 0x3F64, table 0x1E82C0 in the executable), the
 *     per-instance distance cap (0x3FA4), the fade colour table (0x3F88), the list pointers: the
 *     billboard list (0x3F6C, at 0x2000), a class's plain list (0x3F68, at 0x2C00), the fading
 *     list (0x3F70, at 0x3700), the clip list (0x3F74, at 0x3D00), the light list (0x3F8C, at
 *     0x3200); the VU buffer addresses (0x3F7C / 0x3F80 / 0x3F84: 2, 0xEE, 0x11); copies the sway
 *     sine table (256 signed bytes) to 0x3B00 and the sway constants (2 qwords) to 0x3C00, the
 *     sway clock to 0x3C20, the sway subroutine's return address to 0x3FA0; clears the instance
 *     slots (0x1000..0x1FFF); loads the camera and the frustum; the six chain tag templates (gp)
 *     to 0x3F00..0x3F50; writes the chain's start: a REF to the shrub VU program (MPG) and 5
 *     qwords of constants, then a placeholder (0x3F90) and the shrub part's 3 qwords.
 *   - per class (0x40 bytes and its packet table, 8 bytes a packet, at +0x40): skipped when its
 *     flags (+0x14) have bit 0. Else a qword is kept free in the chain (a NEXT over its blocks,
 *     written at the end), its sway mode ((flags & 6) >> 1) goes to 0x3F9C, and its instances
 *     (+0x18, count +0x16, 0x20 bytes each; their matrices, 0x40 bytes each, in the array at gp's
 *     word, in step) are DMAed to the scratchpad slots 128 at a time. Per instance: its distance
 *     cap (zero: the game's routine retries it for ever); its centre to camera space; dropped if
 *     beyond its draw distance, behind the near plane or outside a side plane; one crossing the
 *     frustum's edge goes to the clip list (its address). Else: its light list entry (its index)
 *     unless its byte 0x1B is 0 and its half 0x1E is 0xFFFF; for a class with billboards (+0x1C),
 *     its fade from its distance and its byte 0x17: wholly a billboard (the billboard list: index,
 *     alpha 0x80), or partly (the billboard list with its alpha, and drawn as well); the class's
 *     nearest distance (vf17.x); then drawn: plain (the class's plain list, its block's address
 *     and index) or fading (the fading list; its colour from the fade colour table and its alpha
 *     in its block's last qword); its matrix (the sway applied when the class has a sway mode
 *     and it is near enough) through the view-projection matrix in its block's first 4 qwords.
 *     The blocks go through the scratchpad in 2 KB halves, DMAed to the chain (the packet
 *     pointer) when a half fills. At the class's end: its counts (billboards, clip, fading) to
 *     0x3600 / 0x3C80 / 0x3680 and to the class (+0x38, +0x36, +0x34 with the plain ones), its
 *     blocks' span to 0x3000, its nearest distance to 0x3100; then, per packet of its table: a REF
 *     to the packet and, per 5 plain instances, a CNT, REFs to their blocks and colours (the class
 *     colour table + index * 0x60), an MSCAL, the VU buffers swapped.
 *   - the fading part: the same draws for each class's fading instances.
 *   - the billboard part: placeholder 0x3F94; the billboard program's 4 qwords; per class with
 *     billboards, in two passes (wholly opaque ones, alpha 0x80; then the others, after 3 qwords of
 *     state): its texture block (3 qwords of its billboard data) before its first, then per
 *     billboard 2 template qwords and its four corners (the corner table at 0x3800, scaled by its
 *     size and turned toward the camera), each as colour, texture coordinates and screen position.
 *   - the clip part: placeholder 0x3F98, the shrub part's 3 qwords; if the clip list has entries:
 *     the clip VU program's MPG and 8 qwords; per class with clip entries a NEXT over their blocks,
 *     each entry's matrix (sway too) through the clip-space matrix in its block (its list entry
 *     rewritten as its block's address and index), then its packets as above; a closing CNT.
 *   - the end: the light list closed (0xFFFF); each class's NEXT written; the three placeholders
 *     linked (start, billboards, shrubs, clip); the packet pointer saved.
 *
 * This file does the same, word for word: every store (scratchpad, chain, class counts, DMA
 * registers), every VU0 macro operation with its result and its MAC/status flags kept in the VU0
 * registers, the VU's float arithmetic as the translated game code has it (each product and sum
 * clamped to +-MAX), in the routine's order. Where a GPR's upper 64 bits reach a VU register
 * (qmtc2) or a store they are carried too. The data addresses differ per level: tools/recomp.py
 * passes the level's own (its gp offsets and its address pairs, read from the level's bytes).
 *
 * UYA_NSHRUBPROC: 0 the game's routine (translated); 1 check (this file runs first, its results
 * are put aside, the game's routine runs from the same state, and the two are compared at its
 * end: scratchpad, chain, class counts, the packet pointer, the VU0 registers); 2 this file,
 * direct (default): the main and fading parts' draws go to native_shrub.c as records when every
 * packet of the frame can, and so do the clip part's (the clip program's start and draws; its MPG
 * stays in the chain); the billboards' GS packets go there as records too (sent as their VIF1 DIRECT
 * sent them); 3 this file, every
 * draw into the chain (what check mode compares). UYA_SHRUBDEC=1 (testing, with 2 or 3): the draws
 * in the chain, each class packet read from the level data checked against VU memory.
 * UYA_SHRUBDCHK=1 (testing): the draws in the chain and the records beside them, run on a private
 * image, each group and packet compared (the program's quirk colour aside, see native_shrub.c). */
#include "rc.h"
#include "uya_host.h"
#include "native_shrub.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_log(const char *fmt, ...);

#define SPR 0x70000000u
#define VW(n, k) (*(u32 *)&VF(n)[k])
static float fc(float x) { return rc_fc(x); }
/* the square root as the translated game code has it: built with -fno-builtin, its rc_fsqrt is a
 * call of the C library's sqrtf (on 32-bit Linux an x87 fsqrt, rounding to nearest), not the SSE
 * sqrtss this file would inline (rounding as MXCSR says: toward zero) */
float sqrtf(float);
static float (*volatile lib_sqrtf)(float) = sqrtf;
static float vsqrt(float x) { return lib_sqrtf(__builtin_fabsf(x)); }

/* the level's data, from the hook's arguments */
typedef struct {
    s32 g_mtx, g_cnt, g_cap, g_fade, g_clock, g_pkt, g_t[6], g_mtx2, g_b[4], g_mtx3, g_pkt2;   /* gp offsets */
    u32 classes, sine, sway, ret1, cam, frust, mpg_qwc, mpg_addr, consts, head, close, classes2, bstart,
        cam2, frust2, corners, classes3, classes4, bstate, head2, mpg2_qwc, mpg2_addr, consts2, cam3, ret2, classes5;
} ShRefs;

/* ---- the VU0 macro operations, as tools/recomp.py translates them ---- */
enum { ADD, SUB, MUL, MADD, MSUB, MAX, MIN };
enum { X = 8, Y = 4, Z = 2, W = 1, XY = 12, YZ = 6, ZW = 3, YZW = 7, XYZ = 14, XYZW = 15 };
#define ACC (-1)
#define BV (-1)
#define BQ 4
static void fm(int fd, u32 m, int op, int fs, int ft, int bc)
{
    f32 r[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        if (!(m & (8u >> i))) continue;
        f32 a = VF(fs)[i], b = bc == BV ? VF(ft)[i] : bc == BQ ? rc_vu0.q : VF(ft)[bc];
        switch (op) {
        case ADD: r[i] = fc(a + b); break;
        case SUB: r[i] = fc(a - b); break;
        case MUL: r[i] = fc(a * b); break;
        case MADD: r[i] = fc(VACC[i] + fc(a * b)); break;
        case MSUB: r[i] = fc(VACC[i] - fc(a * b)); break;
        case MAX: r[i] = fmaxf(a, b); break;
        default: r[i] = fminf(a, b); break;
        }
    }
    if (fd == 0) { if (op < MAX) rc_vu0_flags(r, m); return; }
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
    if (op < MAX) rc_vu0_flags(d, m);
}
static void mat3(int d, u32 m, int a, int b, int c, int v)
{
    fm(ACC, m, MUL, a, v, 0); fm(ACC, m, MADD, b, v, 1); fm(d, m, MADD, c, v, 2);
}
/* d = rows a, b, c by v's x, y, z (into ACC), + row t by vf(tv)'s component tc */
static void aff(int d, int a, int b, int c, int t, int v, int tv, int tc)
{
    fm(ACC, XYZW, MUL, a, v, 0); fm(ACC, XYZW, MADD, b, v, 1); fm(ACC, XYZW, MADD, c, v, 2); fm(d, XYZW, MADD, t, tv, tc);
}
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
static void qmtc2(int d, u64 lo, u64 hi) { u64 w[2] = { lo, hi }; memcpy(VF(d), w, 16); }
static u64 hi_of(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[1]; }
static u64 lo_of(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[0]; }
static void itof(int d, u32 m, int s, int sh) { f32 t[4]; for (int k = 0; k < 4; k++) t[k] = rc_itof(*(s32 *)&VF(s)[k], sh); for (int k = 0; k < 4; k++) if (m & (8u >> k)) VF(d)[k] = t[k]; }
static void ftoi(int d, u32 m, int s, int sh) { s32 t[4]; for (int k = 0; k < 4; k++) t[k] = rc_ftoi(VF(s)[k], sh); for (int k = 0; k < 4; k++) if (m & (8u >> k)) memcpy(&VF(d)[k], &t[k], 4); }
/* pminw on two upper doublewords (the words compared signed) */
static u64 minhi(u64 a, u64 b)
{
    s32 a0 = (s32)(u32)a, a1 = (s32)(u32)(a >> 32), b0 = (s32)(u32)b, b1 = (s32)(u32)(b >> 32);
    return (u64)(u32)(a0 < b0 ? a0 : b0) | (u64)(u32)(a1 < b1 ? a1 : b1) << 32;
}

/* ---- the chain ---- */
static int busy(u32 ch) { return (M32(ch) & 0x100) != 0; }
static void wait_idle(u32 ch) { for (int k = 0; k < 1000000 && busy(ch); k++) ; }
static void sq_(u32 a, const R128 *v) { rc_sq(v, a); }
static void sb_(u32 a, u32 v) { M8(a) = (u8)v; }
static void sh_(u32 a, u32 v) { M16(a) = (u16)v; }
static const R128 ZQ = {{ 0, 0 }};
/* a 2 KB half of chain words filled: to the chain at fp */
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
/* n qwords copied into the chain (the last three loads land in v0, v1, a0: their upper halves) */
static void copyq(u32 src, int n, u32 *sp, u32 *fp, u64 *v0h, u64 *v1h, u64 *a0h)
{
    R128 v[9];
    for (int k = 0; k < n; k++) rc_lq(&v[k], src + 16 * (u32)k);
    for (int k = 0; k < n; k++) sq_(*sp + 16 * (u32)k, &v[k]);
    *sp += 16 * (u32)n; *fp += 16 * (u32)n;
    if (v0h) { *v0h = v[0].d[1]; *v1h = v[1].d[1]; *a0h = v[2].d[1]; }
}
/* ---- the chain start as a record (direct mode; UYA_SSTARTREC: 0 the chain, 1 check, 2 records) ----
 * The start's 5 chain qwords, in the game's template: CNT 3 (VIF STCYCL 4/4, UNPACK V4-32 3 at VU 0,
 * the 3 constants inline), CNT 0 (MSCALF 0, FLUSHE), after a REF to the program's MPG packet. With
 * that template, one marker takes their place, and at the marker the record does the same, in the
 * same order: the MPG, STCYCL, the unpack (the constants as the chain had them), the start (the
 * program's 0x0000: native_shrub_run, as the VIF ran it). The records' R_START (native_shrub.c)
 * stays as it was. Check mode: the record, then the original qwords through the VIF from the same
 * state; VIF1's state and VU1 data memory compared. */
#include "native_moby.h"
void vu_mpg(int vu, u32 addr, const u32 *w, u32 ninstr);
void uya_dma_vif1_replay(const u32 *q, int tte);
#define SS_N 64
static struct { u32 q[20]; u32 mpg_addr, mpg_qwc; } ss_rec[SS_N];
static u32 ss_next;
static int ss_mode = -1;
static unsigned long long ss_n, ss_bad, ss_mpgbad;
static int sstart_template_ok(u32 c)
{
    return M32(c) == 0x10000003u && M32(c + 8) == 0x01000404u && M32(c + 12) == 0x6C030000u &&
           M32(c + 0x40) == 0x10000000u && M32(c + 0x48) == 0x15000000u && M32(c + 0x4C) == 0x10000000u;
}
static void emit_sstart_rec(u32 qwc_at, u32 data, u32 consts, u32 *sp, u32 *fp)
{
    u32 i = ss_next++ % SS_N;
    for (int k = 0; k < 20; k++) ss_rec[i].q[k] = M32(consts + 4 * (u32)k);
    ss_rec[i].mpg_addr = data; ss_rec[i].mpg_qwc = M16(qwc_at);
    sq_(*sp, &ZQ);
    rc_sw(*sp, 0x10000000u); rc_sw(*sp + 8, NATIVE_SSTART_MARK); rc_sw(*sp + 12, i);
    *sp += 0x10; *fp += 0x10;
}
/* the MPG packet's uploads (FLUSH, FLUSHE, FLUSHA, NOP codes and MPGs only; 0 if it has anything else) */
static int sapply_mpg(u32 addr, u32 qwc)
{
    const u32 *w = (const u32 *)RC_A(addr);
    u32 n = qwc * 4, i = 0;
    while (i < n) {
        u32 code = w[i], cmd = (code >> 24) & 0x7F;
        if (cmd == 0x00 || cmd == 0x10 || cmd == 0x11 || cmd == 0x13) { i++; continue; }
        if (cmd != 0x4A) return 0;
        u32 num = (code >> 16) & 0xFF; if (!num) num = 256;
        if (i + 1 + num * 2 > n) return 0;
        vu_mpg(1, (code & 0xFFFF) * 8, w + i + 1, num);
        i += 1 + num * 2;
    }
    return 1;
}
static void sapply_start(u32 i)
{
    const u32 *c = ss_rec[i].q;
    if (!sapply_mpg(ss_rec[i].mpg_addr, ss_rec[i].mpg_qwc)) { if (ss_mpgbad++ < 3) uya_log("native ShrubProc: the start's MPG packet at %08X has other codes", ss_rec[i].mpg_addr); }
    uya_vif1_op_reg(c[2]);                                  /* STCYCL 4/4 */
    uya_vif1_op_unpack(c[3], c + 4, 3 * 4);                 /* VU 0..2 */
    uya_vif1_op_mscal((c[18] & 0xFFFF) * 8);                /* MSCALF 0 (FLUSHE after it: nothing to wait for here) */
}
int native_shrub_start_tag(const u32 *q)
{
    u32 i = q[3] % SS_N;
    if (ss_mode != 1) { sapply_start(i); ss_n++; return 1; }
    static u8 m0[0x4000], ma[0x4000]; u8 *vu = (u8 *)(uintptr_t)0x1100C000u;
    UyaVifState s0, sa, sb;
    uya_vif1_get_state(&s0); memcpy(m0, vu, sizeof m0);
    sapply_start(i);
    uya_vif1_get_state(&sa); memcpy(ma, vu, sizeof ma);
    uya_vif1_set_state(&s0); memcpy(vu, m0, sizeof m0);
    const u32 *c = ss_rec[i].q;
    u32 mq[4] = { 0x30000000u | ss_rec[i].mpg_qwc, ss_rec[i].mpg_addr, 0, 0 };
    uya_dma_vif1_replay(mq, 1);
    uya_dma_vif1_replay(c, 1); uya_dma_vif1_replay(c + 16, 1);
    uya_vif1_get_state(&sb);
    ss_n++;
    sa.need = sb.need;
    int dv = memcmp(&sa, &sb, sizeof sa) != 0, dm = memcmp(ma, vu, sizeof ma) != 0;
    if ((dv || dm) && ss_bad++ < 10) {
        u32 q0 = 0; while (q0 < 0x400 && !memcmp(ma + q0 * 16, vu + q0 * 16, 16)) q0++;
        uya_log("native ShrubProc start check: run %llu differs: VIF state %s (TOPS %03X/%03X TOP %03X/%03X DBF %u/%u), VU memory %s (first qword %03X)",
                ss_n, dv ? "differs" : "same", sa.tops, sb.tops, sa.top, sb.top, sa.dbf, sb.dbf, dm ? "differs" : "same", q0);
    }
    if (ss_n % 200 == 0) uya_log("native ShrubProc start check: %llu starts, %llu differ", ss_n, ss_bad);
    return 1;
}

/* a REF to an MPG packet (qwc at q, data after it) */
static void emit_mpg(u32 qwc_at, u32 data, u32 *sp, u32 *fp)
{
    sq_(*sp, &ZQ);
    u32 v0 = M32(qwc_at);
    rc_sw(*sp + 4, data); rc_sw(*sp, 0x30000000u); *fp += 0x10; sh_(*sp, v0); *sp += 0x10;
}
static void emit_close(u32 *sp, u32 *fp)
{
    sq_(*sp, &ZQ); rc_sw(*sp, 0x10000000u); rc_sw(*sp + 8, 0x13000000u); *sp += 0x10; *fp += 0x10;
}
/* the VU buffers: a3 the packet's, t0 the instances', t1 the colours' */
typedef struct { u32 a3, t0, t1; } Bufs;
/* a packet's REF (s4: its table entry, 8 bytes) and the template after it */
static void emit_packet(const R128 *T, u32 s4, const Bufs *b, u32 *sp, u32 *fp)
{
    u32 at = M32(s4), v0 = M32(s4 + 4);
    sq_(*sp, &T[0]); rc_sw(*sp + 4, at); sh_(*sp, v0 >> 4); *fp += 0x10; sh_(*sp + 8, b->a3); *sp += 0x10;
    sq_(*sp, &T[1]); *fp += 0x10; sh_(*sp + 0xC, b->t1); *sp += 0x10;
}
/* the instances of a list (n entries from a2: index << 20 | block >> 4), 5 per MSCAL; swap: the
 * fading part's order (the colours' REF first) */
static void emit_insts(const R128 *T, u32 a2, u32 n, u64 *a0h, u64 athi, u64 s7h, int swap, Bufs *b, u32 *sp, u32 *fp)
{
    u32 s7 = n;
    for (;;) {
        u32 a1 = b->t0 + 1;
        u32 a0 = (s32)s7 < 5 ? s7 : 5;
        *a0h = minhi(s7h, athi);
        if (s7 == 0) return;
        sq_(*sp, &T[2]); s7 -= a0; sh_(*sp + 8, b->t0); *fp += 0x10; rc_sw(*sp + 0xC, a0); *sp += 0x10;
        do {
            u32 e = M32(a2); a2 += 4;
            u32 blk = (e << 12) >> 8, idx = e >> 20;
            u32 v1 = (a1 + 4) | 0x4000;
            R128 c = T[5];
            c.d[0] += (u64)(idx * 0x60u) << 32;
            u32 bq = swap ? 0x10 : 0, cq = swap ? 0 : 0x10;
            sq_(*sp + bq, &T[3]); a0--; sh_(*sp + bq + 0xC, a1); rc_sw(*sp + bq + 4, blk);
            a1 += 0x1C;
            sq_(*sp + cq, &T[5]); M64(*sp + cq) = c.d[0]; sh_(*sp + cq + 0xC, v1);
            *fp += 0x20; *sp += 0x20;
        } while (a0 != 0);
        sq_(*sp, &T[4]); *sp += 0x10;
        b->t0 = 0x269 - b->t0; b->t1 = 0x26 - b->t1;
        *fp += 0x10;
        if ((*sp & 0x600) == 0x600) flush(sp, *fp);
    }
}
/* a class's packets (its table at t8 + 0x40), each with the instances of its list */
static int dec_test = -1, in_clip;
static void emit_class(const R128 *T, u32 t8, u32 a2, u32 n, u64 *a0h, u64 athi, u64 s7h, int swap, Bufs *b, u32 *sp, u32 *fp)
{
    u32 s4 = t8 + 0x40, s5 = s4 + ((u32)M16(t8 + 0x28) << 3);
    do {
        if (dec_test > 0 && !in_clip) {
            /* testing: the packet's slot, for the chain walker to hand on before the packet's class run */
            u32 w[4] = { 0x10000000u, 0, NATIVE_SHRUB_DECMARK, (u32)native_shrub_direct_class(s4) };
            memcpy((void *)(uintptr_t)*sp, w, 16); *sp += 0x10; *fp += 0x10;
            if ((*sp & 0x600) == 0x600) flush(sp, *fp);
        }
        emit_packet(T, s4, b, sp, fp);
        s4 += 8;
        emit_insts(T, a2, n, a0h, athi, s7h, swap, b, sp, fp);
        b->a3 = 0x7A - b->a3;
    } while (s4 != s5);
}

/* ---- direct: the main and fading parts' draws to native_shrub.c (records), a marker in the chain
 * where a run of them starts; the VU buffers move as they would ---- */
static int direct, dshr, dshr_ok, dchk_on = -1, seg_open, seg_started;
static unsigned long long st_ddirect, st_dchain;
static void mark(u32 *sp, u32 *fp)
{
    u32 w[4] = { 0x10000000u, 0, NATIVE_SHRUB_MARK, native_shrub_seg_begin() };
    memcpy((void *)(uintptr_t)*sp, w, 16);
    *sp += 0x10; *fp += 0x10;
    seg_open = 1;
    if (!seg_started) { native_shrub_seg_start(); seg_started = 1; }
    if ((*sp & 0x600) == 0x600) flush(sp, *fp);
}
static void direct_class(const R128 *T, u32 t8, u32 a2, u32 n, u64 *a0h, u64 athi, u64 s7h, int swap, int clip, Bufs *b, u32 *sp, u32 *fp)
{
    u32 s4 = t8 + 0x40, s5 = s4 + ((u32)M16(t8 + 0x28) << 3);
    if (!seg_open) mark(sp, fp);
    do {
        native_shrub_seg_packet(native_shrub_direct_class(s4), b->a3, b->t1 * 8, clip);
        s4 += 8;
        u32 s7 = n, e = a2;
        for (;;) {
            u32 a1 = b->t0 + 1;
            u32 a0 = (s32)s7 < 5 ? s7 : 5;
            *a0h = minhi(s7h, athi);
            if (s7 == 0) break;
            s7 -= a0;
            native_shrub_seg_group(b->t0, a0, clip);
            for (u32 k = 0; k < a0; k++, e += 4, a1 += 0x1C) {
                u32 w = M32(e), blk = (w << 12) >> 8, idx = w >> 20;
                native_shrub_seg_inst(a1, blk, T[5].w[1] + idx * 0x60u, swap);
            }
            b->t0 = 0x269 - b->t0; b->t1 = 0x26 - b->t1;
        }
        b->a3 = 0x7A - b->a3;
    } while (s4 != s5);
}
/* the chain's templates and constants as native_shrub.c reads the draws; every packet of the
 * frame readable (else the frame's draws go into the chain) */
static int direct_ok(const R128 *T, const ShRefs *R, u32 gp)
{
    static const u32 want[6][4] = { { 3, 0x03000000u, 0x02000000u, 0xFFFF0000u }, { 1, 0, 0x14000000u, 0xFFFFFFFFu }, { 1, 0x60010000u, 0, 0 },
        { 3, 0, 0x6C050000u, 0xFFFF0000u }, { 1, 0, 0x15000063u, 0xFFFFFFFFu }, { 3, 0, 0x6E180000u, 0xFFFF0000u } };
    int ok = 1;
    for (int k = 0; k < 6; k++)
        ok &= (T[k].w[0] >> 28) == want[k][0] && (T[k].w[2] & 0xFFFF0000u) == want[k][1] && (T[k].w[3] & want[k][3]) == want[k][2];
    u32 c = R->consts;
    ok &= M32(c) == 0x10000003u && M32(c + 8) == 0x01000404u && M32(c + 12) == 0x6C030000u;
    if (!ok) { static int told; if (!told++) uya_log("native ShrubProc: the chain's templates are not as expected: the draws go into the chain"); return 0; }
    u32 k[12];
    memcpy(k, (void *)(uintptr_t)(c + 0x10), sizeof k);
    native_shrub_direct_frame(R->classes * 31u + M32(gp + (u32)R->g_mtx) * 7u + M32(R->classes), k);
    u32 n = M32(gp + (u32)R->g_cnt);
    for (u32 i = 0; i < n && ok; i++) {
        u32 t8 = M32(R->classes + 4 * i);
        if (M16(t8 + 0x14) & 1) continue;
        u32 s4 = t8 + 0x40, s5 = s4 + ((u32)M16(t8 + 0x28) << 3);
        for (; s4 != s5 && ok; s4 += 8) if (native_shrub_direct_class(s4) < 0) ok = 0;
    }
    return ok;
}

/* path counts (logged by check mode; a path at 0 is untested) */
static unsigned long long st_inst, st_retry, st_far, st_near, st_side, st_clip, st_clipfull, st_bb, st_bbpart, st_bbfull,
    st_plain, st_plainfull, st_fade, st_fadefull, st_sway, st_swayfar, st_skip, st_empty, st_bbdraw[2], st_clipdraw;

/* the sway (the class's sway mode in at, the matrix's address in a3): rows vf1..vf3 bent by the
 * sine table; returns to the caller (the game's routine jumps to 0x3FA0's address) */
static void sway(u32 at, u32 a3, u64 a0h)
{
    u32 a2 = SPR | 0x3C00;
    lqc2(9, a2 + 0x10); lqc2(8, a2);
    u32 a1 = M32(a2 + 0x20);
    at >>= 1;
    fm(11, X, ADD, 0, 0, 3);
    fm(12, XYZ, MUL, 4, 4, BV);
    a2 = SPR | 0x3B00;
    fm(ACC, X, ADD, 12, 12, 1);
    fm(12, X, MADD, 11, 12, 2);
    fm(0, Y, SUB, 9, 12, 0);
    u32 a0 = rc_vu0.mac;
    fm(12, X, MUL, 12, 9, 2);
    a0 &= 0x40;
    fm(12, W, SUB, 0, 12, 0);
    st_sway++;
    if (a0) { st_swayfar++; return; }
    fm(5, XYZW, MUL, 0, 0, 0);
    u32 v0 = a3 << 6;
    fm(6, XYZW, MUL, 0, 0, 0);
    { f32 t[4] = { VF(0)[1], VF(0)[2], VF(0)[3], VF(0)[0] }; memcpy(VF(7), t, 16); }
    v0 += a3 << 1;
    fm(5, X, ADD, 5, 0, 3);
    v0 += a3;
    fm(6, Y, ADD, 6, 0, 3);
    u32 v1 = (a3 << 7) - (a3 << 2) - a3;
    v0 += a1;
    v1 += a1;
    if (at) v1 += a1;
    s32 s;
    s = (s32)((u32)(s32)(s8)M8(a2 + (v0 & 0xFF)) << 5); qmtc2(10, (u64)(s64)s, a0h); itof(10, XYZW, 10, 12);
    s = (s32)((u32)(s32)(s8)M8(a2 + ((v0 >> 1) & 0xFF)) << 5); qmtc2(11, (u64)(s64)s, a0h); itof(11, XYZW, 11, 12);
    fm(13, X, MUL, 10, 11, BV);
    u32 a4 = v1 + 0x40;
    fm(13, W, SUB, 0, 9, 0);
    fm(13, X, MUL, 13, 9, 0);
    fm(13, X, ADD, 13, 13, 3);
    s = (s32)((u32)(s32)(s8)M8(a2 + (a4 & 0xFF)) << 5); qmtc2(10, (u64)(s64)s, a0h); itof(10, XYZW, 10, 12);
    s = (s32)((u32)(s32)(s8)M8(a2 + ((v1 >> 1) & 0xFF)) << 5); qmtc2(11, (u64)(s64)s, a0h); itof(11, XYZW, 11, 12);
    fm(13, X, MUL, 13, 8, 0);
    fm(ACC, X, MUL, 10, 8, 1);
    fm(10, X, MADD, 13, 8, 2);
    fm(10, X, MUL, 10, 12, 3);
    fm(ACC, X, MUL, 11, 8, 1);
    fm(11, X, MADD, 13, 8, 3);
    fm(11, X, MUL, 11, 12, 3);
    if (!at) { fm(10, X, MUL, 10, 9, 3); fm(11, X, MUL, 11, 9, 3); }
    fm(7, X, ADD, 0, 10, 0);
    fm(7, Y, ADD, 0, 11, 0);
    mat3(1, XYZ, 5, 6, 7, 1);
    mat3(2, XYZ, 5, 6, 7, 2);
    mat3(3, XYZ, 5, 6, 7, 3);
}

/* the billboard part's output: into the chain, or (direct) each VIF1 DIRECT payload as GS packets for
 * native_shrub.c, behind a marker; the templates checked first (CNT tags of the payload's size, VIF
 * NOP and DIRECT) */
static int dbb;
static u32 gb[4096][4], gbn;
static void gb_put(const void *q) { if (gbn < 4096) memcpy(gb[gbn++], q, 16); }
static void gb_flush(void) { if (gbn) native_shrub_seg_gif((const u32 (*)[4])gb, gbn); gbn = 0; }
static int cnt_direct(const u32 *w, u32 n) { return w[0] == (0x10000000u | n) && w[1] == 0 && w[2] == 0 && w[3] == (0x50000000u | n); }
/* n qwords of the executable's data: a CNT and its DIRECT payload */
static void copy_part(u32 src, int n, u32 *sp, u32 *fp, u64 *v0h, u64 *v1h, u64 *a0h)
{
    if (!dbb) { copyq(src, n, sp, fp, v0h, v1h, a0h); return; }
    R128 v[9];
    for (int k = 0; k < n; k++) rc_lq(&v[k], src + 16 * (u32)k);
    *v0h = v[0].d[1]; *v1h = v[1].d[1]; *a0h = v[2].d[1];
    for (int k = 1; k < n; k++) gb_put(&v[k]);
    gb_flush();
}

static void run(RcCtx *c, const ShRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    u64 r1hi = c->r[1].d[1], t7h = c->r[15].d[1], s7h = c->r[23].d[1];
    /* the registers it saves */
    rc_sd(SPR + 0x3FF8, c->r[31].d[0]); rc_sd(SPR + 0x3FF0, c->r[30].d[0]); rc_sd(SPR + 0x3FE8, c->r[29].d[0]);
    for (int k = 23; k >= 16; k--) rc_sd(SPR + 0x3FA8 + 8 * (u32)(k - 16), c->r[k].d[0]);
    u32 s3 = M32(gp + (u32)R->g_mtx);
    {
        u32 n = M32(gp + (u32)R->g_cnt), cap = M32(gp + (u32)R->g_cap), fade = M32(gp + (u32)R->g_fade);
        rc_sw(SPR + 0x3F60, n); rc_sw(SPR + 0x3F64, R->classes); rc_sw(SPR + 0x3FA4, cap); rc_sw(SPR + 0x3F88, fade);
        rc_sw(SPR + 0x3F70, SPR + 0x3700); rc_sw(SPR + 0x3F74, SPR + 0x3D00); rc_sw(SPR + 0x3F6C, SPR + 0x2000); rc_sw(SPR + 0x3F8C, SPR + 0x3200);
    }
    s3 -= 0x40;
    u32 t9 = 0;
    for (u32 k = 0; k < 0x100; k += 16) { R128 v; rc_lq(&v, R->sine + k); sq_(SPR + 0x3B00 + k, &v); }
    for (u32 k = 0; k < 0x20; k += 16) { R128 v; rc_lq(&v, R->sway + k); sq_(SPR + 0x3C00 + k, &v); }
    rc_sw(SPR + 0x3C20, M32(gp + (u32)R->g_clock));
    rc_sw(SPR + 0x3FA0, R->ret1);
    rc_sw(SPR + 0x3F7C, 2); rc_sw(SPR + 0x3F80, 0xEE); rc_sw(SPR + 0x3F84, 0x11);
    u32 fp = M32(gp + (u32)R->g_pkt), sp = SPR;
    for (u32 a = SPR + 0x1000; a < SPR + 0x2000; a += 0x10) sq_(a, &ZQ);
    /* the camera */
    qmtc2(1, 0x44800000u, M64(R->sway + 8));
    lqc2(24, R->cam + 0x140); lqc2(25, R->cam); lqc2(26, R->cam + 0x10); lqc2(27, R->cam + 0x20);
    lqc2(28, R->cam + 0x80); lqc2(29, R->cam + 0x90); lqc2(30, R->cam + 0xA0); lqc2(31, R->cam + 0xB0);
    fm(24, W, MUL, 0, 1, 0);
    lqc2(18, R->frust + 0x220); lqc2(19, R->frust + 0xA0); lqc2(20, R->frust + 0xB0);
    lqc2(21, R->frust + 0x1D0); lqc2(22, R->frust + 0x1E0); lqc2(23, R->frust + 0x1F0);
    qmtc2(1, 0x3A800000u, r1hi);
    fm(19, XY, MUL, 19, 1, 0); fm(23, XY, MUL, 23, 1, 0);
    u64 athi, v0h, v1h, a0h;
    { R128 v[6]; for (int k = 0; k < 6; k++) rc_lq(&v[k], gp + (u32)R->g_t[k]); for (int k = 0; k < 6; k++) sq_(SPR + 0x3F00 + 16 * (u32)k, &v[k]); athi = v[0].d[1];
      dshr = 0; dshr_ok = 0; seg_open = 0; seg_started = 0;
      if (dchk_on < 0) dchk_on = getenv("UYA_SHRUBDCHK") != 0;
      if (direct || dec_test || dchk_on) { int ok = direct_ok(v, R, gp); dshr_ok = ok; dshr = ok && direct && !dec_test && !dchk_on; if (direct && !dec_test) { if (dshr) st_ddirect++; else st_dchain++; } } }
    {   /* testing: UYA_SSTARTLOG=1: the chain start's words, once */
        static int left = -1; if (left < 0) left = getenv("UYA_SSTARTLOG") ? 1 : 0;
        if (left > 0) {
            left--;
            uya_log("ShrubProc start: MPG qwc %u at %08X; consts at %08X, head at %08X:", M16(R->mpg_qwc), R->mpg_addr, R->consts, R->head);
            for (int k = 0; k < 5; k++) uya_log("   %08X %08X %08X %08X", M32(R->consts + 16 * k), M32(R->consts + 16 * k + 4), M32(R->consts + 16 * k + 8), M32(R->consts + 16 * k + 12));
            for (int k = 0; k < 3; k++) uya_log("   head %08X %08X %08X %08X", M32(R->head + 16 * k), M32(R->head + 16 * k + 4), M32(R->head + 16 * k + 8), M32(R->head + 16 * k + 12));
        }
    }
    if (ss_mode < 0) { const char *e = getenv("UYA_SSTARTREC"); ss_mode = e ? atoi(e) : 2; }
    if (dshr && ss_mode > 0 && sstart_template_ok(R->consts)) {
        emit_sstart_rec(R->mpg_qwc, R->mpg_addr, R->consts, &sp, &fp);
        v0h = M64(R->consts + 8); v1h = M64(R->consts + 0x18); a0h = M64(R->consts + 0x28);   /* (as copyq leaves them) */
    } else {
        emit_mpg(R->mpg_qwc, R->mpg_addr, &sp, &fp);
        copyq(R->consts, 5, &sp, &fp, &v0h, &v1h, &a0h);
    }
    rc_sw(SPR + 0x3F90, fp);
    fp += 0x10; sq_(sp, &ZQ); sp += 0x10;
    copyq(R->head, 3, &sp, &fp, &v0h, &v1h, &a0h);

    /* the classes */
    for (;;) {
        u32 n = M32(SPR + 0x3F60), tp = M32(SPR + 0x3F64);
        if (n == t9) break;
        u32 t8 = M32(tp);
        rc_sw(SPR + 0x3F64, tp + 4);
        qmtc2(17, 0x50000000u, v1h);
        u32 s0 = 0, s1 = 0, s2 = 0;
        u32 fl = M16(t8 + 0x14), s6 = M32(t8 + 0x18), s7 = M16(t8 + 0x16);
        sp += 0x10; fp += 0x10;
        int empty;
        if (fl & 1) {
            st_skip++;
            s3 += s7 << 6;
            empty = 1;
        } else {
            u32 t6 = M32(t8 + 0x1C);
            rc_sw(SPR + 0x3F9C, (fl & 6) >> 1);
            rc_sw(SPR + 0x3F68, SPR + 0x2C00);
            u32 start = fp;                               /* (pcpyld s3, fp, s3: s3's upper half) */
            /* the instances, 128 at a time */
            while (s7 != 0) {
                u32 t7 = M32(SPR + 0x3FA4);
                rc_sw(0x1000D480u, 0x1000);
                u32 nb = (s32)s7 < 0x80 ? s7 : 0x80;
                athi = minhi(athi, s7h);
                rc_sw(0x1000D410u, s6); rc_sw(0x1000D420u, nb << 1); rc_sw(0x1000D400u, 0x100);
                s7 -= nb;
                u32 s4 = SPR + 0x1000, s5 = nb << 5;
                rc_sw(SPR + 0x3F78, s6);
                s6 += s5; s5 += s4; s4 -= 0x20;
                for (;;) {
                    u32 w10 = M32(s4 + 0x30);
                    s4 += 0x20;
                    lqc2(1, s4);
                    if (s4 == s5) break;
                    st_inst++;
                    s32 m0 = (s32)w10 < (s32)t7 ? (s32)w10 : (s32)t7;
                    v0h = minhi(v0h, t7h);
                    fm(2, XYZ, SUB, 1, 24, BV);
                    fm(3, X, ADD, 0, 1, 3);
                    qmtc2(4, (u64)(u32)m0 << 32, v0h);
                    fm(3, Y, SUB, 0, 1, 3);
                    if (m0 == 0) { st_retry++; s4 -= 0x20; continue; }   /* (the game's routine: for ever) */
                    fm(ACC, XYZ, MUL, 25, 2, 0);
                    fm(ACC, XYZ, MADD, 26, 2, 1);
                    s3 += 0x40;
                    fm(2, XYZ, MADD, 27, 2, 2);
                    rc_sw(s4 + 0x10, 0);
                    fm(19, Y, SUB, 4, 1, 3);
                    fm(8, XY, MUL, 22, 1, 3);
                    fm(1, XY, ADD, 3, 2, 2);
                    fm(5, XY, MUL, 20, 2, 2);
                    VF(2)[0] = __builtin_fabsf(VF(2)[0]); VF(2)[1] = __builtin_fabsf(VF(2)[1]);
                    fm(7, XY, MUL, 8, 22, 2);
                    fm(3, XY, SUB, 19, 1, BV);
                    fm(9, XY, MUL, 5, 21, BV);
                    fm(6, XY, SUB, 2, 8, BV);
                    fm(8, XY, ADD, 2, 7, BV);
                    fm(4, XY, SUB, 1, 23, BV);
                    fm(1, Z, MAX, 0, 2, BV);
                    u32 f3x = VW(3, 0), f3y = VW(3, 1);
                    athi = hi_of(3);
                    fm(7, XY, SUB, 5, 6, BV);
                    fm(8, XY, SUB, 9, 8, BV);
                    u32 f4x = VW(4, 0), f4y = VW(4, 1);
                    a0h = hi_of(4);
                    if (f3y & 0x80000000u) { st_far++; continue; }                 /* beyond its draw distance */
                    { f32 y = VF(1)[1], z = VF(1)[2]; VF(1)[0] = y; VF(1)[1] = z; }
                    if (!(f3x & 0x80000000u)) { st_near++; continue; }            /* behind the near plane */
                    ftoi(3, XY, 3, 12);
                    u32 t2 = M16(s4 + 0x18);
                    u32 f7x = VW(7, 0), f7y = VW(7, 1);
                    v0h = hi_of(7);
                    u32 f8x = VW(8, 0), f8y = VW(8, 1);
                    v1h = hi_of(8);
                    if (f7y & 0x80000000u) { st_side++; continue; }               /* outside a side plane */
                    if (f7x & 0x80000000u) { st_side++; continue; }
                    fm(2, X, MUL, 1, 24, 3);
                    lqc2(4, s3 + 0x30);
                    if (M8(s4 + 0x1B) != 0 || M16(s4 + 0x1E) != 0xFFFF) {
                        /* the light list */
                        u32 lp = M32(SPR + 0x3F8C);
                        if (lp != SPR + 0x35FE) { sh_(lp, t2); rc_sw(SPR + 0x3F8C, lp + 2); }
                    }
                    /* crossing: the second test's delay slot (vftoi12.y vf5) runs when the first is not taken */
                    int crossing = ((f8y | f4y) & 0x80000000u) != 0;
                    if (!crossing) { ftoi(5, Y, 1, 12); crossing = ((f8x | (f4x ^ 0x80000000u)) & 0x80000000u) != 0; }
                    if (crossing) {
                        /* crossing the frustum's edge: the clip list */
                        u32 at = M32(SPR + 0x3F74);
                        fm(17, X, MIN, 2, 17, BV);
                        if (at == SPR + 0x3F00) { st_clipfull++; continue; }
                        st_clip++;
                        s1++;
                        rc_sw(at, M32(SPR + 0x3F78) + (s4 & 0xFFF));
                        rc_sw(SPR + 0x3F74, at + 4);
                        continue;
                    }
                    /* v1: 0x8000 for no fade, else the fade (its upper word, then shifted down) */
                    u64 v1 = lo_of(3);
                    v1h = hi_of(3);
                    if (t6 != 0) {
                        u32 b17 = M8(s4 + 0x17);
                        u64 a2 = 0x8000;
                        v1 = (u64)0x8000 << 32;
                        int go = 1;
                        if (b17 != 0) {
                            u64 v0 = lo_of(5);
                            v0h = hi_of(5);
                            u64 at = (u64)b17 << 44;
                            v1 = v1 + at;
                            a2 = v0 - at;
                            v1 = v1 - v0;
                            if ((s64)a2 < 0) go = 0;
                            else {
                                s32 hi = (s32)((s64)a2 >> 32);
                                a2 = (u64)(s64)(hi < 0x8000 ? hi : 0x8000);
                            }
                        }
                        if (go) {
                            u32 al = (u32)a2 >> 8;
                            u32 at = M32(SPR + 0x3F6C);
                            if (al == 0x80) {
                                /* wholly a billboard */
                                u64 w = lo_of(3);
                                v1h = hi_of(3);
                                if (at == SPR + 0x2C00) { st_bbfull++; continue; }
                                s32 y = (s32)((s64)w >> 33);
                                st_bb++;
                                s0++;
                                v1h = minhi(v1h, v0h);
                                sh_(at, t2);
                                sh_(at + 2, (u32)(y < 0x8000 ? y : 0x8000) >> 8);
                                rc_sw(SPR + 0x3F6C, at + 4);
                                continue;
                            }
                            /* partly: a billboard too */
                            if (at == SPR + 0x2C00) { st_bbfull++; continue; }
                            st_bbpart++;
                            sh_(at + 2, al);
                            s0++;
                            sh_(at, t2);
                            rc_sw(SPR + 0x3F6C, at + 4);
                        }
                    }
                    fm(17, X, MIN, 2, 17, BV);
                    u32 a0 = fp >> 4;
                    s32 fv = (s32)((s64)v1 >> 32);
                    sq_(sp + 0x40, &ZQ);
                    s32 fade = fv < 0x8000 ? fv : 0x8000;
                    v1h = minhi(v1h, v0h);
                    u32 a1 = t2 << 20;
                    if (fade == 0x8000) {
                        u32 at = M32(SPR + 0x3F68);
                        if (at == SPR + 0x3000) { st_plainfull++; continue; }
                        st_plain++;
                        rc_sw(at, a0 | a1);
                        rc_sw(SPR + 0x3F68, at + 4);
                    } else {
                        /* fading: its colour and alpha in the block's last qword */
                        u32 col = M32(M32(SPR + 0x3F88) + t2 * 0x60u);
                        u32 w[4] = { col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, col >> 24 };
                        u32 at = M32(SPR + 0x3F70);
                        memcpy((void *)(uintptr_t)(sp + 0x40), w, 16);
                        rc_sw(sp + 0x4C, (u32)fade >> 8);
                        if (at == SPR + 0x3B00) { st_fadefull++; continue; }
                        st_fade++;
                        rc_sw(at, a0 | a1);
                        rc_sw(SPR + 0x3F70, at + 4);
                        s2++;
                    }
                    /* the matrix */
                    lqc2(1, s3); lqc2(2, s3 + 0x10); lqc2(3, s3 + 0x20);
                    fm(1, XYZ, MUL, 1, 4, 3);
                    fm(2, XYZ, MUL, 2, 4, 3);
                    u32 swm = M32(SPR + 0x3F9C);
                    fm(3, XYZ, MUL, 3, 4, 3);
                    sp += 0x50;
                    fm(4, XYZ, SUB, 4, 24, BV);
                    if (swm) sway(swm, s3, a0h);
                    aff(1, 28, 29, 30, 31, 1, 0, 0);
                    fm(4, XYZ, MUL, 4, 24, 3);
                    fp += 0x50;
                    aff(2, 28, 29, 30, 31, 2, 0, 0);
                    aff(3, 28, 29, 30, 31, 3, 0, 0);
                    aff(4, 28, 29, 30, 31, 4, 0, 3);
                    sqc2(1, sp - 0x50); sqc2(2, sp - 0x40); sqc2(3, sp - 0x30); sqc2(4, sp - 0x20);
                    if ((sp & 0x600) == 0x600) flush(&sp, fp);
                }
            }
            /* the class's end */
            a0h = 0;                                      /* (pcpyud a0, s3, zero) */
            { s32 k = rc_ftoi(VF(17)[0], 0); memcpy(&VF(17)[0], &k, 4); }
            empty = start == fp;
            if (!empty) {
                u32 v1 = ((fp - start) << 16) + (start >> 4);
                u32 s6n = (M32(SPR + 0x3F68) - SPR - 0x2C00) >> 2;
                sh_(SPR + 0x3600 + 2 * t9, s0); sh_(SPR + 0x3C80 + 2 * t9, s1); sh_(SPR + 0x3680 + 2 * t9, s2);
                v0h = hi_of(17);
                rc_sw(SPR + 0x3000 + 4 * t9, v1); rc_sw(SPR + 0x3100 + 4 * t9, VW(17, 0));
                t9++;
                sh_(t8 + 0x38, s0); sh_(t8 + 0x36, s1); sh_(t8 + 0x34, s2 + s6n);
                if (s6n) {
                    /* the plain instances' draws */
                    Bufs b = { M32(SPR + 0x3F7C), M32(SPR + 0x3F80), M32(SPR + 0x3F84) };
                    R128 T[6];
                    for (int k = 0; k < 6; k++) rc_lq(&T[k], SPR + 0x3F00 + 16 * (u32)k);
                    t7h = T[5].d[1];
                    if (dshr) { seg_open = 0; direct_class(T, t8, SPR + 0x2C00, s6n, &a0h, athi, s7h, 0, 0, &b, &sp, &fp); }
                    else {
                        if (dchk_on && dshr_ok) { Bufs c2 = b; u64 x = a0h; seg_open = 0; direct_class(T, t8, SPR + 0x2C00, s6n, &x, athi, s7h, 0, 0, &c2, &sp, &fp); }
                        emit_class(T, t8, SPR + 0x2C00, s6n, &a0h, athi, s7h, 0, &b, &sp, &fp);
                    }
                    rc_sw(SPR + 0x3F7C, b.a3); rc_sw(SPR + 0x3F80, b.t0); rc_sw(SPR + 0x3F84, b.t1);
                }
                continue;
            }
            st_empty++;
        }
        /* nothing in the chain: the free qword back */
        (void)empty;
        sp -= 0x10; fp -= 0x10;
        sh_(t8 + 0x38, s0); sh_(t8 + 0x36, s1); sh_(t8 + 0x34, 0);
        sh_(SPR + 0x3600 + 2 * t9, s0); sh_(SPR + 0x3C80 + 2 * t9, s1); sh_(SPR + 0x3680 + 2 * t9, s2);
        rc_sw(SPR + 0x3000 + 4 * t9, 0);
        t9++;
        v0h = hi_of(17);
        rc_sw(SPR + 0x3100 + 4 * (t9 - 1), VW(17, 0));
    }

    /* the fading part */
    copyq(R->close, 3, &sp, &fp, &v0h, &v1h, &a0h);
    seg_open = 0;
    {
        Bufs b = { M32(SPR + 0x3F7C), M32(SPR + 0x3F80), M32(SPR + 0x3F84) };
        R128 T[6];
        for (int k = 0; k < 6; k++) rc_lq(&T[k], SPR + 0x3F00 + 16 * (u32)k);
        t7h = T[5].d[1];
        u32 cls = R->classes2, n = M32(SPR + 0x3F60), cnt = SPR + 0x3680, lst = SPR + 0x3700;
        for (t9 = 0; t9 != n; t9++, cnt += 2, cls += 4) {
            u32 k = M16(cnt);
            if (k == 0) continue;
            if (dshr) direct_class(T, M32(cls), lst, k, &a0h, athi, s7h, 1, 0, &b, &sp, &fp);
            else {
                if (dchk_on && dshr_ok) { Bufs c2 = b; u64 x = a0h; seg_open = 0; direct_class(T, M32(cls), lst, k, &x, athi, s7h, 1, 0, &c2, &sp, &fp); }
                emit_class(T, M32(cls), lst, k, &a0h, athi, s7h, 1, &b, &sp, &fp);
            }
            lst += k << 2;
        }
        rc_sw(SPR + 0x3F7C, b.a3); rc_sw(SPR + 0x3F80, b.t0); rc_sw(SPR + 0x3F84, b.t1);
    }
    emit_close(&sp, &fp);
    {
        u32 at = sp & 0x7FF;
        wait_idle(0x1000D000u);
        rc_sw(0x1000D080u, sp - at); rc_sw(0x1000D010u, fp - at); rc_sw(0x1000D020u, at >> 4); rc_sw(0x1000D000u, 0x100);
        sp = (sp - at) ^ 0x800;
    }

    /* the billboard part */
    rc_sw(SPR + 0x3F94, fp);
    fp += 0x10; sq_(sp, &ZQ); sp += 0x10;
    dbb = 0;
    if (direct) {
        R128 B[4];
        for (int k = 0; k < 4; k++) rc_lq(&B[k], gp + (u32)R->g_b[k]);
        dbb = cnt_direct(B[0].w, 4) && cnt_direct(B[2].w, 0xD) && cnt_direct((const u32 *)(uintptr_t)R->bstart, 3) && cnt_direct((const u32 *)(uintptr_t)R->bstate, 2);
        if (!dbb) { static int told; if (!told++) uya_log("native ShrubProc: the billboard templates are not as expected: the billboards go into the chain"); }
        else {
            u32 w[4] = { 0x10000000u, 0, NATIVE_SHRUB_MARK, native_shrub_seg_begin() };
            memcpy((void *)(uintptr_t)sp, w, 16); sp += 0x10; fp += 0x10;
            if ((sp & 0x600) == 0x600) flush(&sp, fp);
        }
    }
    copy_part(R->bstart, 4, &sp, &fp, &v0h, &v1h, &a0h);
    lqc2(28, R->cam2 + 0x40); lqc2(29, R->cam2 + 0x50); lqc2(30, R->cam2 + 0x60); lqc2(31, R->cam2 + 0x70);
    lqc2(21, R->frust2 + 0x220); lqc2(22, R->frust2 + 0x210); lqc2(23, R->frust2 + 0x1A0);
    {
        u32 t7 = M32(gp + (u32)R->g_mtx2);
        for (u32 k = 0; k < 0x80; k += 16) { R128 v; rc_lq(&v, R->corners + k); sq_(SPR + 0x3800 + k, &v); }
        R128 B[4];
        for (int k = 0; k < 4; k++) rc_lq(&B[k], gp + (u32)R->g_b[k]);
        u32 s7 = 0x80, cls = R->classes3;
        for (;;) {
            u32 n = M32(SPR + 0x3F60), cnt = SPR + 0x3600, lst = SPR + 0x2000;
            for (t9 = 0; t9 != n; t9++, cnt += 2, cls += 4) {
                u32 s6 = M16(cnt);
                if (s6 == 0) continue;
                u32 t8 = M32(cls);
                u32 s4 = M32(t8 + 0x1C);
                lqc2(16, s4);
                for (; s6 != 0; s6--) {
                    u32 idx = M16(lst), al = M16(lst + 2);
                    lst += 4;
                    u32 v0 = (idx << 6) + t7;
                    if (!(al & s7)) continue;
                    u32 col = M32(v0 + 0x1C);
                    u32 a0 = M32(v0 + 0x2C);
                    if (s4) {
                        /* the class's texture block, before its first billboard */
                        R128 h[3];
                        for (int k = 0; k < 3; k++) rc_lq(&h[k], s4 + 0x10 + 16 * (u32)k);
                        s4 = 0;
                        if (dbb) { gb_put(&B[1]); for (int k = 0; k < 3; k++) gb_put(&h[k]); gb_flush(); }
                        else {
                            sq_(sp, &B[0]); sq_(sp + 0x10, &B[1]);
                            for (int k = 0; k < 3; k++) sq_(sp + 0x20 + 16 * (u32)k, &h[k]);
                            sp += 0x50; fp += 0x50;
                        }
                    }
                    st_bbdraw[s7 == 0x80 ? 0 : 1]++;
                    lqc2(1, v0 + 0x30);
                    col |= (u32)(al << 24);
                    /* pextlh a0, zero, a0 (a0 a word loaded: its halves, and the sign's) */
                    u32 hx = (s32)a0 < 0 ? 0xFFFFu : 0;
                    u32 aw[4] = { a0 & 0xFFFF, a0 >> 16, hx, hx };
                    a0h = (u64)hx | (u64)hx << 32;
                    R128 cw; cw.w[0] = col & 0xFF; cw.w[1] = (col >> 8) & 0xFF; cw.w[2] = (col >> 16) & 0xFF; cw.w[3] = col >> 24;
                    if (dbb) gb_put(&B[3]);
                    else { sq_(sp, &B[2]); sq_(sp + 0x10, &B[3]); }
                    memcpy(VF(3), aw, 16);
                    fm(1, XYZ, SUB, 1, 24, BV);
                    if (!dbb) sp += 0x20;
                    itof(3, XY, 3, 12);
                    if (!dbb) fp += 0x20;
                    fm(4, XYZ, ADD, 0, 0, 3);
                    fm(2, XYZ, MUL, 1, 1, BV);
                    fm(ACC, X, ADD, 2, 2, 1);
                    fm(2, X, MADD, 4, 2, 2);
                    rc_vu0.q = rc_fdiv(VF(0)[3], vsqrt(VF(2)[0]));
                    fm(15, Y, MUL, 16, 3, 0);
                    fm(4, XYZW, MUL, 0, 0, 0);
                    fm(5, XYZW, MUL, 0, 0, 0);
                    { f32 t[4] = { VF(0)[1], VF(0)[2], VF(0)[3], VF(0)[0] }; memcpy(VF(6), t, 16); }
                    memcpy(VF(7), VF(0), 16);
                    fm(4, X, ADD, 4, 0, 3);
                    fm(5, Y, ADD, 5, 0, 3);
                    fm(15, ZW, MUL, 16, 3, 1);
                    fm(2, XYZ, MUL, 1, 0, BQ);
                    fm(15, YZW, MUL, 15, 1, 3);
                    fm(4, XY, ADD, 0, 2, BV);
                    fm(5, X, ADD, 0, 2, 1);
                    fm(5, Y, SUB, 0, 2, 0);
                    fm(7, XYZ, MUL, 1, 24, 3);
                    fm(7, W, ADD, 0, 0, 0);
                    aff(4, 28, 29, 30, 31, 4, 0, 0);
                    aff(5, 28, 29, 30, 31, 5, 0, 0);
                    aff(6, 28, 29, 30, 31, 6, 0, 0);
                    aff(7, 28, 29, 30, 31, 7, 7, 3);
                    /* the four corners */
                    for (u32 at = SPR + 0x3800; at != SPR + 0x3840; at += 0x10) {
                        lqc2(1, at); lqc2(2, at + 0x40);
                        fm(1, YZ, MUL, 1, 15, BV);
                        fm(1, Z, ADD, 1, 15, 3);
                        aff(1, 4, 5, 6, 7, 1, 0, 3);
                        rc_vu0.q = fc(rc_fdiv(VF(22)[0], VF(1)[3]));
                        fm(1, W, ADD, 1, 23, BV);
                        fm(1, W, MAX, 1, 21, BV);
                        fm(1, W, MIN, 1, 21, 2);
                        fm(1, XYZ, MUL, 1, 0, BQ);
                        fm(2, XYZ, MUL, 2, 0, BQ);
                        fm(1, XYZ, ADD, 1, 23, BV);
                        ftoi(1, XYZW, 1, 4);
                        if (dbb) { gb_put(VF(2)); gb_put(&cw); gb_put(VF(1)); continue; }
                        sq_(sp + 0x10, &cw);
                        sqc2(2, sp);
                        fp += 0x30;
                        sqc2(1, sp + 0x20);
                        sp += 0x30;
                    }
                    if (dbb) gb_flush();
                    else if ((sp & 0x600) == 0x600) flush(&sp, fp);
                }
            }
            if (s7 == 0x7F) break;
            /* the second pass: the others, after the state for them */
            s7 = 0x7F; cls = R->classes4;
            copy_part(R->bstate, 3, &sp, &fp, &v0h, &v1h, &a0h);
        }
    }

    /* the clip part */
    rc_sw(SPR + 0x3F98, fp);
    fp += 0x10; sq_(sp, &ZQ); sp += 0x10;
    {
        u32 at = sp & 0x7FF;
        wait_idle(0x1000D000u);
        if (at) {
            rc_sw(0x1000D080u, sp - at); rc_sw(0x1000D010u, fp - at); rc_sw(0x1000D020u, at >> 4); rc_sw(0x1000D000u, 0x100);
            sp = (sp - at) ^ 0x800;
        }
    }
    copyq(R->head2, 3, &sp, &fp, &v0h, &v1h, &a0h);
    int clip = M32(SPR + 0x3F74) != SPR + 0x3D00;
    if (clip) {
        /* direct: the MPG (UYA_SSTARTREC > 0), the constants and the clip program's start in the records */
        if (ss_mode < 0) { const char *e = getenv("UYA_SSTARTREC"); ss_mode = e ? atoi(e) : 2; }
        int cmpg = dshr && ss_mode > 0;
        if (!cmpg) emit_mpg(R->mpg2_qwc, R->mpg2_addr, &sp, &fp);
        if (dshr) {
            R128 v[3]; for (int k = 0; k < 3; k++) rc_lq(&v[k], R->consts2 + 16 * (u32)k);
            v0h = v[0].d[1]; v1h = v[1].d[1]; a0h = v[2].d[1];
            seg_open = 0; mark(&sp, &fp);
            native_shrub_seg_cstart(R->consts2 + 0x10, cmpg ? R->mpg2_addr : 0, cmpg ? M16(R->mpg2_qwc) : 0);
        } else copyq(R->consts2, 8, &sp, &fp, &v0h, &v1h, &a0h);
        lqc2(28, R->cam3 + 0xC0); lqc2(29, R->cam3 + 0xD0); lqc2(30, R->cam3 + 0xE0); lqc2(31, R->cam3 + 0xF0);
        rc_sw(SPR + 0x3FA0, R->ret2);
        rc_sw(SPR + 0x3F7C, 2); rc_sw(SPR + 0x3F80, 0xEE); rc_sw(SPR + 0x3F84, 0x11);
        R128 T[6];
        for (int k = 0; k < 6; k++) rc_lq(&T[k], SPR + 0x3F00 + 16 * (u32)k);
        t7h = T[5].d[1];
        u32 base = M32(gp + (u32)R->g_mtx3);
        qmtc2(16, (u64)(s64)(s32)base, athi);
        u32 cls = R->classes5, n = M32(SPR + 0x3F60), cnt = SPR + 0x3C80, s0 = SPR + 0x3D00;
        for (t9 = 0; t9 != n; t9++, cnt += 2, cls += 4) {
            u32 s6 = M16(cnt);
            if (s6 == 0) continue;
            u32 a0c = M32(cls);
            /* a NEXT over the blocks */
            sq_(sp, &ZQ); rc_sw(sp, 0x20000000u);
            fp += 0x10;
            rc_sw(sp + 4, s6 * 0x50u + fp);
            sp += 0x10;
            rc_sw(SPR + 0x3F9C, (M16(a0c + 0x14) & 6) >> 1);
            u32 t8 = s0;
            for (; s6 != 0; s6--) {
                st_clipdraw++;
                u32 at = M32(s0);
                s0 += 4;
                u32 v1 = M16(at + 0x18);
                a0h = hi_of(16);
                u32 v0 = (v1 << 6) + (u32)lo_of(16);
                lqc2(4, v0 + 0x30);
                u32 a0 = fp >> 4;
                lqc2(1, v0);
                v1 = (v1 << 20) | a0;
                lqc2(2, v0 + 0x10);
                lqc2(3, v0 + 0x20);
                rc_sw(s0 - 4, v1);
                fm(4, XYZ, SUB, 4, 24, BV);
                fm(1, XYZ, MUL, 1, 4, 3);
                u32 swm = M32(SPR + 0x3F9C);
                fm(2, XYZ, MUL, 2, 4, 3);
                fm(3, XYZ, MUL, 3, 4, 3);
                if (swm) sway(swm, v0, a0h);
                aff(1, 28, 29, 30, 31, 1, 0, 0);
                fm(4, XYZ, MUL, 4, 24, 3);
                aff(2, 28, 29, 30, 31, 2, 0, 0);
                aff(3, 28, 29, 30, 31, 3, 0, 0);
                aff(4, 28, 29, 30, 31, 4, 0, 3);
                sqc2(1, sp); sqc2(2, sp + 0x10); sqc2(3, sp + 0x20); sqc2(4, sp + 0x30); sq_(sp + 0x40, &ZQ);
                sp += 0x50;
                fp += 0x50;
                if ((sp & 0x600) == 0x600) flush(&sp, fp);
            }
            Bufs b = { M32(SPR + 0x3F7C), M32(SPR + 0x3F80), M32(SPR + 0x3F84) };
            if (dshr) { seg_open = 0; direct_class(T, a0c, t8, M16(cnt), &a0h, athi, s7h, 0, 1, &b, &sp, &fp); }
            else {
                in_clip = 1;
                emit_class(T, a0c, t8, M16(cnt), &a0h, athi, s7h, 0, &b, &sp, &fp);
                in_clip = 0;
            }
            rc_sw(SPR + 0x3F7C, b.a3); rc_sw(SPR + 0x3F80, b.t0); rc_sw(SPR + 0x3F84, b.t1);
        }
            emit_close(&sp, &fp);
    }
    {
        u32 at = sp & 0x7FF;
        wait_idle(0x1000D000u);
        if (at) { rc_sw(0x1000D080u, sp - at); rc_sw(0x1000D010u, fp - at); rc_sw(0x1000D020u, at >> 4); rc_sw(0x1000D000u, 0x100); }
        wait_idle(0x1000D000u);
    }

    /* the end */
    sh_(M32(SPR + 0x3F8C), 0xFFFF);
    {
        u32 at = SPR + 0x3000, e = at + (M32(SPR + 0x3F60) << 2);
        for (; at != e; at += 4) {
            u32 v1 = M32(at);
            if (v1 == 0) continue;
            u32 sz = (v1 >> 16) & 0xFFF0, a = (v1 << 12) >> 8;
            sq_(a - 0x10, &ZQ); rc_sw(a - 0x10, 0x20000000u); rc_sw(a - 0xC, a + sz);
        }
    }
    {
        u32 p0 = M32(SPR + 0x3F90), p1 = M32(SPR + 0x3F94), p2 = M32(SPR + 0x3F98);
        rc_sw(p0, 0x20000000u); rc_sw(p1, 0x20000000u); rc_sw(p2, 0x20000000u);
        rc_sw(p0 + 4, p1 + 0x10); rc_sw(p1 + 4, p2 + 0x10); rc_sw(p2 + 4, p0 + 0x10);
    }
    rc_sw(gp + (u32)R->g_pkt2, fp);
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;
static ShRefs refs_of(const s32 *g, const u32 *a)
{
    ShRefs R;
    R.g_mtx = g[0]; R.g_cnt = g[1]; R.g_cap = g[2]; R.g_fade = g[3]; R.g_clock = g[4]; R.g_pkt = g[5];
    for (int k = 0; k < 6; k++) R.g_t[k] = g[6 + k];
    R.g_mtx2 = g[12];
    for (int k = 0; k < 4; k++) R.g_b[k] = g[13 + k];
    R.g_mtx3 = g[17]; R.g_pkt2 = g[18];
    R.classes = a[0]; R.sine = a[1]; R.sway = a[2]; R.ret1 = a[3]; R.cam = a[4]; R.frust = a[5]; R.mpg_qwc = a[6]; R.mpg_addr = a[7];
    R.consts = a[8]; R.head = a[9]; R.close = a[10]; R.classes2 = a[11]; R.bstart = a[12]; R.cam2 = a[13]; R.frust2 = a[14];
    R.corners = a[15]; R.classes3 = a[16]; R.classes4 = a[17]; R.bstate = a[18]; R.head2 = a[19]; R.mpg2_qwc = a[20];
    R.mpg2_addr = a[21]; R.consts2 = a[22]; R.cam3 = a[23]; R.ret2 = a[24]; R.classes5 = a[25];
    return R;
}

/* check mode: the state the routine reads and writes, put aside and compared */
#define WIN 0x100000u
#define MAXCLS 1024
static struct { u8 spr[0x4000]; u8 *win; u32 fp0, fp1; u32 ncls, cls[MAXCLS], cnt[MAXCLS][2]; RcVu0 vu0; u32 pkt; int valid; } A, N;
static u32 chk_gp_pkt;
static void grab(typeof(A) *s, u32 fp0, const ShRefs *R, u32 ncls, u32 gp)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    if (!s->win) s->win = malloc(WIN);
    u32 n = fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - fp0;
    memcpy(s->win, (void *)(uintptr_t)fp0, n);
    s->fp0 = fp0;
    s->ncls = ncls < MAXCLS ? ncls : MAXCLS;
    for (u32 k = 0; k < s->ncls; k++) { s->cls[k] = M32(R->classes + 4 * k); s->cnt[k][0] = M32(s->cls[k] + 0x34); s->cnt[k][1] = M16(s->cls[k] + 0x38); }
    s->vu0 = rc_vu0;
    s->pkt = M32(gp);
    s->valid = 1;
}
static void put_back(const typeof(A) *s, u32 gp)
{
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    u32 n = s->fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - s->fp0;
    memcpy((void *)(uintptr_t)s->fp0, s->win, n);
    for (u32 k = 0; k < s->ncls; k++) { M32(s->cls[k] + 0x34) = s->cnt[k][0]; M16(s->cls[k] + 0x38) = (u16)s->cnt[k][1]; }
    rc_vu0 = s->vu0;
    rc_sw(gp, s->pkt);
}
static ShRefs chk_R;
static u32 chk_ncls;

int uya_shrubproc_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (dec_test < 0) dec_test = getenv("UYA_SHRUBDEC") != 0;
    if (mode_ < 0) { const char *e = getenv("UYA_NSHRUBPROC"), *n = getenv("UYA_NATIVE"); mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2; uya_log("native ShrubProc: %s", mode_ == 0 ? "off (the game's routine)" : mode_ == 1 ? "check against the game's routine" : "on"); }
    if (!mode_) return 0;
    if (ng != 19 || na != 26) { static int told; if (!told++) uya_log("native ShrubProc: the level's routine has %d / %d data references (not 19 / 26): the game's routine runs", ng, na); return 0; }
    ShRefs R = refs_of(g, a);
    if (mode_ >= 2) {
        direct = mode_ == 2;
        run(c, &R);
        { static unsigned long long nr; if (direct && ++nr % 300 == 0) uya_log("native ShrubProc: %llu runs; the main and fading parts direct %llu, in the chain %llu", nr, st_ddirect, st_dchain); }
        return 1;
    }
    direct = 0;
    const u32 gp = (u32)c->r[28].d[0];
    u32 fp0 = M32(gp + (u32)R.g_pkt);
    chk_gp_pkt = gp + (u32)R.g_pkt2;
    chk_R = R;
    chk_ncls = M32(gp + (u32)R.g_cnt);
    RcCtx c0 = *c;
    grab(&A, fp0, &R, chk_ncls, chk_gp_pkt);
    run(c, &R);
    grab(&N, fp0, &R, chk_ncls, chk_gp_pkt);
    N.fp1 = M32(chk_gp_pkt);
    put_back(&A, chk_gp_pkt);
    *c = c0;
    return 0;
}

void uya_shrubproc_exit(RcCtx *c)
{
    (void)c;
    if (mode_ != 1 || !N.valid) return;
    N.valid = 0;
    n_runs++;
    static typeof(A) G;
    grab(&G, N.fp0, &chk_R, chk_ncls, chk_gp_pkt);
    G.fp1 = M32(chk_gp_pkt);
    int bad = 0;
    char why[256] = "";
    if (G.fp1 != N.fp1) { bad = 1; snprintf(why, sizeof why, "packet pointer %08X, native %08X", G.fp1, N.fp1); }
    else if (memcmp(G.win, N.win, G.fp1 - G.fp0 <= WIN ? G.fp1 - G.fp0 : WIN)) {
        u32 k = 0; while (G.win[k] == N.win[k]) k++;
        const u32 *g = (const u32 *)(G.win + (k & ~15u)), *q = (const u32 *)(N.win + (k & ~15u));
        bad = 1; snprintf(why, sizeof why, "chain differs at +0x%X (of 0x%X): game %08X %08X %08X %08X, native %08X %08X %08X %08X", k, G.fp1 - G.fp0,
                          g[0], g[1], g[2], g[3], q[0], q[1], q[2], q[3]);
    } else if (memcmp(G.spr, N.spr, 0x4000)) {
        u32 k = 0; while (G.spr[k] == N.spr[k]) k++;
        bad = 1; snprintf(why, sizeof why, "scratchpad differs at 0x%04X", k);
    } else if (memcmp(G.cnt, N.cnt, sizeof G.cnt[0] * G.ncls)) {
        u32 k = 0; while (!memcmp(G.cnt[k], N.cnt[k], sizeof G.cnt[0])) k++;
        bad = 1; snprintf(why, sizeof why, "class %u's counts %08X %04X, native %08X %04X", k, G.cnt[k][0], G.cnt[k][1], N.cnt[k][0], N.cnt[k][1]);
    } else if (memcmp(G.vu0.vf, N.vu0.vf, sizeof G.vu0.vf) || memcmp(G.vu0.acc, N.vu0.acc, sizeof G.vu0.acc) || G.vu0.clip != N.vu0.clip ||
               memcmp(&G.vu0.q, &N.vu0.q, 4) || G.vu0.mac != N.vu0.mac || G.vu0.status != N.vu0.status) {
        int r = 0; while (r < 32 && !memcmp(G.vu0.vf[r], N.vu0.vf[r], 16)) r++;
        u32 g[4] = { 0 }, q[4] = { 0 };
        if (r < 32) { memcpy(g, G.vu0.vf[r], 16); memcpy(q, N.vu0.vf[r], 16); }
        bad = 1; snprintf(why, sizeof why, "VU0 registers differ (first vf%d: game %08X %08X %08X %08X, native %08X %08X %08X %08X; mac %04X / %04X, status %03X / %03X)",
                          r, g[0], g[1], g[2], g[3], q[0], q[1], q[2], q[3], G.vu0.mac, N.vu0.mac, G.vu0.status, N.vu0.status);
    }
    /* UYA_SHRUBDBG=n: every differing chain qword of run n logged */
    if (bad && getenv("UYA_SHRUBDBG") && (unsigned long long)atoll(getenv("UYA_SHRUBDBG")) == n_runs) {
        u32 len = G.fp1 - G.fp0 <= WIN ? G.fp1 - G.fp0 : WIN;
        for (u32 k = 0, shown = 0; k < len && shown < 40; k += 16) {
            if (!memcmp(G.win + k, N.win + k, 16)) continue;
            const u32 *g = (const u32 *)(G.win + k), *q = (const u32 *)(N.win + k);
            uya_log("shrub dbg: +0x%X game %08X %08X %08X %08X native %08X %08X %08X %08X", k, g[0], g[1], g[2], g[3], q[0], q[1], q[2], q[3]);
            shown++;
        }
    }
    if (bad) { n_bad++; if (n_bad <= 10) uya_log("native ShrubProc check: run %llu differs: %s", n_runs, why); }
    else n_ok++;
    if (n_runs % 500 == 0 || n_runs == 100)
        uya_log("native ShrubProc check: %llu runs, %llu identical, %llu differ; instances %llu: retried %llu, far %llu, near %llu, side %llu; "
                "to the clip list %llu (full %llu), clip draws %llu; billboards %llu, partly %llu (list full %llu), drawn %llu + %llu; plain %llu (full %llu), fading %llu (full %llu); "
                "sway %llu (too far %llu); classes skipped %llu, empty %llu",
                n_runs, n_ok, n_bad, st_inst, st_retry, st_far, st_near, st_side, st_clip, st_clipfull, st_clipdraw, st_bb, st_bbpart, st_bbfull,
                st_bbdraw[0], st_bbdraw[1], st_plain, st_plainfull, st_fade, st_fadefull, st_sway, st_swayfar, st_skip, st_empty);
}
