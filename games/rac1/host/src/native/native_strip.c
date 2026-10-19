/* The strip service of VU1 image 57843 (the game's strip effects: trails, beams, ribbons) as native
 * C: the three routines that build its part of the VIF1 chain.
 *
 * func_001F7868, the setup (C in the game, decomp src/game/draw.c; 488 bytes, one copy in every
 * level and the executable's; level 3: 0x1F5FD8). In order:
 *   - a matrix M on its stack (sp - 0x80): identity scaled by 1024 (func_001FA1C0: vf1..vf4 from
 *     vf0, vf5 = 1024 with at's upper half), its translation the camera position (camera block,
 *     D_00187180 in the executable) times -1024 (FastVecScale func_001F9C30: vf1, vf2), w = 1.
 *   - if the resident VU1 program (D_0015F704) is not 7: a REF to the program's MPG packet (qwc at
 *     0x10E800, data 0x10E810) through VU1_addDataRef (func_00234B48), and D_0015F704 = 7.
 *   - at the chain pointer a CNT: FLUSH, STCYCL 4/4, UNPACK V4-32 12 at VU 932: the two camera
 *     matrices at camera - 0x100 and - 0x80 times M (sce_vu0_mul_matrix func_001FA540: vf4..vf9 and
 *     ACC), each with element [14] plus the z bias (a gp float, D_0015F448); the GIF tag (0x8000,
 *     0x303EC000, 0x412, view + 0x210); view + 0x190 and + 0x1A0; (view + 0x22C, view + 0x228, 0, 0);
 *     then BASE 0, OFFSET 0x1D2, MSCALF 0x0000, NOP. The CNT's qwc patched (`sra`), the chain
 *     pointer (gp) moved on, and func_00234FA8: a REF 11 to D_0013D010 (VIF NOP, DIRECT 11: the GS
 *     register reset; resident, the same address in every level), the pointer moved on again.
 *   - s0..s2 and ra saved and restored with sq / lq (whole), so only at, v0..v3, a0..a3, t0, f0,
 *     f1, f12 and the VU0 registers keep what the routine leaves.
 *
 * func_L00_001FDE48, one strip (hand-written, shared level code, 500 bytes; level 3: 0x1F7730):
 * a0 = n vertices, a1 = xyz (12 bytes each), a2 = colours (4 bytes), a3 = ST (8 bytes), t0 bit 0 =
 * clipped. At the chain pointer (gp): the CNT template (5 qwords: STCYCL 4/4, UNPACK the strip's GIF
 * tag at TOPS + 0 (NLOOP |= n), UNPACK at 940, STCOL 1.0 x4, STCYCL 3/1, STMOD 0; the CNT's qwc
 * |= patched at the end, the tag word |= n), then three UNPACK templates with num |= n << 16:
 * STMASK + V2-32 at TOPS + 1 and the ST pairs (8 bytes each, ld/sd), V4-8 at TOPS + 2 and the
 * colours (words), STMASK + V3-32 at TOPS + 3 and the xyz (3 words each); after each array zero
 * words up to the next qword; then MSCALF 0x60 (plain) or 0x70 (clipped), FLUSH, 0, 0. The CNT's
 * qwc |= qwords - 1 (`srl`), the chain pointer moved on. v0 keeps the last template's upper half
 * (lq), t9 the address of the xyz UNPACK.
 *
 * func_001F8B6C, the strip list (hand-written, 1612 bytes, one copy in every level and the
 * executable's; level 3: 0x1F7924; called after the setup by func_001F7B70 in DrawFrame):
 *   - saves s0..s4 with sw (32 bits; restored with lw, so their upper halves keep what the routine
 *     loaded: s0's from an lq), waits on the word at the caller's t2 (bit 8, a DMA CHCR);
 *     vf16 = 0.5, vf4 = 2^-10 (with at's upper half).
 *   - the visibility samples in the scratchpad (0x2400..0x24FF, 8 of 2 qwords): those with a
 *     non-zero doubleword at +0x10 compacted to the front of 0x2400 as (first qword with w = 1 / the
 *     second's w, second qword); their count to s7's upper half (pcpyld).
 *   - per list entry (count D_0015F578, entries of 0x20 bytes at *D_0015F57C, data pointers at
 *     *D_0015F580): the samples' fade: per sample, d^2 = |sample - entry position|^2 - sample.w^2
 *     (VU0 ACC / vf5 / vf8 .y); a sample with d^2 > 0 (tested as a 64-bit integer, so with vf5.x)
 *     adds nothing, else min(max(p.x, p.y, p.z) * 0.5, 1) * (1 - p.w * sqrt|d^2|) to vf15.x
 *     (p = the compacted pair's first qword, p.w = 1 / the sample's w; the sample = its second
 *     qword, xyz the position); the fade = (1 - min(sum, 1)) * 256 as an
 *     integer, kept in s6's upper half. The entry's word +0x14: its low half the
 *     entry's alpha (0: skipped), bit 31 the clipped flag. Its header: words +0x18/+0x1C into the
 *     header template (+0x20/+0x24, the template is written), the template's 3 qwords to the
 *     chain. Per strip of the entry's data (count in the first word's upper half; the first word's
 *     lower half times the entry's alpha times the fade >> 16 = the strip alpha): its vertex count,
 *     xyz as 3 halfwords per vertex (unaligned, ldl/ldr), ST as 2 halfwords; written as the strip
 *     routine writes them: the CNT template, the ST UNPACK (two vertices a qword: the halfwords << 2
 *     as 12-bit fixed point - 32.0), the colour UNPACK (0x00FFFFFF | alpha << 24 for every vertex),
 *     the xyz UNPACK (the halfwords << 2 as 12-bit fixed point + the entry position - 32.0), the
 *     zero padding (here the delay slot writes one zero word also when already aligned), MSCALF
 *     0x60/0x70, FLUSH; the CNT's qwc and the chain pointer as there.
 *
 * This file does the same, word for word: every store in the routine's order (chain, stack,
 * scratchpad, templates, gp words), every VU0 macro operation with its result kept in the VU0
 * registers (each product and sum clamped to +-MAX as the translated code has it; the routines read
 * no MAC or status flag, so those are left alone), the GPRs and FPRs as the routines leave them,
 * with upper halves where they are written whole (lq, qmfc2, MMI) or reach a VU register (qmtc2).
 * The data addresses differ per level: tools/recomp.py passes the level's own to the hooks it puts
 * at the routines' starts (these three hooks get every gp access and every lui's first use, see
 * HOST_HOOKS_ALLREFS there).
 *
 * UYA_NSTRIP: 0 the game's routines (translated); 1 check (this file runs first, its results are
 * put aside, the game's routine runs from the same state, and the two are compared at its end:
 * the chain up to the new chain pointer and the CNT it patches, the gp chain pointer, D_0015F704,
 * the stack frame, the scratchpad, the list's header template, the VU0 registers, every GPR, hi,
 * lo and FPR); 2 this file (default), the chain built natively exactly as the game builds it.
 * UYA_NATIVE=0 also turns it off. A census of the calls per level (and the 57843 starts they make)
 * is logged at the end. */
#include "rc.h"
#include "uya_host.h"
#include "rac1_levels.h"
#include "native_stripd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void uya_log(const char *fmt, ...);
const rac1_level_t *rac1_current_level(void);

#define SPR 0x70000000u
#define VW(n, k) (*(u32 *)&VF(n)[k])
#define HI16(a) SE32(((u32)(a) + 0x8000u) & 0xFFFF0000u)     /* what `lui` leaves for address a */
static float (*volatile lib_sqrtf)(float) = sqrtf;
static float (*volatile lib_fmaxf)(float, float) = fmaxf;
static float (*volatile lib_fminf)(float, float) = fminf;

/* ---- the VU0 macro operations, as tools/recomp.py translates them (no MAC/status flags) ---- */
enum { ADD, SUB, MUL, MADD };
enum { X = 8, Y = 4, Z = 2, W = 1, XYZ = 14, XYZW = 15 };
#define ACC (-1)     /* destination: the accumulator */
#define BV (-1)      /* ft: every component (else 0..3 one component) */
static void fm(int fd, u32 m, int op, int fs, int ft, int bc)
{
    f32 r[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        if (!(m & (8u >> i))) continue;
        f32 a = VF(fs)[i], b = bc == BV ? VF(ft)[i] : VF(ft)[bc];
        switch (op) {
        case ADD: r[i] = rc_fc(a + b); break;
        case SUB: r[i] = rc_fc(a - b); break;
        case MUL: r[i] = rc_fc(a * b); break;
        default: r[i] = rc_fc(VACC[i] + rc_fc(a * b)); break;
        }
    }
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
}
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
static void qmtc2(int d, u64 lo, u64 hi) { u64 w[2] = { lo, hi }; memcpy(VF(d), w, 16); }
static void qmfc2(int s, u64 *lo, u64 *hi) { u64 w[2]; memcpy(w, VF(s), 16); *lo = w[0]; *hi = w[1]; }
/* vitof12 on the components in m */
static void itof12(int d, u32 m)
{
    f32 r[4];
    for (int i = 0; i < 4; i++) { s32 v; memcpy(&v, &VF(d)[i], 4); r[i] = rc_itof(v, 12); }
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) VF(d)[i] = r[i];
}
/* pextlh with zero, then psllw 2: the 4 halfwords of a doubleword as 4 words, times 4 */
static void halves4(u64 x, u64 *lo, u64 *hi)
{
    u32 w[4];
    for (int i = 0; i < 4; i++) w[i] = ((u32)(x >> (16 * i)) & 0xFFFFu) << 2;
    *lo = (u64)w[0] | (u64)w[1] << 32; *hi = (u64)w[2] | (u64)w[3] << 32;
}
/* prot3w */
static void prot3w(u64 *lo, u64 *hi)
{
    u32 w0 = (u32)*lo, w1 = (u32)(*lo >> 32), w2 = (u32)*hi, w3 = (u32)(*hi >> 32);
    *lo = (u64)w1 | (u64)w2 << 32; *hi = (u64)w0 | (u64)w3 << 32;
}

/* ---- path counts (logged by check mode; a path at 0 is untested) and the census ---- */
static unsigned long long st_set, st_setmpg, st_strip, st_strip_clip, st_list, st_list_ent, st_list_skip,
    st_list_strip, st_list_clip, st_samples, st_samp_out, st_samp_in, st_list_nosamp;
#define NLV 20     /* levels 0..18, 19 = the executable's program (no level) */
static unsigned long long cen_calls[3][NLV], cen_starts[3][NLV];
static u32 cen_f0[NLV], cen_f1[NLV];
static int lv_now(void) { const rac1_level_t *l = rac1_current_level(); return l && l->id >= 0 && l->id < 19 ? l->id : 19; }
static void census(int k, unsigned long long starts)
{
    int l = lv_now();
    u32 f = uya_frame_count();
    if (!cen_calls[0][l] && !cen_calls[1][l] && !cen_calls[2][l]) cen_f0[l] = f;
    cen_f1[l] = f;
    cen_calls[k][l]++; cen_starts[k][l] += starts;
}
static void census_log(void)
{
    for (int l = 0; l < NLV; l++) {
        if (!cen_calls[0][l] && !cen_calls[1][l] && !cen_calls[2][l]) continue;
        uya_log("native strips census, %s %d (vsyncs %u..%u): setup %llu calls; strip %llu calls; list %llu calls, %llu strips; 57843 starts made: %llu",
                l == 19 ? "executable" : "level", l == 19 ? -1 : l, cen_f0[l], cen_f1[l], cen_calls[0][l], cen_calls[1][l], cen_calls[2][l],
                cen_starts[2][l], cen_starts[0][l] + cen_starts[1][l] + cen_starts[2][l]);
    }
}

/* =============================== func_001F7868, the setup =============================== */
typedef struct { s32 g_zb, g_chain; u32 cam, f704, mpg_qwc, mpg_addr, chain, view; } SetupRefs;
#define SET_NG 3
#define SET_NA 12
#define GSRESET 0x0013D010u   /* func_00234FA8's REF (resident; every level's copy has it) */

/* sce_vu0_mul_matrix (func_001FA540): out a0, a1 x a2 (both 4 rows) */
static void mul_matrix(u64 *a0, u64 a1, u64 *a2, u64 *a3)
{
    lqc2(4, (u32)a1); lqc2(5, (u32)a1 + 16); lqc2(6, (u32)a1 + 32); lqc2(7, (u32)a1 + 48);
    *a3 = SE32((u32)*a2 + 64);
    do {
        lqc2(8, (u32)*a2);
        *a2 = SE32((u32)*a2 + 16);
        fm(ACC, XYZW, MUL, 4, 8, 0);
        fm(ACC, XYZW, MADD, 5, 8, 1);
        fm(ACC, XYZW, MADD, 6, 8, 2);
        fm(9, XYZW, MADD, 7, 8, 3);
        sqc2(9, (u32)*a0);
        *a0 = SE32((u32)*a0 + 16);
    } while (*a2 != *a3);
}

static void run_setup(RcCtx *c, const SetupRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    const u64 ath = c->r[1].d[1];
    u64 at, v0, v1, a0, a1, a2, a3, t0, s0, s1, s2, v0h = c->r[2].d[1];
    f32 f0, f1, f12;
    const u64 sp = SE32((u32)c->r[29].d[0] - 0x80);

    at = SE32(0x44800000u); f12 = rc_bitsf((u32)at);
    rc_sq(&c->r[18], (u32)sp + 0x60);
    a0 = sp;
    rc_sq(&c->r[16], (u32)sp + 0x40);
    rc_sq(&c->r[31], (u32)sp + 0x70);
    s0 = 7;
    rc_sq(&c->r[17], (u32)sp + 0x50);
    /* func_001FA1C0: identity times f12 */
    at = SE32(rc_fbits(f12));
    qmtc2(5, at, ath);
    fm(1, XYZW, MUL, 0, 0, 0);
    fm(2, XYZW, MUL, 0, 0, 0);
    fm(3, XYZW, MUL, 0, 0, 0);
    memcpy(VF(4), VF(0), 16);
    fm(1, X, ADD, 1, 5, 0);
    fm(2, Y, ADD, 2, 5, 0);
    fm(3, Z, ADD, 3, 5, 0);
    sqc2(1, (u32)a0); sqc2(2, (u32)a0 + 16); sqc2(3, (u32)a0 + 32); sqc2(4, (u32)a0 + 48);

    at = SE32(0xC4800000u); f12 = rc_bitsf((u32)at);
    s2 = SE32(R->cam);
    a0 = SE32((u32)sp + 0x30);
    a1 = s2;
    /* FastVecScale (func_001F9C30) */
    at = SE32(rc_fbits(f12));
    lqc2(1, (u32)a1);
    qmtc2(2, at, ath);
    fm(1, XYZ, MUL, 1, 2, 0);
    sqc2(1, (u32)a0);

    at = SE32(0x3F800000u); f0 = rc_bitsf((u32)at);
    v0 = SE32(M32(R->f704));
    MF((u32)sp + 0x3C) = f0;
    if (v0 != s0) {
        st_setmpg++;
        a1 = M16(R->mpg_qwc);
        a0 = SE32(R->mpg_addr);
        /* VU1_addDataRef (func_00234B48) */
        v1 = SE32(M32(R->chain));
        v0 = SE32(0x30000000u);
        a1 |= v0;
        rc_sw((u32)v1, (u32)a1);
        v0 = SE32(M32(R->chain));
        rc_sw((u32)v0 + 4, (u32)a0);
        v1 = SE32(M32(R->chain));
        rc_sw((u32)v1 + 8, 0);
        a0 = SE32(M32(R->chain));
        rc_sw((u32)a0 + 12, 0);
        v0 = SE32(M32(R->chain));
        v0 = SE32((u32)v0 + 16);
        rc_sw(gp + (u32)R->g_chain, (u32)v0);
        at = HI16(R->f704);
        rc_sw(R->f704, (u32)s0);
    }
    v1 = SE32(M32(R->chain));
    v0 = SE32(0x10000000u);
    t0 = SE32(0x11000000u);
    a0 = SE32(0x01000000u);
    rc_sw((u32)v1, (u32)v0);
    a0 |= 0x404;
    a3 = SE32(0x6C0C0000u);
    a2 = sp;
    v1 = SE32(M32(R->chain));
    a3 |= 0x43A4;
    a1 = SE32((u32)s2 - 256);
    rc_sw((u32)v1 + 4, 0);
    v0 = SE32(M32(R->chain));
    rc_sw((u32)v0 + 8, (u32)t0);
    v1 = SE32(M32(R->chain));
    rc_sw((u32)v1 + 12, (u32)a0);
    s0 = SE32(M32(R->chain));
    s1 = SE32((u32)s0 + 32);
    rc_sw((u32)s0 + 16, 0);
    rc_sw((u32)s0 + 20, 0);
    a0 = s1;
    rc_sw((u32)s0 + 24, 0);
    rc_sw((u32)s0 + 28, (u32)a3);
    mul_matrix(&a0, a1, &a2, &a3);
    f0 = MF((u32)s1 + 56);
    a1 = SE32((u32)s2 - 128);
    f1 = MF(gp + (u32)R->g_zb);
    a2 = sp;
    f0 = rc_fc(f0 + f1);
    MF((u32)s1 + 56) = f0;
    s1 = SE32((u32)s0 + 96);
    a0 = s1;
    mul_matrix(&a0, a1, &a2, &a3);
    f0 = MF((u32)s1 + 56);
    v0 = SE32(0x303E0000u);
    f1 = MF(gp + (u32)R->g_zb);
    v1 = 0x8000;
    v0 |= 0xC000;
    a2 = 0x412;
    f0 = rc_fc(f0 + f1);
    a0 = SE32(R->view);
    a1 = SE32((u32)a0 + 400);
    MF((u32)s1 + 56) = f0;
    rc_sw((u32)s0 + 160, (u32)v1);
    s1 = SE32((u32)s0 + 176);
    rc_sw((u32)s0 + 164, (u32)v0);
    rc_sw((u32)s0 + 168, (u32)a2);
    f0 = MF((u32)a0 + 528);
    MF((u32)s0 + 172) = f0;
    {
        R128 q;
        rc_lq(&q, (u32)a1); rc_sq(&q, (u32)s1);
        s1 = SE32((u32)s0 + 192);
        v1 = SE32((u32)a0 + 416);
        rc_lq(&q, (u32)v1); rc_sq(&q, (u32)s1);
        v0 = q.d[0]; v0h = q.d[1];
    }
    f0 = MF((u32)a0 + 556);
    v0 = SE32(0x02000000u);
    v1 = SE32(0x03000000u);
    v0 |= 0x1D2;
    MF((u32)s0 + 208) = f0;
    a1 = SE32(0x15000000u);
    s1 = SE32((u32)s0 + 240);
    f0 = MF((u32)a0 + 552);
    rc_sw((u32)s0 + 228, (u32)v0);
    MF((u32)s0 + 212) = f0;
    rc_sw((u32)s0 + 232, (u32)a1);
    rc_sw((u32)s0 + 224, (u32)v1);
    rc_sw((u32)s0 + 216, 0);
    rc_sw((u32)s0 + 220, 0);
    rc_sw((u32)s0 + 236, 0);
    a0 = SE32(M32(R->chain));
    v0 = SE32((u32)s1 - (u32)a0);
    v1 = SE32(M32((u32)a0));
    v0 = SE32((s32)(u32)v0 >> 4);
    v0 = SE32((u32)v0 - 1);
    v1 |= v0;
    rc_sw((u32)a0, (u32)v1);
    rc_sw(gp + (u32)R->g_chain, (u32)s1);
    /* func_00234FA8: REF 11 to the GS register reset, VIF NOP, DIRECT 11 */
    a0 = SE32(M32(R->chain));
    v0 = SE32(0x3000000Bu);
    rc_sw((u32)a0, (u32)v0);
    v1 = SE32(GSRESET);
    a0 = SE32(0x5000000Bu);
    a1 = SE32(M32(R->chain));
    rc_sw((u32)a1 + 4, (u32)v1);
    v0 = SE32(M32(R->chain));
    rc_sw((u32)v0 + 8, 0);
    v1 = SE32(M32(R->chain));
    rc_sw((u32)v1 + 12, (u32)a0);
    v0 = SE32(M32(R->chain));
    v0 = SE32((u32)v0 + 16);
    rc_sw(gp + (u32)R->g_chain, (u32)v0);
    /* lq ra, s2, s1, s0: their entry values (unchanged in c); sp back */
    c->r[1].d[0] = at;
    c->r[2].d[0] = v0; c->r[2].d[1] = v0h;
    c->r[3].d[0] = v1;
    c->r[4].d[0] = a0; c->r[5].d[0] = a1; c->r[6].d[0] = a2; c->r[7].d[0] = a3;
    c->r[8].d[0] = t0;
    c->f[0] = f0; c->f[1] = f1; c->f[12] = f12;
    st_set++;
}

/* ========================= func_L00_001FDE48, one strip ========================= */
typedef struct { s32 g_chain; u32 tcnt, u1, u2, u3; } StripRefs;
#define STRIP_NG 2
#define STRIP_NA 4

static void run_strip(RcCtx *c, const StripRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    const u64 a0 = c->r[4].d[0], a1 = c->r[5].d[0], a2 = c->r[6].d[0], a3 = c->r[7].d[0];
    u64 at, v0, v0h, v1, t0, t2, t3, t5, t7, t9;
    R128 q;

    t0 = c->r[8].d[0] & 1;
    if (t0) st_strip_clip++;
    t0 = SE32((u32)t0 << 1);
    t2 = SE32(gp + (u32)R->g_chain);
    t0 = SE32((u32)t0 + 12);
    t2 = SE32(M32((u32)t2));
    t5 = SE32(0x70000000u);
    at = t2;
    t7 = SE32((u32)a0 << 16);
    t3 = SE32(R->tcnt);
    v1 = 5;
    do {
        rc_lq(&q, (u32)t3);
        v1 = SE32((u32)v1 - 1);
        rc_sq(&q, (u32)at);
        t3 = SE32((u32)t3 + 16);
        at = SE32((u32)at + 16);
    } while (v1 != 0);
    v0 = SE32(M32((u32)at - 64));
    v0 |= a0;
    rc_sw((u32)at - 64, (u32)v0);
    /* the ST UNPACK and the pairs */
    t3 = SE32(R->u1);
    rc_lq(&q, (u32)t3);
    rc_sq(&q, (u32)at);
    at = SE32((u32)at + 16);
    v0 = SE32(M32((u32)at - 4));
    v0 |= t7;
    rc_sw((u32)at - 4, (u32)v0);
    t3 = a3;
    v1 = a0;
    do {
        v0 = M64((u32)t3);
        v1 = SE32((u32)v1 - 1);
        rc_sd((u32)at, v0);
        t3 = SE32((u32)t3 + 8);
        at = SE32((u32)at + 8);
    } while (v1 != 0);
    for (;;) { v1 = at & 0xF; if (v1 == 0) break; rc_sw((u32)at, 0); at = SE32((u32)at + 4); }
    /* the colour UNPACK and the colours */
    t3 = SE32(R->u2);
    rc_lq(&q, (u32)t3);
    rc_sq(&q, (u32)at);
    at = SE32((u32)at + 16);
    v0 = SE32(M32((u32)at - 4));
    v0 |= t7;
    rc_sw((u32)at - 4, (u32)v0);
    t3 = a2;
    v1 = a0;
    do {
        v0 = SE32(M32((u32)t3));
        v1 = SE32((u32)v1 - 1);
        rc_sw((u32)at, (u32)v0);
        t3 = SE32((u32)t3 + 4);
        at = SE32((u32)at + 4);
    } while (v1 != 0);
    for (;;) { v1 = at & 0xF; if (v1 == 0) break; rc_sw((u32)at, 0); at = SE32((u32)at + 4); }
    /* the xyz UNPACK and the positions */
    t9 = at;
    t3 = SE32(R->u3);
    rc_lq(&q, (u32)t3);
    rc_sq(&q, (u32)at);
    v0h = q.d[1];
    at = SE32((u32)at + 16);
    v0 = SE32(M32((u32)at - 4));
    v0 |= t7;
    rc_sw((u32)at - 4, (u32)v0);
    t3 = a1;
    v1 = a0;
    do {
        v0 = SE32(M32((u32)t3));
        v1 = SE32((u32)v1 - 1);
        rc_sw((u32)at, (u32)v0);
        v0 = SE32(M32((u32)t3 + 4));
        rc_sw((u32)at + 4, (u32)v0);
        v0 = SE32(M32((u32)t3 + 8));
        rc_sw((u32)at + 8, (u32)v0);
        t3 = SE32((u32)t3 + 12);
        at = SE32((u32)at + 12);
    } while (v1 != 0);
    for (;;) { v1 = at & 0xF; if (v1 == 0) break; rc_sw((u32)at, 0); at = SE32((u32)at + 4); }
    /* MSCALF 0x60 / 0x70, FLUSH; the CNT's qwc; the chain pointer */
    v0 = SE32(0x15000000u);
    v0 |= t0;
    rc_sw((u32)at, (u32)v0);
    v0 = SE32(0x11000000u);
    rc_sw((u32)at + 4, (u32)v0);
    rc_sd((u32)at + 8, 0);
    at = SE32((u32)at + 16);
    v0 = SE32((u32)at - (u32)t2);
    v0 = SE32((u32)v0 >> 4);
    v0 = SE32((u32)v0 - 1);
    v1 = SE32(M32((u32)t2));
    v1 |= v0;
    rc_sw((u32)t2, (u32)v1);
    v0 = SE32(gp + (u32)R->g_chain);
    t2 = at;
    rc_sw((u32)v0, (u32)t2);

    c->r[1].d[0] = at;
    c->r[2].d[0] = v0; c->r[2].d[1] = v0h;
    c->r[3].d[0] = v1;
    c->r[8].d[0] = t0;
    c->r[10].d[0] = t2;
    c->r[11].d[0] = t3;
    c->r[13].d[0] = t5;
    c->r[15].d[0] = t7;
    c->r[25].d[0] = t9;
    st_strip++;
}

/* ========================= func_001F8B6C, the strip list ========================= */
typedef struct { s32 g_chain; u32 ents, cnt, ptrs, hdr, tcnt, u1, u2, u3; } ListRefs;
#define LIST_NG 3
#define LIST_NA 8

static unsigned long long run_list(RcCtx *c, const ListRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    u64 at = c->r[1].d[0], ath = c->r[1].d[1];
    u64 v0 = c->r[2].d[0], v0h = c->r[2].d[1], v1 = c->r[3].d[0], v1h = c->r[3].d[1];
    u64 a0 = c->r[4].d[0], a1 = c->r[5].d[0], a1h = c->r[5].d[1], a2 = c->r[6].d[0], a3 = c->r[7].d[0];
    u64 t0 = c->r[8].d[0], t1 = c->r[9].d[0], t1h = c->r[9].d[1], t2 = c->r[10].d[0], t3 = c->r[11].d[0];
    u64 t4 = c->r[12].d[0], t4h = c->r[12].d[1], t6 = c->r[14].d[0], t6h = c->r[14].d[1], t7 = c->r[15].d[0];
    u64 t8 = c->r[24].d[0], t9 = c->r[25].d[0], t5;
    u64 s0 = c->r[16].d[0], s0h = c->r[16].d[1], s1, s2, s3 = c->r[19].d[0], s4;
    u64 s6h = c->r[22].d[1], s7h = c->r[23].d[1];
    u64 lo = c->lo.d[0], hi = c->hi.d[0];
    unsigned long long strips = 0;
    const u64 sp = SE32((u32)c->r[29].d[0] - 32);
    R128 q;

    rc_sw((u32)sp + 0, (u32)c->r[16].d[0]);
    rc_sw((u32)sp + 4, (u32)c->r[17].d[0]);
    rc_sw((u32)sp + 8, (u32)c->r[18].d[0]);
    rc_sw((u32)sp + 12, (u32)c->r[19].d[0]);
    rc_sw((u32)sp + 16, (u32)c->r[20].d[0]);
    rc_sw((u32)sp + 16, (u32)c->r[20].d[0]);
    t5 = SE32(0x70000000u);
    do { v0 = SE32(M32((u32)t2)); v0 &= 0x100; } while (v0 != 0);
    at = SE32(0x3F000000u);
    qmtc2(16, at, ath);
    at = SE32(0x3A800000u);
    qmtc2(4, at, ath);
    /* the visibility samples, compacted */
    at = SE32((u32)t5 + 0x2400);
    v0 = SE32((u32)t5 + 0x2500);
    v1 = SE32((u32)t5 + 0x2400);
    a0 = 0;
    for (;;) {
        int bt = at == v0;
        a1 = M64((u32)at + 16);
        if (bt) break;
        bt = a1 == 0;
        at = SE32((u32)at + 32);
        if (bt) continue;
        lqc2(1, (u32)at - 16);
        lqc2(2, (u32)at - 32);
        rc_vu0.q = rc_fc(rc_fdiv(VF(0)[3], VF(1)[3]));
        sqc2(1, (u32)v1 + 16);
        { f32 w = rc_fc(VF(0)[3] * rc_vu0.q); VF(2)[3] = w; }
        sqc2(2, (u32)v1);
        a0 = SE32((u32)a0 + 1);
        v1 = SE32((u32)v1 + 32);
    }
    s7h = a0;
    st_samples += (u32)a0;
    s1 = SE32(R->ents);
    s2 = SE32(M32(R->cnt));
    s1 = SE32(M32((u32)s1));
    s4 = SE32(M32(R->ptrs));
    do {
        /* one list entry */
        st_list_ent++;
        s3 = SE32(M32((u32)s4));
        lqc2(1, (u32)s1);
        v1 = s7h; v1h = 0;
        {
            int bt = v1 == 0;
            fm(15, XYZW, ADD, 0, 0, BV);
            if (bt) { st_list_nosamp++; goto fade_done; }
        }
        lqc2(2, (u32)t5 + 0x2410);
        v0 = SE32((u32)t5 + 0x2400);
        at = SE32((u32)t5 + 0x2500);
        a0 = 0;
        fm(3, XYZ, SUB, 2, 1, BV);
        fm(6, XYZ, ADD, 0, 0, 3);
        fm(7, XYZ, SUB, 0, 0, 3);
        for (;;) {
            {
                int bt = v1 == 0;
                fm(3, XYZ, MUL, 3, 3, BV);
                if (bt) break;
            }
            fm(4, W, MUL, 2, 2, BV);
            v0 = SE32((u32)v0 + 32);
            lqc2(2, (u32)v0 + 16);
            v1 = SE32((u32)v1 - 1);
            fm(ACC, Y, ADD, 3, 3, 0);
            fm(ACC, Y, MADD, 6, 3, 2);
            { f32 y = rc_fc(VACC[1] + rc_fc(VF(7)[1] * VF(4)[3])); VF(5)[1] = y; }
            { f32 y = rc_fc(VACC[1] + rc_fc(VF(0)[1] * VF(0)[0])); VF(8)[1] = y; }
            fm(3, XYZ, SUB, 2, 1, BV);
            qmfc2(5, &a1, &a1h);
            rc_vu0.q = lib_sqrtf(__builtin_fabsf(VF(8)[1]));
            if ((s64)a1 > 0) { st_samp_out++; continue; }
            st_samp_in++;
            lqc2(8, (u32)v0 - 16);
            lqc2(9, (u32)v0 - 32);
            fm(8, XYZ, SUB, 1, 8, BV);
            { f32 w = rc_fc(VF(0)[3] * rc_vu0.q); VF(5)[3] = w; }
            { f32 w = rc_fc(VF(9)[3] * rc_vu0.q); VF(9)[3] = w; }
            { f32 w = rc_fc(VF(0)[3] - VF(9)[3]); VF(9)[3] = w; }
            { f32 x = lib_fmaxf(VF(9)[0], VF(9)[1]); VF(9)[0] = x; }
            { f32 x = lib_fmaxf(VF(9)[0], VF(9)[2]); VF(9)[0] = x; }
            { f32 x = rc_fc(VF(9)[0] * VF(16)[0]); VF(9)[0] = x; }
            { f32 x = lib_fminf(VF(9)[0], VF(0)[3]); VF(9)[0] = x; }
            { f32 x = rc_fc(VF(9)[0] * VF(9)[3]); VF(9)[0] = x; }
            { f32 x = rc_fc(VF(15)[0] + VF(9)[0]); VF(15)[0] = x; }
            a0 = SE32((u32)a0 + 1);
            at = SE32((u32)at + 32);
        }
    fade_done:
        fm(2, XYZ, ADD, 0, 0, 3);
        { f32 x = lib_fminf(VF(15)[0], VF(0)[3]); VF(15)[0] = x; }
        { f32 x = rc_fc(VF(2)[0] - VF(15)[0]); VF(15)[0] = x; }
        a1 = SE32(0x43800000u);
        qmtc2(3, a1, a1h);
        fm(15, XYZW, MUL, 15, 3, 0);
        { s32 r[4]; for (int i = 0; i < 4; i++) r[i] = rc_ftoi(VF(15)[i], 0); memcpy(VF(15), r, 16); }
        qmfc2(15, &at, &ath);
        s6h = at;
        rc_lq(&q, (u32)s1); t1 = q.d[0]; t1h = q.d[1];
        v0 = SE32(M32((u32)s1 + 20));
        a2 = v0 & 0xFFFF;
        {
            int bt = a2 == 0;
            t0 = SE32((u32)v0 >> 31);
            if (bt) { st_list_skip++; goto next_entry; }
        }
        t0 &= 1;
        t0 = SE32((u32)t0 << 1);
        a0 = SE32(M32((u32)s1 + 24));
        t0 = SE32((u32)t0 + 12);
        a1 = SE32(M32((u32)s1 + 28));
        t9 = SE32(gp + (u32)R->g_chain);
        t8 = SE32(M32((u32)t9));
        v0 = SE32(R->hdr);
        {   /* testing: UYA_SHDRLOG=1: the list's header template and strip templates, once */
            static int left = -1; if (left < 0) left = getenv("UYA_SHDRLOG") ? 1 : 0;
            if (left > 0) {
                left--;
                for (int k = 0; k < 3; k++) uya_log("strip list header %d: %08X %08X %08X %08X", k, M32(R->hdr + 16 * k), M32(R->hdr + 16 * k + 4), M32(R->hdr + 16 * k + 8), M32(R->hdr + 16 * k + 12));
                for (int k = 0; k < 5; k++) uya_log("strip tcnt %d: %08X %08X %08X %08X", k, M32(R->tcnt + 16 * k), M32(R->tcnt + 16 * k + 4), M32(R->tcnt + 16 * k + 8), M32(R->tcnt + 16 * k + 12));
                uya_log("strip u1 %08X %08X %08X %08X u2 %08X %08X %08X %08X u3 %08X %08X %08X %08X", M32(R->u1), M32(R->u1 + 4), M32(R->u1 + 8), M32(R->u1 + 12), M32(R->u2), M32(R->u2 + 4), M32(R->u2 + 8), M32(R->u2 + 12), M32(R->u3), M32(R->u3 + 4), M32(R->u3 + 8), M32(R->u3 + 12));
            }
        }
        rc_sw((u32)v0 + 32, (u32)a0);
        rc_sw((u32)v0 + 36, (u32)a1);
        rc_lq(&q, (u32)v0); t4 = q.d[0]; t4h = q.d[1];
        rc_lq(&q, (u32)v0 + 16); t6 = q.d[0]; t6h = q.d[1];
        rc_lq(&q, (u32)v0 + 32); s0 = q.d[0]; s0h = q.d[1];
        q.d[0] = t4; q.d[1] = t4h; rc_sq(&q, (u32)t8);
        q.d[0] = t6; q.d[1] = t6h; rc_sq(&q, (u32)t8 + 16);
        q.d[0] = s0; q.d[1] = s0h; rc_sq(&q, (u32)t8 + 32);
        t8 = SE32((u32)t8 + 48);
        rc_sw((u32)t9, (u32)t8);
        a1 = s3;
        t4 = SE32(M32((u32)a1));
        at = t4 & 0xFFFF;
        { u64 p = (u64)((s64)(s32)(u32)a2 * (s32)(u32)at); lo = SE32((u32)p); hi = SE32((u32)(p >> 32)); a2 = lo; }
        at = s6h; ath = 0;
        { u64 p = (u64)((s64)(s32)(u32)a2 * (s32)(u32)at); lo = SE32((u32)p); hi = SE32((u32)(p >> 32)); a2 = lo; }
        a2 = SE32((u32)a2 >> 16);
        s0 = SE32((u32)t4 >> 16);
        a1 = SE32((u32)a1 + 8);
        do {
            /* one strip of the entry */
            strips++;
            st_list_strip++;
            if (t0 & 2) st_list_clip++;
            a1 = SE32((u32)a1 + 15);
            t3 = SE32(-16);
            a1 &= t3;
            t8 = SE32(M32((u32)a1));
            a1 = SE32((u32)a1 + 4);
            t9 = a1;
            t4 = SE32((s32)(u32)t8 >> 1);
            t3 = 12;
            { u64 p = (u64)((s64)(s32)(u32)t4 * (s32)(u32)t3); lo = SE32((u32)p); hi = SE32((u32)(p >> 32)); t4 = lo; }
            a1 = SE32((u32)a1 + (u32)t4);
            a1 = SE32((u32)a1 + 15);
            t3 = SE32(-16);
            a1 &= t3;
            a3 = a1;
            t4 = t8;
            t3 = 4;
            { u64 p = (u64)((s64)(s32)(u32)t4 * (s32)(u32)t3); lo = SE32((u32)p); hi = SE32((u32)(p >> 32)); t4 = lo; }
            a1 = SE32((u32)a1 + (u32)t4);
            t2 = SE32(gp + (u32)R->g_chain);
            t2 = SE32(M32((u32)t2));
            at = t2;
            t7 = SE32((u32)t8 << 16);
            t3 = SE32(R->tcnt);
            v1 = 5;
            do {
                rc_lq(&q, (u32)t3); v0 = q.d[0]; v0h = q.d[1];
                v1 = SE32((u32)v1 - 1);
                rc_sq(&q, (u32)at);
                t3 = SE32((u32)t3 + 16);
                at = SE32((u32)at + 16);
            } while (v1 != 0);
            v0 = SE32(M32((u32)at - 64));
            v0 |= t8;
            rc_sw((u32)at - 64, (u32)v0);
            v0 = SE32(0x42000000u);
            qmtc2(3, v0, v0h);
            /* the ST UNPACK: two vertices a qword */
            t3 = SE32(R->u1);
            rc_lq(&q, (u32)t3); v0 = q.d[0]; v0h = q.d[1];
            rc_sq(&q, (u32)at);
            at = SE32((u32)at + 16);
            v0 = SE32(M32((u32)at - 4));
            v0 |= t7;
            rc_sw((u32)at - 4, (u32)v0);
            t3 = a3;
            v1 = t8;
            do {
                v0 = M64((u32)t3);
                v1 = SE32((u32)v1 - 2);
                halves4(v0, &v0, &v0h);
                qmtc2(1, v0, v0h);
                itof12(1, XYZW);
                fm(1, XYZW, SUB, 1, 3, 0);
                qmfc2(1, &v0, &v0h);
                rc_sw((u32)at, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 4, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 8, (u32)v0);
                v0 = v0h; v0h = 0;
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 12, (u32)v0);
                t3 = SE32((u32)t3 + 8);
                at = SE32((u32)at + 16);
            } while (v1 != 0);
            for (;;) { v1 = at & 0xF; rc_sw((u32)at, 0); if (v1 == 0) break; at = SE32((u32)at + 4); }
            /* the colour UNPACK: 0x00FFFFFF | alpha << 24, two vertices a doubleword */
            t3 = SE32(R->u2);
            rc_lq(&q, (u32)t3); v0 = q.d[0]; v0h = q.d[1];
            rc_sq(&q, (u32)at);
            at = SE32((u32)at + 16);
            v0 = SE32(M32((u32)at - 4));
            v0 |= t7;
            rc_sw((u32)at - 4, (u32)v0);
            t3 = a2;
            v1 = t8;
            v0 = SE32(0x00FF0000u);
            v0 |= 0xFFFF;
            t4 = SE32((u32)a2 << 24);
            t4 |= v0;
            v0 = t4 << 32;
            t4 |= v0;
            do {
                v1 = SE32((u32)v1 - 2);
                rc_sd((u32)at, t4);
                at = SE32((u32)at + 8);
            } while (v1 != 0);
            for (;;) { v1 = at & 0xF; rc_sw((u32)at, 0); if (v1 == 0) break; at = SE32((u32)at + 4); }
            /* the xyz UNPACK: two vertices (6 halfwords, unaligned) at a time */
            t3 = SE32(R->u3);
            rc_lq(&q, (u32)t3); v0 = q.d[0]; v0h = q.d[1];
            rc_sq(&q, (u32)at);
            at = SE32((u32)at + 16);
            v0 = SE32(M32((u32)at - 4));
            v0 |= t7;
            rc_sw((u32)at - 4, (u32)v0);
            t3 = t9;
            v1 = t8;
            qmtc2(4, t1, t1h);
            fm(4, XYZW, SUB, 4, 3, 0);
            do {
                t9 = rc_ldr(t9, (u32)t3);
                t9 = rc_ldl(t9, (u32)t3 + 7);
                t3 = SE32((u32)t3 + 4);
                v1 = SE32((u32)v1 - 2);
                halves4(t9, &v0, &v0h);
                qmtc2(1, v0, v0h);
                t9 = t9 >> 32;
                t9 = rc_ldl(t9, (u32)t3 + 7);
                t9 = t9 >> 16;
                t3 = SE32((u32)t3 + 8);
                halves4(t9, &v0, &v0h);
                itof12(1, XYZ);
                qmtc2(2, v0, v0h);
                itof12(2, XYZ);
                fm(1, XYZ, ADD, 1, 4, BV);
                qmfc2(1, &v0, &v0h);
                fm(2, XYZ, ADD, 2, 4, BV);
                rc_sw((u32)at, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 4, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 8, (u32)v0);
                qmfc2(2, &v0, &v0h);
                rc_sw((u32)at + 12, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 16, (u32)v0);
                prot3w(&v0, &v0h);
                rc_sw((u32)at + 20, (u32)v0);
                at = SE32((u32)at + 24);
            } while (v1 != 0);
            for (;;) { v1 = at & 0xF; rc_sw((u32)at, 0); if (v1 == 0) break; at = SE32((u32)at + 4); }
            /* MSCALF 0x60 / 0x70, FLUSH; the CNT's qwc; the chain pointer */
            v0 = SE32(0x15000000u);
            v0 |= t0;
            rc_sw((u32)at, (u32)v0);
            v0 = SE32(0x11000000u);
            rc_sw((u32)at + 4, (u32)v0);
            rc_sd((u32)at + 8, 0);
            at = SE32((u32)at + 16);
            v0 = SE32((u32)at - (u32)t2);
            v0 = SE32((u32)v0 >> 4);
            v1 = SE32(M32((u32)t2));
            v0 = SE32((u32)v0 - 1);
            v1 |= v0;
            rc_sw((u32)t2, (u32)v1);
            v0 = SE32(gp + (u32)R->g_chain);
            t2 = at;
            rc_sw((u32)v0, (u32)t2);
            s0 = SE32((u32)s0 - 1);
        } while (s0 != 0);
    next_entry:
        s1 = SE32((u32)s1 + 32);
        s2 = SE32((u32)s2 - 1);
        s4 = SE32((u32)s4 + 4);
    } while (s2 != 0);
    s0 = SE32(M32((u32)sp + 0));
    s1 = SE32(M32((u32)sp + 4));
    s2 = SE32(M32((u32)sp + 8));
    s3 = SE32(M32((u32)sp + 12));
    s4 = SE32(M32((u32)sp + 16));

    c->r[1].d[0] = at; c->r[1].d[1] = ath;
    c->r[2].d[0] = v0; c->r[2].d[1] = v0h;
    c->r[3].d[0] = v1; c->r[3].d[1] = v1h;
    c->r[4].d[0] = a0;
    c->r[5].d[0] = a1; c->r[5].d[1] = a1h;
    c->r[6].d[0] = a2; c->r[7].d[0] = a3;
    c->r[8].d[0] = t0;
    c->r[9].d[0] = t1; c->r[9].d[1] = t1h;
    c->r[10].d[0] = t2; c->r[11].d[0] = t3;
    c->r[12].d[0] = t4; c->r[12].d[1] = t4h;
    c->r[13].d[0] = t5;
    c->r[14].d[0] = t6; c->r[14].d[1] = t6h;
    c->r[15].d[0] = t7;
    c->r[16].d[0] = s0; c->r[16].d[1] = s0h;
    c->r[17].d[0] = s1; c->r[18].d[0] = s2; c->r[19].d[0] = s3; c->r[20].d[0] = s4;
    c->r[22].d[1] = s6h;
    c->r[23].d[1] = s7h;
    c->r[24].d[0] = t8; c->r[25].d[0] = t9;
    c->lo.d[0] = lo; c->hi.d[0] = hi;
    st_list++;
    return strips;
}

/* ============================== the hooks and check mode ============================== */
static int mode_ = -1;
static int get_mode(void)
{
    if (mode_ < 0) {
        const char *e = getenv("UYA_NSTRIP"), *n = getenv("UYA_NATIVE");
        mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2;
        uya_log("native strips (57843 setup, strip, strip list): %s", mode_ == 0 ? "off (the game's routines)" : mode_ == 1 ? "check against the game's routines" : "on");
        atexit(census_log);
    }
    return mode_;
}

/* check mode: the state a routine reads and writes, put aside and compared at its end */
#define NAREA 6
#define WINMAX 0x100000u
typedef struct { const char *what; u32 a, n; } Area;
typedef struct {
    const char *name;
    Area ar[NAREA]; int nar;
    u8 *A[NAREA], *N[NAREA], *G[NAREA];
    u32 cap[NAREA];
    RcCtx c0, cN;
    RcVu0 v0, vN;
    u32 chainvar;           /* the gp chain pointer word */
    int valid;
    unsigned long long runs, ok, bad;
} Chk;
static Chk chk[3] = { { "setup" }, { "strip" }, { "strip list" } };
static u32 clampn(u32 a, u32 n) { return a >= 0x2000000u ? 0 : a + n <= 0x2000000u ? n : 0x2000000u - a; }
static void area(Chk *k, const char *what, u32 a, u32 n)
{
    if ((a >> 28) == 7) n = a >= SPR && a + n <= SPR + 0x4000 ? n : 0;   /* the scratchpad */
    else { a = (u32)RC_A(a); n = clampn(a, n); }
    k->ar[k->nar].what = what; k->ar[k->nar].a = a; k->ar[k->nar].n = n;
    int i = k->nar++;
    if (k->cap[i] < n) {
        free(k->A[i]); free(k->N[i]); free(k->G[i]);
        k->A[i] = malloc(n); k->N[i] = malloc(n); k->G[i] = malloc(n); k->cap[i] = n;
    }
}
static void grab(Chk *k, u8 **to)
{
    for (int i = 0; i < k->nar; i++) if (k->ar[i].n) memcpy(to[i], (void *)(uintptr_t)k->ar[i].a, k->ar[i].n);
}
static void put(Chk *k, u8 **from)
{
    for (int i = 0; i < k->nar; i++) if (k->ar[i].n) memcpy((void *)(uintptr_t)k->ar[i].a, from[i], k->ar[i].n);
}
static void summary(Chk *k)
{
    uya_log("native strips check, %s: %llu runs, %llu identical, %llu differ; paths: setups %llu (MPG REF %llu); strips %llu (clipped %llu); "
            "lists %llu: entries %llu (skipped, alpha 0: %llu; no samples %llu), samples %llu (outside %llu, inside %llu), strips %llu (clipped %llu)",
            k->name, k->runs, k->ok, k->bad, st_set, st_setmpg, st_strip, st_strip_clip, st_list, st_list_ent, st_list_skip, st_list_nosamp,
            st_samples, st_samp_out, st_samp_in, st_list_strip, st_list_clip);
}
static void summary_end(void) { for (int i = 0; i < 3; i++) if (chk[i].runs % 500) summary(&chk[i]); }

static const char *const rname[32] = { "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };
static void check_begin(Chk *k, RcCtx *c)
{
    k->c0 = *c; k->v0 = rc_vu0;
    grab(k, k->A);
}
static void check_native_done(Chk *k, RcCtx *c)
{
    grab(k, k->N);
    k->cN = *c; k->vN = rc_vu0;
    put(k, k->A);
    *c = k->c0; rc_vu0 = k->v0;
    k->valid = 1;
}
static void check_exit(Chk *k, RcCtx *c)
{
    if (mode_ != 1 || !k->valid) return;
    k->valid = 0;
    static int at_end;
    if (!at_end++) atexit(summary_end);
    k->runs++;
    grab(k, k->G);
    char why[512] = "";
    int bad = 0;
    for (int i = 0; i < k->nar && !bad; i++) {
        u32 n = k->ar[i].n, j = 0;
        while (j < n && k->G[i][j] == k->N[i][j]) j++;
        if (j < n) {
            u32 a = k->ar[i].a + (j & ~3u), g, v;
            memcpy(&g, k->G[i] + (j & ~3u), 4); memcpy(&v, k->N[i] + (j & ~3u), 4);
            bad = 1; snprintf(why, sizeof why, "%s differs at %08X (+0x%X): %08X, native %08X", k->ar[i].what, a, j & ~3u, g, v);
        }
    }
    for (int r = 0; r < 32 && !bad; r++)
        if (memcmp(&c->r[r], &k->cN.r[r], 16)) {
            bad = 1; snprintf(why, sizeof why, "%s %016llX %016llX, native %016llX %016llX", rname[r],
                              (unsigned long long)c->r[r].d[1], (unsigned long long)c->r[r].d[0], (unsigned long long)k->cN.r[r].d[1], (unsigned long long)k->cN.r[r].d[0]);
        }
    if (!bad && (memcmp(&c->hi, &k->cN.hi, 16) || memcmp(&c->lo, &k->cN.lo, 16))) {
        bad = 1; snprintf(why, sizeof why, "hi/lo %016llX/%016llX, native %016llX/%016llX", (unsigned long long)c->hi.d[0], (unsigned long long)c->lo.d[0],
                          (unsigned long long)k->cN.hi.d[0], (unsigned long long)k->cN.lo.d[0]);
    }
    for (int r = 0; r < 32 && !bad; r++)
        if (memcmp(&c->f[r], &k->cN.f[r], 4)) { bad = 1; snprintf(why, sizeof why, "f%d %08X, native %08X", r, rc_fbits(c->f[r]), rc_fbits(k->cN.f[r])); }
    if (!bad && (c->fcc != k->cN.fcc || memcmp(&c->facc, &k->cN.facc, 4) || c->sa != k->cN.sa)) { bad = 1; snprintf(why, sizeof why, "fcc / facc / sa differ"); }
    if (!bad) {
        const RcVu0 *G = &rc_vu0, *N = &k->vN;
        int r = 0; while (r < 32 && !memcmp(G->vf[r], N->vf[r], 16)) r++;
        if (r < 32) {
            bad = 1; snprintf(why, sizeof why, "VU0 vf%d %08X %08X %08X %08X, native %08X %08X %08X %08X", r,
                              *(const u32 *)&G->vf[r][0], *(const u32 *)&G->vf[r][1], *(const u32 *)&G->vf[r][2], *(const u32 *)&G->vf[r][3],
                              *(const u32 *)&N->vf[r][0], *(const u32 *)&N->vf[r][1], *(const u32 *)&N->vf[r][2], *(const u32 *)&N->vf[r][3]);
        } else if (memcmp(G->acc, N->acc, sizeof G->acc) || memcmp(&G->q, &N->q, 4) || G->clip != N->clip || G->mac != N->mac || G->status != N->status) {
            bad = 1; snprintf(why, sizeof why, "VU0 acc %08X %08X %08X %08X / %08X %08X %08X %08X, q %08X / %08X, clip %06X / %06X, mac %04X / %04X, status %03X / %03X",
                              *(const u32 *)&G->acc[0], *(const u32 *)&G->acc[1], *(const u32 *)&G->acc[2], *(const u32 *)&G->acc[3],
                              *(const u32 *)&N->acc[0], *(const u32 *)&N->acc[1], *(const u32 *)&N->acc[2], *(const u32 *)&N->acc[3],
                              *(const u32 *)&G->q, *(const u32 *)&N->q, G->clip, N->clip, G->mac, N->mac, G->status, N->status);
        }
    }
    if (bad) { k->bad++; static int told; if (told++ < 10) uya_log("native strips check, %s: run %llu differs: %s", k->name, k->runs, why); }
    else k->ok++;
    if (k->runs % 500 == 0) summary(k);
}

static int refs_bad(const char *name, int ng, int na, int NG_, int NA_)
{
    if (ng == NG_ && na == NA_) return 0;
    static int told[3];
    int i = name[1] == 'e' ? 0 : name[5] == 0 ? 1 : 2;
    if (!told[i]++) uya_log("native strips: the level's %s routine has %d / %d data references (not %d / %d): the game's routine runs", name, ng, na, NG_, NA_);
    return 1;
}

/* ---- func_001F7868 ---- */
int uya_stripsetup_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    census(0, 1);
    if (!get_mode()) return 0;
    if (refs_bad("setup", ng, na, SET_NG, SET_NA)) return 0;
    const u32 gp = (u32)c->r[28].d[0];
    if (g[0] != g[1] || a[1] != a[4] || a[5] != a[6] || a[5] != a[7] || a[5] != a[8] || a[5] != a[9] || a[5] != a[11] || gp + (u32)g[2] != a[5]) {
        static int told; if (!told++) uya_log("native strips: the level's setup routine's references are not the expected shape: the game's routine runs");
        return 0;
    }
    SetupRefs R = { g[0], g[2], a[0], a[1], a[2], a[3], a[5], a[10] };
    if (mode_ >= 2) { u32 c0 = M32(R.chain); run_setup(c, &R); native_stripd_convert(R.chain, c0, M32(R.chain)); return 1; }
    Chk *k = &chk[0];
    u32 ch0 = M32(R.chain), sp = (u32)c->r[29].d[0];
    k->nar = 0;
    area(k, "chain", ch0, 0x140);
    area(k, "stack", sp - 0x80, 0x80);
    area(k, "D_0015F704", R.f704, 4);
    area(k, "chain pointer", R.chain, 4);
    check_begin(k, c);
    run_setup(c, &R);
    check_native_done(k, c);
    return 0;
}
void uya_stripsetup_exit(RcCtx *c) { check_exit(&chk[0], c); }

/* ---- func_L00_001FDE48 ---- */
int uya_strip_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    census(1, 1);
    if (!get_mode()) return 0;
    if (refs_bad("strip", ng, na, STRIP_NG, STRIP_NA)) return 0;
    if (g[0] != g[1]) { static int told; if (!told++) uya_log("native strips: the level's strip routine's gp references differ: the game's routine runs"); return 0; }
    StripRefs R = { g[0], a[0], a[1], a[2], a[3] };
    if (mode_ >= 2) { u32 cw = (u32)c->r[28].d[0] + (u32)R.g_chain, c0 = M32(cw); run_strip(c, &R); native_stripd_convert(cw, c0, M32(cw)); return 1; }
    Chk *k = &chk[1];
    const u32 gp = (u32)c->r[28].d[0];
    u32 ch0 = M32(gp + (u32)R.g_chain);
    u64 n = (u32)c->r[4].d[0];
    u64 win = 0x100 + 24 * n;
    k->nar = 0;
    area(k, "chain", ch0, win > WINMAX ? WINMAX : (u32)win);
    area(k, "chain pointer", gp + (u32)R.g_chain, 4);
    check_begin(k, c);
    run_strip(c, &R);
    check_native_done(k, c);
    return 0;
}
void uya_strip_exit(RcCtx *c) { check_exit(&chk[1], c); }

/* ---- func_001F8B6C ---- */
int uya_striplist_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (!get_mode()) { census(2, 0); return 0; }
    if (refs_bad("strip list", ng, na, LIST_NG, LIST_NA)) { census(2, 0); return 0; }
    if (g[0] != g[1] || g[0] != g[2]) { static int told; if (!told++) uya_log("native strips: the level's strip list routine's gp references differ: the game's routine runs"); census(2, 0); return 0; }
    ListRefs R = { g[0], a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7] };
    if (mode_ >= 2) { u32 cw = (u32)c->r[28].d[0] + (u32)R.g_chain, c0 = M32(cw); census(2, run_list(c, &R)); native_stripd_convert(cw, c0, M32(cw)); return 1; }
    Chk *k = &chk[2];
    const u32 gp = (u32)c->r[28].d[0];
    u32 ch0 = M32(gp + (u32)R.g_chain), sp = (u32)c->r[29].d[0];
    k->nar = 0;
    area(k, "chain", ch0, WINMAX);
    area(k, "chain pointer", gp + (u32)R.g_chain, 4);
    area(k, "stack", sp - 0x20, 0x20);
    area(k, "scratchpad", SPR, 0x4000);
    area(k, "header template", R.hdr, 0x30);
    check_begin(k, c);
    census(2, run_list(c, &R));
    check_native_done(k, c);
    return 0;
}
void uya_striplist_exit(RcCtx *c) { check_exit(&chk[2], c); }
