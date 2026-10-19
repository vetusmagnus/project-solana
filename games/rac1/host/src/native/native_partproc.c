/* PartProc (the game's particle culling, sorting and chain building, hand-written assembly in the
 * game: decomp text 0x218B10, 3060 bytes, one copy in every level (level 3: 0x256C08); its texture
 * subroutine _part_load_tex 0x219704, 112 bytes, is reached with bgezal) as native C.
 *
 * What the game's routine does, in order:
 *   - setup: s0..s3 and ra are kept in their own upper halves (pcpyld; pcpyud at the end). Loads
 *     the camera (vf28 the camera position (cam + 0x140), vf29..31 its rotation rows (cam + 0..0x20),
 *     vf20..23 the view-projection matrix (cam + 0xC0..0xF0), vf20..22 scaled by 1024), vf24 (view
 *     block + 0x1C0, its x and y scaled by 0.9375), vf26 (+0xB0), vf27 (+0x1E0), vf25 (a gp
 *     constant); s3 = the far limit (gp). The particle count + 1 is the loop counter (t9); a count of
 *     -1 ends the routine at once.
 *   - pass 1, cull and sort: the particle array (64 bytes a particle) comes to the scratchpad by
 *     toSPR DMA (channel 9) in batches of 32 particles, alternating between 0x0000 and 0x0800, the
 *     next batch sent before the current one is walked; the bucket heads (0x3000..0x37FF, 1024
 *     halfwords) are cleared first. Per particle (count + 1 of them, the last one past the end):
 *     skipped if byte +1 has bit 7. Byte +1 bit 1 clear (a screen quad): its position to camera
 *     space; culled when its depth (ftoi12) is not beyond the near limit (byte +9 low nibble << 10),
 *     not short of the far limit (byte +9 high nibble << 13, capped by s3), its size projected is
 *     under 256 depth units while byte +1 bit 0 is set, or it lies outside the side planes (|x|,|y|
 *     against vf26 * depth - vf27 * size). Bit 1 set (a world quad, a line or a streak, two points:
 *     +0x10 and +0x20): both points through the view-projection matrix, culled when either is
 *     outside the frustum (VCLIPW), when both are outside one plane of the band scaled by vf24, or
 *     by the near / far limits of their midpoint's depth (the two points blended by vf25.z). A
 *     survivor goes into its depth's bucket (depth >> 10, at most 1023): a node at 0x1000.. (its
 *     t9, the old head) and the new head. A culled particle whose byte +1 has bit 5 (drawn last
 *     frame) goes to the special list (0x3800.., up to 0x3FFF: its t9 and byte +1 without bit 5).
 *   - the special list's bytes are written back into the particle array (byte +1).
 *   - the buckets from 1023 down to 1 (bucket 0 is never read) become a back-to-front list of
 *     particle numbers (count - t9) at 0x0000, ended by 0. Nothing in it: the routine ends.
 *   - two tables to the scratchpad (toSPR): 0x40 qwords of texture cache (TEX0 per texture, 8 bytes;
 *     bit 63 set = resident this frame) to 0x3000, 0x40 qwords of a cosine table to 0x3400.
 *   - the chain start, at the chain pointer (gp), built in the scratchpad at 0x1000 and sent by
 *     fromSPR DMA (channel 8): a REF to the particle program's MPG packet (qwc at 0x101070, data
 *     0x101080), the CNT template (gp), the matrix (cam + 0x40..0x70; rows 0..2 scaled by 1024), the
 *     view block's +0x210 and +0x1A0 qwords, the REF template (gp). Then the record blocks at
 *     0x1800 / 0x1000 alternately, each starting one qword in (its CNT header).
 *   - pass 2, per particle in the list (from RAM), by byte +1 & 3; byte +1 gets bit 5 (drawn):
 *       0 screen quad (4 qw): position - camera; the 4 colour bytes (+4) as words, word 3 replaced
 *         by the fade (byte +7 times min((far - depth) >> 4, depth - near, 0x1000), >> 12); the
 *         corner offsets (cosine table at byte +8 and +8 + 0x300/4) times Q (size * vf25.w / (depth
 *         + vf25.z)); byte +3 (and 0) at +0x28; the texture's TEX0 at +0x30.
 *       1 world quad (4 qw): as 0 with +0x20 / +0x24 the table entries times the size (+0xC) times
 *         vf25.x, and word 3 = 1.
 *       2 line (4 qw): the two points, the two colours, word 3 = -1; no texture.
 *       3 streak (6 qw): the two points, the half width (the normalised cross product of the
 *         segment and the midpoint's position), the two colours with their fades, TEX0 at +0x50,
 *         byte +3 at +0x58; word 3 = -2.
 *     A texture not resident (its cache word's bit 63 clear) gets a slot in the GIF paging table
 *     (16 bytes at the table + 16 * count; count and VRAM cursor in gp words): its TEX0's TBP and
 *     size, the VRAM cursor (moved on by 0x400 - 0x100 * (TEX0 & 0xF), then 0x400 more), and the TEX0
 *     rebuilt from vf20 with the new addresses, kept in the scratchpad cache.
 *     A block whose record started in its last 0x100 bytes is closed: the trailer (vf18: MSCALF),
 *     the header (vf17: CNT with the qword count, the UNPACK with its size), the record count in
 *     the first record's qword 2 .w, and it goes to the chain by fromSPR; the chain pointer moves on.
 *   - the end: the last block closed the same way (unless empty), a CNT with FLUSHA (vf19), sent;
 *     the paging cursor, the paging count and the chain pointer back to their gp words.
 *
 * This file does the same, word for word: every store (scratchpad, chain, particle bytes, paging
 * slots, gp words, DMA registers, in order), every VU0 macro operation with its result kept in the
 * VU0 registers, the VU's float arithmetic as the translated game code has it (each product and sum
 * clamped to +-MAX). The routine reads no MAC or status flag (only the clip flags), so those are
 * left alone, as the translated routine leaves them. Where a GPR's upper 64 bits reach a VU
 * register or a 128-bit store, they are carried (at's at entry into vf1 and vf3, a1's into vf4, the
 * view block's qword into vf5, vf20's into the paged records' +0x38). At the end s0..s3 and ra get
 * their entry values with zero upper halves, f0..f2 and f4 as the routine leaves them.
 * The data addresses differ per level: tools/recomp.py passes the level's own (its gp offsets and
 * its address pairs, read from the level's bytes) to the hook it puts at the routine's start.
 *
 * UYA_NPARTPROC: 0 the game's routine (translated); 1 check (this file runs first, its results
 * are put aside, the game's routine runs from the same state, and the two are compared at its
 * end: the chain, the scratchpad, the particle array, the paging slots and gp words, the DMA
 * registers, the VU0 registers); 2 this file (default), the chain built as the game builds it. */
#include "rc.h"
#include "uya_host.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "native_moby.h"
#include "native_part.h"
#include "uya_runtime.h"

void uya_log(const char *fmt, ...);

#define SPR 0x70000000u
#define W32(x) SE32((u32)(x))
#define VW(n, k) (*(u32 *)&VF(n)[k])
static float (*volatile lib_sqrtf)(float) = sqrtf;

/* the level's data, from the hook's arguments (in the routine's order) */
typedef struct {
    s32 g_arr, g_cnt, g_vf25, g_far, g_arr2, g_cnt2, g_chain, g_arr3, g_pgn, g_cur,
        g_vf17, g_vf18, g_vf19, g_vf20, g_tcnt, g_tref, g_cur_w, g_pgn_w, g_chain_w;
    u32 cam, view, pgtab, texc, cost, mpg_qwc, mpg_addr, view2, cam2;
} PartRefs;
#define NG 19
#define NA 9

/* ---- the VU0 macro operations, as tools/recomp.py translates them (no MAC/status flags) ---- */
enum { ADD, SUB, MUL, MADD };
enum { X = 8, Y = 4, Z = 2, W = 1, XY = 12, XYZ = 14, XYZW = 15 };
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
        case ADD: r[i] = rc_fc(a + b); break;
        case SUB: r[i] = rc_fc(a - b); break;
        case MUL: r[i] = rc_fc(a * b); break;
        default: r[i] = rc_fc(VACC[i] + rc_fc(a * b)); break;
        }
    }
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
}
static void clipw(int s)
{
    float w = __builtin_fabsf(VF(s)[3]), x = VF(s)[0], y = VF(s)[1], z = VF(s)[2];
    u32 fl = (x > w) | ((x < -w) << 1) | ((y > w) << 2) | ((y < -w) << 3) | ((z > w) << 4) | ((z < -w) << 5);
    rc_vu0.clip = ((rc_vu0.clip << 6) | fl) & 0xFFFFFF;
}
static void ftoi12(int d, int k, int s) { s32 v = rc_ftoi(VF(s)[k], 12); memcpy(&VF(d)[k], &v, 4); }
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
static void qmtc2(int d, u64 lo, u64 hi) { u64 w[2] = { lo, hi }; memcpy(VF(d), w, 16); }
static u64 vlo(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[0]; }
static u64 vhi(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[1]; }

/* ---- MMI on the low doubleword (the upper one never matters here, except where carried) ---- */
static u64 pminw(u64 a, u64 b)
{
    s32 a0 = (s32)(u32)a, a1 = (s32)(u32)(a >> 32), b0 = (s32)(u32)b, b1 = (s32)(u32)(b >> 32);
    return (u64)(u32)(a0 < b0 ? a0 : b0) | (u64)(u32)(a1 < b1 ? a1 : b1) << 32;
}
static u64 pmaxw0(u64 a)
{
    s32 a0 = (s32)(u32)a, a1 = (s32)(u32)(a >> 32);
    return (u64)(u32)(a0 > 0 ? a0 : 0) | (u64)(u32)(a1 > 0 ? a1 : 0) << 32;
}
/* pextlb then pextlh with zero: the low word's 4 bytes as 4 words */
static R128 bytes4(u64 v)
{
    R128 r;
    for (int i = 0; i < 4; i++) r.w[i] = (u32)(v >> (8 * i)) & 0xFF;
    return r;
}

static int busy(u32 ch) { return (M32(ch) & 0x100) != 0; }
static void wait_idle(u32 ch) { for (int k = 0; k < 1000000 && busy(ch); k++) ; }

/* path counts (logged by check mode; a path at 0 is untested) */
static unsigned long long st_part, st_hidden, st_k0, st_k1, st_near0, st_far0, st_small0, st_side0,
    st_clip1, st_band1, st_near1, st_far1, st_bucket, st_special, st_specfull, st_specw, st_empty,
    st_list, st_m0, st_m1, st_line, st_streak, st_page, st_blocks, st_lastblk, st_emptylast, st_quit;

/* _part_load_tex (0x219704): t0 the texture's cache word, t1 its scratchpad entry - 0x3000 */
static void load_tex(u64 *t0, u64 *t0h, u64 t1, u64 *s0, u64 *s1, u64 *s2)
{
    st_page++;
    u64 a2 = W32((u32)*t0 >> 4), a3 = *t0 & 0xF, a1 = *t0 >> 36;
    rc_sw((u32)*s2, (u32)a2);
    u64 x = vlo(20); *t0h = vhi(20);
    rc_sw((u32)*s2 + 8, (u32)a1);
    M16((u32)*s2 + 0xC) = 0x505;
    a1 = 0x400;
    M16((u32)*s2 + 4) = (u16)a3;
    a3 = W32((u32)a3 << 8);
    a1 = W32((u32)a1 - (u32)a3);
    a2 = W32((u32)*s0 >> 8);
    M16((u32)*s2 + 6) = (u16)a2;
    a3 = a3 << 43;
    a2 = a2 << 37;
    *s0 = W32((u32)*s0 + (u32)a1);
    x |= a2;
    a2 = W32((u32)*s0 >> 8);
    x |= a2;
    M16((u32)*s2 + 0xE) = (u16)a2;
    *s2 = W32((u32)*s2 + 0x10);
    *s1 = W32((u32)*s1 + 1);
    *s0 = W32((u32)*s0 + 0x400);
    x |= a3;
    rc_sd((u32)t1 + 0x3000, x);
    *t0 = x;
}

/* a full or last block (or the start) to the chain by fromSPR: base t8 - at, at bytes */
static void send(u64 t8, u64 at, u64 t9)
{
    rc_sw(0x1000D080u, (u32)t8);
    rc_sw(0x1000E010u, 0x100);
    rc_sw(0x1000D010u, (u32)W32((u32)t9 - (u32)at));
    rc_sw(0x1000D020u, (u32)at >> 4);
    rc_sw(0x1000D000u, 0x100);
}

static void run(RcCtx *c, const PartRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    u64 ath = c->r[1].d[1], a1h = c->r[5].d[1];
    f32 f0 = c->f[0], f1 = c->f[1], f2 = c->f[2], f4 = c->f[4];
    u64 at, v0, v1, a0, a1, a2, a3, t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, s0, s1, s2, s3;
    u64 t0h = 0, s3h = c->r[19].d[0];   /* s3's upper half after pcpyld s3, s3, zero */

    t8 = SE32(M32(gp + (u32)R->g_arr));
    at = R->cam; v0 = R->view;
    t9 = SE32(M32(gp + (u32)R->g_cnt));
    lqc2(28, (u32)at + 0x140);
    lqc2(29, (u32)at); lqc2(30, (u32)at + 0x10); lqc2(31, (u32)at + 0x20);
    lqc2(20, (u32)at + 0xC0); lqc2(21, (u32)at + 0xD0); lqc2(22, (u32)at + 0xE0); lqc2(23, (u32)at + 0xF0);
    lqc2(24, (u32)v0 + 0x1C0); lqc2(26, (u32)v0 + 0xB0); lqc2(27, (u32)v0 + 0x1E0);
    lqc2(25, gp + (u32)R->g_vf25);
    s3 = SE32(M32(gp + (u32)R->g_far));
    at = SE32(0x44800000u);
    t7 = 0x1000D400u;
    qmtc2(1, at, ath);
    t9 = W32((u32)t9 + 1);
    at = SE32(0x3F700000u);
    fm(20, XYZW, MUL, 20, 1, 0); fm(21, XYZW, MUL, 21, 1, 0); fm(22, XYZW, MUL, 22, 1, 0);
    qmtc2(1, at, ath);
    fm(24, XY, MUL, 24, 1, 0);
    t2 = SPR + 0x3800; t3 = SPR + 0x1000; t4 = SPR + 0x3000; t5 = SPR + 0x800;
    if (t9 == 0) { st_quit++; goto done; }

    /* pass 1: the first batch, the bucket heads cleared */
    rc_sw((u32)t7 + 0x10, (u32)t8); rc_sw((u32)t7 + 0xC10, 0x200); rc_sw((u32)t7 + 0x20, 0x80);
    rc_sw((u32)t7 + 0x80, 0); rc_sw((u32)t7, 0x100);
    t8 = W32((u32)t8 + 0x800);
    for (u32 a = (u32)t4; a != (u32)t4 + 0x800; a += 0x10) memset((void *)(uintptr_t)a, 0, 16);
    for (;;) {
        v0 = SE32(-0x800);
        if (t9 == 0) break;
        v0 &= t5;
        t6 = W32((u32)t9 - 32);
        t6 = pmaxw0(t6);
        t5 = v0 ^ 0x800;
        if (t6 != 0) {
            /* the next batch to the other half */
            rc_sw((u32)t7 + 0x10, (u32)t8); rc_sw((u32)t7 + 0xC10, 0x200); rc_sw((u32)t7 + 0x20, 0x80);
            rc_sw((u32)t7 + 0x80, (u32)v0); rc_sw((u32)t7, 0x100);
            t8 = W32((u32)t8 + 0x800);
        }
        t5 = W32((u32)t5 - 0x40);
        for (;;) {
            lqc2(1, (u32)t5 + 0x50);
            if (t9 == t6) break;
            at = SE32(M32((u32)t5 + 0x4C));
            t5 = W32((u32)t5 + 0x40);
            fm(2, XYZ, SUB, 1, 28, BV);
            t1 = (u64)(s64)(s8)M8((u32)t5 + 1);
            qmtc2(3, at, ath);
            t9 = W32((u32)t9 - 1);
            at = t1 & 2;
            st_part++;
            if ((s64)t1 < 0) { st_hidden++; continue; }
            fm(3, XY, MUL, 25, 3, 0);
            int keep = 0;
            if (at == 0) {
                /* a screen quad */
                st_k0++;
                fm(ACC, XYZ, MUL, 29, 2, 0);
                t0 = t1 & 0xDF;
                fm(ACC, XYZ, MADD, 30, 2, 1);
                a2 = M8((u32)t5 + 9);
                fm(2, XYZ, MADD, 31, 2, 2);
                fm(4, XY, MUL, 27, 3, 0);
                a0 = a2 & 0xF0;
                ftoi12(7, 0, 3);
                a0 = W32((u32)a0 << 13);
                a2 &= 0xF;
                ftoi12(6, 2, 2);
                a2 = W32((u32)a2 << 10);
                { f32 x = __builtin_fabsf(VF(2)[0]), y = __builtin_fabsf(VF(2)[1]); VF(3)[0] = x; VF(3)[1] = y; }
                fm(5, XY, MUL, 26, 2, 2);
                a0 = pminw(a0, s3);
                a1 = vlo(7); a1h = vhi(7);
                fm(3, XY, SUB, 3, 4, BV);
                a3 = 0x3FF;
                a1 = SE32((u32)a1);
                fm(3, XY, SUB, 5, 3, BV);
                v0 = SE32(VW(6, 2));
                v1 = v0 - a2;
                a1 = v0 - a1;
                if ((s64)v1 <= 0) st_near0++;
                else if ((s64)(v0 - a0) >= 0) st_far0++;
                else {
                    a1 = (s64)a1 < 256;
                    a1 &= t1;
                    v0 = SE32((s32)(u32)v0 >> 10);
                    v1 = vlo(3);
                    v0 = pminw(v0, a3);
                    v0 = W32((u32)v0 << 1);
                    if (a1 != 0) st_small0++;
                    else if ((s64)v1 < 0 || (s64)(v1 << 32) < 0) st_side0++;
                    else { v0 = W32((u32)v0 + (u32)t4); keep = 1; }
                }
            } else {
                /* two points (a world quad, a line, a streak) */
                st_k1++;
                lqc2(4, (u32)t5 + 0x20);
                t0 = t1 & 0xDF;
                fm(ACC, XYZW, MUL, 20, 2, 0);
                a0 = M8((u32)t5 + 9);
                fm(4, XYZ, SUB, 4, 28, BV);
                fm(ACC, XYZW, MADD, 21, 2, 1);
                a1 = a0 & 0xF0;
                fm(ACC, XYZW, MADD, 22, 2, 2);
                a1 = W32((u32)a1 << 13);
                fm(1, XYZW, MADD, 23, 0, 3);
                a0 &= 0xF;
                fm(ACC, XYZW, MUL, 20, 4, 0);
                a0 = W32((u32)a0 << 10);
                fm(ACC, XYZW, MADD, 21, 4, 1);
                fm(ACC, XYZW, MADD, 22, 4, 2);
                fm(3, XYZW, MADD, 23, 0, 3);
                fm(ACC, XYZW, MUL, 2, 25, 2);
                fm(2, XYZW, MADD, 4, 25, 2);
                clipw(1);
                clipw(3);
                fm(1, XY, MUL, 1, 24, BV);
                fm(3, XY, MUL, 3, 24, BV);
                fm(ACC, XYZ, MUL, 29, 2, 0);
                fm(ACC, XYZ, MADD, 30, 2, 1);
                fm(2, XYZ, MADD, 31, 2, 2);
                at = SE32(rc_vu0.clip);
                clipw(1);
                clipw(3);
                at &= 0xFFF;
                ftoi12(2, 2, 2);
                if (at != 0) st_clip1++;
                else {
                    a1 = pminw(a1, s3); a1h = pminw(a1h, s3h);
                    at = SE32(rc_vu0.clip);
                    a3 = 0x3FF;
                    v0 = W32((u32)at >> 6);
                    at &= 0x3F;
                    v1 = SE32(VW(2, 2));
                    at &= v0;
                    if (at != 0) st_band1++;
                    else {
                        a0 = W32((u32)v1 - (u32)a0);
                        if ((s64)a0 <= 0) st_near1++;
                        else {
                            a1 = W32((u32)v1 - (u32)a1);
                            if ((s64)a1 >= 0) st_far1++;
                            else {
                                v0 = SE32((s32)(u32)v1 >> 10);
                                v0 = pminw(v0, a3);
                                v0 = W32((u32)v0 << 1);
                                v0 = W32((u32)v0 + (u32)t4);
                                keep = 1;
                            }
                        }
                    }
                }
            }
            if (keep) {
                /* into its depth's bucket */
                st_bucket++;
                a0 = M16((u32)v0);
                M16((u32)v0) = (u16)t3;
                M16((u32)t3) = (u16)t9;
                M16((u32)t3 + 2) = (u16)a0;
                t3 = W32((u32)t3 + 4);
                continue;
            }
            /* culled: drawn last frame, to the special list */
            if (t0 == t1) continue;
            if (t2 & 0x4000) { st_specfull++; continue; }
            st_special++;
            M16((u32)t2) = (u16)t9;
            M16((u32)t2 + 2) = (u16)t0;
            t2 = W32((u32)t2 + 4);
        }
    }

    /* the special list's bytes back into the array */
    t8 = SE32(M32(gp + (u32)R->g_arr2));
    t7 = SE32(M32(gp + (u32)R->g_cnt2));
    t3 = SPR + 0x3800;
    if (t3 != t2) {
        v0 = M16((u32)t3);
        for (;;) {
            v1 = M16((u32)t3 + 2);
            t3 = W32((u32)t3 + 4);
            v0 = W32((u32)t7 - (u32)v0);
            v0 = W32((u32)v0 << 6);
            v0 = W32((u32)v0 + (u32)t8);
            M8((u32)v0 + 1) = (u8)v1;
            st_specw++;
            if (t3 == t2) break;
            v0 = M16((u32)t3);
        }
    }
    /* the buckets, far to near, into the list at 0x0000 */
    t3 = SPR; t4 = SPR; t5 = SPR + 0x37FE; t6 = SPR + 0x2FFE;
    for (;;) {
        at = M16((u32)t5);
        t5 = W32((u32)t5 - 2);
        if (t5 == t6) break;
        if (at == 0) continue;
        at |= t3;
        do {
            v0 = M16((u32)at);
            at = M16((u32)at + 2);
            v0 = W32((u32)t7 - (u32)v0);
            at = W32((u32)at + (u32)t3);
            M16((u32)t4) = (u16)v0;
            t4 = W32((u32)t4 + 2);
            st_list++;
        } while (at != t3);
    }
    M16((u32)t4) = 0;
    t2 = SPR; t7 = t4; t6 = SPR; t8 = SPR + 0x1000;
    t4 = 0;
    if (t6 == t7) { st_empty++; goto done; }

    /* the tables; the chain start */
    t9 = SE32(M32(gp + (u32)R->g_chain));
    t3 = SE32(M32(gp + (u32)R->g_arr3));
    s1 = SE32(M32(gp + (u32)R->g_pgn));
    s0 = SE32(M32(gp + (u32)R->g_cur));
    at = W32((u32)s1 << 4);
    s2 = W32(R->pgtab + (u32)at);
    lqc2(17, gp + (u32)R->g_vf17); lqc2(18, gp + (u32)R->g_vf18); lqc2(19, gp + (u32)R->g_vf19); lqc2(20, gp + (u32)R->g_vf20);
    f4 = VF(25)[0];
    rc_sw(0x1000D410u, R->texc); rc_sw(0x1000E010u, 0x200); rc_sw(0x1000D420u, 0x40); rc_sw(0x1000D480u, 0x3000); rc_sw(0x1000D400u, 0x100);
    rc_sw(0x1000E010u, 0x200); rc_sw(0x1000D410u, R->cost); rc_sw(0x1000D420u, 0x40); rc_sw(0x1000D480u, 0x3400); rc_sw(0x1000D400u, 0x100);
    {
        static const R128 zq = {{ 0, 0 }};
        rc_sq(&zq, (u32)t8);
        v0 = SE32(M32(R->mpg_qwc));
        rc_sw((u32)t8 + 4, R->mpg_addr);
        rc_sw((u32)t8, 0x30000000u);
        t9 = W32((u32)t9 + 0x10);
        M16((u32)t8) = (u16)v0;
        t8 = W32((u32)t8 + 0x10);
        R128 q1, q2, q3, q4;
        {   /* testing: UYA_PSTARTLOG=1: the chain start's templates, once */
            static int left = -1; if (left < 0) left = getenv("UYA_PSTARTLOG") ? 1 : 0;
            if (left > 0) {
                left--;
                u32 tc = gp + (u32)R->g_tcnt, tr = gp + (u32)R->g_tref, ra = M32(tr + 4);
                uya_log("PartProc start: MPG qwc %u at %08X; tcnt %08X %08X %08X %08X; tref %08X %08X %08X %08X", M16(R->mpg_qwc), R->mpg_addr, M32(tc), M32(tc + 4), M32(tc + 8), M32(tc + 12), M32(tr), M32(tr + 4), M32(tr + 8), M32(tr + 12));
                for (int k = 0; k < 16; k++) uya_log("   ref %08X: %08X %08X %08X %08X", ra + 16 * k, M32(ra + 16 * k), M32(ra + 16 * k + 4), M32(ra + 16 * k + 8), M32(ra + 16 * k + 12));
                for (int k = 17; k <= 19; k++) { u32 g = gp + (u32)(k == 17 ? R->g_vf17 : k == 18 ? R->g_vf18 : R->g_vf19); uya_log("   vf%d template %08X %08X %08X %08X", k, M32(g), M32(g + 4), M32(g + 8), M32(g + 12)); }
            }
        }
        rc_lq(&q1, gp + (u32)R->g_tcnt); rc_lq(&q2, gp + (u32)R->g_tref);
        rc_sq(&q1, (u32)t8); rc_sq(&q2, (u32)t8 + 0x70);
        rc_lq(&q3, R->view2 + 0x210); rc_lq(&q4, R->view2 + 0x1A0);
        rc_sq(&q3, (u32)t8 + 0x50); rc_sq(&q4, (u32)t8 + 0x60);
        qmtc2(5, SE32(0x44800000u), q3.d[1]);
        lqc2(1, R->cam2 + 0x40); lqc2(2, R->cam2 + 0x50); lqc2(3, R->cam2 + 0x60); lqc2(4, R->cam2 + 0x70);
        fm(1, XYZW, MUL, 1, 5, 0); fm(2, XYZW, MUL, 2, 5, 0); fm(3, XYZW, MUL, 3, 5, 0);
        sqc2(1, (u32)t8 + 0x10); sqc2(2, (u32)t8 + 0x20); sqc2(3, (u32)t8 + 0x30); sqc2(4, (u32)t8 + 0x40);
        t8 = W32((u32)t8 + 0x80);
        t9 = W32((u32)t9 + 0x80);
        at = t8 & 0x7FF;
        t8 = W32((u32)t8 - (u32)at);
        send(t8, at, t9);
        t8 ^= 0x800;
        t8 = W32((u32)t8 + 0x10);
        t9 = W32((u32)t9 + 0x10);
    }

    /* pass 2: the records */
    t5 = M16((u32)t6);
    t5 = W32((u32)t5 << 6);
    t5 = W32((u32)t5 + (u32)t3);
    for (;;) {
        v0 = M8((u32)t5 + 1);
        if (t6 == t7) break;
        lqc2(1, (u32)t5 + 0x10);
        at = t5;
        t6 = W32((u32)t6 + 2);
        v1 = v0 & 3;
        t5 = M16((u32)t6);
        if (v1 == 0) {
            /* a screen quad */
            st_m0++;
            fm(1, XYZ, SUB, 1, 28, BV);
            t1 = M8((u32)at + 2);
            a2 = M8((u32)at + 8);
            t5 = W32((u32)t5 << 6);
            a3 = SE32(M32((u32)at + 4));
            t5 = W32((u32)t5 + (u32)t3);
            lqc2(3, (u32)at);
            t1 = W32((u32)t1 << 3);
            a0 = M8((u32)at + 9);
            t1 = W32((u32)t1 + (u32)t2);
            fm(ACC, XYZ, MUL, 29, 1, 0);
            fm(ACC, XYZ, MADD, 30, 1, 1);
            a2 = W32((u32)a2 << 2);
            fm(2, XYZ, MADD, 31, 1, 2);
            a1 = W32((u32)a2 + 0x300);
            fm(3, W, MUL, 3, 25, BV);
            a1 &= 0x3FF;
            t0 = M64((u32)t1 + 0x3000);
            v1 = a0 & 0xF;
            ftoi12(5, 2, 2);
            a0 &= 0xF0;
            fm(2, Z, ADD, 2, 25, BV);
            a2 = W32((u32)a2 + (u32)t2);
            a1 = W32((u32)a1 + (u32)t2);
            a0 = W32((u32)a0 << 13);
            a1 = M32((u32)a1 + 0x3400);
            R128 a3q = bytes4(a3);
            a2 = M32((u32)a2 + 0x3400);
            rc_vu0.q = rc_fc(rc_fdiv(VF(3)[3], VF(2)[2]));
            a1 = a1 << 32;
            a1 |= a2;
            a2 = v0 | 0x20;
            qmtc2(4, a1, a1h);
            if (v0 != a2) M8((u32)at + 1) = (u8)a2;
            v1 = W32((u32)v1 << 10);
            a1 = M8((u32)at + 7);
            a0 = pminw(a0, s3);
            sqc2(1, (u32)t8);
            a2 = VW(5, 2);
            v1 = W32((u32)a2 - (u32)v1);
            a0 = W32((u32)a0 - (u32)a2);
            a0 = W32((u32)a0 >> 4);
            a2 = 0x1000;
            rc_sq(&a3q, (u32)t8 + 0x10);
            a0 = pminw(a0, v1);
            rc_sw((u32)t8 + 0xC, 0);
            a0 = pminw(a0, a2);
            fm(4, XY, MUL, 4, 0, BQ);
            u32 lo = (u32)((s64)(s32)(u32)a0 * (s32)(u32)a1);
            t0h = 0;
            if ((s64)t0 >= 0) load_tex(&t0, &t0h, t1, &s0, &s1, &s2);
            a0 = M8((u32)at + 3);
            at = t8 & 0x700;
            { R128 q = {{ t0, t0h }}; rc_sq(&q, (u32)t8 + 0x30); }
            v0 = 0x700;
            sqc2(4, (u32)t8 + 0x20);
            v1 = SE32(lo);
            rc_sd((u32)t8 + 0x28, a0);
            t9 = W32((u32)t9 + 0x40);
            v1 = W32((u32)v1 >> 12);
            rc_sw((u32)t8 + 0x1C, (u32)v1);
            t8 = W32((u32)t8 + 0x40);
            t4 = W32((u32)t4 + 4);
        } else {
            v1 = W32((u32)v1 - 2);
            fm(1, XYZ, SUB, 1, 28, BV);
            a3 = SE32(M32((u32)at + 4));
            if ((s64)v1 < 0) {
                /* a world quad */
                st_m1++;
                t1 = M8((u32)at + 2);
                t5 = W32((u32)t5 << 6);
                a2 = M8((u32)at + 8);
                t5 = W32((u32)t5 + (u32)t3);
                f0 = MF((u32)at + 0xC);
                t1 = W32((u32)t1 << 3);
                fm(ACC, XYZ, MUL, 29, 1, 0);
                a0 = M8((u32)at + 9);
                fm(ACC, XYZ, MADD, 30, 1, 1);
                fm(2, XYZ, MADD, 31, 1, 2);
                a2 = W32((u32)a2 << 2);
                a1 = W32((u32)a2 + 0x300);
                t1 = W32((u32)t1 + (u32)t2);
                t0 = M64((u32)t1 + 0x3000);
                v1 = a0 & 0xF;
                ftoi12(2, 2, 2);
                a0 &= 0xF0;
                f0 = rc_fc(f0 * f4);
                a1 &= 0x3FF;
                a1 = W32((u32)a1 + (u32)t2);
                a2 = W32((u32)a2 + (u32)t2);
                f1 = MF((u32)a2 + 0x3400);
                R128 a3q = bytes4(a3);
                f2 = MF((u32)a1 + 0x3400);
                v1 = W32((u32)v1 << 10);
                a1 = M8((u32)at + 7);
                a0 = W32((u32)a0 << 13);
                a0 = pminw(a0, s3);
                sqc2(1, (u32)t8);
                a2 = VW(2, 2);
                f1 = rc_fc(f1 * f0);
                v1 = W32((u32)a2 - (u32)v1);
                f2 = rc_fc(f2 * f0);
                a0 = W32((u32)a0 - (u32)a2);
                rc_sq(&a3q, (u32)t8 + 0x10);
                a2 = v0 | 0x20;
                a0 = W32((u32)a0 >> 4);
                if (v0 != a2) M8((u32)at + 1) = (u8)a2;
                a2 = 0x1000;
                MF((u32)t8 + 0x20) = f1;
                a0 = pminw(a0, v1);
                MF((u32)t8 + 0x24) = f2;
                a0 = pminw(a0, a2);
                rc_sw((u32)t8 + 0xC, 1);
                u32 lo = (u32)((s64)(s32)(u32)a0 * (s32)(u32)a1);
                t0h = 0;
                if ((s64)t0 >= 0) load_tex(&t0, &t0h, t1, &s0, &s1, &s2);
                a0 = M8((u32)at + 3);
                at = t8 & 0x700;
                { R128 q = {{ t0, t0h }}; rc_sq(&q, (u32)t8 + 0x30); }
                v0 = 0x700;
                rc_sd((u32)t8 + 0x28, a0);
                v1 = W32(lo >> 12);
                t9 = W32((u32)t9 + 0x40);
                rc_sw((u32)t8 + 0x1C, (u32)v1);
                t8 = W32((u32)t8 + 0x40);
                t4 = W32((u32)t4 + 4);
            } else {
                lqc2(2, (u32)at + 0x20);
                t5 = W32((u32)t5 << 6);
                a2 = SE32(M32((u32)at + 0xC));
                t5 = W32((u32)t5 + (u32)t3);
                fm(2, XYZ, SUB, 2, 28, BV);
                if (v1 == 0) {
                    /* a line */
                    st_line++;
                    R128 a3q = bytes4(a3);
                    sqc2(1, (u32)t8);
                    R128 a2q = bytes4(a2);
                    rc_sq(&a3q, (u32)t8 + 0x10);
                    rc_sq(&a2q, (u32)t8 + 0x30);
                    sqc2(2, (u32)t8 + 0x20);
                    rc_sw((u32)t8 + 0xC, 0xFFFFFFFFu);
                    a0 = v0 | 0x20;
                    rc_sw((u32)t8 + 0x2C, 0);
                    if (v0 != a0) M8((u32)at + 1) = (u8)a0;
                    at = t8 & 0x700;
                    v0 = 0x700;
                    t9 = W32((u32)t9 + 0x40);
                    t8 = W32((u32)t8 + 0x40);
                    t4 = W32((u32)t4 + 4);
                } else {
                    /* a streak */
                    st_streak++;
                    t1 = M8((u32)at + 2);
                    R128 a3q = bytes4(a3);
                    a0 = M8((u32)at + 7);
                    fm(ACC, XYZ, MUL, 1, 25, 2);
                    a1 = M8((u32)at + 0xF);
                    fm(4, XYZ, MADD, 2, 25, 2);
                    t1 = W32((u32)t1 << 3);
                    fm(3, XYZ, SUB, 2, 1, BV);
                    t1 = W32((u32)t1 + (u32)t2);
                    v1 = M8((u32)at + 9);
                    R128 a2q = bytes4(a2);
                    t0 = M64((u32)t1 + 0x3000);
                    {   /* vopmula, vopmsub: vf3 = vf3 x vf4 */
                        f32 x = rc_fc(VF(3)[1] * VF(4)[2]), y = rc_fc(VF(3)[2] * VF(4)[0]), z = rc_fc(VF(3)[0] * VF(4)[1]);
                        VACC[0] = x; VACC[1] = y; VACC[2] = z;
                        x = rc_fc(VACC[0] - rc_fc(VF(4)[1] * VF(3)[2]));
                        y = rc_fc(VACC[1] - rc_fc(VF(4)[2] * VF(3)[0]));
                        z = rc_fc(VACC[2] - rc_fc(VF(4)[0] * VF(3)[1]));
                        VF(3)[0] = x; VF(3)[1] = y; VF(3)[2] = z;
                    }
                    fm(ACC, XYZ, MUL, 29, 4, 0);
                    fm(ACC, XYZ, MADD, 30, 4, 1);
                    fm(4, XYZ, MADD, 31, 4, 2);
                    fm(5, XYZ, MUL, 3, 3, BV);
                    fm(6, XYZ, ADD, 0, 0, 3);
                    u64 a1lo = a0, a1hi = a1;        /* pcpyld a1, a1, a0 */
                    a1 = a1lo; a1h = a1hi;
                    ftoi12(4, 2, 4);
                    a0 = v1 & 0xF0;
                    fm(ACC, X, ADD, 5, 5, 1);
                    v1 &= 0xF;
                    fm(5, X, MADD, 6, 5, 2);
                    rc_sq(&a3q, (u32)t8 + 0x30);
                    v1 = W32((u32)v1 << 10);
                    rc_sq(&a2q, (u32)t8 + 0x40);
                    a0 = W32((u32)a0 << 13);
                    a0 = pminw(a0, s3);
                    rc_vu0.q = rc_fdiv(VF(1)[3], lib_sqrtf(__builtin_fabsf(VF(5)[0])));
                    sqc2(1, (u32)t8);
                    a2 = VW(4, 2);
                    sqc2(2, (u32)t8 + 0x10);
                    v1 = W32((u32)a2 - (u32)v1);
                    a0 = W32((u32)a0 - (u32)a2);
                    a2 = v0 | 0x20;
                    a0 = W32((u32)a0 >> 4);
                    if (v0 != a2) M8((u32)at + 1) = (u8)a2;
                    a2 = 0x1000;
                    a0 = pminw(a0, v1);
                    v1 = M8((u32)at + 3);
                    a0 = pminw(a0, a2);
                    rc_sw((u32)t8 + 0xC, 0xFFFFFFFEu);
                    rc_sd((u32)t8 + 0x58, v1);
                    /* pmultuw a0, a0, a1 (a0 in both doublewords) */
                    u64 p0 = (u64)(u32)a0 * (u32)a1lo, p1 = (u64)(u32)a0 * (u32)a1hi;
                    if ((s64)t0 >= 0) load_tex(&t0, &t0h, t1, &s0, &s1, &s2);
                    rc_sd((u32)t8 + 0x50, t0);
                    fm(3, XYZ, MUL, 3, 0, BQ);
                    rc_sw((u32)t8 + 0x3C, (u32)p0 >> 12);
                    rc_sw((u32)t8 + 0x4C, (u32)p1 >> 12);
                    at = t8 & 0x700;
                    v0 = 0x700;
                    sqc2(3, (u32)t8 + 0x20);
                    t9 = W32((u32)t9 + 0x60);
                    rc_sw((u32)t8 + 0x2C, 0);
                    t8 = W32((u32)t8 + 0x60);
                    t4 = W32((u32)t4 + 6);
                }
            }
        }
        if (at != v0) continue;
        /* the block is full: closed and sent */
        st_blocks++;
        sqc2(18, (u32)t8);
        t8 = W32((u32)t8 + 0x10);
        t9 = W32((u32)t9 + 0x10);
        at = t8 & 0x7FF;
        t8 = W32((u32)t8 - (u32)at);
        v0 = W32((u32)at >> 4);
        sqc2(17, (u32)t8);
        v0 = W32((u32)v0 - 1);
        M16((u32)t8) = (u16)v0;
        v0 = W32((u32)v0 - 1);
        M8((u32)t8 + 0xE) = (u8)v0;
        rc_sw((u32)t8 + 0x3C, (u32)t4);
        t4 = 0;
        wait_idle(0x1000D000u);
        send(t8, at, t9);
        t8 ^= 0x800;
        t8 = W32((u32)t8 + 0x10);
        t9 = W32((u32)t9 + 0x10);
    }
    /* the last block, the closing CNT */
    at = t8 & 0x7FF;
    if (t4 != 0) {
        st_lastblk++;
        sqc2(18, (u32)t8);
        v0 = W32((u32)t8 - (u32)at);
        t8 = W32((u32)t8 + 0x10);
        t9 = W32((u32)t9 + 0x10);
        at = W32((u32)at >> 4);
        rc_sw((u32)v0 + 0x3C, (u32)t4);
        sqc2(17, (u32)v0);
        M16((u32)v0) = (u16)at;
        at = W32((u32)at - 1);
        M8((u32)v0 + 0xE) = (u8)at;
        t8 = W32((u32)t8 + 0x10);
        t9 = W32((u32)t9 + 0x10);
    } else st_emptylast++;
    sqc2(19, (u32)t8 - 0x10);
    at = t8 & 0x7FF;
    t8 = W32((u32)t8 - (u32)at);
    wait_idle(0x1000D000u);
    send(t8, at, t9);
    t8 ^= 0x800;
    wait_idle(0x1000D000u);
    rc_sw(gp + (u32)R->g_cur_w, (u32)s0);
    rc_sw(gp + (u32)R->g_pgn_w, (u32)s1);
    rc_sw(gp + (u32)R->g_chain_w, (u32)t9);
done:
    /* pcpyud: s0..s3 and ra from their upper halves (their entry values), zero above */
    c->r[16].d[1] = 0; c->r[17].d[1] = 0; c->r[18].d[1] = 0; c->r[19].d[1] = 0; c->r[31].d[1] = 0;
    c->f[0] = f0; c->f[1] = f1; c->f[2] = f2; c->f[4] = f4;
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;

/* ---- direct (UYA_NPARTDIRECT: 0 the chain, 2 direct, the default) ----
 * The part PartProc built, from its chain start to the chain pointer it leaves, in the game's templates:
 *   REF to the MPG packet;
 *   CNT 6, VIF STCYCL 4/4, UNPACK V4-32 6 at VU 0 (the matrix and the view qwords, inline);
 *   REF 14, VIF STCYCL 4/4, UNPACK V4-32 10 at VU 6, then in the REF's data: MSCALF 0x0000, BASE 0,
 *     OFFSET 0x200, DIRECT 3 (and its 3 GIF qwords);
 *   per block: CNT n, VIF NOP, UNPACK V4-32 n-1 at TOPS + 0 (the records), the trailer NOP NOP NOP
 *     MSCALF 0x00A0;
 *   CNT 0 with FLUSHA.
 * When the whole part has that shape, its first qword becomes a NEXT past the part (VIF words NOP
 * NATIVE_PART_MARK and the record's index), and where the chain meets it, native_part_tag does the
 * same as the VIF, in the same order, reading the part's own words (checked again there): the MPG,
 * the unpacks, the starts (the program runs as from the VIF), the base, the offset, the DIRECT. Any
 * other shape: the qword is put back and the chain goes through the VIF. */
#define PD_N 64
static struct { u32 ch0, end, q0[4]; } pd[PD_N];
static u32 pd_next;
static int pd_mode = -1;
static unsigned long long pd_made, pd_drawn, pd_back, pd_unknown, pd_blocks;
#define W(a) (*(const u32 *)RC_A(a))
/* the part's shape: 1 if it is the template's; blocks counted in *nb */
static int part_shape(u32 ch0, u32 end, u32 *nb)
{
    if ((W(ch0) >> 28) != 3 || W(ch0 + 8) || W(ch0 + 12)) return 0;
    if (W(ch0 + 0x10) != 0x10000006u || W(ch0 + 0x18) != 0x01000404u || W(ch0 + 0x1C) != 0x6C064000u) return 0;
    if (W(ch0 + 0x80) != 0x3000000Eu || W(ch0 + 0x88) != 0x01000404u || W(ch0 + 0x8C) != 0x6C0A4006u) return 0;
    u32 ra = W(ch0 + 0x84);
    if (ra >= 0x2000000u || W(ra + 0xA0) != 0x15000000u || W(ra + 0xA4) != 0x03000000u || W(ra + 0xA8) != 0x02000200u || W(ra + 0xAC) != 0x50000003u) return 0;
    u32 h = ch0 + 0x90, n = 0;
    while (h != end - 0x10) {
        if (h > end - 0x10 || n > 4096) return 0;
        u32 t = W(h), qwc = t & 0xFFFF, w3 = W(h + 12), num = (w3 >> 16) & 0xFF;
        if ((t & 0xFFFF0000u) != 0x10000000u || W(h + 8) || (w3 & 0xFF00FFFFu) != 0x6C00C000u || qwc < 2 || num != qwc - 1) return 0;
        u32 tr = h + 16 * qwc;
        if (W(tr) || W(tr + 4) || W(tr + 8) || W(tr + 12) != 0x15000014u) return 0;
        h = tr + 16; n++;
    }
    if (W(h) != 0x10000000u || W(h + 8) || W(h + 12) != 0x13000000u) return 0;
    *nb = n;
    return 1;
}
static void part_direct(u32 ch0, u32 end)
{
    if (pd_mode < 0) { const char *e = getenv("UYA_NPARTDIRECT"); pd_mode = e ? atoi(e) : 2; uya_log("native PartProc direct draws (UYA_NPARTDIRECT): %s", pd_mode ? "on" : "off"); }
    {   /* testing: UYA_PDLOG=f: every part's chain range at vsync f */
        static long f = -2; if (f == -2) { const char *e = getenv("UYA_PDLOG"); f = e ? atol(e) : -1; }
        if (f >= 0 && (long)uya_frame_count() == f) uya_log("PartProc part: %08X..%08X", ch0, end);
    }
    if (!pd_mode || end == ch0 || end < ch0 + 0xA0) return;   /* (the start and the closing CNT: a part with no blocks is the template too) */
    u32 nb;
    if (!part_shape(ch0, end, &nb)) { static int told; if (told++ < 3) uya_log("native PartProc: the part at %08X is not the template's shape: it goes through the VIF", ch0); return; }
    u32 i = pd_next++ % PD_N;
    pd[i].ch0 = ch0; pd[i].end = end;
    for (int k = 0; k < 4; k++) pd[i].q0[k] = W(ch0 + 4 * (u32)k);
    rc_sw(ch0, 0x20000000u); rc_sw(ch0 + 4, end); rc_sw(ch0 + 8, NATIVE_PART_MARK); rc_sw(ch0 + 12, i);
    pd_made++;
}
void vu_mpg(int vu, u32 addr, const u32 *w, u32 ninstr);
void native_gif(int path, const u32 *d, u32 qwc);
int native_part_tag(const u32 *q, u32 t)
{
    u32 i = q[3] % PD_N;
    if (pd[i].ch0 != t) { if (pd_unknown++ < 3) uya_log("native PartProc direct: marker %u at %08X not known: the part is skipped", q[3], t); return 0; }
    /* the first qword back, then the shape checked (from the part's own words) */
    u32 *q0 = (u32 *)RC_A(t);
    for (int k = 0; k < 4; k++) q0[k] = pd[i].q0[k];
    pd[i].ch0 = 0;
    u32 nb;
    if (!part_shape(t, pd[i].end, &nb)) { pd_back++; return 2; }
    /* the MPG packet (FLUSH, FLUSHE, FLUSHA, NOP and MPG codes only) */
    {
        const u32 *w = (const u32 *)RC_A(q0[1]);
        u32 n = (q0[0] & 0xFFFF) * 4, k = 0;
        while (k < n) {
            u32 code = w[k], cmd = (code >> 24) & 0x7F;
            if (cmd == 0x00 || cmd == 0x10 || cmd == 0x11 || cmd == 0x13) { k++; continue; }
            if (cmd != 0x4A) { pd_back++; return 2; }
            u32 num = (code >> 16) & 0xFF; if (!num) num = 256;
            if (k + 1 + num * 2 > n) { pd_back++; return 2; }
            vu_mpg(1, (code & 0xFFFF) * 8, w + k + 1, num);
            k += 1 + num * 2;
        }
    }
    uya_vif1_op_reg(W(t + 0x18)); uya_vif1_op_unpack(W(t + 0x1C), (const u32 *)RC_A(t + 0x20), 6 * 4);
    u32 ra = W(t + 0x84);
    uya_vif1_op_reg(W(t + 0x88)); uya_vif1_op_unpack(W(t + 0x8C), (const u32 *)RC_A(ra), 10 * 4);
    uya_vif1_op_mscal((W(ra + 0xA0) & 0xFFFF) * 8);
    uya_vif1_op_reg(W(ra + 0xA4)); uya_vif1_op_reg(W(ra + 0xA8));
    { extern const char *uya_gs_src; uya_gs_src = "PATH2 (VIF1 DIRECT)"; native_gif(1, (const u32 *)RC_A(ra + 0xB0), 3); }
    for (u32 h = t + 0x90; h != pd[i].end - 0x10; ) {
        u32 qwc = W(h) & 0xFFFF;
        uya_vif1_op_unpack(W(h + 12), (const u32 *)RC_A(h + 16), (qwc - 1) * 4);
        uya_vif1_op_mscal((W(h + 16 * qwc + 12) & 0xFFFF) * 8);
        h += 16 * (qwc + 1);
        pd_blocks++;
    }
    pd_drawn++;
    if (pd_drawn % 200 == 0) uya_log("native PartProc direct: %llu parts drawn (%llu blocks), %llu back to the chain, %llu markers not known; made %llu", pd_drawn, pd_blocks, pd_back, pd_unknown, pd_made);
    return 1;
}
#undef W
static PartRefs refs_of(const s32 *g, const u32 *a)
{
    PartRefs R;
    R.g_arr = g[0]; R.g_cnt = g[1]; R.g_vf25 = g[2]; R.g_far = g[3]; R.g_arr2 = g[4]; R.g_cnt2 = g[5];
    R.g_chain = g[6]; R.g_arr3 = g[7]; R.g_pgn = g[8]; R.g_cur = g[9];
    R.g_vf17 = g[10]; R.g_vf18 = g[11]; R.g_vf19 = g[12]; R.g_vf20 = g[13]; R.g_tcnt = g[14]; R.g_tref = g[15];
    R.g_cur_w = g[16]; R.g_pgn_w = g[17]; R.g_chain_w = g[18];
    R.cam = a[0]; R.view = a[1]; R.pgtab = a[2]; R.texc = a[3]; R.cost = a[4]; R.mpg_qwc = a[5]; R.mpg_addr = a[6];
    R.view2 = a[7]; R.cam2 = a[8];
    return R;
}

/* check mode: the state the routine reads and writes, put aside and compared */
#define WIN 0x100000u      /* the chain window compared, from the chain pointer */
#define PARTMAX 0x100000u  /* particle array bytes kept */
#define PGMAX 512u         /* paging slots kept, from the count at entry */
static const u32 dma_regs[9] = { 0x1000D000u, 0x1000D010u, 0x1000D020u, 0x1000D080u, 0x1000D400u, 0x1000D410u, 0x1000D420u, 0x1000D480u, 0x1000E010u };
typedef struct {
    u8 spr[0x4000];
    u8 *win, *parts;
    u8 pg[PGMAX * 16];
    u32 gw[3], dma[9];
    RcVu0 vu0;
    int valid;
} Snap;
static Snap A, N, G;
static struct { u32 gp, chain0, nwin, arr, nparts, pg0, npg; PartRefs R; } K;
static u32 clampn(u32 a, u32 n) { return a >= 0x2000000u ? 0 : a + n <= 0x2000000u ? n : 0x2000000u - a; }
static void grab(Snap *s)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    if (!s->win) s->win = malloc(WIN);
    if (!s->parts) s->parts = malloc(PARTMAX);
    if (K.nwin) memcpy(s->win, (void *)RC_A(K.chain0), K.nwin);
    if (K.nparts) memcpy(s->parts, (void *)RC_A(K.arr), K.nparts);
    if (K.npg) memcpy(s->pg, (void *)RC_A(K.pg0), K.npg);
    s->gw[0] = M32(K.gp + (u32)K.R.g_cur_w); s->gw[1] = M32(K.gp + (u32)K.R.g_pgn_w); s->gw[2] = M32(K.gp + (u32)K.R.g_chain_w);
    for (int k = 0; k < 9; k++) s->dma[k] = M32(dma_regs[k]);
    s->vu0 = rc_vu0;
    s->valid = 1;
}
static void put_back(const Snap *s)
{
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    if (K.nwin) memcpy((void *)RC_A(K.chain0), s->win, K.nwin);
    if (K.nparts) memcpy((void *)RC_A(K.arr), s->parts, K.nparts);
    if (K.npg) memcpy((void *)RC_A(K.pg0), s->pg, K.npg);
    M32(K.gp + (u32)K.R.g_cur_w) = s->gw[0]; M32(K.gp + (u32)K.R.g_pgn_w) = s->gw[1]; M32(K.gp + (u32)K.R.g_chain_w) = s->gw[2];
    for (int k = 0; k < 9; k++) M32(dma_regs[k]) = s->dma[k];
    rc_vu0 = s->vu0;
}

int uya_partproc_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (mode_ < 0) {
        const char *e = getenv("UYA_NPARTPROC"), *n = getenv("UYA_NATIVE");
        mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2;
        uya_log("native PartProc: %s", mode_ == 0 ? "off (the game's routine)" : mode_ == 1 ? "check against the game's routine" : "on");
    }
    if (!mode_) return 0;
    if (ng != NG || na != NA) {
        static int told;
        if (!told++) uya_log("native PartProc: the level's routine has %d / %d data references (not %d / %d): the game's routine runs", ng, na, NG, NA);
        return 0;
    }
    PartRefs R = refs_of(g, a);
    if (mode_ >= 2) {
        const u32 gp = (u32)c->r[28].d[0], ch0 = M32(gp + (u32)R.g_chain);
        run(c, &R);
        part_direct(ch0, M32(gp + (u32)R.g_chain_w));
        return 1;
    }
    /* check: run here, keep the results, put the state back for the game's routine */
    const u32 gp = (u32)c->r[28].d[0];
    K.R = R; K.gp = gp;
    K.chain0 = M32(gp + (u32)R.g_chain);
    K.nwin = clampn(K.chain0, WIN);
    K.arr = M32(gp + (u32)R.g_arr);
    {
        u32 n = M32(gp + (u32)R.g_cnt) + 1;
        u64 b = (u64)n * 64;
        K.nparts = clampn(K.arr, b > PARTMAX ? PARTMAX : (u32)b);
    }
    K.pg0 = R.pgtab + 16 * M32(gp + (u32)R.g_pgn);
    K.npg = clampn(K.pg0, PGMAX * 16);
    RcCtx c0 = *c;
    grab(&A);
    run(c, &R);
    grab(&N);
    put_back(&A);
    *c = c0;
    return 0;
}

static u32 first_diff(const u8 *x, const u8 *y, u32 n) { u32 k = 0; while (k < n && x[k] == y[k]) k++; return k; }

static void summary(void)
{
    uya_log("native PartProc check: %llu runs, %llu identical, %llu differ; ended at once %llu, nothing visible %llu; "
            "particles %llu: hidden %llu, screen %llu (near %llu, far %llu, small %llu, side %llu), two-point %llu (frustum %llu, band %llu, near %llu, far %llu); "
            "bucketed %llu, listed %llu; specials %llu (list full %llu, bytes written %llu); "
            "records: screen quads %llu, world quads %llu, lines %llu, streaks %llu; paging appends %llu; full blocks %llu, last blocks %llu (empty %llu)",
            n_runs, n_ok, n_bad, st_quit, st_empty, st_part, st_hidden, st_k0, st_near0, st_far0, st_small0, st_side0,
            st_k1, st_clip1, st_band1, st_near1, st_far1, st_bucket, st_list, st_special, st_specfull, st_specw,
            st_m0, st_m1, st_line, st_streak, st_page, st_blocks, st_lastblk, st_emptylast);
}
static void summary_end(void) { if (n_runs % 500) summary(); }

void uya_partproc_exit(RcCtx *c)
{
    (void)c;
    if (mode_ != 1 || !N.valid) return;
    N.valid = 0;
    if (!n_runs) atexit(summary_end);
    n_runs++;
    grab(&G);
    char why[384] = "";
    int bad = 0;
    u32 k;
    if (G.gw[2] != N.gw[2]) { bad = 1; snprintf(why, sizeof why, "chain pointer %08X, native %08X (from %08X)", G.gw[2], N.gw[2], K.chain0); }
    else if (K.nwin && (k = first_diff(G.win, N.win, G.gw[2] - K.chain0 <= K.nwin ? G.gw[2] - K.chain0 : K.nwin)) < (G.gw[2] - K.chain0 <= K.nwin ? G.gw[2] - K.chain0 : K.nwin)) {
        bad = 1; snprintf(why, sizeof why, "chain differs at +0x%X (of 0x%X): %02X, native %02X", k, G.gw[2] - K.chain0, G.win[k], N.win[k]);
    } else if ((k = first_diff(G.spr, N.spr, 0x4000)) < 0x4000) {
        bad = 1; snprintf(why, sizeof why, "scratchpad differs at 0x%04X: %02X, native %02X", k, G.spr[k], N.spr[k]);
    } else if ((k = first_diff(G.parts, N.parts, K.nparts)) < K.nparts) {
        bad = 1; snprintf(why, sizeof why, "particle %u byte +%u: %02X, native %02X", k / 64, k % 64, G.parts[k], N.parts[k]);
    } else if (G.gw[0] != N.gw[0] || G.gw[1] != N.gw[1]) {
        bad = 1; snprintf(why, sizeof why, "paging cursor %08X / count %u, native %08X / %u", G.gw[0], G.gw[1], N.gw[0], N.gw[1]);
    } else if ((k = first_diff(G.pg, N.pg, K.npg)) < K.npg) {
        bad = 1; snprintf(why, sizeof why, "paging slot +%u byte %u: %02X, native %02X", k / 16, k % 16, G.pg[k], N.pg[k]);
    } else if (memcmp(G.dma, N.dma, sizeof G.dma)) {
        int r = 0; while (G.dma[r] == N.dma[r]) r++;
        bad = 1; snprintf(why, sizeof why, "DMA register %08X: %08X, native %08X", dma_regs[r], G.dma[r], N.dma[r]);
    } else if (memcmp(G.vu0.vf, N.vu0.vf, sizeof G.vu0.vf) || memcmp(G.vu0.acc, N.vu0.acc, sizeof G.vu0.acc) || G.vu0.clip != N.vu0.clip ||
               memcmp(&G.vu0.q, &N.vu0.q, 4) || G.vu0.mac != N.vu0.mac || G.vu0.status != N.vu0.status) {
        int r = 0; while (r < 32 && !memcmp(G.vu0.vf[r], N.vu0.vf[r], 16)) r++;
        bad = 1;
        if (r < 32) snprintf(why, sizeof why, "VU0 vf%d %08X %08X %08X %08X, native %08X %08X %08X %08X", r,
                             *(u32 *)&G.vu0.vf[r][0], *(u32 *)&G.vu0.vf[r][1], *(u32 *)&G.vu0.vf[r][2], *(u32 *)&G.vu0.vf[r][3],
                             *(u32 *)&N.vu0.vf[r][0], *(u32 *)&N.vu0.vf[r][1], *(u32 *)&N.vu0.vf[r][2], *(u32 *)&N.vu0.vf[r][3]);
        else snprintf(why, sizeof why, "VU0 acc / clip %06X / %06X / q %08X / %08X / mac %04X / %04X / status %03X / %03X differ", G.vu0.clip, N.vu0.clip,
                      *(u32 *)&G.vu0.q, *(u32 *)&N.vu0.q, G.vu0.mac, N.vu0.mac, G.vu0.status, N.vu0.status);
    }
    if (bad) { n_bad++; if (n_bad <= 10) uya_log("native PartProc check: run %llu differs: %s", n_runs, why); }
    else n_ok++;
    if (n_runs % 500 == 0) summary();
}

/* ---- the other particle list, func_0022CEB8 (UYA_NPARTLIST: 0 its part stays in the chain, 1 the default) ----
 * A short routine (one list, up to 30 particles a block, the VU0 microprograms at 0xC80 and 0xC90) that
 * writes PartProc's chain format: the REF to the same MPG, the CNT 6 and REF 14 templates (gp), the blocks
 * and the closing CNT. The game's routine (translated) runs; its part, from the chain pointer (gp, its
 * second data reference) on entry to the pointer it leaves, is made direct as PartProc's is (part_direct:
 * the shape checked, the first qword a NEXT past the part, native_part_tag at the chain). */
static int pl_mode = -1;
static u32 pl_ch0, pl_gofs;
static int pl_on;
int uya_partlist_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)a; (void)na;
    if (pl_mode < 0) { const char *e = getenv("UYA_NPARTLIST"); pl_mode = e ? atoi(e) : 1; uya_log("native particle list (func_0022CEB8) direct: %s (UYA_NPARTLIST)", pl_mode ? "on" : "off"); }
    pl_on = 0;
    if (!pl_mode) return 0;
    if (ng != 9 || g[1] != g[8]) { static int told; if (!told++) uya_log("native particle list: the routine has %d data references (not 9, the chain pointer second and last): its part stays in the chain", ng); return 0; }
    pl_gofs = (u32)g[1];
    pl_ch0 = M32((u32)c->r[28].d[0] + pl_gofs);
    pl_on = 1;
    return 0;
}
void uya_partlist_exit(RcCtx *c)
{
    if (!pl_on) return;
    pl_on = 0;
    part_direct(pl_ch0, M32((u32)c->r[28].d[0] + pl_gofs));
}
