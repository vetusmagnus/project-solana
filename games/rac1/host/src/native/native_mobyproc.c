/* MobyProc (the game's moby culling, level of detail, lighting, animation records and chain
 * building, hand-written assembly in the game: two catalogue entries that jump into each other,
 * level 0 func_L00_002569F8 (3664 bytes) and func_L00_00257848 (1488 bytes), 5152 bytes in all,
 * one copy in every level) as native C.
 *
 * Called as MobyProc(a0 = the first moby (0x100 bytes each), a1 = where the animation records go,
 * a2 = the moby count (-1: up to the moby whose state is 0xFF), a3 = the deferral pass on); it
 * returns the records' end + 0x10. What the game's routine does, in order:
 *   - setup: saves s0..s7, sp, fp, ra in the scratchpad (0x3FA8..); DMAs (channel 9, to the
 *     scratchpad) the level's 16 light sets (64 bytes each) and 8 point lights (32 bytes each) to
 *     0x2000; clears the per-class LOD counters (0x2600..0x27FF); copies the visibility mask table
 *     (128 bytes) to 0x3800 and the shadow planes (64 bytes) to 0x3300; the VIF unpack template
 *     (gp) to 0x3F40, the four GS-state qwords to 0x3F00..0x3F30, the six chain templates (gp) to
 *     0x3E80..0x3ED0 and the extra pass's offset to 0x3EE0; loads the camera (position * 1024 in
 *     vf24 with w 1024, rotation rows vf25..vf27, the view-projection matrix vf28..vf31) and the
 *     frustum constants (vf19..vf23); writes the chain's start: a CNT with the VU1 program's
 *     MSCAL 0, a REF to the program's setup (4 qwords) and the per-buffer constants (6 qwords,
 *     twice, at VU1 0x123 and 0x287). Then the point lights are packed in place: those with a
 *     zero dword at +0x10 dropped, the others' position and range scaled by 1024 and their
 *     colour's w the reciprocal of the range; their count kept in s6's upper half.
 *   - pass 1, per moby: state 0x80..0xFE skipped, 0xFF ends the pass. Its bounding sphere to
 *     camera space; culled (its drawn flag, +0x31, cleared) if its mask byte (+0x36) and the
 *     table's byte for its group (+0x37) have no bit in common, if it is hidden (flags & 0x81),
 *     beyond its draw distance (+0x32 of it, capped by the gp word), in front of the near plane
 *     or outside a side plane. A moby with flags & 0xC00 when the deferral pass is on goes to the
 *     deferral list (0x3000, 16 bytes each, up to 0x3300) instead: those with flags & 0x800 are
 *     kept for pass 2; the others' shadow sphere (projected on the shadow plane of their byte
 *     0xBD) is culled the same way and gives the entry's fade and edge flag (the shadow list), and
 *     the moby itself is drawn now if it was not culled. Else its drawn flag is set and it is
 *     drawn: the program entry with clipping (VU1 0x50) unless the sphere is wholly inside the
 *     guard band (0x70).
 *   - the draw, per moby (also for pass 2's entries): a record in the scratchpad (0x2800 or 0x2C00,
 *     the halves taken in turn): its class's nearest distance kept (0x3A00 + class slot * 4); its
 *     colour (+0x3C, alpha faded by +0x23 and its distance) as four floats at +0xB0; its matrix
 *     (scaled by +0x2C) through the view-projection matrix (vf15..vf18, the rows VU1 gets at
 *     0x129); its fade (+0x73) for the extra pass; its level of detail (+0x72: near or far, counted
 *     in 0x2600 + slot * 2) choosing the class's packet table; its animation frames (+0x50..+0x6C,
 *     the class's +8/+9: current and previous frame at +0x20 and +0x30); its lights: one light set
 *     (+0x38) or two blended (+0x3A), the directions through the object's inverse rotation at
 *     +0x40..+0x68, the colours at +0x70/+0x80; the point lights in range, one or the sum of
 *     several (+0x48..+0x68, colour +0x90). A moby with flags & 0x10 gets an entry in the colour
 *     patch list (0x3400, up to 0x3800). Then per packet of its table: the matrix (the first two
 *     packets, into the VU1 buffer in use), the unpack tags with a hole the skin driver fills
 *     later (its address in the record's list at +0xC0), a REF to the packet's data, the GS state
 *     before and after the first packet when the flags ask for it (0x208), an MSCAL. With a fade
 *     (+0x73) the moby is drawn a second time with a turned matrix (the extra pass, its own list
 *     after the record). The record goes to main memory at a1 (DMA channel 8, from the
 *     scratchpad), a1 moving on.
 *   - pass 2 (when the deferral pass is on): a CNT whose following qword is kept free for the
 *     shadows (its address to gp), then each kept deferral entry is drawn as above.
 *   - the end: the colour patch list copied out (its size to gp), the shadow list built from the
 *     deferral list (entries with a non-zero second word; its count to gp), a closing CNT, the
 *     packet pointer saved, a zero record as the records' terminator.
 *   The routine stops a pass early when the chain reaches the packet limit (gp).
 *
 * This file does the same, word for word: every store (scratchpad, chain, records, the mobys'
 * drawn flags, the lists, the gp words, DMA registers), every VU0 macro operation with its result
 * as the translated game code has it (each product and sum clamped to +-MAX, results into
 * temporaries before they are written), in the routine's order. This routine is translated
 * without VU0 flags (it has no cfc2): the MAC and status flags are neither computed nor kept
 * here. Where a GPR's upper 64 bits reach a VU register (qmtc2) or a 128-bit store they are
 * carried too (ath, v0h, v1h, a0h, t3h; the routine's own upper-half state in s5h, s6h, sph,
 * t6h, t7h). The data addresses differ per level: tools/recomp.py passes the level's own (its gp
 * offsets and its address pairs, read from the level's bytes, both pieces) to the hook it puts at
 * the routine's start.
 *
 * UYA_NMOBYPROC: 0 the game's routine (translated; the default while this file is new); 1 check
 * (this file runs first, its results are put aside, the game's routine runs from the same state,
 * and the two are compared at its end: scratchpad, chain, records, drawn flags, the two lists'
 * copies, the gp words, the return value, the VU0 registers); 2 this file. With 2, UYA_NMOBYDIRECT
 * (native_mobyseg.c) 1 or 2 also has the call record its draws where it writes them into the chain
 * (the D_ sites below), each run of them a segment the chain walker draws at the run's marker. */
#include "rc.h"
#include "uya_host.h"
#include "native_moby.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void uya_log(const char *fmt, ...);

#define SPR 0x70000000u
static float fc(float x) { return rc_fc(x); }
/* the square root as the translated game code has it: built with -fno-builtin, its rc_fsqrt is a
 * call of the C library's sqrtf (on 32-bit Linux an x87 fsqrt, rounding to nearest), not the SSE
 * sqrtss this file would inline (rounding as MXCSR says: toward zero) */
float sqrtf(float);
static float (*volatile lib_sqrtf)(float) = sqrtf;
static float vsqrt(float x) { return lib_sqrtf(__builtin_fabsf(x)); }

/* the level's data, from the hook's arguments */
typedef struct {
    s32 g_pkt, g_unpack, g_lim, g_cap, g_t[6], g_off, g_cpsz, g_shcnt, g_pkt2, g_slot;   /* gp offsets */
    u32 lights, mask, planes, cam, frust, gs, ref, consts, cpl, shl;
} MbRefs;

/* ---- the VU0 macro operations, as tools/recomp.py translates them (no flags: see above) ---- */
enum { ADD, SUB, MUL, MADD, MSUB, MAX, MIN };
enum { X = 8, Y = 4, Z = 2, W = 1, XY = 12, XZ = 10, XYZ = 14, XYZW = 15 };
#define ACC (-1)     /* destination: the accumulator */
#define BV (-1)      /* ft: every component (else 0..3 one component, BQ the Q register) */
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
    if (fd == 0) return;                                  /* vf0 stays */
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
}
/* d = rows a, b, c by v's x, y, z (ACC, then the last into d) */
static void mat3(int d, u32 m, int a, int b, int c, int v)
{
    fm(ACC, m, MUL, a, v, 0); fm(ACC, m, MADD, b, v, 1); fm(d, m, MADD, c, v, 2);
}
/* d = the view-projection rows (vf28..vf31) by v's x, y, z and (tv)'s component tc */
static void vp(int d, int v, int tv, int tc)
{
    fm(ACC, XYZW, MUL, 28, v, 0); fm(ACC, XYZW, MADD, 29, v, 1); fm(ACC, XYZW, MADD, 30, v, 2); fm(d, XYZW, MADD, 31, tv, tc);
}
static void vabs_xy(int d) { f32 a = __builtin_fabsf(VF(d)[0]), b = __builtin_fabsf(VF(d)[1]); VF(d)[0] = a; VF(d)[1] = b; }
static void itof(int d, u32 m, int s, int sh) { f32 t[4]; for (int k = 0; k < 4; k++) t[k] = rc_itof(*(s32 *)&VF(s)[k], sh); for (int k = 0; k < 4; k++) if (m & (8u >> k)) VF(d)[k] = t[k]; }
static void ftoi(int d, u32 m, int s, int sh) { s32 t[4]; for (int k = 0; k < 4; k++) t[k] = rc_ftoi(VF(s)[k], sh); for (int k = 0; k < 4; k++) if (m & (8u >> k)) memcpy(&VF(d)[k], &t[k], 4); }
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
/* qmtc2 from a GPR: its low doubleword and its upper one (writes to vf0 dropped: none here) */
static void qmtc2(int d, u64 lo, u64 hi) { u64 w[2] = { lo, hi }; memcpy(VF(d), w, 16); }
static u64 lo_of(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[0]; }
static u64 hi_of(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[1]; }
/* pminw / pmaxw on one doubleword (its two words compared signed) */
static u64 pmin2(u64 a, u64 b)
{
    s32 a0 = (s32)(u32)a, a1 = (s32)(u32)(a >> 32), b0 = (s32)(u32)b, b1 = (s32)(u32)(b >> 32);
    return (u64)(u32)(a0 < b0 ? a0 : b0) | (u64)(u32)(a1 < b1 ? a1 : b1) << 32;
}
static u64 pmax2(u64 a, u64 b)
{
    s32 a0 = (s32)(u32)a, a1 = (s32)(u32)(a >> 32), b0 = (s32)(u32)b, b1 = (s32)(u32)(b >> 32);
    return (u64)(u32)(a0 > b0 ? a0 : b0) | (u64)(u32)(a1 > b1 ? a1 : b1) << 32;
}

/* ---- the chain ---- */
static int busy(u32 ch) { return (M32(ch) & 0x100) != 0; }
static void wait_idle(u32 ch) { for (int k = 0; k < 1000000 && busy(ch); k++) ; }
static const R128 ZQ = {{ 0, 0 }};
/* a CNT with a VIF FLUSHA */
static void emit_close(u32 *sp)
{
    rc_sq(&ZQ, *sp); rc_sw(*sp, 0x10000000u); rc_sw(*sp + 12, 0x13000000u); *sp += 16;
}

/* path counts (logged by check mode; a path at 0 is untested) */
static unsigned long long st_moby, st_skip, st_mask, st_hidden, st_far, st_near, st_side, st_guard, st_clip, st_draw1,
    st_defer, st_dside, st_dfull, st_d800, st_dnow, st_shadow, st_shcull, st_edge, st_p2, st_p2skip, st_full,
    st_lod[2], st_frames, st_seq, st_one, st_blend, st_pl[3], st_cp, st_cpfull, st_cpnone, st_extra, st_gs, st_pkt, st_xpkt;

/* ---- the direct draws (UYA_NMOBYDIRECT, native_mobyseg.c) ----
 * dmode_: this call's (0 none, 1 check, 2 direct), set by the hook; dcall: the call records (its
 * templates as native_mobyseg.c reads the draws); the records are made at the D_ sites, where the
 * chain items they stand for are written. direct_ok: the scratchpad templates (copied from the
 * level's gp data at the setup), the GS block's tag and the program's setup block (read through the
 * level's own addresses) with the values the drawer's fixed formats assume; once per call, else the
 * whole call goes into the chain. */
static int dmode_, dcall;
static int direct_ok(const MbRefs *R)
{
    static const u32 want[8][4] = {
        { 0x10000004u, 0, 0x01000404u, 0x6C040000u },   /* 0x3F40 matrix / constants: CNT 4; STCYCL 4/4; UNPACK V4-32 x4, absolute */
        { 0x30000000u, 0, 0, 0x69008000u },              /* 0x3E80 positions: REF; NOP; UNPACK V3-16 signed, +TOPS 0 */
        { 0x20000000u, 0, 0, 0x6E00C061u },              /* 0x3E90 colours: NEXT; NOP; UNPACK V4-8 unsigned, +TOPS 0x061 */
        { 0x30000000u, 0, 0, 0x6E00C061u },              /* 0x3EA0 the extra pass's colours: REF; the same */
        { 0x20000000u, 0, 0, 0x750080C2u },              /* 0x3EB0 the extra pass's ST: NEXT; NOP; UNPACK V2-16 masked signed, +TOPS 0x0C2 */
        { 0x30000000u, 0, 0, 0 },                        /* 0x3EC0 the class packet: REF; NOP; NOP */
        { 0x10000000u, 0, 0, 0x14000000u },              /* 0x3ED0 the start: CNT 0; NOP; MSCAL */
        { 0x10000002u, 0, 0, 0x50000002u } };            /* 0x3F00 GS state: CNT 2; NOP; DIRECT 2 */
    static const u32 at[8] = { 0x3F40, 0x3E80, 0x3E90, 0x3EA0, 0x3EB0, 0x3EC0, 0x3ED0, 0x3F00 };
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 4; j++)
            if (M32(SPR + at[k] + 4 * (u32)j) != want[k][j]) {
                static int told; if (told++ < 4) uya_log("native MobyProc: the template at scratchpad 0x%04X word %d is %08X (not %08X): the call's draws go into the chain", at[k], j, M32(SPR + at[k] + 4 * (u32)j), want[k][j]);
                return 0;
            }
    for (u32 k = 0; k < 16; k++)
        if (M32(R->ref + 4 * k) != native_moby_setup_words[k]) {
            static int told; if (told++ < 4) uya_log("native MobyProc: the program's setup block (%08X) word %u is %08X (not %08X): the call's draws go into the chain", R->ref, k, M32(R->ref + 4 * k), native_moby_setup_words[k]);
            return 0;
        }
    return 1;
}

/* what the routine leaves in the registers the caller may read */
typedef struct { u64 v0, v1, s5h, s6h, sph, lo, hi; } Out;

static void run(RcCtx *c, const MbRefs *R, Out *o)
{
    const u32 gp = (u32)c->r[28].d[0];
    const u32 a0in = (u32)c->r[4].d[0], a1in = (u32)c->r[5].d[0], a2in = (u32)c->r[6].d[0];
    const u64 a3in = c->r[7].d[0], s7h = c->r[23].d[1];
    u64 lo_ = c->lo.d[0], hi_ = c->hi.d[0];
    /* the GPRs (low doublewords); the upper doublewords where they reach a VU register or a store */
    u64 at, v0, v1, a0, a1, a2, a3, t0, t1, t2, t3, t5, t6, t7, s3, s7;
    u64 ath = c->r[1].d[1], v0h = c->r[2].d[1], v1h = c->r[3].d[1], a0h = c->r[4].d[1], t3h = c->r[11].d[1], t6h = c->r[14].d[1];
    u64 t7h, s5h, s6h, sph;
    u32 fp, s5, sp, t8, t9, s1 = 0, s0 = 0;
    s64 s2 = 0, s4, ra;
    R128 q, q2, T1, T2, T3, T4, T5, T6;
    int bt;
    u32 d_hole = 0, d_cnt = 0, d_q0 = 0;                 /* (the direct draws' records) */

    /* ---- setup, 0x2569F8 ---- */
    rc_sd(SPR + 0x3FF8, c->r[31].d[0]); rc_sd(SPR + 0x3FF0, c->r[30].d[0]); rc_sd(SPR + 0x3FE8, c->r[29].d[0]);
    for (int k = 23; k >= 16; k--) rc_sd(SPR + 0x3FA8 + 8 * (u32)(k - 16), c->r[k].d[0]);
    fp = a0in - 0x100;
    s5 = (a2in << 8) + a0in;
    t9 = a1in;
    ra = a3in != 0 ? 0xC00 : 0;
    /* the light sets and point lights: DMA ch9 (to the scratchpad), 0x50 qwords to 0x2000 */
    rc_sw(0x1000D410u, R->lights); rc_sw(0x1000D480u, 0x2000); rc_sw(0x1000D420u, 0x50); rc_sw(0x1000D400u, 0x100);
    for (u32 p = SPR + 0x2600; p != SPR + 0x2800; p += 0x40) { rc_sq(&ZQ, p); rc_sq(&ZQ, p + 16); rc_sq(&ZQ, p + 32); rc_sq(&ZQ, p + 48); }
    for (u32 s = R->mask, d = SPR + 0x3800; d != SPR + 0x3880; s += 32, d += 32) { rc_lq(&q, s); rc_lq(&q2, s + 16); rc_sq(&q, d); rc_sq(&q2, d + 16); }
    for (u32 s = R->planes, d = SPR + 0x3300; d != SPR + 0x3340; s += 32, d += 32) { rc_lq(&q, s); rc_lq(&q2, s + 16); rc_sq(&q, d); rc_sq(&q2, d + 16); }
    a0h = q.d[1];
    sp = M32(gp + (u32)R->g_pkt);
    t7 = 0; t7h = SE32(SPR + 0x3400);                    /* pcpyld t7, t7, zero */
    rc_lq(&q, gp + (u32)R->g_unpack); at = q.d[0]; ath = q.d[1];
    sph = SE32(M32(gp + (u32)R->g_lim));                 /* pcpyld sp, v0, sp: the packet limit */
    rc_sq(&q, SPR + 0x3F40);
    fm(11, X, ADD, 0, 0, 3);
    fm(11, Y, SUB, 0, 0, 3);
    s7 = SE32(M32(gp + (u32)R->g_cap));
    s4 = 1;
    at = SE32(0x44800000u);
    qmtc2(1, at, ath);                                   /* (zw: the unpack template's upper words) */
    s5h = SE32(SPR + 0x3000);                            /* the deferral list's write pointer */
    /* the camera, the frustum */
    lqc2(24, R->cam + 0x140); lqc2(25, R->cam); lqc2(26, R->cam + 0x10); lqc2(27, R->cam + 0x20);
    lqc2(28, R->cam + 0x40); lqc2(29, R->cam + 0x50); lqc2(30, R->cam + 0x60); lqc2(31, R->cam + 0x70);
    fm(24, XYZ, MUL, 24, 1, 0);
    t8 = SPR + 0x2C00;
    fm(24, W, MUL, 0, 1, 0);
    lqc2(19, R->frust + 0xA0); lqc2(20, R->frust + 0xB0); lqc2(21, R->frust + 0x1D0); lqc2(22, R->frust + 0x1E0); lqc2(23, R->frust + 0x1F0);
    { R128 g[6];
      for (int k = 0; k < 4; k++) rc_lq(&g[k], R->gs + 16 * (u32)k);
      for (int k = 0; k < 4; k++) rc_sq(&g[k], SPR + 0x3F00 + 16 * (u32)k);
      v1h = g[0].d[1]; a0h = g[1].d[1];
      for (int k = 0; k < 6; k++) rc_lq(&g[k], gp + (u32)R->g_t[k]);
      for (int k = 0; k < 6; k++) rc_sq(&g[k], SPR + 0x3E80 + 16 * (u32)k);
      ath = g[0].d[1]; v0h = g[1].d[1]; v1h = g[2].d[1]; a0h = g[3].d[1]; }
    rc_lq(&q, gp + (u32)R->g_off); ath = q.d[1];
    rc_sq(&q, SPR + 0x3EE0);
    /* the chain's start: CNT + MSCAL 0, a REF to the program's setup, the per-buffer constants */
    d_q0 = sp;
    rc_sq(&ZQ, sp); rc_sw(sp, 0x10000000u); M16(sp + 14) = 0x1400; M16(sp + 12) = 0; sp += 16;
    rc_sq(&ZQ, sp); rc_sw(sp + 4, R->ref); rc_sw(sp, 0x30000004u); sp += 16;
    rc_lq(&q, SPR + 0x3F40); v0h = q.d[1];
    rc_sq(&q, sp);
    rc_sq(&q, sp + 0x70);
    M8(sp) = 6; M8(sp + 14) = 6; M8(sp + 0x70) = 6; M8(sp + 0x7E) = 6;
    M16(sp + 12) = 0x123; M16(sp + 0x7C) = 0x287;
    sp += 0xE0;
    { R128 k6[6];
      for (int k = 0; k < 6; k++) rc_lq(&k6[k], R->consts + 16 * (u32)k);
      for (int k = 0; k < 6; k++) rc_sq(&k6[k], sp - 0xD0 + 16 * (u32)k);
      for (int k = 0; k < 6; k++) rc_sq(&k6[k], sp - 0x60 + 16 * (u32)k);
      a0h = k6[0].d[1]; }
    dcall = dmode_ && direct_ok(R);
    if (dcall) {                                         /* D_: run A from q0: its MSCAL 0, the setup and the constants */
        native_mobyseg_call_begin();
        native_mobyseg_begin(d_q0);
        native_mobyseg_mscal(0x0000);
        native_mobyseg_setup(R->ref, sp - 0xD0, sp - 0x60);
    }
    wait_idle(0x1000D400u);
    at = SE32(0x44800000u); qmtc2(3, at, ath);           /* (zw: the offset qword's upper words) */
    at = SE32(0x3A800000u); qmtc2(4, at, ath);
    /* the point lights packed in place, 0x256CF0 */
    {
        u32 p = SPR + 0x2400, e = SPR + 0x2500, w = SPR + 0x2400;
        a0 = 0;
        for (;;) {
            a1 = M64(p + 16);                            /* (the delay slot: before the test) */
            if (p == e) break;
            p += 32;
            if (a1 == 0) continue;
            lqc2(1, p - 16);
            lqc2(2, p - 32);
            rc_vu0.q = fc(rc_fdiv(VF(4)[0], VF(1)[3]));
            fm(1, XYZW, MUL, 1, 3, 0);
            sqc2(1, w + 16);
            fm(2, W, MUL, 0, 0, BQ);
            sqc2(2, w);
            a0 = SE32((u32)a0 + 1);
            w += 32;
        }
        at = SE32(p);
    }
    s6h = a0;                                            /* pcpyld s6, a0, s6: the point light count */

    /* ---- pass 1, per moby, 0x256D50 ---- */
next_moby:
    v0 = (u64)(s64)(s16)M16(fp + 0x132);                 /* the next moby's draw distance */
    fp += 0x100;
    lqc2(1, fp);
    if (fp == s5) goto pass_end;
    at = (u64)(s64)(s8)M8(fp + 0x20);
    v0 = pmin2(v0, s7); v0h = pmin2(v0h, s7h);
    v0 <<= 42;
    fm(2, XYZ, SUB, 1, 24, BV);
    qmtc2(4, v0, v0h);
    if ((s64)at < 0) {                                   /* 0x257708: state 0x80.. */
        at = SE32((u32)at + 1);
        if (at != 0) { st_skip++; goto next_moby; }
        goto pass_end;                                   /* 0xFF: the list's end */
    }
    st_moby++;
    /* the culling */
    fm(3, XY, MUL, 11, 1, 3);
    itof(19, Y, 4, 0);
    a2 = M8(fp + 0x37);
    fm(ACC, XYZ, MUL, 25, 2, 0);
    a3 = M8(fp + 0x36);
    fm(ACC, XYZ, MADD, 26, 2, 1);
    a2 = SE32((u32)a2 + SPR);
    fm(2, XYZ, MADD, 27, 2, 2);
    a2 = M8((u32)a2 + 0x3800);
    fm(19, Y, SUB, 19, 1, 3);
    t0 = M8(fp + 0x31);
    fm(8, XY, MUL, 22, 1, 3);
    a3 &= a2;
    bt = a3 == 0; t6 = M16(fp + 0x34); if (bt) { st_mask++; goto culled; }
    fm(1, XY, ADD, 3, 2, 2);
    a3 = M8(fp + 0x22);
    fm(5, XY, MUL, 20, 2, 2);
    v0 = t6 & 0x81;
    bt = v0 != 0; vabs_xy(2); if (bt) { st_hidden++; goto culled; }
    fm(7, XY, MUL, 8, 22, 2);
    t1 = SE32((u32)a3 << 1);
    fm(3, XY, SUB, 19, 1, BV);
    a3 = SE32((u32)a3 << 2);
    fm(9, XY, MUL, 5, 21, BV);
    a3 = SE32((u32)a3 + SPR);
    fm(6, XY, SUB, 2, 8, BV);
    fm(8, XY, ADD, 2, 7, BV);
    a1 = SE32(0x40000000u);
    fm(4, XY, SUB, 1, 23, BV);
    a1 = a1 + a1;                                        /* daddu: 0x80000000, not sign-extended */
    fm(1, Z, MAX, 0, 2, BV);
    s3 = 10;
    at = lo_of(3); ath = hi_of(3);
    a2 = t6 & (u64)ra;
    fm(7, XY, SUB, 5, 6, BV);
    fm(8, XY, SUB, 9, 8, BV);
    v0 = at << 32;
    bt = (s64)at < 0; a0 = lo_of(4); a0h = hi_of(4); if (bt) { st_far++; goto culled; }          /* beyond its draw distance */
    bt = (s64)v0 >= 0; { f32 y = VF(1)[1], z = VF(1)[2]; VF(1)[0] = y; VF(1)[1] = z; } if (bt) { st_near++; goto culled; }
    bt = a2 != 0; v0 = lo_of(7); v0h = hi_of(7); if (bt) goto defer;
    v1 = lo_of(8); v1h = hi_of(8);
    a0 ^= a1;
    bt = (s64)v0 < 0; v0 <<= 32; if (bt) { st_side++; goto culled; }                           /* outside a side plane */
    bt = (s64)v0 < 0; ftoi(1, XY, 1, 0); if (bt) { st_side++; goto culled; }
    v1 |= a0;
    ftoi(3, Y, 3, 0);
    bt = (s64)v1 < 0; v1 <<= 32; if (bt) goto drawn_flag;                                      /* crossing the guard band */
    if ((s64)v1 < 0) goto drawn_flag;
    s3 = 14;
drawn_flag:                                              /* 0x256E70 */
    if (s3 == 14) st_guard++; else st_clip++;
    st_draw1++;
    bt = t0 != 0; at = 1; if (!bt) M8(fp + 0x31) = 1;

    /* ---- the draw, 0x256E80 (pass 1, the deferral path, pass 2) ---- */
draw:
    at = SE32(M32((u32)a3 + 0x3A00));
    s2 = 2;
    lqc2(18, fp + 0x10);
    v0 = SE32(0x14000000u);
    a0 = SE32(M32(fp + 0x2C));
    s3 |= v0;
    v0 = lo_of(1); v0h = hi_of(1);
    fm(18, XYZ, MUL, 18, 24, 3);
    t8 ^= 0x400;                                         /* the other record half */
    qmtc2(10, a0, a0h);                                  /* (only vf10.x is read: the scale) */
    v0 = pmin2(v0, at); v0h = pmin2(v0h, ath);
    rc_sw((u32)a3 + 0x3A00, (u32)v0);                    /* the class's nearest distance */
    t2 = M64(fp + 0x38);
    at = lo_of(3); ath = hi_of(3);
    fm(18, XYZ, SUB, 18, 24, BV);
    v1 = M8(fp + 0x23);
    at >>= 39;
    v0 = SE32(0x80u);
    at = pmin2(at, v0); ath = pmin2(ath, v0h);
    { s64 p = (s64)(s32)(u32)at * (s32)(u32)v1; lo_ = SE32((u32)p); hi_ = SE32((u32)((u64)p >> 32)); }
    a0 = SE32(0x47800000u);
    rc_sq(&ZQ, t8 + 0x40);
    t6 &= 0x208;
    rc_sq(&ZQ, t8 + 0x50);
    at = (s64)at < 128;
    rc_sq(&ZQ, t8 + 0x60);
    at = SE32((u32)at << 3);
    rc_sq(&ZQ, t8 + 0x70);
    t6 |= at;                                            /* bit 3: fading */
    rc_sq(&ZQ, t8 + 0x80);
    a0 = 0x4780000047800000ull; a0h = 0;                 /* pextlw a0, a0, a0 */
    rc_sq(&ZQ, t8 + 0x90);
    a0h = 0x4780000047800000ull;                         /* pextlw again: 65536.0f four times */
    v1 = lo_;
    v0 = t2 >> 32;
    v1 = SE32((u32)v1 >> 7);
    v1 = SE32((u32)v1 << 24);
    rc_sq(&ZQ, t8 + 0xA0);
    v0 |= v1; v0h |= v1h;
    {   /* pextlb, pextlh (the low four bytes to words), por with 65536.0f */
        u32 w = (u32)v0;
        q.w[0] = 0x47800000u | (w & 0xFF); q.w[1] = 0x47800000u | ((w >> 8) & 0xFF);
        q.w[2] = 0x47800000u | ((w >> 16) & 0xFF); q.w[3] = 0x47800000u | (w >> 24);
        v0 = q.d[0]; v0h = q.d[1];
        rc_sq(&q, t8 + 0xB0);
    }
    at = sph; ath = 0;                                   /* pcpyud at, sp, zero: the limit */
    lqc2(15, fp + 0xC0);
    lqc2(16, fp + 0xD0);
    at = SE32((u32)at - sp);
    bt = (s64)at < 0; lqc2(17, fp + 0xE0); if (bt) { st_full++; goto pass_end; }               /* the chain is full */
    /* the object's inverse rotation (negated transpose; .w never written) and its matrix */
    fm(12, X, SUB, 0, 15, 0);
    fm(13, Y, SUB, 0, 16, 1);
    fm(14, Z, SUB, 0, 17, 2);
    fm(13, X, SUB, 0, 15, 1);
    fm(14, X, SUB, 0, 15, 2);
    fm(12, Y, SUB, 0, 16, 0);
    fm(14, Y, SUB, 0, 16, 2);
    fm(12, Z, SUB, 0, 17, 0);
    fm(13, Z, SUB, 0, 17, 1);
    fm(15, XYZ, MUL, 15, 10, 0);
    fm(16, XYZ, MUL, 16, 10, 0);
    fm(17, XYZ, MUL, 17, 10, 0);
    vp(15, 15, 15, 3);
    vp(16, 16, 16, 3);
    vp(17, 17, 17, 3);
    vp(18, 18, 0, 3);
    s1 = M32(fp + 0x24);                                 /* the class */
    t7 = M8(fp + 0x73);
    t1 = SE32((u32)t1 + SPR);
    at = M8(fp + 0x72);
    t1 = SE32((u32)t1 + 0x2600);
    t0 = lo_of(1);
    if (t7 != 0) {                                       /* 0x257B58: the extra pass's fade */
        v1 = SE32((u32)t7 << 10);
        a0 = SE32((u32)t0);
        v1 = SE32((u32)v1 - (u32)a0);
        bt = (s64)v1 <= 0; t7 = 0;
        if (!bt) {
            v1 = SE32((u32)v1 >> 7);
            a0 = SE32(0x80u);
            v1 = pmin2(v1, a0); v1h = pmin2(v1h, a0h);
            t7 = SE32((u32)v1);
        }
    }
    /* the level of detail, 0x257008 */
    v1 = SE32(M32(fp + 0x54));
    a0 = SE32(0x30000u);
    t0 = pmin2(t0, a0);
    a0 = SE32(M32(fp + 0x68));
    at = SE32((u32)at << 10);
    a1 = SE32(M32(fp + 0x6C));
    t0 = pmax2(t0, 0);
    a2 = SE32(M32(fp + 0x60));
    t0 = SE32((u32)t0);
    a3 = SE32(M32(fp + 0x64));
    at = SE32((u32)t0 - (u32)at);
    rc_sw(t8 + 4, (u32)v1);
    rc_sw(t8 + 8, (u32)a2);
    rc_sw(t8 + 12, (u32)a3);
    bt = (s64)at > 0; t3 = M32(fp + 0x50);               /* lwu */
    if (!bt) {
        st_lod[0]++;
        t0 = M8((u32)t1);
        t6h = 0;                                         /* pcpyld t6, zero, t6 */
        at = 0;
        v0 = M8(s1 + 4);
        t0 = SE32((u32)t0 + 1);
        a2 = M8(s1 + 8);
        M8((u32)t1) = (u8)t0;
    } else {
        st_lod[1]++;
        t0 = M8((u32)t1 + 1);
        a3 = 1;
        at = M8(s1 + 4);
        v0 = M8(s1 + 5);
        t0 = SE32((u32)t0 + 1);
        a2 = M8(s1 + 9);
        M8((u32)t1 + 1) = (u8)t0;
        t6h = a3;                                        /* pcpyld t6, a3, t6 */
    }
    /* the record's head, 0x257090 */
    {
        u64 t4;
        t4 = SE32((u32)t3 >> 16) & 0xFF;
        t0 = SE32(M32(s1 + 24));
        t1 = SE32(M32(s1 + 20));
        t5 = SE32((u32)t3 >> 24);
        s1 = M32(s1);                                    /* the packet table */
        rc_sw(t8, (u32)v0);
        at = SE32((u32)at << 4);
        rc_sw(t8 + 16, (u32)a2);
        v0 = SE32((u32)v0 << 4);
        rc_sw(t8 + 20, (u32)t7);
        s1 += (u32)at;
        rc_sw(t8 + 24, (u32)t0);
        s0 = s1 + (u32)v0;
        rc_sw(t8 + 28, (u32)t1);
        bt = a2 == 0; a2 = t3 & 0xFF;
        if (!bt) {
            /* the frames */
            st_frames++;
            bt = t4 != t5; a3 = SE32((u32)t3 >> 8);
            if (!bt) {
                a3 &= 0xFF;
                a2 = SE32((u32)a2 + 1);
                bt = a2 != a3; a2 = SE32(0x400u);
                if (!bt) { M16(t8 + 18) = (u16)a2; st_seq++; }
            }
            a2 = M16((u32)a0 + 6);
            a3 = SE32(M32((u32)a0 + 8));
            t0 = SE32(M32((u32)a0 + 12));
            a0 = SE32((u32)a0 + 16);
            rc_sw(t8 + 32, (u32)a0); rc_sw(t8 + 36, (u32)a2); rc_sw(t8 + 40, (u32)a3); rc_sw(t8 + 44, (u32)t0);
            a2 = M16((u32)a1 + 6);
            a3 = SE32(M32((u32)a1 + 8));
            t0 = SE32(M32((u32)a1 + 12));
            a1 = SE32((u32)a1 + 16);
            rc_sw(t8 + 48, (u32)a1); rc_sw(t8 + 52, (u32)a2); rc_sw(t8 + 56, (u32)a3); rc_sw(t8 + 60, (u32)t0);
        }
    }
    /* the lights, 0x257138 */
    t5 = M16(fp + 0x34);
    v1 = SE32((u32)t2 >> 16);
    at = t2 & 0xFF;
    v1 &= 0xFF;
    at = SE32((u32)at << 6);
    t5 &= 0x10;
    at = SE32((u32)at + SPR);
    if (v1 == 0) {
        /* one light set */
        st_one++;
        lqc2(1, (u32)at + 0x2010);
        lqc2(2, (u32)at + 0x2030);
        fm(ACC, XYZW, MUL, 12, 1, 0);
        lqc2(3, (u32)at + 0x2000);
        fm(ACC, XYZW, MADD, 13, 1, 1);
        lqc2(4, (u32)at + 0x2020);
        fm(1, XYZW, MADD, 14, 1, 2);
        sqc2(3, t8 + 112);
        fm(ACC, XYZW, MUL, 12, 2, 0);
        sqc2(4, t8 + 128);
        fm(ACC, XYZW, MADD, 13, 2, 1);
        a0 = SE32(M32((u32)at + 0x200C));
        fm(2, XYZW, MADD, 14, 2, 2);
        a1 = SE32(M32((u32)at + 0x202C));
        rc_sw(t8 + 160, (u32)a0);
        rc_sw(t8 + 164, (u32)a1);
        rc_sw(t8 + 124, 0);
        rc_sw(t8 + 140, 0);
        a0 = lo_of(1); a0h = hi_of(1);
        a2 = lo_of(2);
        { u64 a2h = hi_of(2);
          rc_sw(t8 + 64, (u32)a0);
          a1 = a0 >> 32;
          rc_sw(t8 + 80, (u32)a1);
          a0 = a0h;                                      /* pcpyud a0, a0, a0 */
          rc_sw(t8 + 96, (u32)a0);
          rc_sw(t8 + 68, (u32)a2);
          a3 = a2 >> 32;
          rc_sw(t8 + 84, (u32)a3);
          a2 = a2h;
          rc_sw(t8 + 100, (u32)a2); }
    } else {
        /* two light sets blended, 0x2571F0 */
        st_blend++;
        v1 = SE32((u32)v1 << 4);
        v0 = t2 & 0xFF00;
        a0 = SE32(0x1000u);
        v0 = SE32((u32)v0 >> 2);
        a0 = SE32((u32)a0 - (u32)v1);
        v0 = SE32((u32)v0 + SPR);
        fm(10, XYZ, ADD, 0, 0, 3);
        v1h = a0;                                        /* pcpyld v1, a0, v1 */
        qmtc2(9, v1, v1h);
        lqc2(1, (u32)at + 0x2010);
        lqc2(2, (u32)at + 0x2030);
        lqc2(3, (u32)at + 0x2000);
        lqc2(4, (u32)at + 0x2020);
        itof(9, XZ, 9, 12);
        lqc2(5, (u32)v0 + 0x2010);
        lqc2(6, (u32)v0 + 0x2030);
        lqc2(7, (u32)v0 + 0x2000);
        lqc2(8, (u32)v0 + 0x2020);
        fm(1, XYZ, MUL, 1, 9, 2);
        fm(2, XYZ, MUL, 2, 9, 2);
        fm(3, XYZW, MUL, 3, 9, 2);
        fm(4, XYZW, MUL, 4, 9, 2);
        fm(5, XYZ, MUL, 5, 9, 0);
        fm(6, XYZ, MUL, 6, 9, 0);
        fm(7, XYZW, MUL, 7, 9, 0);
        fm(8, XYZW, MUL, 8, 9, 0);
        fm(1, XYZ, ADD, 1, 5, BV);
        fm(2, XYZ, ADD, 2, 6, BV);
        fm(3, XYZW, ADD, 3, 7, BV);
        fm(4, XYZW, ADD, 4, 8, BV);
        fm(5, XYZ, MUL, 1, 1, BV);
        fm(6, XYZ, MUL, 2, 2, BV);
        sqc2(3, t8 + 112);
        sqc2(4, t8 + 128);
        fm(ACC, X, ADD, 5, 5, 1);
        fm(5, X, MADD, 10, 5, 2);
        fm(ACC, X, ADD, 6, 6, 1);
        fm(6, X, MADD, 10, 6, 2);
        rc_vu0.q = rc_fdiv(VF(0)[3], vsqrt(VF(5)[0]));
        mat3(1, XYZW, 12, 13, 14, 1);
        at = SE32(M32(t8 + 124));
        v0 = SE32(M32(t8 + 140));
        rc_sw(t8 + 124, 0);
        rc_sw(t8 + 140, 0);
        rc_sw(t8 + 160, (u32)at);
        rc_sw(t8 + 164, (u32)v0);
        fm(1, XYZ, MUL, 1, 0, BQ);
        rc_vu0.q = rc_fdiv(VF(0)[3], vsqrt(VF(6)[0]));
        mat3(2, XYZW, 12, 13, 14, 2);
        a0 = lo_of(1); a0h = hi_of(1);
        rc_sw(t8 + 64, (u32)a0);
        a1 = a0 >> 32;
        rc_sw(t8 + 80, (u32)a1);
        a0 = a0h;
        rc_sw(t8 + 96, (u32)a0);
        fm(2, XYZ, MUL, 2, 0, BQ);
        a0 = lo_of(2); a0h = hi_of(2);
        rc_sw(t8 + 68, (u32)a0);
        a1 = a0 >> 32;
        rc_sw(t8 + 84, (u32)a1);
        a0 = a0h;
        rc_sw(t8 + 100, (u32)a0);
    }
    /* the point lights in range, 0x257330 */
    lqc2(1, fp);
    v1 = s6h; v1h = 0;                                   /* pcpyud v1, s6, zero */
    bt = v1 == 0; t8 += 0xC0; if (bt) { st_pl[0]++; goto lights_done; }
    lqc2(2, SPR + 0x2410);
    v0 = SE32(SPR + 0x2400);
    at = SE32(SPR + 0x2500);
    a0 = 0;
    fm(3, XYZ, SUB, 2, 1, BV);
    fm(6, XYZ, ADD, 0, 0, 3);
    fm(7, XYZ, SUB, 0, 0, 3);
    for (;;) {                                           /* 0x257368 */
        bt = v1 == 0; fm(3, XYZ, MUL, 3, 3, BV); if (bt) break;
        fm(4, W, MUL, 2, 2, BV);
        v0 = SE32((u32)v0 + 32);
        lqc2(2, (u32)v0 + 16);
        v1 = SE32((u32)v1 - 1);
        fm(ACC, Y, ADD, 3, 3, 0);
        fm(ACC, Y, MADD, 6, 3, 2);
        fm(5, Y, MADD, 7, 4, 3);                          /* d^2 - range^2 */
        fm(8, Y, MADD, 0, 0, 0);                          /* d^2 */
        fm(3, XYZ, SUB, 2, 1, BV);
        a1 = lo_of(5);
        rc_vu0.q = vsqrt(VF(8)[1]);
        if ((s64)a1 > 0) continue;                       /* out of range ({vf5.x, vf5.y} as one dword: vf5.x stale) */
        lqc2(8, (u32)v0 - 16);
        lqc2(9, (u32)v0 - 32);
        fm(8, XYZ, SUB, 1, 8, BV);
        fm(5, W, MUL, 0, 0, BQ);
        fm(9, W, MUL, 9, 0, BQ);
        rc_vu0.q = fc(rc_fdiv(VF(0)[3], VF(5)[3]));
        fm(9, W, SUB, 0, 9, BV);
        mat3(8, XYZW, 12, 13, 14, 8);
        fm(9, XYZ, MUL, 9, 9, 3);
        fm(8, XYZ, MUL, 8, 0, BQ);
        sqc2(9, (u32)at);
        a0 = SE32((u32)a0 + 1);
        sqc2(8, (u32)at + 16);
        at = SE32((u32)at + 32);
    }
    bt = a0 == 0; at = 1; if (bt) { st_pl[0]++; goto lights_done; }
    bt = a0 != at; at = SE32(SPR + 0x2500);
    if (!bt) {
        /* one in range */
        st_pl[1]++;
        rc_lq(&q, (u32)at); v0 = q.d[0]; v0h = q.d[1];
        rc_lq(&q2, (u32)at + 16); v1 = q2.d[0]; v1h = q2.d[1];
        rc_sq(&q, t8 - 48);
        rc_sw(t8 - 36, 0);
        rc_sw(t8 - 120, (u32)v1);
        a0 = v1 >> 32;
        rc_sw(t8 - 104, (u32)a0);
        v1 = v1h;                                        /* pcpyud v1, v1, v1 */
        rc_sw(t8 - 88, (u32)v1);
    } else {
        /* several: their colours summed, their directions weighted */
        st_pl[2]++;
        fm(1, XYZ, ADD, 0, 0, BV);
        fm(ACC, XYZ, MUL, 0, 0, BV);
        do {
            lqc2(3, (u32)at);
            lqc2(4, (u32)at + 16);
            fm(1, XYZ, ADD, 1, 3, BV);
            at = SE32((u32)at + 32);
            fm(ACC, XYZ, MADD, 4, 3, 3);
            a0 = SE32((u32)a0 - 1);
            bt = a0 != 0; fm(2, XYZ, MADD, 0, 0, 0);
        } while (bt);
        fm(3, XYZ, MUL, 2, 2, BV);
        fm(ACC, X, ADD, 3, 3, 1);
        fm(3, X, MADD, 6, 3, 2);
        rc_vu0.q = rc_fdiv(VF(0)[3], vsqrt(VF(3)[0]));
        fm(2, XYZ, MUL, 2, 0, BQ);
        sqc2(1, t8 - 48);
        rc_sw(t8 - 36, 0);
        v1 = lo_of(2); v1h = hi_of(2);
        rc_sw(t8 - 120, (u32)v1);
        a0 = v1 >> 32;
        rc_sw(t8 - 104, (u32)a0);
        v1 = v1h;
        rc_sw(t8 - 88, (u32)v1);
    }
lights_done:                                             /* 0x2574F8 */
    rc_lq(&T1, SPR + 0x3F40); t1 = T1.d[0];
    if (t5 != 0) {
        /* 0x257AE8: an entry in the colour patch list */
        at = t7h; ath = 0;                               /* pcpyud at, t7, zero */
        v1 = SE32(M32(fp + 0x24));
        v0 = SE32(SPR + 0x3800);
        a2 = SE32(M32(fp + 0x90));
        bt = at == v0; a0 = t6h; a0h = 0;                /* pcpyud a0, t6, zero: the LOD */
        if (bt) { st_cpfull++; goto packets; }
        a3 = M8(t8 - 4);
        v1 = SE32((u32)v1 + (u32)a0);
        v1 = M8((u32)v1 + 10);
        a1 = SE32(s0 - s1);
        a0 = SE32((u32)v1 << 3);
        v1 = SE32((u32)v1 << 4);
        a1 = SE32((u32)a1 - (u32)v1);
        v1 = SE32(s1 + (u32)v1);
        bt = (s64)a1 <= 0; a1 = SE32((u32)a1 >> 4);
        if (bt) { st_cpnone++; goto packets; }
        rc_sw((u32)at, (u32)a2);
        a0 = SE32(t9 + (u32)a0);
        M8((u32)at + 3) = (u8)a3;
        rc_sw((u32)at + 4, (u32)v1);
        a0 = SE32((u32)a0 + 192);
        rc_sw((u32)at + 8, (u32)a0);
        rc_sw((u32)at + 12, (u32)a1);
        at = SE32((u32)at + 16);
        t7h = at;                                        /* pcpyld t7, at, t7 */
        st_cp++;
    }
packets:                                                 /* 0x257500 */
    rc_lq(&T2, SPR + 0x3E80); t2 = T2.d[0];
    rc_lq(&T3, SPR + 0x3E90); t3 = T3.d[0]; t3h = T3.d[1];
    rc_lq(&T4, SPR + 0x3EC0);
    rc_lq(&T5, SPR + 0x3ED0); t5 = T5.d[0];
    do {                                                 /* 0x257510, per packet */
        st_pkt++;
        bt = s2 <= 0; s4 = (s64)(s32)(0u - (u32)s4);
        if (!bt) {
            /* the matrix, into the VU1 buffer in use */
            rc_sq(&T1, sp);
            s2 = (s64)(s32)((u32)s2 - 1);
            sqc2(15, sp + 16); sqc2(16, sp + 32); sqc2(17, sp + 48); sqc2(18, sp + 64);
            at = s4 > 0 ? 653 : 297;
            M16(sp + 12) = (u16)at;
            if (dcall) native_mobyseg_mtx(sp + 16, (u32)at);    /* D_ */
            sp += 80;
        }
        at = SE32(M32(s1 + 8));
        v0 = SE32(M32(s1 + 12));
        a2 = SE32(sp + 32);
        d_hole = (u32)a2; d_cnt = (u32)v0;
        rc_sq(&T2, sp);
        at = SE32((u32)at << 4);
        rc_sq(&T3, sp + 16);
        v1 = v0 & 0xFF;
        at |= v1;
        v1 = SE32((u32)v0 >> 24);
        rc_sw(t8, (u32)at);                              /* the record's list: source << 4 | qwc, the hole */
        a0 = SE32((u32)v0 >> 8);
        rc_sw(t8 + 4, (u32)a2);
        a0 &= 0xFF;
        M8(sp + 14) = (u8)v1;
        a1 = SE32((u32)v0 >> 16);
        M8(sp + 30) = (u8)v1;
        a1 &= 0xFF;
        M16(sp) = (u16)a0;
        a0 = SE32((u32)a0 << 4);
        M16(sp + 16) = (u16)a1;
        a1 = SE32((u32)a1 << 4);
        t8 += 8;
        a2 = SE32((u32)a2 + (u32)a1);
        rc_sw(sp + 4, (u32)a2);
        a2 = SE32((u32)a2 + (u32)a0);
        rc_sw(sp + 20, (u32)a2);
        sp = (u32)a2;                                    /* (the hole left unwritten) */
        v0 = M64(s1);
        s1 += 16;
        rc_sq(&T4, sp);
        sp += 16;
        rc_sw(sp - 12, (u32)v0);
        v1 = v0 >> 32;
        bt = s2 <= 0; M16(sp - 16) = (u16)v1;
        if (dcall) native_mobyseg_pkt(0, d_hole, d_cnt, (u32)v0, (u32)(u16)v1);   /* D_: positions, colours, the class packet */
        if (!bt) {
            bt = t6 == 0; at = t6 & 0x200;
            if (!bt) {
                /* the GS state for the first packet */
                st_gs++;
                emit_close(&sp);
                rc_lq(&q, SPR + 0x3F00); v0 = q.d[0]; v0h = q.d[1];
                rc_lq(&q2, SPR + 0x3F10); v1 = q2.d[0]; v1h = q2.d[1];
                rc_sq(&q, sp);
                rc_sq(&q2, sp + 16);
                rc_lq(&q, SPR + 0x3F20);
                bt = at != 0; q.d[0] = 0x48;             /* ori a0, zero, 0x48: the low doubleword only */
                if (!bt) {
                    rc_lq(&q, SPR + 0x3F30);
                    v1 = SE32((u32)t6 << 4);
                    v1 = SE32((u32)v1 - 1536);
                    q.d[0] = SE32((u32)q.d[0] + (u32)v1);
                }
                a0 = q.d[0]; a0h = q.d[1];
                rc_sq(&q, sp + 32);
                sp += 48;
                if (dcall) native_mobyseg_gif(sp - 32, 2);   /* D_ */
            }
        }
        rc_sq(&T5, sp);
        sp += 16;
        M16(sp - 4) = (u16)s3;                           /* MSCAL 0x0A / 0x0E */
        if (dcall) native_mobyseg_mscal(((u32)s3 & 0xFFFF) * 8);   /* D_ */
    } while (s1 != s0);
    bt = t6 == 0; at = t6 & 0x200;
    if (!bt) {
        /* the GS state put back */
        emit_close(&sp);
        rc_lq(&q, SPR + 0x3F00); v0 = q.d[0]; v0h = q.d[1];
        rc_lq(&q2, SPR + 0x3F10); v1 = q2.d[0]; v1h = q2.d[1];
        rc_sq(&q, sp);
        rc_sq(&q2, sp + 16);
        bt = at != 0; rc_lq(&q, SPR + 0x3F20);
        if (!bt) rc_lq(&q, SPR + 0x3F30);
        a0 = q.d[0]; a0h = q.d[1];
        rc_sq(&q, sp + 32);
        sp += 48;
        if (dcall) native_mobyseg_gif(sp - 32, 2);           /* D_ */
    }
    bt = t7 != 0; rc_sd(t8, 0); if (bt) goto extra;      /* the list's end */
    at = SE32(0x1000D000u);
    t8 += 16;
record_out:                                              /* 0x2576A0: the record to main memory, DMA ch8 */
    wait_idle(0x1000D000u);
    a0 = t8 & 0x3FF;
    rc_sd(t8 - 8, 0);
    t8 &= 0x2C00;
    a0 = SE32((u32)a0 >> 4);
    rc_sw(0x1000D080u, t8);
    rc_sw(0x1000D020u, (u32)a0);
    a0 = SE32((u32)a0 << 4);
    rc_sw(0x1000D010u, t9);
    v0 = SE32(0x100u);
    rc_sw(0x1000D000u, 0x100);
    t9 += (u32)a0;
    t8 += SPR;
    if (ra >= 0) goto next_moby;
    goto pass2_next;

    /* ---- a culled moby, 0x2576F8 ---- */
culled:
    if (t0 != 0) M8(fp + 0x31) = 0;
    goto next_moby;

    /* ---- the extra pass (a near fade, +0x73), 0x257B88 ---- */
extra:
    st_extra++;
    lqc2(9, fp);
    a0 = t8 & 0x3FF;
    at = SE32(M32(fp + 0x24));
    a0 = SE32(t8 - (u32)a0);                             /* the record's start */
    v0 = M8((u32)at + 7);
    v1 = M8((u32)at + 6);
    t8 += 16;
    s1 = M32((u32)at);
    v0 = SE32((u32)v0 << 4);
    a1 = SE32((u32)v1 + 10);
    v1 = SE32((u32)v1 << 4);
    fm(9, XYZ, SUB, 9, 24, BV);
    a1 = SE32((u32)a1 >> 1);
    s1 += (u32)v0;
    M16((u32)a0 + 22) = (u16)a1;
    s0 = s1 + (u32)v1;
    t8 = (t8 >> 4) << 4;
    rc_lq(&T2, SPR + 0x3E80); t2 = T2.d[0];
    rc_lq(&T3, SPR + 0x3EA0); t3 = T3.d[0]; t3h = T3.d[1];
    fm(5, XYZ, MUL, 9, 9, BV);
    rc_lq(&T4, SPR + 0x3EB0);
    fm(6, XYZ, ADD, 0, 0, 3);
    rc_lq(&T5, SPR + 0x3EC0); t5 = T5.d[0];
    rc_lq(&T6, SPR + 0x3ED0); t6 = T6.d[0]; t6h = T6.d[1];
    fm(ACC, X, ADD, 5, 5, 1);
    fm(5, X, MADD, 6, 5, 2);
    lqc2(1, fp + 0xC0);
    lqc2(2, fp + 0xD0);
    lqc2(3, fp + 0xE0);
    rc_vu0.q = rc_fdiv(VF(0)[3], vsqrt(VF(5)[0]));
    mat3(1, XYZW, 25, 26, 27, 1);
    mat3(2, XYZW, 25, 26, 27, 2);
    mat3(3, XYZW, 25, 26, 27, 3);
    memcpy(VF(4), VF(0), 16);
    fm(5, XYZ, ADD, 0, 0, BV);
    fm(6, XYZ, ADD, 0, 0, BV);
    fm(7, XYZ, ADD, 0, 0, BV);
    fm(9, XYZ, MUL, 9, 0, BQ);
    mat3(9, XYZW, 25, 26, 27, 9);
    /* two rotations toward the camera (vf5..vf7 .w stale) */
    fm(5, X, ADD, 0, 0, 3);
    fm(6, Y, ADD, 0, 9, 2);
    fm(7, Y, SUB, 0, 9, 1);
    fm(6, Z, ADD, 0, 9, 1);
    fm(7, Z, ADD, 0, 9, 2);
    mat3(1, XYZW, 5, 6, 7, 1);
    mat3(2, XYZW, 5, 6, 7, 2);
    mat3(3, XYZW, 5, 6, 7, 3);
    fm(5, X, ADD, 0, 9, 2);
    fm(6, XYZ, ADD, 0, 0, BV);
    fm(7, X, SUB, 0, 9, 0);
    fm(5, Z, ADD, 0, 9, 0);
    fm(6, Y, ADD, 0, 0, 3);
    fm(7, Y, ADD, 0, 0, BV);
    fm(7, Z, ADD, 0, 9, 2);
    mat3(1, XYZW, 5, 6, 7, 1);
    mat3(2, XYZW, 5, 6, 7, 2);
    mat3(3, XYZW, 5, 6, 7, 3);
    sqc2(1, t8); sqc2(2, t8 + 16); sqc2(3, t8 + 32); sqc2(4, t8 + 48);
    t8 += 64;
    lqc2(1, SPR + 0x3EE0);
    rc_lq(&T1, SPR + 0x3F40); t1 = T1.d[0];
    fm(18, XYZW, ADD, 18, 1, BV);
    s2 = 2;
    s3 = SE32(0x1400000Au);
    do {                                                 /* 0x257D08, per packet */
        st_xpkt++;
        bt = s2 <= 0; s4 = (s64)(s32)(0u - (u32)s4);
        if (!bt) {
            rc_sq(&T1, sp);
            s2 = (s64)(s32)((u32)s2 - 1);
            sqc2(15, sp + 16); sqc2(16, sp + 32); sqc2(17, sp + 48); sqc2(18, sp + 64);
            at = s4 > 0 ? 653 : 297;
            M16(sp + 12) = (u16)at;
            if (dcall) native_mobyseg_mtx(sp + 16, (u32)at);    /* D_ */
            sp += 80;
        }
        at = SE32(M32(s1 + 8));
        v0 = SE32(M32(s1 + 12));
        rc_sq(&T2, sp);
        at = SE32((u32)at << 4);
        rc_sq(&T3, sp + 16);
        v1 = v0 & 0xFF;
        rc_sq(&T4, sp + 32);
        a2 = SE32(sp + 48);
        d_hole = (u32)a2; d_cnt = (u32)v0;
        at |= v1;
        v1 = SE32((u32)v0 >> 24);
        rc_sw(t8, (u32)at);
        a0 = SE32((u32)v0 >> 8);
        rc_sw(t8 + 4, (u32)a2);
        a0 &= 0xFF;
        M8(sp + 14) = (u8)v1;
        a1 = SE32((u32)v0 >> 16);
        M8(sp + 30) = (u8)v1;
        a1 &= 0xFF;
        M8(sp + 46) = (u8)v1;
        M16(sp) = (u16)a0;
        a0 = SE32((u32)a0 << 4);
        M16(sp + 16) = (u16)a1;
        M16(sp + 32) = (u16)a1;
        a1 = SE32((u32)a1 << 4);
        t8 += 8;
        a2 = SE32((u32)a2 + (u32)a1);
        rc_sw(sp + 4, (u32)a2);
        a2 = SE32((u32)a2 + (u32)a0);
        rc_sw(sp + 20, (u32)a2);
        a2 = SE32((u32)a2 + (u32)a1);
        rc_sw(sp + 36, (u32)a2);
        sp = (u32)a2;
        v0 = M64(s1);
        s1 += 16;
        rc_sq(&T5, sp);
        sp += 16;
        rc_sw(sp - 12, (u32)v0);
        v1 = v0 >> 32;
        M16(sp - 16) = (u16)v1;
        if (dcall) native_mobyseg_pkt(1, d_hole, d_cnt, (u32)v0, (u32)(u16)v1);   /* D_: positions, colours, ST, the class packet */
        rc_sq(&T6, sp);
        sp += 16;
        M16(sp - 4) = (u16)s3;
        if (dcall) native_mobyseg_mscal(((u32)s3 & 0xFFFF) * 8);   /* D_ */
    } while (s1 != s0);
    rc_sd(t8, 0);
    at = SE32(0x1000D000u);
    t8 += 16;
    goto record_out;

    /* ---- deferral (flags & 0xC00, the deferral pass on), 0x2578C8 ---- */
defer:
    st_defer++;
    s3 = 0;
    ftoi(3, Y, 3, 0);
    v1 = lo_of(8); v1h = hi_of(8);
    a0 ^= a1;
    bt = (s64)v0 < 0; v0 <<= 32; if (bt) goto d_side;
    bt = (s64)v0 < 0; ftoi(1, XY, 1, 0); if (bt) goto d_side;
    s3 = 10;
    bt = t0 != 0; v1 |= a0;
    if (!bt) { at = 1; M8(fp + 0x31) = 1; }
    bt = (s64)v1 < 0; v1 <<= 32; if (bt) goto d_entry;
    if ((s64)v1 < 0) goto d_entry;
    s3 = 14;
    goto d_entry;
d_side:                                                  /* 0x257918: outside a side plane (s3 stays 0) */
    st_dside++;
    if (t0 != 0) M8(fp + 0x31) = 0;
d_entry:                                                 /* 0x257928 */
    t5 = s5h;                                            /* pcpyud t5, s5, zero */
    t3 = lo_of(1); t3h = hi_of(1);
    v0 = SE32(SPR + 0x3300);
    bt = t5 == v0; v0 = M16(fp + 0x34); if (bt) { st_dfull++; goto culled; }                    /* the list is full */
    rc_sw((u32)t5, fp);
    M16((u32)t5 + 8) = (u16)t1;
    M16((u32)t5 + 10) = (u16)s3;
    v0 &= 0x800;
    bt = v0 != 0; rc_sw((u32)t5 + 12, (u32)t3); if (bt) goto d_800;
    a0 = SE32(M32(fp + 0x88));
    v1 = SE32(M32(fp + 0x84));
    v0 = M8(fp + 0xBD);
    bt = (s64)a0 <= 0;
    {   /* pextlw a0, a0, v1 */
        u64 lo = (u64)(u32)v1 | (u64)(u32)a0 << 32, hi = (u64)(u32)(v1 >> 32) | (u64)(u32)(a0 >> 32) << 32;
        a0 = lo; a0h = hi;
    }
    if (bt) goto d_now;
    /* the shadow sphere: the bounding sphere projected on the moby's shadow plane, culled the same way */
    st_shadow++;
    qmtc2(4, a0, a0h);
    v0 = SE32((u32)v0 << 4);
    at = M8(fp + 0x7F);
    v0 = SE32((u32)v0 + SPR);
    fm(4, XY, MUL, 4, 24, 3);
    v1 = SE32(0x3F000000u);
    qmtc2(8, v1, v1h);                                   /* (zw: vf8's own, from 0x2578D0) */
    lqc2(5, (u32)v0 + 0x3300);
    lqc2(6, fp);
    fm(7, Y, SUB, 4, 4, 0);
    fm(7, Y, MUL, 7, 8, 0);
    fm(1, W, ADD, 6, 7, 1);
    fm(4, X, ADD, 4, 7, 1);
    fm(4, X, SUB, 4, 6, 2);
    rc_vu0.q = fc(rc_fdiv(VF(4)[0], VF(5)[2]));
    fm(ACC, XYZ, MUL, 5, 0, BQ);
    fm(1, XYZ, MADD, 6, 0, 3);
    at <<= 42;
    fm(2, XYZ, SUB, 1, 24, BV);
    qmtc2(4, at, ath);
    fm(3, XY, MUL, 11, 1, 3);
    itof(19, Y, 4, 0);
    fm(ACC, XYZ, MUL, 25, 2, 0);
    fm(ACC, XYZ, MADD, 26, 2, 1);
    fm(2, XYZ, MADD, 27, 2, 2);
    fm(19, Y, SUB, 19, 1, 3);
    fm(8, XY, MUL, 22, 1, 3);
    fm(1, XY, ADD, 3, 2, 2);
    fm(5, XY, MUL, 20, 2, 2);
    vabs_xy(2);
    fm(7, XY, MUL, 8, 22, 2);
    fm(3, XY, SUB, 19, 1, BV);
    fm(9, XY, MUL, 5, 21, BV);
    fm(6, XY, SUB, 2, 8, BV);
    fm(8, XY, ADD, 2, 7, BV);
    a1 = SE32(0x40000000u);
    fm(4, XY, SUB, 1, 23, BV);
    a1 = a1 + a1;
    at = lo_of(3); ath = hi_of(3);
    VF(1)[0] = VF(1)[1];                                 /* vmr32.x */
    fm(7, XY, SUB, 5, 6, BV);
    fm(8, XY, SUB, 9, 8, BV);
    v0 = at << 32;
    t2 = 1;
    bt = (s64)at < 0; a0 = lo_of(4); a0h = hi_of(4); if (bt) goto d_shcull;
    bt = (s64)v0 >= 0; v0 = lo_of(7); v0h = hi_of(7); if (bt) goto d_shcull;
    v1 = lo_of(8); v1h = hi_of(8);
    a0 ^= a1;
    bt = (s64)v0 < 0; v0 <<= 32; if (bt) goto d_shcull;
    bt = (s64)v0 < 0; v1 |= a0; if (bt) goto d_shcull;
    ftoi(1, X, 1, 0);
    bt = (s64)v1 < 0; v1 <<= 32; if (bt) goto d_shadow;
    if ((s64)v1 < 0) goto d_shadow;
    t2 = 0;
d_shadow:                                                /* 0x257A78: the entry's fade and edge flag */
    if (t2) st_edge++;
    v1 = M8(fp + 0x7F);
    a0 = lo_of(1); a0h = hi_of(1);
    v1 = SE32((u32)v1 << 10);
    a0 = SE32((u32)a0);
    v1 = SE32((u32)v1 - (u32)a0);
    v1 = SE32((u32)v1 >> 1);
    a0 = SE32(0x1000u);
    v1 = pmin2(v1, a0); v1h = pmin2(v1h, a0h);
    M16((u32)t5 + 6) = (u16)t2;
    M16((u32)t5 + 4) = (u16)v1;
    t5 = SE32((u32)t5 + 16);
    s5h = t5;                                            /* pcpyld s5, t5, s5 */
    goto next_moby;
d_800:                                                   /* 0x257AB0: kept for pass 2 */
    bt = s3 == 0; M16((u32)t5 + 6) = 0; if (bt) goto next_moby;
    st_d800++;
    M16((u32)t5 + 4) = 0;
    t5 = SE32((u32)t5 + 16);
    s5h = t5;
    goto next_moby;
d_shcull:
    st_shcull++;
d_now:                                                   /* 0x257AC8: the moby drawn now */
    bt = s3 == 0; at = (u64)(s64)(s16)M16(fp + 0x32); if (bt) goto next_moby;
    st_dnow++;
    at = SE32((u32)at << 10);
    at = SE32((u32)at - (u32)t3);
    at <<= 32;
    qmtc2(1, t3, t3h);
    qmtc2(3, at, ath);
    goto draw;

    /* ---- the end of a pass, 0x257718 ---- */
pass_end:
    if (ra > 0) goto pass2;
    goto finish;

    /* ---- pass 2, 0x257848: the kept deferral entries ---- */
pass2:
    if (dcall) native_mobyseg_close(sp);                 /* D_: run A ends at this CNT+FLUSHA */
    emit_close(&sp);
    rc_sw(gp + (u32)R->g_slot, sp);                      /* the qword kept for the shadows */
    sp += 16;
    if (dcall) native_mobyseg_begin(sp);                 /* D_: run B (pass 2's mobys) after the slot */
    ra = -1;
    t5 = SE32(SPR + 0x3000);
    rc_sw(SPR + 0x3EF0, (u32)t5);
pass2_next:                                              /* 0x257878 */
    t5 = SE32(M32(SPR + 0x3EF0));
    at = s5h; ath = 0;                                   /* pcpyud at, s5, zero */
    bt = t5 == at; fp = M32((u32)t5); if (bt) goto finish;
    t1 = (u64)(s64)(s16)M16((u32)t5 + 8);
    t3 = SE32(M32((u32)t5 + 12));
    t6 = M16(fp + 0x34);
    a3 = SE32((u32)t1 << 1);
    at = (u64)(s64)(s16)M16(fp + 0x32);
    a3 = SE32((u32)a3 + SPR);
    s3 = (u64)(s64)(s16)M16((u32)t5 + 10);
    t5 = SE32((u32)t5 + 16);
    rc_sw(SPR + 0x3EF0, (u32)t5);
    at = SE32((u32)at << 10);
    bt = s3 == 0; at = SE32((u32)at - (u32)t3); if (bt) { st_p2skip++; goto pass2_next; }
    st_p2++;
    at <<= 32;
    qmtc2(1, t3, t3h);
    qmtc2(3, at, ath);
    goto draw;

    /* ---- the end, 0x257720 ---- */
finish:
    v0 = t7h; v0h = 0;                                   /* pcpyud v0, t7, zero */
    at = SE32(SPR + 0x3400);
    v1 = SE32((u32)v0 - (u32)at);
    bt = (s64)v1 <= 0; rc_sw(gp + (u32)R->g_cpsz, (u32)v1);
    if (!bt) {
        /* the colour patch list copied out */
        v1 = SE32(R->cpl);
        do {
            rc_lq(&q, (u32)at); a0 = q.d[0]; a0h = q.d[1];
            at = SE32((u32)at + 16);
            v1 = SE32((u32)v1 + 16);
            bt = at != v0; rc_sq(&q, (u32)v1 - 16);
        } while (bt);
    }
    /* the shadow list: the deferral entries with a non-zero second word */
    v0 = s5h; v0h = 0;
    a0 = SE32(R->shl);
    at = SE32(SPR + 0x3000);
    bt = at == v0; v1 = 0;
    if (!bt) {
        do {
            a1 = M64((u32)at);
            at = SE32((u32)at + 16);
            a2 = a1 >> 32;
            rc_sd((u32)a0, a1);
            if (a2 != 0) { a0 = SE32((u32)a0 + 8); v1 = SE32((u32)v1 + 1); }
        } while (at != v0);
    }
    rc_sw(gp + (u32)R->g_shcnt, (u32)v1);
    if (dcall) native_mobyseg_close(sp);                 /* D_: the last run ends at the closing CNT+FLUSHA */
    emit_close(&sp);
    if (dcall) { native_mobyseg_call_end(1, dmode_); dcall = 0; }   /* D_: the runs' markers (or none) */
    rc_sw(gp + (u32)R->g_pkt2, sp);
    v1 = SE32(t9);
    /* (s0..s7, sp, fp, ra back from the scratchpad: their low doublewords, as on entry) */
    wait_idle(0x1000D000u);
    rc_sq(&ZQ, SPR);
    rc_sw(0x1000D080u, 0);
    rc_sw(0x1000D020u, 1);
    rc_sw(0x1000D010u, t9);
    rc_sw(0x1000D000u, 0x100);                           /* the records' terminator */
    o->v0 = SE32(t9 + 16);
    o->v1 = v1;
    o->s5h = s5h; o->s6h = s6h; o->sph = sph;
    o->lo = lo_; o->hi = hi_;
    (void)t2; (void)t3; (void)t5; (void)v0h; (void)a0h; (void)t6h;
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;
static int n_told;
static MbRefs refs_of(const s32 *g, const u32 *a)
{
    MbRefs R;
    R.g_pkt = g[0]; R.g_unpack = g[1]; R.g_lim = g[2]; R.g_cap = g[3];
    for (int k = 0; k < 6; k++) R.g_t[k] = g[4 + k];
    R.g_off = g[10]; R.g_cpsz = g[11]; R.g_shcnt = g[12]; R.g_pkt2 = g[13]; R.g_slot = g[14];
    R.lights = a[0]; R.mask = a[1]; R.planes = a[2]; R.cam = a[3]; R.frust = a[4]; R.gs = a[5]; R.ref = a[6];
    R.consts = a[7]; R.cpl = a[8]; R.shl = a[9];
    return R;
}

/* check mode: the state the routine reads and writes, put aside and compared */
#define WIN 0x100000u    /* the chain window, from the packet pointer */
#define RWIN 0x80000u    /* the records' window, from a1 */
#define MAXM 16384       /* mobys whose drawn flag is kept */
#define LWIN 0x400u      /* the colour patch and shadow lists' copies */
typedef struct { u8 spr[0x4000]; u8 *win, *rec; u8 m31[MAXM]; u8 cpl[LWIN], shl[LWIN]; u32 gw[4]; RcVu0 vu0; int valid; } Snap;
static Snap A, N, G;
static struct { u32 sp0, nwin, rec0, nrec, nm, mby[MAXM], cpl, shl, gwa[4]; u64 ret; } K;
static u32 span(u32 a, u32 w)
{
    uintptr_t p = RC_A(a);
    return p < 0x2000000u ? (p + w <= 0x2000000u ? w : (u32)(0x2000000u - p)) : 0;
}
static void grab(Snap *s)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    if (!s->win) s->win = malloc(WIN);
    if (!s->rec) s->rec = malloc(RWIN);
    memcpy(s->win, (void *)RC_A(K.sp0), K.nwin);
    memcpy(s->rec, (void *)RC_A(K.rec0), K.nrec);
    for (u32 k = 0; k < K.nm; k++) s->m31[k] = M8(K.mby[k] + 0x31);
    memcpy(s->cpl, (void *)RC_A(K.cpl), LWIN);
    memcpy(s->shl, (void *)RC_A(K.shl), LWIN);
    for (int k = 0; k < 4; k++) s->gw[k] = M32(K.gwa[k]);
    s->vu0 = rc_vu0;
    s->valid = 1;
}
static void put_back(const Snap *s)
{
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    memcpy((void *)RC_A(K.sp0), s->win, K.nwin);
    memcpy((void *)RC_A(K.rec0), s->rec, K.nrec);
    for (u32 k = 0; k < K.nm; k++) M8(K.mby[k] + 0x31) = s->m31[k];
    memcpy((void *)RC_A(K.cpl), s->cpl, LWIN);
    memcpy((void *)RC_A(K.shl), s->shl, LWIN);
    for (int k = 0; k < 4; k++) M32(K.gwa[k]) = s->gw[k];
    rc_vu0 = s->vu0;
}
/* an area compared: its first difference logged (the first 10 differences of the session) */
static int differs(const char *what, u32 base, const u8 *g, const u8 *n, u32 len)
{
    if (!memcmp(g, n, len)) return 0;
    u32 k = 0;
    while (g[k] == n[k]) k++;
    u32 nq = 0;
    for (u32 j = 0; j < len; j += 16) if (memcmp(g + j, n + j, len - j < 16 ? len - j : 16)) nq++;
    if (n_told < 10) {
        n_told++;
        u32 o = k & ~15u, gq[4] = { 0 }, xq[4] = { 0 };
        memcpy(gq, g + o, len - o < 16 ? len - o : 16);
        memcpy(xq, n + o, len - o < 16 ? len - o : 16);
        uya_log("native MobyProc check: run %llu: %s differs at %08X (+0x%X; %u qwords differ): game %08X %08X %08X %08X, native %08X %08X %08X %08X",
                n_runs, what, base + o, k, nq, gq[0], gq[1], gq[2], gq[3], xq[0], xq[1], xq[2], xq[3]);
    }
    return 1;
}

int uya_mobyproc_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (mode_ < 0) {
        const char *e = getenv("UYA_NMOBYPROC"), *n = getenv("UYA_NATIVE");
        mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2;
        uya_log("native MobyProc: %s", mode_ == 0 ? "off (the game's routine)" : mode_ == 1 ? "check against the game's routine" : "on");
    }
    if (!mode_) return 0;
    if (ng != 15 || na != 10) { static int told; if (!told++) uya_log("native MobyProc: the level's routine has %d / %d data references (not 15 / 10): the game's routine runs", ng, na); return 0; }
    MbRefs R = refs_of(g, a);
    Out o;
    if (mode_ >= 2) {
        dmode_ = native_mobyseg_mode();                  /* (the direct draws: only here, never in check mode) */
        run(c, &R, &o);
        /* what the game's routine leaves: v0 (the records' end + 0x10), v1, at, the upper halves of
         * s5, s6 and sp, LO/HI (the low doublewords of s0..s7, sp, fp, ra are as on entry) */
        c->r[2].d[0] = o.v0;
        c->r[3].d[0] = o.v1;
        c->r[1].d[0] = SE32(0x1000D000u);
        c->r[21].d[1] = o.s5h; c->r[22].d[1] = o.s6h; c->r[29].d[1] = o.sph;
        c->lo.d[0] = o.lo; c->hi.d[0] = o.hi;
        return 1;
    }
    /* check: run here, keep the results, put the state back for the game's routine */
    const u32 gp = (u32)c->r[28].d[0];
    K.sp0 = M32(gp + (u32)R.g_pkt); K.nwin = span(K.sp0, WIN);
    K.rec0 = (u32)c->r[5].d[0]; K.nrec = span(K.rec0, RWIN);
    K.cpl = R.cpl; K.shl = R.shl;
    K.gwa[0] = gp + (u32)R.g_cpsz; K.gwa[1] = gp + (u32)R.g_shcnt; K.gwa[2] = gp + (u32)R.g_pkt2; K.gwa[3] = gp + (u32)R.g_slot;
    {
        u32 m = (u32)c->r[4].d[0], e = ((u32)c->r[6].d[0] << 8) + m;
        K.nm = 0;
        for (; m != e && RC_A(m) + 0x100 <= 0x2000000u && M8(m + 0x20) != 0xFF; m += 0x100) {
            if (K.nm == MAXM) { static int told; if (!told++) uya_log("native MobyProc check: more than %u mobys: the drawn flags after them are not compared", MAXM); break; }
            K.mby[K.nm++] = m;
        }
    }
    RcCtx c0 = *c;
    grab(&A);
    dmode_ = 0;
    run(c, &R, &o);
    K.ret = o.v0;
    grab(&N);
    put_back(&A);
    *c = c0;
    return 0;
}

void uya_mobyproc_exit(RcCtx *c)
{
    if (mode_ != 1 || !N.valid) return;
    N.valid = 0;
    n_runs++;
    grab(&G);
    int bad = 0;
    if (c->r[2].d[0] != K.ret) {
        bad = 1;
        if (n_told < 10) { n_told++; uya_log("native MobyProc check: run %llu: v0 %08X, native %08X", n_runs, (u32)c->r[2].d[0], (u32)K.ret); }
    }
    bad |= differs("scratchpad", SPR, G.spr, N.spr, 0x4000);
    bad |= differs("chain", K.sp0, G.win, N.win, K.nwin);
    bad |= differs("records", K.rec0, G.rec, N.rec, K.nrec);
    bad |= differs("colour patch list", K.cpl, G.cpl, N.cpl, LWIN);
    bad |= differs("shadow list", K.shl, G.shl, N.shl, LWIN);
    for (u32 k = 0; k < K.nm; k++)
        if (G.m31[k] != N.m31[k]) {
            bad = 1;
            if (n_told < 10) { n_told++; uya_log("native MobyProc check: run %llu: moby %u (%08X) drawn flag %02X, native %02X", n_runs, k, K.mby[k], G.m31[k], N.m31[k]); }
            break;
        }
    for (int k = 0; k < 4; k++)
        if (G.gw[k] != N.gw[k]) {
            bad = 1;
            if (n_told < 10) { n_told++; uya_log("native MobyProc check: run %llu: gp word %08X: %08X, native %08X", n_runs, K.gwa[k], G.gw[k], N.gw[k]); }
        }
    if (memcmp(G.vu0.vf, N.vu0.vf, sizeof G.vu0.vf) || memcmp(G.vu0.acc, N.vu0.acc, sizeof G.vu0.acc) || memcmp(&G.vu0.q, &N.vu0.q, 4) ||
        G.vu0.clip != N.vu0.clip || G.vu0.mac != N.vu0.mac || G.vu0.status != N.vu0.status) {
        bad = 1;
        if (n_told < 10) {
            n_told++;
            int r = 0; while (r < 32 && !memcmp(G.vu0.vf[r], N.vu0.vf[r], 16)) r++;
            u32 gq[4] = { 0 }, xq[4] = { 0 }, ga[4], xa[4], gqq, xqq;
            if (r < 32) { memcpy(gq, G.vu0.vf[r], 16); memcpy(xq, N.vu0.vf[r], 16); }
            memcpy(ga, G.vu0.acc, 16); memcpy(xa, N.vu0.acc, 16); memcpy(&gqq, &G.vu0.q, 4); memcpy(&xqq, &N.vu0.q, 4);
            uya_log("native MobyProc check: run %llu: VU0 registers differ (first vf%d: game %08X %08X %08X %08X, native %08X %08X %08X %08X; "
                    "acc %08X %08X %08X %08X / %08X %08X %08X %08X; q %08X / %08X; mac %04X / %04X, status %03X / %03X, clip %06X / %06X)",
                    n_runs, r, gq[0], gq[1], gq[2], gq[3], xq[0], xq[1], xq[2], xq[3], ga[0], ga[1], ga[2], ga[3], xa[0], xa[1], xa[2], xa[3],
                    gqq, xqq, G.vu0.mac, N.vu0.mac, G.vu0.status, N.vu0.status, G.vu0.clip, N.vu0.clip);
        }
    }
    if (bad) n_bad++; else n_ok++;
    if (n_runs % 500 == 0 || n_runs == 100 || n_runs == 10)
        uya_log("native MobyProc check: %llu runs, %llu identical, %llu differ; mobys %llu (skipped %llu): mask %llu, hidden %llu, far %llu, near %llu, side %llu, "
                "drawn %llu (inside the guard band %llu, clipped %llu); chain full %llu; deferred %llu: side %llu, list full %llu, for pass 2 %llu, shadow %llu "
                "(culled %llu, on the edge %llu), drawn now %llu; pass 2 drawn %llu, skipped %llu; LOD near %llu, far %llu; frames %llu (same sequence %llu); "
                "one light set %llu, blended %llu; point lights none %llu, one %llu, several %llu; colour patch %llu (full %llu, none %llu); GS state %llu; "
                "packets %llu; extra pass %llu (packets %llu)",
                n_runs, n_ok, n_bad, st_moby, st_skip, st_mask, st_hidden, st_far, st_near, st_side, st_draw1, st_guard, st_clip, st_full,
                st_defer, st_dside, st_dfull, st_d800, st_shadow, st_shcull, st_edge, st_dnow, st_p2, st_p2skip, st_lod[0], st_lod[1],
                st_frames, st_seq, st_one, st_blend, st_pl[0], st_pl[1], st_pl[2], st_cp, st_cpfull, st_cpnone, st_gs, st_pkt, st_extra, st_xpkt);
}
