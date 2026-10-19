/* The level routines that write VU1 image 57843's strips (and the setups around them) into the
 * VIF1 chain, as native C: seven hand-written or compiled routines of the level programs, word
 * for word. The model and the shared pieces are native_strip.c (func_001F7868, the setup;
 * func_L00_001FDE48, one strip; func_001F8B6C, the strip list), whose header gives the strip
 * block. In short, a strip block is
 *
 *   CNT template (5 qwords; level data, the same bytes in every level):
 *     qw0  CNT tag (qwc |= the block's qwords - 1 at the end) | STCYCL 4/4 | UNPACK V4-32 1 to TOPS + 0
 *     qw1  the strip's GIF tag (0x00008000 | n, 0x303E4000, 0x412, 0): NLOOP |= n (word 0)
 *     qw2  UNPACK V4-32 1 to 940 | the 940 qword's words 0..2 (0x00008000, 0x303EC000, 0x412)
 *     qw3  the 940 qword's word 3 (a float set at run time, e.g. -0.02) | STCOL | 1.0 1.0
 *     qw4  1.0 1.0 | STCYCL 3/1 | STMOD 0
 *   ST:     STMASK 0xA0A0A0A0 | NOP | UNPACK V2-32 n to TOPS + 1 (num |= n << 16), n float pairs,
 *           zero words to the next qword
 *   colour: NOP NOP NOP | UNPACK V4-8 n to TOPS + 2, n RGBA words, zero words to the qword
 *   xyz:    STMASK 0x80808080 | NOP | UNPACK V3-32 n to TOPS + 3, n float triples, zeros
 *   MSCALF 0x60 (plain) or 0x70 (clipped) | FLUSH | 0 | 0
 * (the UNPACK templates are resident data at 0x160F40 / 0x160F60 / 0x160F80; the CNT template is
 * level data, e.g. 0x1C4720 in level 1, 0x1C48A0 in level 2, 0x1C4AA0 in level 6.) "The GIF tag"
 * below is qw1 and "the 940 qword" qw2 word 1 .. qw3 word 0, both as the VIF unpacks them.
 *
 * func_L01_0021F9C8 (hand-written, 784 bytes; levels 1, 9, 12, 14): a0 = n, a1 xyz (12 bytes a
 * vertex), a2 colours (4 bytes), a3 ST (8 bytes), t0 a second ST array, t1 bit 0 clipped. Writes
 * two strips:
 *   1. a whole strip block as func_L00_001FDE48 writes it (template, ST from a3, colours from a2,
 *      xyz from a1, MSCALF 0x60 / 0x70 from t1), CNT closed (qwc `srl`), the chain pointer moved on.
 *   2. the CNT template again with 0x01000000 or'ed into the GIF tag's word 1 and into the 940
 *      qword's word 1 (bit 56 of each GIF tag: PRIM.CTXT = 1), NLOOP |= n; the ST UNPACK and n
 *      pairs from t0, zero padding; the CNT closed (qwc = up to here); then a REF tag right after
 *      it: REF qwc = (end of block 1 - start of block 1's colour UNPACK) / 16, address = block 1's
 *      colour UNPACK: block 1's colour UNPACK and data, xyz UNPACK and data and its MSCALF / FLUSH
 *      qword run again. Its MSCALF is block 1's (0x60 / 0x70).
 *   Leaves at = the end of block 2's ST padding, t2 = the chain pointer (after the REF), v0 = its
 *   gp address, v1 = the REF tag word, t8 / t9 = the REF'd range, t1 = 12 / 14, t3, t4 =
 *   0x01000000, t5 = 0x70000000, t7 = n << 16; v0's upper half = the last lq's (the ST UNPACK
 *   template's).
 *
 * func_L01_002BA150 (compiled C, 556 bytes; levels 1, 5, 7, 11, 12, 13): the setup of
 * func_001F7868 with its own end. Calls func_001FA1C0 (the matrix: identity x 1024),
 * FastVecScale (the camera position at 0x1672C0 (level 1) x -1024), VU1_addDataRef (the MPG REF,
 * only when D_0015F704 != 7; then D_0015F704 = 7), sce_vu0_mul_matrix twice; writes the setup
 * block (CNT: FLUSH, STCYCL 4/4, UNPACK V4-32 12 at VU 932: the two camera matrices x M, the GIF
 * tag (0x8000, 0x303EC000, 0x412, view + 0x210), view + 0x190, view + 0x1A0, (view + 0x22C,
 * view + 0x228, 0, 0); BASE 0, OFFSET 0x1D2, MSCALF 0x0000, NOP; qwc patched with `sra`; unlike
 * func_001F7868 no z bias is added); calls func_001FB848 (a REF to the GS register reset); then
 * REF 7 to the level's GS block (level 1: 0x1CAF00; VIF NOP, DIRECT 7: A+D x 6: TEST_1 and TEST_2
 * 0x5360B, CLAMP_1 and CLAMP_2 0, TEX1_1 and TEX1_2 0xFF9000000260), and the four gp words at
 * -0x5A58..-0x5A4C = -1. No strips.
 *
 * func_L02_00250A58 (hand-written, 544 bytes; levels 2, 3, 5, 7, 8, 9, 14): a grid. a0 = the
 * object: +0 (x, y, z) the corner, +0x10 (dx, dy) a grid step, +0x18 / +0x1A the width / height
 * in steps, +0x34 the cells' data (0x200 bytes a cell: REF'd, its own VIF codes). Cells of 7 x 7
 * steps, x inner; per cell a flag byte from the scratchpad (0x70002000 on, one a cell): 0 = not
 * drawn; else the cell's 8 x 8 grid points (x, y as two floats) to the scratchpad 0x70000000..
 * 0x700001FF, and a block:
 *   CNT template, NLOOP |= 124 (0x7C); the xyz UNPACK (V3-32, num 124) and 124 vertices: x and y
 *   of the grid point at 0x70000000 | (the s16 at 0x70002800 + 2i: a byte offset; the index list
 *   is the same for every cell), z the grid's z; zero padding; the CNT closed (`srl`);
 *   REF 0x20 to the cell's data (+0x34, then + 0x200 a cell): the colour UNPACK (NOP NOP NOP,
 *     UNPACK V4-8 124 to TOPS + 2) and 124 RGBA words (31 qwords);
 *   REF 0x3F to level data (level 2: 0x1CBC40, level 9: 0x1CB7C0; filled at run time): STMASK
 *     0xA0A0A0A0, NOP, UNPACK V2-32 124 to TOPS + 1 and 124 ST float pairs (62 qwords), the
 *     same for every cell;
 *   CNT 0 with MSCALF ((flag - 1) * 2 + 12) * 8 (flag 1: 0x60, 2: 0x70; only 1 and 2 were seen)
 *   and NOP in its VIF words.
 *   So per cell the xyz are in the chain, the colours in the cell's REF'd data and the ST in the
 *   shared REF'd block.
 * VU0: vf1, vf2, vf13 (the steps), vf24, vf25, vf29 left as the routine leaves them; at = vf25
 * (whole, qmfc2), a2 = vf1 (whole), t5 = the last cell's flag, t6, t7 (the flag pointer),
 * a3 (+= 0x200 a cell), t0 / t1 the counters, t4 / t8 / t9 / v0 / v1 as the last drawn cell left them.
 *
 * func_L02_002A52D0 (compiled C, 1516 bytes; levels 2, 3, 5, 7, 8, 9, 14): a0 = the object.
 * The view's 0x218 / 0x21C / 0x228 / 0x22C saved to 0x161378 / 0x16137C / 0x161380 / gp - 0x597C;
 * unless D_0015F558 == 1, the object's +0x20..+0x2C put there; calls UpdateViewContext,
 * build_rotation_view_proj, func_001FA1C0, FastVecScale (the camera position 0x167440, level 2),
 * VU1_addDataRef (the MPG REF, always; D_0015F704 untouched), the setup block as above
 * (sce_vu0_mul_matrix twice; no z bias); then
 *   REF 3 to the level's GS block (level 2: 0x1CBB70; DIRECT 3: A+D CLAMP_1 0, TEX1_1),
 *   CNT 9 with DIRECT 9: GIF tag (0x8000000000008001, 0xEEEEEEEE: A+D x 8): ALPHA_1 (0x42) =
 *   alpha << 32 | 0x64 (alpha = the object's +0x3F), TEX0_1 (0x06) = the dword at 0x161370
 *   ((D_0015EF74 >> 8) | 0x18100000 | 0x580000000, written here), FOGCOL (0x3D) = the object's
 *   +0x30..+0x32 (or the view's +0x230 / +0x234 / +0x238 when D_0015F558 == 1), BITBLTBUF (0x50)
 *   = (D_0015EF74 >> 8) << 32 | 1 << 48, TRXPOS (0x51) 0, TRXREG (0x52) 64 x 64, TRXDIR (0x53) 0,
 *   TEST_1 (0x47) = 0x5360A (alpha > 0x60) or 0x5370B;
 *   REF 0x401 to the object's buffer (+0x1C; DIRECT 0x401): an IMAGE GIF packet (its tag
 *   0x0800000000000400 written here) of 0x400 qwords, a 64 x 64 32-bit texture that
 *   func_L02_00250C78 fills just before (two animation frames' texels interpolated);
 *   CNT 2 with DIRECT 2: GIF tag (0x1000000000008001, 0xE) and TEXFLUSH (0x3F);
 *   then D_0015EF74 += 0x4000 (the next texture's address). No strip: an animated texture uploaded
 *   by PATH2, with the strip setup (camera matrices, the MPG) for the strips drawn with it.
 *
 * func_L06_00216B38 (hand-written, 940 bytes; level 6): a ribbon. a0 = the ribbon (+0 / +4 the
 * head / tail times, +8 4 pass ends, +0xC / +0x10 two RGBA colours, +0x14 points a pass, +0x18 the
 * points (qwords), +0x1C the U step, +0x20 the first U), a1 = a matrix. Four passes (t1 = 3 .. 0),
 * each: its points (from +0x18 + pass * points * 16) -+ 0.05 in z through the matrix, two
 * vertices a point, to the scratchpad 0x70001000.. (a qword each, xyz used), their ST (u, 0) and
 * (u, 1) (u += the step) to 0x70002000..; the pass's colour: the two colours blended by
 * (end - tail) / (head - tail) (VU0 vdiv), packed to one RGBA word; then one strip block with
 * n = 2 x points: template, ST from 0x70002000 (8 bytes a vertex), the colour word n times, xyz
 * from 0x70001000 (16 bytes a vertex, 12 copied), MSCALF 0x70 (always clipped).
 *
 * func_L09_002C2B08 (compiled C, 1224 bytes; level 9): a0 = the object, a1 = entries (0x20 bytes:
 * +0x10 xyz, +0x14 ST, +0x1C n), a2 = their count. As func_L02_002A52D0 up to the setup block
 * (view values saved / replaced, UpdateViewContext, build_rotation_view_proj, the matrix, the MPG
 * REF always, the setup block); then REF 3 to the level's GS block (0x1CB6F0; DIRECT 3), CNT 8
 * with DIRECT 8: GIF tag (0x7000000000008001, 0xEEEEEEEE: A+D x 7): ALPHA_1 = alpha << 32 | 0x64,
 * TEX0_1 = the dword at 0x161370 (as left there), FOGCOL, CLAMP_1 0, TEST_1 0x50000, TEX1_1
 * 0xFF9000000260, TEST_1 0x5360A / 0x5370B (alpha > 0x60 or not); the colours: 256 words of
 * 0x00757C8E to the scratchpad 0x70000000..0x700003FF; then per entry FastBSphereCheck(entry,
 * 256) (-1: not drawn); else its ST pairs x 0.5 to the scratchpad 0x70001000.. and
 * func_L00_001FDE48 (n = +0x1C, xyz +0x10, colours 0x70000000, ST 0x70001000, clipped = the check
 * returned 0): one strip block each, as native_strip.c writes it. Then func_L02_002A58C0 (the
 * view values back, a GS packet).
 *
 * func_L12_002BD3D0 (compiled C, 328 bytes; levels 12, 14): a0 = entries (0x20 bytes: +0x10 xyz,
 * +0x14 ST, +0x18 a source for the second ST, +0x1C n), a1 = their count, a2 / a3 two GS register
 * values: VU1_addGSregister(6, a2) and (7, a3) (TEX0_1 and TEX0_2),
 * REF 9 to the level's GS block (0x1CBA20; DIRECT 9), func_001F7868 (the strip setup: native_strip.c),
 * the colours: 180 words of 0x80808080 to the scratchpad 0x70000000..; per entry
 * FastBSphereCheck(entry, 256) (-1: not drawn), func_L12_002677B4 (the second ST to the
 * scratchpad 0x70001000..) and func_L01_0021F9C8 (n, xyz, colours 0x70000000, ST +0x14, the
 * second ST 0x70001000, clipped = the check returned 0): two strip blocks each, above.
 *
 * The semantic data: stripb_record() is called once per block with a StripbRec (below): the
 * block's chain range, n, the GIF tag and the 940 qword as unpacked, where the ST / colour / xyz
 * unpack data are (as the VIF takes them: in the chain, or in REF'd memory for the second strip of
 * func_L01_0021F9C8 and the grid's cells), the REFs, the MSCALF address and the clipped flag. It
 * is called where each block is complete: in func_L01_0021F9C8 after each of its two strips, in
 * func_L02_00250A58 after each drawn cell, in func_L06_00216B38 after each pass, in
 * func_L09_002C2B08 after each func_L00_001FDE48 call (read back from the block it wrote), and for
 * the setups and the DIRECT packet at the routines' ends; func_L12_002BD3D0's strips are its
 * func_L01_0021F9C8 calls' records. It counts them (the census at exit) and, with
 * UYA_NSTRIPB_LOG=k, logs the first k of each routine and kind with the first words of the REF'd
 * data.
 *
 * As native_strip.c: every store in the routine's order, every VU0 and FPU result clamped as the
 * translated code has it (no MAC or status flag: none is read), the GPRs, FPRs, hi / lo and VU0
 * registers as the routines leave them (whole where written whole: lq, qmfc2, MMI; what a `lui`
 * leaves where it is not overwritten). The routines here call other functions; they call them
 * through rc_dispatch at the level's own address with ra = the level's return address, so the
 * translated function (or its host hook: the strip setup, func_L00_001FDE48 and
 * func_L01_0021F9C8 are native) runs exactly as from the game, and the registers live in the
 * RcCtx throughout. The data addresses come from the hooks tools/recomp.py puts at the routines'
 * starts (HOST_HOOKS_ALLREFS: every gp access and every lui's first use); the routines' own
 * addresses, the functions they call and the view block (whose lui / addiu pair the extraction
 * does not see) per level are in the Lay tables, from the levels' code (functions.tsv and the jal
 * words).
 *
 * UYA_NSTRIPB: 0 the game's routines (translated); 1 check (this file runs first, its results
 * are put aside, the game's routine runs from the same state, the two are compared at its end:
 * the chain from the chain pointer (a window that doubles when a run writes past it; such a run
 * is counted as not compared), the chain pointer, the stack frame, the routine's own words (gp
 * words, D_0015F704, the saved view values, the view's 0x200..0x23F, D_0015EF74, the object's
 * GIF packet), the scratchpad where used, every GPR, hi, lo, FPR and the VU0 state; with
 * UYA_NSTRIPB_FULL=k the first k runs of each routine compare all of RAM (1..32 MB) and the
 * scratchpad instead of the areas, which covers what the called functions write (for
 * func_L12_002BD3D0 every run does: VU1_addGSregister and the others it calls keep state outside
 * the areas, and a run from the native run's leftovers would differ); the first 10
 * differences and a summary with the path counts every 500 runs); 2 this file (default), the
 * chain built natively exactly as the game builds it. UYA_NATIVE=0 also turns it off. */
#include "rc.h"
#include "uya_host.h"
#include "rac1_levels.h"
#include "native_stripd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_log(const char *fmt, ...);
const rac1_level_t *rac1_current_level(void);

/* ---- the EE's registers, by name (the routines keep their state in the RcCtx: they call
 * translated functions, which see and change every register) ---- */
#define RG(n) (c->r[n].d[0])
#define AT RG(1)
#define V0 RG(2)
#define V1 RG(3)
#define A0 RG(4)
#define A1 RG(5)
#define A2 RG(6)
#define A3 RG(7)
#define T0 RG(8)
#define T1 RG(9)
#define T2 RG(10)
#define T3 RG(11)
#define T4 RG(12)
#define T5 RG(13)
#define T6 RG(14)
#define T7 RG(15)
#define S0 RG(16)
#define S1 RG(17)
#define S2 RG(18)
#define S3 RG(19)
#define S4 RG(20)
#define S5 RG(21)
#define S6 RG(22)
#define T8 RG(24)
#define T9 RG(25)
#define GP RG(28)
#define SP RG(29)
#define RA RG(31)
#define LO (c->lo.d[0])
#define HI (c->hi.d[0])
#define FR(n) (c->f[n])
#define ADDIU(x, k) SE32((u32)(x) + (u32)(s32)(k))
#define ADDU(x, y) SE32((u32)(x) + (u32)(y))
#define SUBU(x, y) SE32((u32)(x) - (u32)(y))
#define SLL(x, k) SE32((u32)(x) << (k))
#define SRL(x, k) SE32((u32)(x) >> (k))
#define SRA(x, k) SE32((s32)(u32)(x) >> (k))
#define LW(a) SE32(M32((u32)(a)))
#define LHU(a) ((u64)M16((u32)(a)))
#define LH(a) ((u64)(s64)(s16)M16((u32)(a)))
#define LBU(a) ((u64)M8((u32)(a)))
#define LB(a) ((u64)(s64)(s8)M8((u32)(a)))
#define LD(a) M64((u32)(a))
#define SW(a, v) rc_sw((u32)(a), (u32)(v))
#define SD(a, v) rc_sd((u32)(a), (u64)(v))
#define LQ(gr, a) rc_lq(&c->r[gr], (u32)(a))
#define SQ(gr, a) rc_sq(&c->r[gr], (u32)(a))
#define LWC1(fr, a) (c->f[fr] = MF((u32)(a)))
#define SWC1(fr, a) (MF((u32)(a)) = c->f[fr])
#define LI32(x) SE32((u32)(x))                                   /* lui (+ ori / addiu) */
#define HI16(a) SE32(((u32)(a) + 0x8000u) & 0xFFFF0000u)         /* what `lui` leaves for address a */
#define MTC1(fr, gr) (c->f[fr] = rc_bitsf((u32)(gr)))
#define CVTSW(fr) (c->f[fr] = (f32)(s32)rc_fbits(c->f[fr]))
/* jal: ra = the instruction after the delay slot (the routine's own address in this level),
 * the callee as the game calls it (translated, or a host hook's native routine) */
#define CALL(off, k) do { RA = (u64)(u32)(L->start + (off) + 8); rc_dispatch(c, L->jal[k]); } while (0)

/* div rs, rt as the translated code does it */
static void ee_div(RcCtx *c, u64 rs, u64 rt)
{
    s32 a = (s32)(u32)rs, b = (s32)(u32)rt;
    if (b == -1 && a == (s32)0x80000000) { LO = SE32(a); HI = 0; }
    else if (b) { LO = SE32(a / b); HI = SE32(a % b); }
    else { LO = SE32(a < 0 ? 1 : -1); HI = SE32(a); }
}

/* ---- the VU0 macro operations, as tools/recomp.py translates them (no MAC/status flags: none of
 * the routines reads them) ---- */
enum { OADD, OSUB, OMUL, OMADD };
enum { X = 8, Y = 4, Z = 2, W = 1, XY = 12, XYZ = 14, XYZW = 15 };
#define ACC (-1)     /* destination: the accumulator */
#define BV (-1)      /* ft: every component (else 0..3: one component, broadcast) */
static void vop(int fd, u32 m, int op, int fs, int ft, int bc)
{
    f32 r[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        if (!(m & (8u >> i))) continue;
        f32 a = VF(fs)[i], b = bc == BV ? VF(ft)[i] : VF(ft)[bc];
        switch (op) {
        case OADD: r[i] = rc_fc(a + b); break;
        case OSUB: r[i] = rc_fc(a - b); break;
        case OMUL: r[i] = rc_fc(a * b); break;
        default: r[i] = rc_fc(VACC[i] + rc_fc(a * b)); break;
        }
    }
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
}
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
static void vmove(int d, int s) { f32 t[4]; memcpy(t, VF(s), 16); memcpy(VF(d), t, 16); }
#define QMTC2(vf, gr) memcpy(VF(vf), &c->r[gr], 16)                /* the whole 128-bit GPR */
#define QMFC2(gr, vf) memcpy(&c->r[gr], VF(vf), 16)
static void vitof0(int d)
{
    f32 t[4]; for (int i = 0; i < 4; i++) { s32 v; memcpy(&v, &VF(d)[i], 4); t[i] = rc_itof(v, 0); }
    memcpy(VF(d), t, 16);
}
static void vftoi0(int d)
{
    s32 t[4]; for (int i = 0; i < 4; i++) t[i] = rc_ftoi(VF(d)[i], 0);
    memcpy(VF(d), t, 16);
}
/* MMI with $zero as rs */
static void pextlb0(R128 *r) { R128 t = *r, x; for (int i = 0; i < 8; i++) { x.b[2 * i] = t.b[i]; x.b[2 * i + 1] = 0; } *r = x; }
static void pextlh0(R128 *r) { R128 t = *r, x; for (int i = 0; i < 4; i++) { x.h[2 * i] = t.h[i]; x.h[2 * i + 1] = 0; } *r = x; }
static void ppach0(R128 *r) { R128 t = *r, x; for (int i = 0; i < 4; i++) { x.h[i] = t.h[2 * i]; x.h[4 + i] = 0; } *r = x; }
static void ppacb0(R128 *r) { R128 t = *r, x; for (int i = 0; i < 8; i++) { x.b[i] = t.b[2 * i]; x.b[8 + i] = 0; } *r = x; }

/* the strip blocks' common pieces (the code of func_L00_001FDE48, inlined in the routines that
 * write their own strips): `at` the chain position */
/* the CNT template: 5 qwords through $v0 (lq / sq), the pointer in register tr */
static void tmpl5(RcCtx *c, int tr, u32 tmpl)
{
    RG(tr) = LI32(tmpl);
    V1 = 5;
    do {
        LQ(2, RG(tr));
        V1 = ADDIU(V1, -1);
        SQ(2, AT);
        RG(tr) = ADDIU(RG(tr), 16);
        AT = ADDIU(AT, 16);
    } while (V1 != 0);
}
/* an UNPACK template: one qword, its last word |= t7 (num) */
static void unpack1(RcCtx *c, int tr, u32 tmpl)
{
    RG(tr) = LI32(tmpl);
    LQ(2, RG(tr));
    SQ(2, AT);
    AT = ADDIU(AT, 16);
    V0 = LW(AT - 4);
    V0 = V0 | T7;
    SW(AT - 4, V0);
}
/* zero words up to the next qword */
static void pad16(RcCtx *c)
{
    for (;;) {
        V1 = AT & 0xF;
        if (V1 == 0) break;
        SW(AT, 0);
        AT = ADDIU(AT, 4);
    }
}

/* ================================ the strips' semantic data ================================ */
typedef struct {
    int routine;            /* RT_* */
    int kind;               /* SB_* */
    u32 blk0, blk1;         /* the block in the chain: [blk0, blk1) */
    u32 n;                  /* vertices (the GIF tag's NLOOP, each UNPACK's num) */
    u32 gif[4];             /* the GIF tag unpacked to TOPS + 0 */
    u32 vu940[4];           /* the qword unpacked to VU 940 (a GIF tag for the program's second pass) */
    u32 st, col, xyz;       /* the UNPACK data as the VIF takes it (0: not in this block):
                             * st V2-32 (n float pairs), col V4-8 (n RGBA words), xyz V3-32 (n float triples) */
    u32 ref, refqwc;        /* a REF in the block: its address and qwords */
    u32 ref2, ref2qwc;
    u32 mscal;              /* the MSCALF address: 0x60 plain, 0x70 clipped */
    int clipped;
} StripbRec;
enum { SB_STRIP, SB_STRIP_REF, SB_GRID, SB_SETUP, SB_DIRECT };
enum { RT_F9C8, RT_BA150, RT_50A58, RT_A52D0, RT_16B38, RT_C2B08, RT_BD3D0, NRT };
static const char *const rt_name[NRT] = { "func_L01_0021F9C8", "func_L01_002BA150", "func_L02_00250A58", "func_L02_002A52D0",
                                          "func_L06_00216B38", "func_L09_002C2B08", "func_L12_002BD3D0" };
static const char *const sb_name[] = { "strip", "strip (REF)", "grid cell", "setup", "direct" };
static int rec_log = -1;
static int sb_direct;   /* a mode 2 run: its records go to native_stripd.c */
static unsigned long long rec_count[NRT][5];
/* the point where a block's semantic data is known (native runs only: mode 2, and mode 1's
 * native half). Counted; UYA_NSTRIPB_LOG=k logs the first k of each routine and kind. */
static void stripb_record(StripbRec *r)
{
    if (r->kind <= SB_GRID) {
        memcpy(r->gif, (void *)RC_A(r->blk0 + 0x10), 16);
        memcpy(r->vu940, (void *)RC_A(r->blk0 + 0x24), 16);
    }
    if (sb_direct && r->kind <= SB_GRID) {   /* the strip's semantic data to native_stripd.c (direct draws) */
        if (r->kind == SB_GRID) native_stripd_sem(r->blk0, r->blk1, r->n, r->gif, r->vu940, r->ref2 + 16, r->ref + 16, r->xyz, 1, r->mscal);
        else native_stripd_sem(r->blk0, r->blk1, r->n, r->gif, r->vu940, r->st, r->col, r->xyz, 0, r->mscal);
    }
    if (rec_log < 0) { const char *e = getenv("UYA_NSTRIPB_LOG"); rec_log = e ? atoi(e) : 0; }
    unsigned long long k = rec_count[r->routine][r->kind]++;
    if ((long long)k < rec_log)
        uya_log("native strips B record, %s %s %llu: chain %08X-%08X n %u gif %08X %08X %08X %08X vu940 %08X %08X %08X %08X st %08X col %08X xyz %08X "
                "ref %08X/%u ref2 %08X/%u mscal 0x%X clipped %d", rt_name[r->routine], sb_name[r->kind], k, r->blk0, r->blk1, r->n,
                r->gif[0], r->gif[1], r->gif[2], r->gif[3], r->vu940[0], r->vu940[1], r->vu940[2], r->vu940[3], r->st, r->col, r->xyz,
                r->ref, r->refqwc, r->ref2, r->ref2qwc, r->mscal, r->clipped);
    if ((long long)k < rec_log) {
        u32 w[2][12] = { { 0 } };
        if (r->ref) memcpy(w[0], (void *)RC_A(r->ref), sizeof w[0]);
        if (r->ref2) memcpy(w[1], (void *)RC_A(r->ref2), sizeof w[1]);
        for (int j = 0; j < 2; j++)
            if (j ? r->ref2 : r->ref)
                uya_log("  ref%d data: %08X %08X %08X %08X | %08X %08X %08X %08X | %08X %08X %08X %08X", j + 1, w[j][0], w[j][1], w[j][2], w[j][3],
                        w[j][4], w[j][5], w[j][6], w[j][7], w[j][8], w[j][9], w[j][10], w[j][11]);
    }
}

/* ---- path counts (logged by check mode; a path at 0 is untested) ---- */
static unsigned long long p_f9c8, p_f9c8_clip;
static unsigned long long p_ba150, p_ba150_mpg;
static unsigned long long p_50a58, p_50a58_cells, p_50a58_drawn, p_50a58_flag2, p_50a58_flagx;
static unsigned long long p_a52d0, p_a52d0_mode1, p_a52d0_hialpha, p_a52d0_div0;
static unsigned long long p_16b38, p_16b38_pass, p_16b38_pos, p_16b38_pairs;
static unsigned long long p_c2b08, p_c2b08_mode1, p_c2b08_ent, p_c2b08_vis, p_c2b08_clip, p_c2b08_empty, p_c2b08_none;
static unsigned long long p_bd3d0, p_bd3d0_ent, p_bd3d0_vis, p_bd3d0_clip, p_bd3d0_none;

/* ---- per level: where the routine is and the functions it calls (from the levels' code:
 * config/overlays/functions.tsv and the jal words of each level's copy) ---- */
typedef struct { int lv; u32 start; u32 jal[10]; u32 view; } Lay;
static const Lay lay_ba150[] = {
    {  1, 0x002BA150u, { 0x00221858u, 0x00221140u, 0x002B5980u, 0x00221C18u, 0x00221C18u, 0x002B5DE0u } },
    {  5, 0x002CEB98u, { 0x0022E6B0u, 0x0022DF98u, 0x002CA3E0u, 0x0022EA70u, 0x0022EA70u, 0x002CA840u } },
    {  7, 0x002CD090u, { 0x002277B0u, 0x00227098u, 0x002C88C0u, 0x00227B70u, 0x00227B70u, 0x002C8D20u } },
    { 11, 0x002C92B8u, { 0x002312B0u, 0x00230B98u, 0x002C4B00u, 0x00231670u, 0x00231670u, 0x002C4F60u } },
    { 12, 0x002BCC58u, { 0x00222858u, 0x00222140u, 0x002B84A0u, 0x00222C18u, 0x00222C18u, 0x002B8900u } },
    { 13, 0x002B0A58u, { 0x0020B430u, 0x0020AD18u, 0x002AC288u, 0x0020B7F0u, 0x0020B7F0u, 0x002AC6E8u } },
};
static const Lay lay_a52d0[] = {
    {  2, 0x002A52D0u, { 0x00208B68u, 0x00208030u, 0x002109B0u, 0x00210298u, 0x002A1440u, 0x00210D70u, 0x00210D70u, 0x00250C78u }, 0x0016D0C0u },
    {  3, 0x002923C0u, { 0x001F15E8u, 0x001F0AB0u, 0x001F92E0u, 0x001F8BC8u, 0x0028E548u, 0x001F96A0u, 0x001F96A0u, 0x0023D3C8u }, 0x0016CBC0u },
    {  5, 0x002CF368u, { 0x00226968u, 0x00225E30u, 0x0022E6B0u, 0x0022DF98u, 0x002CA3E0u, 0x0022EA70u, 0x0022EA70u, 0x00278B50u }, 0x0016CF40u },
    {  7, 0x002CD860u, { 0x0021FA68u, 0x0021EF30u, 0x002277B0u, 0x00227098u, 0x002C88C0u, 0x00227B70u, 0x00227B70u, 0x00277298u }, 0x0016CB40u },
    {  8, 0x002ADEC0u, { 0x0020A690u, 0x00209B58u, 0x002124C8u, 0x00211DB0u, 0x002AA030u, 0x00212888u, 0x00212888u, 0x00258CF0u }, 0x0016D2C0u },
    {  9, 0x002C23B0u, { 0x00216F68u, 0x00216430u, 0x0021F5A8u, 0x0021EE90u, 0x002BE538u, 0x0021F968u, 0x0021F968u, 0x0026E178u }, 0x0016CC40u },
    { 14, 0x002ABEB8u, { 0x0020AEF0u, 0x0020A3B8u, 0x00212FD8u, 0x002128C0u, 0x002A8040u, 0x00213398u, 0x00213398u, 0x00256D18u }, 0x0016D240u },
};
static const Lay lay_c2b08[] = {
    {  9, 0x002C2B08u, { 0x00216F68u, 0x00216430u, 0x0021F5A8u, 0x0021EE90u, 0x002BE538u, 0x0021F968u, 0x0021F968u, 0x0021FE70u, 0x0021D410u, 0x002C29A0u }, 0x0016CC40u },
};
static const Lay lay_bd3d0[] = {
    { 12, 0x002BD3D0u, { 0x002B85F0u, 0x002B85F0u, 0x0021F230u, 0x00223120u, 0x002677B4u, 0x00220988u } },
    { 14, 0x002AC628u, { 0x002A8190u, 0x002A8190u, 0x0020F980u, 0x002138A0u, 0x00257064u, 0x002110D8u } },
};
#define NLV 20     /* levels 0..18, 19 = the executable's program (no level) */
static int lv_now(void) { const rac1_level_t *l = rac1_current_level(); return l && l->id >= 0 && l->id < 19 ? l->id : 19; }
static const Lay *lay_find(const Lay *t, int n)
{
    int l = lv_now();
    for (int i = 0; i < n; i++) if (t[i].lv == l) return &t[i];
    return 0;
}

/* =================== func_L01_0021F9C8: a strip, then the same strip again with CTXT 1 ===================
 * a0 n, a1 xyz, a2 colours, a3 ST, t0 the second strip's ST, t1 bit 0 clipped */
typedef struct { s32 g_chain; u32 tcnt, u1, u2, u3; } F9c8Refs;
static void run_f9c8(RcCtx *c, const F9c8Refs *R)
{
    StripbRec r1 = { RT_F9C8, SB_STRIP }, r2 = { RT_F9C8, SB_STRIP_REF };
    T1 = T1 & 0x1;
    T2 = ADDIU(GP, R->g_chain);
    T1 = SLL(T1, 1);
    T1 = ADDIU(T1, 12);
    if (T1 == 14) p_f9c8_clip++;
    T2 = LW(T2);
    T5 = LI32(0x70000000);
    AT = T2;
    T7 = SLL(A0, 16);
    r1.blk0 = (u32)T2; r1.n = (u32)A0;
    tmpl5(c, 11, R->tcnt);
    V0 = LW(AT - 64);
    V0 = V0 | A0;
    SW(AT - 64, V0);
    /* the ST pairs */
    unpack1(c, 11, R->u1);
    r1.st = (u32)AT;
    T3 = A3;
    V1 = A0;
    do {
        V0 = LD(T3);
        V1 = ADDIU(V1, -1);
        SD(AT, V0);
        T3 = ADDIU(T3, 8);
        AT = ADDIU(AT, 8);
    } while (V1 != 0);
    pad16(c);
    /* the colours */
    T8 = AT;
    unpack1(c, 11, R->u2);
    r1.col = (u32)AT;
    T3 = A2;
    V1 = A0;
    do {
        V0 = LW(T3);
        V1 = ADDIU(V1, -1);
        SW(AT, V0);
        T3 = ADDIU(T3, 4);
        AT = ADDIU(AT, 4);
    } while (V1 != 0);
    pad16(c);
    /* the xyz */
    unpack1(c, 11, R->u3);
    r1.xyz = (u32)AT;
    T3 = A1;
    V1 = A0;
    do {
        V0 = LW(T3);
        V1 = ADDIU(V1, -1);
        SW(AT, V0);
        V0 = LW(T3 + 4);
        SW(AT + 4, V0);
        V0 = LW(T3 + 8);
        SW(AT + 8, V0);
        T3 = ADDIU(T3, 12);
        AT = ADDIU(AT, 12);
    } while (V1 != 0);
    pad16(c);
    /* MSCALF 0x60 / 0x70, FLUSH; the CNT's qwc; the chain pointer */
    V0 = LI32(0x15000000);
    V0 = V0 | T1;
    SW(AT, V0);
    V0 = LI32(0x11000000);
    SW(AT + 4, V0);
    SD(AT + 8, 0);
    AT = ADDIU(AT, 16);
    T9 = AT;
    V0 = SUBU(AT, T2);
    V0 = SRL(V0, 4);
    V0 = ADDIU(V0, -1);
    V1 = LW(T2);
    V1 = V1 | V0;
    SW(T2, V1);
    V0 = ADDIU(GP, R->g_chain);
    T2 = AT;
    SW(V0, T2);
    r1.blk1 = (u32)AT; r1.mscal = (u32)T1 << 3; r1.clipped = T1 == 14;
    stripb_record(&r1);
    /* the second strip: the template with CTXT 1 in both GIF tags, its own ST, then a REF to the
     * first strip's colours, xyz and MSCALF */
    r2.blk0 = (u32)AT; r2.n = (u32)A0;
    tmpl5(c, 11, R->tcnt);
    V0 = LW(AT - 64);
    V0 = V0 | A0;
    SW(AT - 64, V0);
    T4 = LI32(0x01000000);
    V0 = LW(AT - 0x3C);
    V1 = LW(AT - 0x28);
    V0 = V0 | T4;
    V1 = V1 | T4;
    SW(AT - 0x3C, V0);
    SW(AT - 0x28, V1);
    unpack1(c, 11, R->u1);
    r2.st = (u32)AT;
    T3 = T0;
    V1 = A0;
    do {
        V0 = LD(T3);
        V1 = ADDIU(V1, -1);
        SD(AT, V0);
        T3 = ADDIU(T3, 8);
        AT = ADDIU(AT, 8);
    } while (V1 != 0);
    pad16(c);
    V0 = SUBU(AT, T2);
    V0 = SRL(V0, 4);
    V0 = ADDIU(V0, -1);
    V1 = LW(T2);
    V1 = V1 | V0;
    SW(T2, V1);
    V0 = ADDIU(GP, R->g_chain);
    T2 = AT;
    SW(V0, T2);
    SD(T2 + 8, 0);
    V1 = LI32(0x30000000);
    AT = SUBU(T9, T8);
    AT = SRL(AT, 4);
    V1 = V1 | AT;
    SW(T2 + 4, T8);
    SW(T2, V1);
    T2 = ADDIU(T2, 16);
    SW(V0, T2);
    r2.blk1 = (u32)T2; r2.col = r1.col; r2.xyz = r1.xyz; r2.ref = (u32)T8; r2.refqwc = (u32)AT;
    r2.mscal = r1.mscal; r2.clipped = r1.clipped;
    stripb_record(&r2);
    p_f9c8++;
}

/* ============== func_L01_002BA150: the strip setup with a GS block of its own ============== */
typedef struct { s32 g_chain, g_a, g_b, g_c, g_d; u32 cammat, f704, mpgq, mpga, chain, view, gs7; } Ba150Refs;
static void run_ba150(RcCtx *c, const Ba150Refs *R, const Lay *L)
{
    StripbRec rs = { RT_BA150, SB_SETUP };
    SP = ADDIU(SP, -0x70);
    AT = LI32(0x44800000);
    MTC1(12, AT);
    SQ(17, SP + 0x50);
    A0 = SP;
    SQ(16, SP + 0x40);
    SQ(31, SP + 0x60);
    S0 = 7;
    CALL(0x1C, 0);                      /* func_001FA1C0: the identity times 1024 */
    V0 = HI16(R->cammat);
    AT = LI32(0xC4800000);
    MTC1(12, AT);
    S1 = LI32(R->cammat);
    A0 = ADDIU(SP, 0x30);
    A1 = S1;
    CALL(0x38, 1);                      /* FastVecScale: the camera position times -1024 */
    AT = LI32(0x3F800000);
    MTC1(0, AT);
    V0 = LW(R->f704);
    {
        int bt = V0 == S0;
        SWC1(0, SP + 0x3C);
        if (!bt) {
            p_ba150_mpg++;
            V0 = HI16(R->mpgq);
            A0 = HI16(R->mpga);
            A1 = LHU(R->mpgq);
            A0 = LI32(R->mpga);
            CALL(0x64, 2);              /* VU1_addDataRef: the MPG */
            AT = HI16(R->f704);
            SW(R->f704, S0);
        }
    }
    V1 = LW(R->chain);
    V0 = LI32(0x10000000);
    T0 = LI32(0x11000000);
    A0 = LI32(0x01000000);
    SW(V1, V0);
    A0 = A0 | 0x404;
    A3 = LI32(0x6C0C0000);
    A2 = SP;
    V1 = LW(R->chain);
    A3 = A3 | 0x43A4;
    A1 = ADDIU(S1, -0x100);
    SW(V1 + 4, 0);
    V0 = LW(R->chain);
    SW(V0 + 8, T0);
    V1 = LW(R->chain);
    SW(V1 + 12, A0);
    S0 = LW(R->chain);
    rs.blk0 = (u32)S0;
    SW(S0 + 16, 0);
    A0 = ADDIU(S0, 32);
    SW(S0 + 20, 0);
    SW(S0 + 24, 0);
    SW(S0 + 28, A3);
    CALL(0xDC, 3);                      /* sce_vu0_mul_matrix */
    A1 = ADDIU(S1, -0x80);
    A0 = ADDIU(S0, 0x60);
    A2 = SP;
    CALL(0xEC, 4);
    V0 = LI32(0x303E0000);
    A2 = 0x412;
    V0 = V0 | 0xC000;
    A0 = 0x8000;
    SW(S0 + 0xA8, A2);
    V1 = HI16(R->view);
    SW(S0 + 0xA0, A0);
    V1 = LI32(R->view);
    SW(S0 + 0xA4, V0);
    A1 = ADDIU(V1, 0x190);
    A2 = ADDIU(S0, 0xB0);
    LWC1(0, V1 + 0x210);
    SWC1(0, S0 + 0xAC);
    LQ(2, A1);
    SQ(2, A2);
    A0 = ADDIU(V1, 0x1A0);
    A2 = ADDIU(S0, 0xC0);
    LQ(2, A0);
    SQ(2, A2);
    LWC1(0, V1 + 0x22C);
    V0 = LI32(0x02000000);
    V0 = V0 | 0x1D2;
    A1 = LI32(0x15000000);
    SWC1(0, S0 + 0xD0);
    A0 = LI32(0x03000000);
    A2 = ADDIU(S0, 0xF0);
    LWC1(0, V1 + 0x228);
    SW(S0 + 0xE4, V0);
    SWC1(0, S0 + 0xD4);
    SW(S0 + 0xE8, A1);
    SW(S0 + 0xE0, A0);
    SW(S0 + 0xD8, 0);
    SW(S0 + 0xDC, 0);
    SW(S0 + 0xEC, 0);
    A0 = LW(R->chain);
    V0 = SUBU(A2, A0);
    V1 = LW(A0);
    V0 = SRA(V0, 4);
    V0 = ADDIU(V0, -1);
    V1 = V1 | V0;
    SW(A0, V1);
    SW(GP + (u32)R->g_chain, A2);
    rs.blk1 = (u32)A2;
    CALL(0x19C, 5);                     /* func_001FB848's copy: the GS register reset REF */
    A0 = LW(R->chain);
    V0 = LI32(0x30000000);
    V0 = V0 | 0x7;
    V1 = HI16(R->gs7);
    SW(A0, V0);
    V1 = LI32(R->gs7);
    A1 = LI32(0x50000000);
    A0 = LI32(0xFFFF0000);
    A2 = LW(R->chain);
    A1 = A1 | 0x7;
    A0 = A0 | 0xFFFF;
    LQ(31, SP + 0x60);
    SW(A2 + 4, V1);
    LQ(17, SP + 0x50);
    V0 = LW(R->chain);
    LQ(16, SP + 0x40);
    SW(V0 + 8, 0);
    V1 = LW(R->chain);
    SW(V1 + 12, A1);
    SW(GP + (u32)R->g_a, A0);
    V0 = LW(R->chain);
    SW(GP + (u32)R->g_b, A0);
    V0 = ADDIU(V0, 16);
    SW(GP + (u32)R->g_c, A0);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    SW(GP + (u32)R->g_d, A0);
    SP = ADDIU(SP, 0x70);
    rs.ref = R->gs7; rs.refqwc = 7;
    stripb_record(&rs);
    p_ba150++;
}

/* ============ func_L02_00250A58: a grid of up to 8 x 8 cells, a strip of 124 vertices each ============
 * a0 the object: +0 (x, y, z) the grid's corner, +0x10 the cell size (x, y), +0x18 / +0x1A its
 * width / height, +0x34 the cells' REF data (0x200 bytes a cell) */
typedef struct { s32 g_chain; u32 tcnt, u3, ref3f; } G50a58Refs;
static void run_50a58(RcCtx *c, const G50a58Refs *R)
{
    V0 = LI32(0x40E00000);
    lqc2(13, (u32)A0 + 16);
    QMTC2(2, 2);
    lqc2(24, (u32)A0);
    vop(13, Z, OADD, 0, 13, 0);
    vop(13, W, OMUL, 0, 13, 1);
    vop(13, XY, OMUL, 13, 2, 0);
    vop(29, Z, OADD, 24, 0, 2);
    T7 = LI32(0x70000000);
    vop(29, Y, OADD, 24, 0, 1);
    T7 = T7 | 0x2000;
    A3 = LW(A0 + 0x34);
    T1 = LHU(A0 + 0x1A);
    do {
        vop(29, X, OADD, 24, 0, 0);
        T0 = LHU(A0 + 0x18);
        int more;
        do {
            /* one cell: its flag byte in the scratchpad (0x2000..), 0 = not drawn */
            p_50a58_cells++;
            T5 = LB(T7);
            T6 = LI32(0x70000000);
            {
                int bt = T5 == 0;
                T7 = ADDIU(T7, 1);
                if (bt) goto next_cell;
            }
            p_50a58_drawn++;
            if (T5 == 2) p_50a58_flag2++; else if (T5 != 1) p_50a58_flagx++;
            /* the 8 x 8 grid points (x, y) of the cell to the scratchpad (0x0000..0x01FF) */
            vop(25, Y, OADD, 29, 0, 1);
            T3 = 7;
            do {
                vop(25, X, OADD, 29, 0, 0);
                T2 = 7;
                do {
                    QMFC2(1, 25);
                    SD(T6, AT);
                    T2 = ADDIU(T2, -1);
                    vop(25, X, OADD, 25, 13, 2);
                    T6 = ADDIU(T6, 8);
                } while ((s64)T2 >= 0);
                T3 = ADDIU(T3, -1);
                vop(25, Y, OADD, 25, 13, 3);
            } while ((s64)T3 >= 0);
            {
                StripbRec r = { RT_50A58, SB_GRID };
                T4 = LI32(0x70000000);
                T9 = LW(GP + (u32)R->g_chain);
                T4 = T4 | 0x2800;
                AT = T9;
                r.blk0 = (u32)T9; r.n = 124;
                tmpl5(c, 24, R->tcnt);
                V0 = LW(AT - 64);
                V0 = V0 | 0x7C;
                SW(AT - 64, V0);
                T8 = LI32(R->u3);
                LQ(2, T8);
                SQ(2, AT);
                AT = ADDIU(AT, 16);
                V0 = LW(AT - 4);
                V1 = 0x7C;
                vop(1, X, OADD, 0, 29, 2);
                V1 = SLL(V1, 16);
                V0 = V0 | V1;
                SW(AT - 4, V0);
                V1 = 0x7C;
                QMFC2(6, 1);
                r.xyz = (u32)AT;
                /* 124 vertices: (x, y) of the grid point the index list (scratchpad 0x2800..)
                 * names, z the grid's */
                do {
                    T8 = LI32(0x70000000);
                    V0 = LH(T4);
                    T8 = T8 | V0;
                    SW(AT + 8, A2);
                    T4 = ADDIU(T4, 2);
                    V0 = LW(T8);
                    V1 = ADDIU(V1, -1);
                    SW(AT, V0);
                    V0 = LW(T8 + 4);
                    SW(AT + 4, V0);
                    AT = ADDIU(AT, 12);
                } while (V1 != 0);
                pad16(c);
                V0 = SUBU(AT, T9);
                V0 = SRL(V0, 4);
                V0 = ADDIU(V0, -1);
                V1 = LW(T9);
                V1 = V1 | V0;
                SW(T9, V1);
                /* REF 0x20 to the cell's data, REF 0x3F, then MSCALF in an empty CNT */
                T9 = AT;
                SD(T9 + 8, 0);
                V1 = LI32(0x30000000);
                V1 = V1 | 0x20;
                SW(T9 + 4, A3);
                SW(T9, V1);
                T9 = ADDIU(T9, 16);
                V0 = LI32(R->ref3f);
                SD(T9 + 8, 0);
                V1 = LI32(0x30000000);
                V1 = V1 | 0x3F;
                SW(T9 + 4, V0);
                SW(T9, V1);
                T9 = ADDIU(T9, 16);
                V1 = LI32(0x10000000);
                SW(T9 + 4, 0);
                SW(T9, V1);
                V1 = ADDIU(T5, -1);
                V1 = SLL(V1, 1);
                V1 = ADDIU(V1, 12);
                V0 = LI32(0x15000000);
                V0 = V0 | V1;
                SW(T9 + 8, V0);
                V0 = 0;
                SW(T9 + 12, V0);
                T9 = ADDIU(T9, 16);
                V0 = ADDIU(GP, R->g_chain);
                SW(V0, T9);
                r.blk1 = (u32)T9; r.ref = (u32)A3; r.refqwc = 0x20; r.ref2 = R->ref3f; r.ref2qwc = 0x3F;
                r.mscal = (u32)V1 << 3; r.clipped = T5 == 2;
                stripb_record(&r);
            }
        next_cell:
            A3 = ADDIU(A3, 512);
            T0 = ADDIU(T0, -7);
            more = (s64)T0 > 0;
            vop(29, X, OADD, 29, 13, 0);
        } while (more);
        T1 = ADDIU(T1, -7);
        vop(29, Y, OADD, 29, 13, 1);
    } while ((s64)T1 > 0);
    p_50a58++;
}

/* ===== func_L02_002A52D0: the setup, then an interpolated vertex table sent to the GS (DIRECT) =====
 * a0 the object (+0x1C the GIF packet's buffer, +0x20..0x2C the view values it draws with,
 * +0x30..0x32 the fog colour, +0x33 the frames per step, +0x3C / +0x3E the first frame and the
 * count, +0x3F the alpha) */
typedef struct { s32 g_sv4; u32 lvlmode, sv1, sv2, sv3, campos, mpgq, mpga, chain, gs3, frames, time, vbase, texc, d1370; } A52d0Refs;
static void run_a52d0(RcCtx *c, const A52d0Refs *R, const Lay *L)
{
    StripbRec rs = { RT_A52D0, SB_SETUP }, rd = { RT_A52D0, SB_DIRECT };
    SP = ADDIU(SP, -0xA0);
    V0 = HI16(L->view);
    SQ(20, SP + 0x80);
    SQ(19, SP + 0x70);
    S4 = 1;
    SQ(18, SP + 0x60);
    S3 = LI32(L->view);
    SQ(31, SP + 0x90);
    S2 = A0;
    SQ(17, SP + 0x50);
    SQ(16, SP + 0x40);
    V1 = LW(R->lvlmode);
    LWC1(3, S3 + 0x218);
    LWC1(0, S3 + 0x21C);
    LWC1(1, S3 + 0x228);
    LWC1(2, S3 + 0x22C);
    AT = HI16(R->sv1);
    SWC1(3, R->sv1);
    AT = HI16(R->sv2);
    SWC1(0, R->sv2);
    AT = HI16(R->sv3);
    SWC1(1, R->sv3);
    {
        int bt = V1 == S4;
        SWC1(2, GP + (u32)R->g_sv4);
        if (!bt) {
            LWC1(0, S2 + 0x20);
            SWC1(0, S3 + 0x218);
            LWC1(1, S2 + 0x24);
            SWC1(1, S3 + 0x21C);
            LWC1(0, S2 + 0x28);
            SWC1(0, S3 + 0x228);
            LWC1(1, S2 + 0x2C);
            SWC1(1, S3 + 0x22C);
        } else p_a52d0_mode1++;
    }
    S1 = HI16(R->campos);
    CALL(0x84, 0);                      /* UpdateViewContext */
    S1 = LI32(R->campos);
    CALL(0x8C, 1);                      /* build_rotation_view_proj */
    AT = LI32(0x44800000);
    MTC1(12, AT);
    A0 = SP;
    CALL(0x9C, 2);                      /* func_001FA1C0 */
    AT = LI32(0xC4800000);
    MTC1(12, AT);
    A0 = ADDIU(SP, 0x30);
    A1 = S1;
    CALL(0xB0, 3);                      /* FastVecScale */
    AT = LI32(0x3F800000);
    MTC1(0, AT);
    V0 = HI16(R->mpgq);
    A1 = LHU(R->mpgq);
    A0 = HI16(R->mpga);
    SWC1(0, SP + 0x3C);
    A0 = LI32(R->mpga);
    CALL(0xD0, 4);                      /* VU1_addDataRef: the MPG (always) */
    V1 = LW(R->chain);
    V0 = LI32(0x10000000);
    T0 = LI32(0x11000000);
    A0 = LI32(0x01000000);
    SW(V1, V0);
    A0 = A0 | 0x404;
    A3 = LI32(0x6C0C0000);
    A2 = SP;
    V1 = LW(R->chain);
    A3 = A3 | 0x43A4;
    A1 = ADDIU(S1, -0x100);
    SW(V1 + 4, 0);
    V0 = LW(R->chain);
    SW(V0 + 8, T0);
    V1 = LW(R->chain);
    SW(V1 + 12, A0);
    S0 = LW(R->chain);
    rs.blk0 = (u32)S0;
    SW(S0 + 16, 0);
    A0 = ADDIU(S0, 32);
    SW(S0 + 20, 0);
    SW(S0 + 24, 0);
    SW(S0 + 28, A3);
    CALL(0x140, 5);                     /* sce_vu0_mul_matrix */
    A0 = ADDIU(S0, 0x60);
    A1 = ADDIU(S1, -0x80);
    A2 = SP;
    CALL(0x150, 6);
    V0 = LI32(0x303E0000);
    V1 = 0x8000;
    V0 = V0 | 0xC000;
    A0 = 0x412;
    SW(S0 + 0xA0, V1);
    T3 = ADDIU(S0, 0xB0);
    SW(S0 + 0xA4, V0);
    V1 = ADDIU(S3, 0x190);
    SW(S0 + 0xA8, A0);
    LWC1(0, S3 + 0x210);
    SWC1(0, S0 + 0xAC);
    LQ(2, V1);
    SQ(2, T3);
    T3 = ADDIU(S0, 0xC0);
    V1 = ADDIU(S3, 0x1A0);
    LQ(2, V1);
    SQ(2, T3);
    LWC1(0, S3 + 0x22C);
    V0 = LI32(0x02000000);
    V0 = V0 | 0x1D2;
    V1 = LI32(0x03000000);
    SWC1(0, S0 + 0xD0);
    A0 = LI32(0x15000000);
    T3 = ADDIU(S0, 0xF0);
    T0 = LI32(0x30000000);
    LWC1(0, S3 + 0x228);
    T0 = T0 | 0x3;
    SW(S0 + 0xE4, V0);
    A1 = HI16(R->gs3);
    SWC1(0, S0 + 0xD4);
    A1 = LI32(R->gs3);
    SW(S0 + 0xE0, V1);
    A2 = LI32(0x50000000);
    SW(S0 + 0xE8, A0);
    A2 = A2 | 0x3;
    SW(S0 + 0xD8, 0);
    A3 = LI32(0x10000000);
    SW(S0 + 0xDC, 0);
    A3 = A3 | 0x9;
    SW(S0 + 0xEC, 0);
    T2 = LI32(0x50000000);
    T2 = T2 | 0x9;
    T4 = HI16(R->frames);
    A0 = LW(R->chain);
    T4 = LI32(R->frames);
    T6 = 0x8000;
    T6 = T6 << 44;
    T6 = T6 | 0x400;
    T1 = 0x1000;
    V0 = SUBU(T3, A0);
    V1 = LW(A0);
    V0 = SRA(V0, 4);
    V0 = ADDIU(V0, -1);
    V1 = V1 | V0;
    SW(A0, V1);
    AT = HI16(R->chain);
    SW(R->chain, T3);
    rs.blk1 = (u32)T3;
    /* REF 3 to the level's GS block (DIRECT 3), then CNT 9 (DIRECT 9): the GS registers below */
    SW(S0 + 0xF0, T0);
    V0 = LW(R->chain);
    SW(V0 + 4, A1);
    V1 = LW(R->chain);
    SW(V1 + 8, 0);
    V0 = LW(R->chain);
    SW(V0 + 12, A2);
    V1 = LW(R->chain);
    V0 = ADDIU(V1, 16);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    SW(V1 + 16, A3);
    V0 = LW(R->chain);
    SW(V0 + 4, 0);
    V1 = LW(R->chain);
    SW(V1 + 8, 0);
    A0 = LW(R->chain);
    SW(A0 + 12, T2);
    V0 = LW(R->chain);
    V1 = LW(R->time);
    V0 = ADDIU(V0, 16);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    /* the animation frames: t / frames per step, the step's two vertex tables and the next one's */
    MTC1(0, V1);
    CVTSW(0);
    V0 = LBU(S2 + 0x33);
    ee_div(c, V1, V0);
    if (V0 == 0) { p_a52d0_div0++; rc_break(L->start + 0x2D8); }
    A1 = LBU(S2 + 0x3E);
    MTC1(1, V0);
    CVTSW(1);
    T2 = LHU(S2 + 0x3C);
    FR(0) = rc_fc(rc_fdiv(FR(0), FR(1)));
    T5 = LW(S2 + 0x1C);
    T3 = LW(R->vbase);
    V1 = LO;
    ee_div(c, V1, A1);
    A0 = ADDIU(V1, 1);
    MTC1(12, V1);
    CVTSW(12);
    FR(12) = rc_fc(FR(0) - FR(12));
    V0 = HI;
    ee_div(c, A0, A1);
    V0 = ADDU(T2, V0);
    V0 = SLL(V0, 4);
    V0 = ADDU(V0, T4);
    A1 = LHU(V0 + 8);
    A1 = SLL(A1, 4);
    A1 = ADDU(T3, A1);
    A2 = HI;
    T2 = ADDU(T2, A2);
    T2 = SLL(T2, 4);
    A2 = LHU(V0 + 0xA);
    T2 = ADDU(T2, T4);
    A3 = LHU(T2 + 8);
    A2 = SLL(A2, 4);
    T0 = LHU(T2 + 0xA);
    A2 = ADDU(T3, A2);
    SD(T5, T6);
    A3 = SLL(A3, 4);
    T0 = SLL(T0, 4);
    A3 = ADDU(T3, A3);
    V0 = LW(S2 + 0x1C);
    T0 = ADDU(T3, T0);
    SD(V0 + 8, 0);
    A0 = LW(S2 + 0x1C);
    A0 = ADDIU(A0, 16);
    CALL(0x380, 7);                     /* func_L02_00250C78: the GIF packet (0x400 qwords) */
    /* the GS registers */
    V0 = LW(R->texc);
    V1 = LI32(0x18100000);
    A0 = 0xB000;
    A0 = A0 << 19;
    A1 = LW(R->lvlmode);
    V0 = SRA(V0, 8);
    V0 = V0 | V1;
    V0 = V0 | A0;
    AT = HI16(R->d1370);
    SD(R->d1370, V0);
    V1 = LBU(S2 + 0x31);
    A0 = LBU(S2 + 0x30);
    V0 = LBU(S2 + 0x32);
    V1 = SLL(V1, 8);
    A0 = A0 | V1;
    V0 = SLL(V0, 16);
    {
        int bt = A1 != S4;
        T3 = V0 | A0;
        if (!bt) {
            V0 = LW(S3 + 0x234);
            A0 = LW(S3 + 0x238);
            V1 = LW(S3 + 0x230);
            V0 = SLL(V0, 8);
            A0 = SLL(A0, 16);
            V1 = V1 | V0;
            V1 = V1 | A0;
            T3 = V1;
        }
    }
    V1 = LW(R->chain);
    rd.blk0 = (u32)V1 - 0x20;
    V0 = 0x8000;
    V0 = V0 << 48;
    V0 = V0 | 0x8001;
    A2 = LBU(S2 + 0x3F);
    A0 = 0xEEEE;
    A0 = A0 << 16;
    A0 = A0 | 0xEEEE;
    SD(V1 + 8, A0);
    A3 = 0x64;
    SD(V1, V0);
    A1 = 0x42;
    T1 = 6;
    T5 = 0x3D;
    V0 = LBU(S2 + 0x3F);
    A0 = 0x60;
    SD(V1 + 0x18, A1);
    A0 = A0 < A2;
    V0 = V0 << 32;
    A1 = LI32(0x00050000);
    A1 = A1 | 0x370B;
    V0 = V0 | A3;
    A2 = LI32(0x00050000);
    A2 = A2 | 0x360A;
    SD(V1 + 0x10, V0);
    if (A0 == 0) A2 = A1; else p_a52d0_hialpha++;
    T6 = 0x8000;
    T6 = T6 << 33;
    T0 = 0x50;
    V0 = LD(R->d1370);
    A1 = 0x51;
    SD(V1 + 0x28, T1);
    T2 = 0x8000;
    T2 = T2 << 23;
    T2 = T2 | 0x40;
    SD(V1 + 0x30, T3);
    T1 = 0x52;
    SD(V1 + 0x38, T5);
    T3 = 0x53;
    SD(V1 + 0x20, V0);
    T4 = 0x47;
    T5 = ADDIU(V1, 0x90);
    A0 = LI32(0x30000000);
    V0 = LW(R->texc);
    A0 = A0 | 0x401;
    SD(V1 + 0x48, T0);
    A3 = LI32(0x50000000);
    V0 = (u64)((s64)V0 >> 8);
    SD(V1 + 0x58, A1);
    V0 = V0 << 32;
    SD(V1 + 0x60, T2);
    V0 = V0 | T6;
    SD(V1 + 0x68, T1);
    SD(V1 + 0x78, T3);
    A3 = A3 | 0x401;
    SD(V1 + 0x80, A2);
    A1 = LI32(0x10000000);
    SD(V1 + 0x88, T4);
    A1 = A1 | 0x2;
    SD(V1 + 0x40, V0);
    A2 = LI32(0x50000000);
    SD(V1 + 0x50, 0);
    A2 = A2 | 0x2;
    SD(V1 + 0x70, 0);
    T1 = 0x8000;
    T1 = T1 << 45;
    T1 = T1 | 0x8001;
    AT = HI16(R->chain);
    SW(R->chain, T5);
    T2 = 0xE;
    /* REF 0x401 to the GIF packet (DIRECT 0x401), then CNT 2 (DIRECT 2): TEXFLUSH */
    SW(V1 + 0x90, A0);
    T0 = 0x3F;
    A0 = LW(S2 + 0x1C);
    rd.ref = (u32)A0; rd.refqwc = 0x401;
    V0 = LW(R->chain);
    SW(V0 + 4, A0);
    V1 = LW(R->chain);
    SW(V1 + 8, 0);
    V0 = LW(R->chain);
    SW(V0 + 12, A3);
    V1 = LW(R->chain);
    V0 = ADDIU(V1, 16);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    SW(V1 + 16, A1);
    LQ(31, SP + 0x90);
    V0 = LW(R->chain);
    LQ(20, SP + 0x80);
    SW(V0 + 4, 0);
    LQ(19, SP + 0x70);
    V1 = LW(R->chain);
    LQ(18, SP + 0x60);
    SW(V1 + 8, 0);
    LQ(17, SP + 0x50);
    V0 = LW(R->chain);
    LQ(16, SP + 0x40);
    SW(V0 + 12, A2);
    V1 = LW(R->chain);
    V0 = ADDIU(V1, 16);
    A0 = ADDIU(V1, 0x30);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    SD(V1 + 0x10, T1);
    SD(V0 + 0x18, T0);
    SD(V0 + 8, T2);
    SD(V0 + 0x10, 0);
    AT = HI16(R->chain);
    SW(R->chain, A0);
    rd.blk1 = (u32)A0;
    V0 = LW(R->texc);
    V0 = ADDIU(V0, 0x4000);
    AT = HI16(R->texc);
    SW(R->texc, V0);
    SP = ADDIU(SP, 0xA0);
    rs.ref = R->gs3; rs.refqwc = 3;
    stripb_record(&rs);
    stripb_record(&rd);
    p_a52d0++;
}

/* =============== func_L06_00216B38: a ribbon in four passes, two vertices a point ===============
 * a0 the ribbon: +0 / +4 the head and tail times, +8 the pass ends (4 words), +0xC / +0x10 the
 * two colours, +0x14 points a pass, +0x18 the points (a qword each), +0x1C the U step,
 * +0x20 the first U; a1 a matrix (4 qwords) */
typedef struct { s32 g_chain; u32 tcnt, u1, u2, u3; } L16b38Refs;
static void run_16b38(RcCtx *c, const L16b38Refs *R)
{
    lqc2(14, (u32)A1);
    T1 = 4;
    lqc2(15, (u32)A1 + 16);
    lqc2(16, (u32)A1 + 32);
    lqc2(17, (u32)A1 + 48);
    AT = LI32(0x3D4C0000);
    AT = AT | 0xCCCD;
    QMTC2(1, 1);
    vmove(20, 0);
    vmove(21, 0);
    vop(20, Z, OSUB, 20, 1, 0);
    vop(21, Z, OADD, 21, 1, 0);
    do {
        StripbRec r = { RT_16B38, SB_STRIP };
        p_16b38_pass++;
        T1 = ADDIU(T1, -1);
        A1 = LI32(0x70000000);
        A1 = A1 | 0x1000;
        A3 = LI32(0x70000000);
        A3 = A3 | 0x2000;
        AT = LW(A0 + 0x20);
        QMTC2(1, 1);
        vop(4, Y, OADD, 0, 0, BV);
        vop(4, X, OADD, 0, 1, BV);
        T7 = LW(A0 + 0x14);
        vop(5, X, OADD, 0, 1, BV);
        vop(5, Y, OADD, 0, 0, 3);
        AT = LW(A0 + 0x1C);
        QMTC2(6, 1);
        A2 = LW(A0 + 0x18);
        {
            u64 p = (u64)((s64)(s32)(u32)T1 * (s32)(u32)T7);
            LO = SE32((u32)p); HI = SE32((u32)(p >> 32));
        }
        AT = LO;
        AT = SLL(AT, 4);
        A2 = ADDU(A2, AT);
        /* per point: the two edge vertices (the point -+ 0.05 z, times the matrix) to the
         * scratchpad 0x1000.. (a qword each), their ST (u, 0) / (u, 1) to 0x2000.. */
        do {
            p_16b38_pairs++;
            vop(4, X, OADD, 4, 6, BV);
            vop(5, X, OADD, 5, 6, BV);
            lqc2(2, (u32)A2);
            vop(3, XYZ, OADD, 2, 21, BV);
            vop(2, XYZ, OADD, 2, 20, BV);
            vop(ACC, XYZW, OMUL, 17, 0, 3);
            vop(ACC, XYZW, OMADD, 16, 3, 2);
            vop(ACC, XYZW, OMADD, 15, 3, 1);
            vop(3, XYZW, OMADD, 14, 3, 0);
            vop(ACC, XYZW, OMUL, 17, 0, 3);
            vop(ACC, XYZW, OMADD, 16, 2, 2);
            vop(ACC, XYZW, OMADD, 15, 2, 1);
            vop(2, XYZW, OMADD, 14, 2, 0);
            QMFC2(1, 4);
            T7 = ADDIU(T7, -1);
            SD(A3, AT);
            sqc2(2, (u32)A1);
            sqc2(3, (u32)A1 + 16);
            QMFC2(1, 5);
            A1 = ADDIU(A1, 0x20);
            SD(A3 + 8, AT);
            A2 = ADDIU(A2, 16);
            A3 = ADDIU(A3, 16);
        } while (T7 != 0);
        /* the pass's colour: the two colours blended by where its end lies between the times */
        AT = LW(A0);
        T4 = LW(A0 + 4);
        QMTC2(11, 12);
        vitof0(11);
        AT = SUBU(AT, T4);
        QMTC2(10, 1);
        vitof0(10);
        rc_vu0.q = rc_fc(rc_fdiv(VF(0)[3], VF(11)[0]));
        { f32 t = rc_fc(VF(0)[0] + rc_vu0.q); VF(11)[0] = t; }
        rc_vu0.q = rc_fc(rc_fdiv(VF(0)[3], VF(10)[0]));
        { f32 t = rc_fc(VF(0)[0] + rc_vu0.q); VF(10)[0] = t; }
        AT = LW(A0 + 8);
        V0 = SLL(T1, 2);
        AT = ADDU(AT, V0);
        AT = LW(AT);
        V0 = SUBU(AT, T4);
        if ((s64)V0 <= 0) {
            QMTC2(1, 1);
            vitof0(1);
            vop(1, X, OMUL, 1, 11, BV);
            vop(1, W, OSUB, 0, 1, 0);
            vop(1, X, OADD, 0, 1, 3);
        } else {
            p_16b38_pos++;
            AT = SUBU(AT, T4);
            QMTC2(1, 1);
            vitof0(1);
            vop(1, X, OMUL, 1, 10, BV);
        }
        AT = LW(A0 + 0xC);
        V0 = LW(A0 + 0x10);
        pextlb0(&c->r[1]);
        pextlh0(&c->r[1]);
        pextlb0(&c->r[2]);
        pextlh0(&c->r[2]);
        QMTC2(2, 1);
        QMTC2(3, 2);
        vop(1, W, OSUB, 0, 1, 0);
        vitof0(2);
        vitof0(3);
        vop(ACC, XYZW, OMUL, 2, 1, 3);
        vop(1, XYZW, OMADD, 3, 1, 0);
        vftoi0(1);
        QMFC2(6, 1);
        ppach0(&c->r[6]);
        ppacb0(&c->r[6]);
        /* the strip: 2 x points vertices, always clipped (MSCALF 0x70) */
        T5 = LW(A0 + 0x14);
        T5 = SLL(T5, 1);
        A1 = LI32(0x70000000);
        A1 = A1 | 0x1000;
        A3 = LI32(0x70000000);
        A3 = A3 | 0x2000;
        T0 = 1;
        T0 = SLL(T0, 1);
        T2 = ADDIU(GP, R->g_chain);
        T0 = ADDIU(T0, 12);
        T2 = LW(T2);
        AT = T2;
        T7 = SLL(T5, 16);
        r.blk0 = (u32)T2; r.n = (u32)T5;
        tmpl5(c, 11, R->tcnt);
        V0 = LW(AT - 64);
        V0 = V0 | T5;
        SW(AT - 64, V0);
        unpack1(c, 11, R->u1);
        r.st = (u32)AT;
        T3 = A3;
        V1 = T5;
        do {
            V0 = LD(T3);
            V1 = ADDIU(V1, -1);
            SD(AT, V0);
            T3 = ADDIU(T3, 8);
            AT = ADDIU(AT, 8);
        } while (V1 != 0);
        pad16(c);
        T8 = AT;
        unpack1(c, 11, R->u2);
        r.col = (u32)AT;
        V1 = T5;
        do {
            V1 = ADDIU(V1, -1);
            SW(AT, A2);
            AT = ADDIU(AT, 4);
        } while (V1 != 0);
        pad16(c);
        T9 = AT;
        unpack1(c, 11, R->u3);
        r.xyz = (u32)AT;
        T3 = A1;
        V1 = T5;
        do {
            V0 = LW(T3);
            V1 = ADDIU(V1, -1);
            SW(AT, V0);
            V0 = LW(T3 + 4);
            SW(AT + 4, V0);
            V0 = LW(T3 + 8);
            SW(AT + 8, V0);
            T3 = ADDIU(T3, 16);
            AT = ADDIU(AT, 12);
        } while (V1 != 0);
        pad16(c);
        V0 = LI32(0x15000000);
        V0 = V0 | T0;
        SW(AT, V0);
        V0 = LI32(0x11000000);
        SW(AT + 4, V0);
        SD(AT + 8, 0);
        AT = ADDIU(AT, 16);
        V0 = SUBU(AT, T2);
        V0 = SRL(V0, 4);
        V0 = ADDIU(V0, -1);
        V1 = LW(T2);
        V1 = V1 | V0;
        SW(T2, V1);
        V0 = ADDIU(GP, R->g_chain);
        T2 = AT;
        SW(V0, T2);
        r.blk1 = (u32)AT; r.mscal = 0x70; r.clipped = 1;
        stripb_record(&r);
    } while (T1 != 0);
    p_16b38++;
}

/* ====== func_L09_002C2B08: the setup, a GS block, then a strip (func_L00_001FDE48) per visible entry ======
 * a0 the object (+0x20..0x2C the view values, +0x30..0x32 the fog colour, +0x3F the alpha),
 * a1 the entries (0x20 bytes: +0x10 xyz, +0x14 ST, +0x1C n), a2 their count */
typedef struct { s32 g_sv4; u32 lvlmode, sv1, sv2, sv3, campos, mpgq, mpga, chain, gs3, d1370; } C2b08Refs;
static void run_c2b08(RcCtx *c, const C2b08Refs *R, const Lay *L)
{
    StripbRec rs = { RT_C2B08, SB_SETUP };
    SP = ADDIU(SP, -0xC0);
    V0 = HI16(L->view);
    SQ(22, SP + 0xA0);
    SQ(21, SP + 0x90);
    S6 = A2;
    SQ(20, SP + 0x80);
    S5 = 1;
    SQ(19, SP + 0x70);
    S4 = A1;
    SQ(18, SP + 0x60);
    S3 = A0;
    SQ(31, SP + 0xB0);
    S2 = LI32(L->view);
    SQ(17, SP + 0x50);
    SQ(16, SP + 0x40);
    V1 = LW(R->lvlmode);
    LWC1(3, S2 + 0x218);
    LWC1(2, S2 + 0x21C);
    LWC1(1, S2 + 0x228);
    LWC1(0, S2 + 0x22C);
    AT = HI16(R->sv1);
    SWC1(3, R->sv1);
    AT = HI16(R->sv2);
    SWC1(2, R->sv2);
    AT = HI16(R->sv3);
    SWC1(1, R->sv3);
    {
        int bt = V1 == S5;
        SWC1(0, GP + (u32)R->g_sv4);
        if (!bt) {
            LWC1(0, S3 + 0x20);
            SWC1(0, S2 + 0x218);
            LWC1(1, S3 + 0x24);
            SWC1(1, S2 + 0x21C);
            LWC1(0, S3 + 0x28);
            SWC1(0, S2 + 0x228);
            LWC1(1, S3 + 0x2C);
            SWC1(1, S2 + 0x22C);
        } else p_c2b08_mode1++;
    }
    S1 = HI16(R->campos);
    CALL(0x94, 0);                      /* UpdateViewContext */
    S1 = LI32(R->campos);
    CALL(0x9C, 1);                      /* build_rotation_view_proj */
    AT = LI32(0x44800000);
    MTC1(12, AT);
    A0 = SP;
    CALL(0xAC, 2);                      /* func_001FA1C0 */
    AT = LI32(0xC4800000);
    MTC1(12, AT);
    A0 = ADDIU(SP, 0x30);
    A1 = S1;
    CALL(0xC0, 3);                      /* FastVecScale */
    AT = LI32(0x3F800000);
    MTC1(0, AT);
    V0 = HI16(R->mpgq);
    A1 = LHU(R->mpgq);
    A0 = HI16(R->mpga);
    SWC1(0, SP + 0x3C);
    A0 = LI32(R->mpga);
    CALL(0xE0, 4);                      /* VU1_addDataRef: the MPG (always) */
    V1 = LW(R->chain);
    V0 = LI32(0x10000000);
    T0 = LI32(0x11000000);
    A0 = LI32(0x01000000);
    SW(V1, V0);
    A0 = A0 | 0x404;
    A3 = LI32(0x6C0C0000);
    A2 = SP;
    V1 = LW(R->chain);
    A3 = A3 | 0x43A4;
    A1 = ADDIU(S1, -0x100);
    SW(V1 + 4, 0);
    V0 = LW(R->chain);
    SW(V0 + 8, T0);
    V1 = LW(R->chain);
    SW(V1 + 12, A0);
    S0 = LW(R->chain);
    rs.blk0 = (u32)S0;
    SW(S0 + 16, 0);
    A0 = ADDIU(S0, 32);
    SW(S0 + 20, 0);
    SW(S0 + 24, 0);
    SW(S0 + 28, A3);
    CALL(0x150, 5);                     /* sce_vu0_mul_matrix */
    A0 = ADDIU(S0, 0x60);
    A1 = ADDIU(S1, -0x80);
    A2 = SP;
    CALL(0x160, 6);
    V0 = LI32(0x303E0000);
    V1 = 0x8000;
    V0 = V0 | 0xC000;
    A0 = 0x412;
    SW(S0 + 0xA0, V1);
    T2 = ADDIU(S0, 0xB0);
    SW(S0 + 0xA4, V0);
    V1 = ADDIU(S2, 0x190);
    SW(S0 + 0xA8, A0);
    LWC1(0, S2 + 0x210);
    SWC1(0, S0 + 0xAC);
    LQ(2, V1);
    SQ(2, T2);
    T2 = ADDIU(S0, 0xC0);
    V1 = ADDIU(S2, 0x1A0);
    LQ(2, V1);
    SQ(2, T2);
    LWC1(0, S2 + 0x22C);
    V0 = LI32(0x02000000);
    V1 = LI32(0x03000000);
    V0 = V0 | 0x1D2;
    SWC1(0, S0 + 0xD0);
    A0 = LI32(0x15000000);
    T2 = ADDIU(S0, 0xF0);
    T0 = LI32(0x30000000);
    LWC1(0, S2 + 0x228);
    T0 = T0 | 0x3;
    SW(S0 + 0xE4, V0);
    A1 = HI16(R->gs3);
    SW(S0 + 0xE0, V1);
    A1 = LI32(R->gs3);
    SWC1(0, S0 + 0xD4);
    A2 = LI32(0x50000000);
    SW(S0 + 0xE8, A0);
    A2 = A2 | 0x3;
    SW(S0 + 0xD8, 0);
    A3 = LI32(0x10000000);
    SW(S0 + 0xDC, 0);
    A3 = A3 | 0x8;
    SW(S0 + 0xEC, 0);
    T1 = LI32(0x50000000);
    T1 = T1 | 0x8;
    A0 = LW(R->chain);
    V0 = SUBU(T2, A0);
    V1 = LW(A0);
    V0 = SRA(V0, 4);
    V0 = ADDIU(V0, -1);
    V1 = V1 | V0;
    SW(A0, V1);
    AT = HI16(R->chain);
    SW(R->chain, T2);
    rs.blk1 = (u32)T2;
    /* REF 3 to the level's GS block (DIRECT 3), then CNT 8 (DIRECT 8): the GS registers below */
    SW(S0 + 0xF0, T0);
    V0 = LW(R->chain);
    SW(V0 + 4, A1);
    V1 = LW(R->chain);
    SW(V1 + 8, 0);
    V0 = LW(R->chain);
    SW(V0 + 12, A2);
    V1 = LW(R->chain);
    V0 = ADDIU(V1, 16);
    AT = HI16(R->chain);
    SW(R->chain, V0);
    SW(V1 + 16, A3);
    V0 = LW(R->chain);
    SW(V0 + 4, 0);
    V1 = LW(R->chain);
    SW(V1 + 8, 0);
    V0 = LW(R->chain);
    SW(V0 + 12, T1);
    T2 = LW(R->chain);
    A1 = LW(R->lvlmode);
    T4 = ADDIU(T2, 16);
    AT = HI16(R->chain);
    SW(R->chain, T4);
    V0 = LBU(S3 + 0x31);
    A0 = LBU(S3 + 0x30);
    V1 = LBU(S3 + 0x32);
    V0 = SLL(V0, 8);
    A0 = A0 | V0;
    V1 = SLL(V1, 16);
    {
        int bt = A1 != S5;
        T5 = V1 | A0;
        if (!bt) {
            V0 = LW(S2 + 0x234);
            A0 = LW(S2 + 0x238);
            V1 = LW(S2 + 0x230);
            V0 = SLL(V0, 8);
            A0 = SLL(A0, 16);
            V1 = V1 | V0;
            V1 = V1 | A0;
            T5 = V1;
        }
    }
    A1 = LBU(S3 + 0x3F);
    V0 = 0xE000;
    V0 = V0 << 47;
    V0 = V0 | 0x8001;
    SD(T2 + 0x10, V0);
    V1 = 0xEEEE;
    V1 = V1 << 16;
    V1 = V1 | 0xEEEE;
    SD(T4 + 8, V1);
    A2 = 0x64;
    A0 = 0x42;
    V1 = 0x60;
    V0 = LBU(S3 + 0x3F);
    V1 = V1 < A1;
    SD(T4 + 0x18, A0);
    A1 = LI32(0x00050000);
    A1 = A1 | 0x360A;
    V0 = V0 << 32;
    A0 = LI32(0x00050000);
    A0 = A0 | 0x370B;
    V0 = V0 | A2;
    if (V1 == 0) A1 = A0;
    SD(T4 + 0x10, V0);
    T3 = 0x47;
    V1 = 6;
    A0 = 0x3D;
    V0 = LD(R->d1370);
    A2 = 8;
    A3 = LI32(0x00050000);
    T0 = 0xFF90;
    T0 = T0 << 32;
    T0 = T0 | 0x260;
    T1 = 0x14;
    SD(T4 + 0x20, V0);
    SD(T4 + 0x28, V1);
    T2 = ADDIU(T2, 0x90);
    SD(T4 + 0x78, T3);
    V1 = LI32(0x00750000);
    SD(T4 + 0x30, T5);
    V0 = LI32(0x70000000);
    SD(T4 + 0x38, A0);
    V1 = V1 | 0x7C8E;
    SD(T4 + 0x48, A2);
    S2 = 0xFF;
    SD(T4 + 0x50, A3);
    V0 = V0 | 0x3FC;
    SD(T4 + 0x60, T0);
    SD(T4 + 0x68, T1);
    SD(T4 + 0x70, A1);
    SD(T4 + 0x40, 0);
    SD(T4 + 0x58, T3);
    AT = HI16(R->chain);
    SW(R->chain, T2);
    /* the colours: 256 words of 0x00757C8E in the scratchpad (0x0000..0x03FF) */
    do {
        SW(V0, V1);
        S2 = ADDIU(S2, -1);
        V0 = ADDIU(V0, -4);
    } while ((s64)S2 >= 0);
    {
        int bt = (s64)S6 <= 0;
        S2 = 0;
        if (bt) goto done;
    }
    S5 = LI32(-1);
    S0 = SLL(S2, 5);
    for (;;) {
        p_c2b08_ent++;
        AT = LI32(0x43800000);
        MTC1(12, AT);
        S1 = ADDU(S4, S0);
        S2 = ADDIU(S2, 1);
        A0 = S1;
        CALL(0x410, 7);                 /* FastBSphereCheck(entry, 256): -1 not visible, 0 clipped */
        {
            int bt = V0 == S5;
            V1 = LI32(0x70000000);
            if (bt) { p_c2b08_none++; goto next; }
        }
        p_c2b08_vis++;
        A2 = LW(S1 + 0x1C);
        A0 = LW(S1 + 0x14);
        T0 = V0 < (u64)(s64)1;
        if (T0) p_c2b08_clip++;
        {
            int bt = (s64)A2 <= 0;
            V1 = V1 | 0x1000;
            if (bt) { p_c2b08_empty++; goto strip; }
        }
        /* the ST: the entry's pairs times 0.5, to the scratchpad 0x1000.. */
        AT = LI32(0x3F000000);
        MTC1(1, AT);
        A1 = A2;
        do {
            LWC1(0, A0);
            A1 = ADDIU(A1, -1);
            A0 = ADDIU(A0, 4);
            FR(0) = rc_fc(FR(0) * FR(1));
            SWC1(0, V1);
            V1 = ADDIU(V1, 4);
            LWC1(0, A0);
            A0 = ADDIU(A0, 4);
            FR(0) = rc_fc(FR(0) * FR(1));
            SWC1(0, V1);
            V1 = ADDIU(V1, 4);
        } while (A1 != 0);
    strip:
        V0 = ADDU(S0, S4);
        A3 = LI32(0x70000000);
        A0 = A2;
        A1 = LW(V0 + 0x10);
        A2 = LI32(0x70000000);
        A3 = A3 | 0x1000;
        {
            StripbRec r = { RT_C2B08, SB_STRIP };
            u32 ch0 = M32(R->chain);
            r.n = (u32)A0; r.clipped = T0 != 0;
            CALL(0x484, 8);             /* func_L00_001FDE48: the strip */
            r.blk0 = ch0; r.blk1 = M32(R->chain);
            r.st = ch0 + 0x60;
            r.col = r.st + ((r.n * 8 + 15) & ~15u) + 16;
            r.xyz = r.col + ((r.n * 4 + 15) & ~15u) + 16;
            r.mscal = r.clipped ? 0x70 : 0x60;
            stripb_record(&r);
        }
    next:
        V0 = (s64)S2 < (s64)S6;
        {
            int bt = V0 != 0;
            S0 = SLL(S2, 5);
            if (!bt) break;
        }
    }
done:
    A0 = S3;
    CALL(0x498, 9);                     /* func_L02_002A58C0: the four view values back, a GS packet */
    LQ(31, SP + 0xB0);
    LQ(22, SP + 0xA0);
    LQ(21, SP + 0x90);
    LQ(20, SP + 0x80);
    LQ(19, SP + 0x70);
    LQ(18, SP + 0x60);
    LQ(17, SP + 0x50);
    LQ(16, SP + 0x40);
    SP = ADDIU(SP, 0xC0);
    rs.ref = R->gs3; rs.refqwc = 3;
    stripb_record(&rs);
    p_c2b08++;
}

/* ===== func_L12_002BD3D0: two texture registers, a GS block, the strip setup, then per visible
 * entry its ST (func_L12_002677B4) and a double strip (func_L01_0021F9C8) =====
 * a0 the entries (0x20 bytes: +0x10 xyz, +0x14 ST, +0x18 the ST source, +0x1C n), a1 their
 * count, a2 / a3 the two texture register values */
typedef struct { s32 g_chain; u32 chain, gs9; } Bd3d0Refs;
static void run_bd3d0(RcCtx *c, const Bd3d0Refs *R, const Lay *L)
{
    SP = ADDIU(SP, -0x50);
    SQ(19, SP + 0x30);
    SQ(17, SP + 0x10);
    S3 = A1;
    SQ(16, SP + 0x00);
    S1 = A0;
    SQ(18, SP + 0x20);
    S0 = A3;
    A1 = A2;
    SQ(31, SP + 0x40);
    A0 = 6;
    S2 = 0xB3;
    CALL(0x2C, 0);                      /* VU1_addGSregister(6, a2) */
    A1 = S0;
    A0 = 7;
    CALL(0x38, 1);                      /* VU1_addGSregister(7, a3) */
    A0 = LW(R->chain);
    V0 = LI32(0x30000000);
    V0 = V0 | 0x9;
    V1 = HI16(R->gs9);
    SW(A0, V0);
    V1 = LI32(R->gs9);
    A0 = LI32(0x50000000);
    A1 = LW(R->chain);
    A0 = A0 | 0x9;
    SW(A1 + 4, V1);
    V0 = LW(R->chain);
    SW(V0 + 8, 0);
    V1 = LW(R->chain);
    SW(V1 + 12, A0);
    V0 = LW(R->chain);
    V0 = ADDIU(V0, 16);
    SW(GP + (u32)R->g_chain, V0);
    CALL(0x94, 2);                      /* func_001F7868: the strip setup */
    /* the colours: 180 words of 0x80808080 in the scratchpad (0x0000..0x02CF) */
    V0 = LI32(0x80800000);
    V1 = LI32(0x70000000);
    V0 = V0 | 0x8080;
    do {
        SW(V1, V0);
        S2 = ADDIU(S2, -1);
        V1 = ADDIU(V1, 4);
    } while ((s64)S2 >= 0);
    {
        int bt = (s64)S3 <= 0;
        S2 = S3;
        if (bt) goto done;
    }
    S3 = LI32(-1);
    do {
        p_bd3d0_ent++;
        AT = LI32(0x43800000);
        MTC1(12, AT);
        A0 = S1;
        CALL(0xD8, 3);                  /* FastBSphereCheck(entry, 256) */
        S0 = V0;
        {
            int bt = S0 == S3;
            A3 = LI32(0x70000000);
            if (bt) { p_bd3d0_none++; goto next; }
        }
        p_bd3d0_vis++;
        if (S0 == 0) p_bd3d0_clip++;
        A0 = LW(S1 + 0x1C);
        A1 = LW(S1 + 0x10);
        A3 = A3 | 0x1000;
        A2 = LW(S1 + 0x18);
        CALL(0xF8, 4);                  /* func_L12_002677B4: the second ST to the scratchpad 0x1000.. */
        T0 = LI32(0x70000000);
        A0 = LW(S1 + 0x1C);
        A1 = LW(S1 + 0x10);
        T1 = S0 < (u64)(s64)1;
        A3 = LW(S1 + 0x14);
        A2 = LI32(0x70000000);
        T0 = T0 | 0x1000;
        CALL(0x118, 5);                 /* func_L01_0021F9C8: the strip and its CTXT 1 copy */
    next:
        S2 = ADDIU(S2, -1);
        S1 = ADDIU(S1, 0x20);
    } while (S2 != 0);
done:
    LQ(31, SP + 0x40);
    LQ(19, SP + 0x30);
    LQ(18, SP + 0x20);
    LQ(17, SP + 0x10);
    LQ(16, SP + 0x00);
    SP = ADDIU(SP, 0x50);
    p_bd3d0++;
}

/* ============================== the hooks and check mode ============================== */
static int mode_ = -1, full_ = 0;
static unsigned long long cen_calls[NRT][NLV];
static void census_log(void)
{
    for (int l = 0; l < NLV; l++)
        for (int k = 0; k < NRT; k++)
            if (cen_calls[k][l])
                uya_log("native strips B census, %s %d: %s %llu calls (records: strips %llu, REF strips %llu, grid cells %llu, setups %llu, direct %llu)",
                        l == 19 ? "executable" : "level", l == 19 ? -1 : l, rt_name[k], cen_calls[k][l],
                        rec_count[k][0], rec_count[k][1], rec_count[k][2], rec_count[k][3], rec_count[k][4]);
}
static int get_mode(void)
{
    if (mode_ < 0) {
        const char *e = getenv("UYA_NSTRIPB"), *n = getenv("UYA_NATIVE"), *f = getenv("UYA_NSTRIPB_FULL");
        mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2;
        full_ = f ? atoi(f) : 0;
        uya_log("native strips B (level strip writers of 57843): %s%s", mode_ == 0 ? "off (the game's routines)" : mode_ == 1 ? "check against the game's routines" : "on",
                mode_ == 1 && full_ ? " (the first runs of each compare all of RAM and the scratchpad)" : "");
        atexit(census_log);
    }
    return mode_;
}

/* check mode: the state a routine reads and writes, put aside and compared at its end */
#define NAREA 8
#define SPR 0x70000000u
#define RAMLO 0x00100000u
#define RAMHI 0x02000000u
typedef struct { const char *what; u32 a, n; } Area;
typedef struct {
    const char *name;
    Area ar[NAREA]; int nar;
    u8 *A[NAREA], *N[NAREA], *G[NAREA];
    u32 cap[NAREA];
    u32 win;                /* the chain window (area 0): doubled when a run writes past it */
    u32 chainvar;           /* the chain pointer's word */
    u32 natend;             /* the chain pointer the native run left */
    RcCtx c0, cN;
    RcVu0 v0, vN;
    int valid, full, always_full;   /* always_full: every run compares all of RAM (func_L12_002BD3D0) */
    u8 *FA, *FN, *SA_, *SN;  /* full mode: RAM and the scratchpad */
    unsigned long long runs, ok, bad, skipped, fullruns;
} Chk;
static Chk chk[NRT];
static unsigned long long told_total;
static u32 clampn(u32 a, u32 n) { return a >= RAMHI ? 0 : a + n <= RAMHI ? n : RAMHI - a; }
static void area(Chk *k, const char *what, u32 a, u32 n)
{
    if ((a >> 28) == 7) n = a >= SPR && a + n <= SPR + 0x4000 ? n : 0;
    else { a = (u32)RC_A(a); n = clampn(a, n); }
    int i = k->nar++;
    k->ar[i].what = what; k->ar[i].a = a; k->ar[i].n = n;
    if (k->cap[i] < n) {
        free(k->A[i]); free(k->N[i]); free(k->G[i]);
        k->A[i] = malloc(n); k->N[i] = malloc(n); k->G[i] = malloc(n); k->cap[i] = n;
    }
}
static void grab(Chk *k, u8 **to) { for (int i = 0; i < k->nar; i++) if (k->ar[i].n) memcpy(to[i], (void *)(uintptr_t)k->ar[i].a, k->ar[i].n); }
static void put(Chk *k, u8 **from) { for (int i = 0; i < k->nar; i++) if (k->ar[i].n) memcpy((void *)(uintptr_t)k->ar[i].a, from[i], k->ar[i].n); }
static void paths_log(void)
{
    uya_log("native strips B paths: F9C8 %llu (clipped %llu); BA150 %llu (MPG REF %llu); 50A58 %llu: cells %llu, drawn %llu (flag 2 %llu, other flags %llu); "
            "A52D0 %llu (level mode 1 %llu, alpha > 0x60 %llu, frames 0 %llu); 16B38 %llu: passes %llu (end after the tail %llu), points %llu; "
            "C2B08 %llu (level mode 1 %llu): entries %llu, not visible %llu, visible %llu (clipped %llu, n <= 0 %llu); BD3D0 %llu: entries %llu, not visible %llu, visible %llu (clipped %llu)",
            p_f9c8, p_f9c8_clip, p_ba150, p_ba150_mpg, p_50a58, p_50a58_cells, p_50a58_drawn, p_50a58_flag2, p_50a58_flagx,
            p_a52d0, p_a52d0_mode1, p_a52d0_hialpha, p_a52d0_div0, p_16b38, p_16b38_pass, p_16b38_pos, p_16b38_pairs,
            p_c2b08, p_c2b08_mode1, p_c2b08_ent, p_c2b08_none, p_c2b08_vis, p_c2b08_clip, p_c2b08_empty,
            p_bd3d0, p_bd3d0_ent, p_bd3d0_none, p_bd3d0_vis, p_bd3d0_clip);
}
static void summary(Chk *k)
{
    uya_log("native strips B check, %s: %llu runs, %llu identical, %llu differ, %llu not compared (chain window grown; now 0x%X); %llu of the runs compared all of RAM",
            k->name, k->runs, k->ok, k->bad, k->skipped, k->win, k->fullruns);
    paths_log();
}
static void summary_end(void) { for (int i = 0; i < NRT; i++) if (chk[i].runs % 500) summary(&chk[i]); }

static const char *const rname[32] = { "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };
/* before the native run: the areas (area 0 the chain window), all of RAM in full mode */
static void check_begin(Chk *k, RcCtx *c)
{
    k->c0 = *c; k->v0 = rc_vu0;
    k->full = k->always_full || (full_ > 0 && k->fullruns < (unsigned long long)full_);
    if (k->full) {
        if (!k->FA) {
            k->FA = malloc(RAMHI - RAMLO); k->FN = malloc(RAMHI - RAMLO); k->SA_ = malloc(0x4000); k->SN = malloc(0x4000);
            if (!k->FA || !k->FN || !k->SA_ || !k->SN) { k->full = 0; full_ = 0; k->always_full = 0; }
        }
        if (k->full) { memcpy(k->FA, (void *)(uintptr_t)RAMLO, RAMHI - RAMLO); memcpy(k->SA_, (void *)(uintptr_t)SPR, 0x4000); }
    }
    grab(k, k->A);
}
static void check_native_done(Chk *k, RcCtx *c)
{
    grab(k, k->N);
    k->natend = M32(k->chainvar);
    k->cN = *c; k->vN = rc_vu0;
    if (k->full) {
        memcpy(k->FN, (void *)(uintptr_t)RAMLO, RAMHI - RAMLO); memcpy(k->SN, (void *)(uintptr_t)SPR, 0x4000);
        memcpy((void *)(uintptr_t)RAMLO, k->FA, RAMHI - RAMLO); memcpy((void *)(uintptr_t)SPR, k->SA_, 0x4000);
    } else put(k, k->A);
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
    char why[600] = "";
    int bad = 0;
    u32 gend = M32(k->chainvar);
    u32 ch0 = k->ar[0].a, lim = ch0 + k->ar[0].n;
    u32 ne = (u32)RC_A(k->natend), ge = (u32)RC_A(gend);
    if (!k->full && (ne > lim - 64 || ge > lim - 64 || ne < ch0 || ge < ch0)) {
        /* written past the window: not compared, the window doubled for the next runs */
        k->skipped++;
        if (k->win < 0x1000000u) k->win *= 2;
        if (k->runs % 500 == 0) summary(k);
        return;
    }
    if (k->full) {
        k->fullruns++;
        const u8 *g = (const u8 *)(uintptr_t)RAMLO, *n = k->FN;
        u32 sz = RAMHI - RAMLO, j = 0;
        while (j < sz && g[j] == n[j]) j++;
        if (j < sz) {
            u32 cnt = 0, last = j;
            for (u32 x = j; x < sz; x++) if (g[x] != n[x]) { cnt++; last = x; }
            u32 a = RAMLO + (j & ~3u), gv, nv;
            memcpy(&gv, g + (j & ~3u), 4); memcpy(&nv, n + (j & ~3u), 4);
            bad = 1; snprintf(why, sizeof why, "RAM differs at %08X: %08X, native %08X (%u bytes differ, up to %08X)", a, gv, nv, cnt, RAMLO + last);
        }
        if (!bad && memcmp((void *)(uintptr_t)SPR, k->SN, 0x4000)) {
            const u8 *s = (const u8 *)(uintptr_t)SPR; u32 x = 0; while (s[x] == k->SN[x]) x++;
            bad = 1; snprintf(why, sizeof why, "scratchpad differs at %08X", SPR + x);
        }
    } else {
        grab(k, k->G);
        for (int i = 0; i < k->nar && !bad; i++) {
            u32 n = k->ar[i].n, j = 0;
            while (j < n && k->G[i][j] == k->N[i][j]) j++;
            if (j < n) {
                u32 a = k->ar[i].a + (j & ~3u), g, v;
                memcpy(&g, k->G[i] + (j & ~3u), 4); memcpy(&v, k->N[i] + (j & ~3u), 4);
                bad = 1; snprintf(why, sizeof why, "%s differs at %08X (+0x%X): %08X, native %08X", k->ar[i].what, a, j & ~3u, g, v);
            }
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
    if (!bad && memcmp(&rc_vu0, &k->vN, sizeof rc_vu0)) {
        const RcVu0 *G = &rc_vu0, *N = &k->vN;
        int r = 0; while (r < 32 && !memcmp(G->vf[r], N->vf[r], 16)) r++;
        bad = 1;
        if (r < 32)
            snprintf(why, sizeof why, "VU0 vf%d %08X %08X %08X %08X, native %08X %08X %08X %08X", r,
                     *(const u32 *)&G->vf[r][0], *(const u32 *)&G->vf[r][1], *(const u32 *)&G->vf[r][2], *(const u32 *)&G->vf[r][3],
                     *(const u32 *)&N->vf[r][0], *(const u32 *)&N->vf[r][1], *(const u32 *)&N->vf[r][2], *(const u32 *)&N->vf[r][3]);
        else
            snprintf(why, sizeof why, "VU0 acc %08X %08X %08X %08X / %08X %08X %08X %08X, q %08X / %08X, or its other state",
                     *(const u32 *)&G->acc[0], *(const u32 *)&G->acc[1], *(const u32 *)&G->acc[2], *(const u32 *)&G->acc[3],
                     *(const u32 *)&N->acc[0], *(const u32 *)&N->acc[1], *(const u32 *)&N->acc[2], *(const u32 *)&N->acc[3],
                     *(const u32 *)&G->q, *(const u32 *)&N->q);
    }
    if (bad) { k->bad++; if (told_total++ < 10) uya_log("native strips B check, %s: run %llu differs: %s", k->name, k->runs, why); }
    else k->ok++;
    if (k->runs % 500 == 0) summary(k);
}
static int refs_bad(int rt, int ng, int na, int NG_, int NA_)
{
    if (ng == NG_ && na == NA_) return 0;
    static int told[NRT];
    if (!told[rt]++) uya_log("native strips B: the level's %s has %d / %d data references (not %d / %d): the game's routine runs", rt_name[rt], ng, na, NG_, NA_);
    return 1;
}
static int shape_bad(int rt, int bad)
{
    static int told[NRT];
    if (bad && !told[rt]++) uya_log("native strips B: the level's %s's references are not the expected shape: the game's routine runs", rt_name[rt]);
    return bad;
}
static const Lay *lay_get(int rt, const Lay *t, int n)
{
    const Lay *L = lay_find(t, n);
    static int told[NRT];
    if (!L && !told[rt]++) uya_log("native strips B: %s is not known in level %d: the game's routine runs", rt_name[rt], lv_now());
    return L;
}
static int all_eq(const u32 *a, int from, int to, u32 v) { for (int i = from; i <= to; i++) if (a[i] != v) return 0; return 1; }
/* the start of a check: chain window and chain pointer as areas 0 and 1, then the routine's own */
static Chk *chk_start(int rt, RcCtx *c, u32 chainvar)
{
    Chk *k = &chk[rt];
    if (!k->name) { k->name = rt_name[rt]; k->win = rt == RT_50A58 ? 0x40000 : 0x10000; }
    (void)c;
    k->nar = 0;
    k->chainvar = chainvar;
    area(k, "chain", M32(chainvar), k->win);
    area(k, "chain pointer", chainvar, 4);
    return k;
}

/* ---- func_L01_0021F9C8 ---- */
int uya_stripb_f9c8_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_F9C8][lv_now()]++;
    if (!mode_ || refs_bad(RT_F9C8, ng, na, 3, 6)) return 0;
    if (shape_bad(RT_F9C8, g[0] != g[1] || g[0] != g[2] || a[0] != a[4] || a[1] != a[5])) return 0;
    F9c8Refs R = { g[0], a[0], a[1], a[2], a[3] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = ((u32)GP + (u32)R.g_chain), c0 = M32(cw); run_f9c8(c, &R); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_F9C8, c, (u32)GP + (u32)R.g_chain);
    check_begin(k, c);
    run_f9c8(c, &R);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_f9c8_exit(RcCtx *c) { check_exit(&chk[RT_F9C8], c); }

/* ---- func_L01_002BA150 ---- */
int uya_stripb_ba150_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_BA150][lv_now()]++;
    if (!mode_ || refs_bad(RT_BA150, ng, na, 5, 19)) return 0;
    const u32 ch = a[5];
    if (shape_bad(RT_BA150, a[1] != a[4] || !all_eq(a, 5, 9, ch) || a[11] != ch || a[12] != ch || !all_eq(a, 14, 18, ch) || (u32)GP + (u32)g[0] != ch)) return 0;
    const Lay *L = lay_get(RT_BA150, lay_ba150, sizeof lay_ba150 / sizeof *lay_ba150);
    if (!L) return 0;
    Ba150Refs R = { g[0], g[1], g[2], g[3], g[4], a[0], a[1], a[2], a[3], ch, a[10], a[13] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = (ch), c0 = M32(cw); run_ba150(c, &R, L); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_BA150, c, ch);
    u32 sp = (u32)SP;
    area(k, "stack", sp - 0x70, 0x70);
    area(k, "D_0015F704", R.f704, 4);
    area(k, "gp words", (u32)GP + (u32)R.g_b, 16);
    check_begin(k, c);
    run_ba150(c, &R, L);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_ba150_exit(RcCtx *c) { check_exit(&chk[RT_BA150], c); }

/* ---- func_L02_00250A58 ---- */
int uya_stripb_50a58_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_50A58][lv_now()]++;
    if (!mode_ || refs_bad(RT_50A58, ng, na, 2, 4)) return 0;
    if (shape_bad(RT_50A58, g[0] != g[1])) return 0;
    G50a58Refs R = { g[0], a[0], a[1], a[2] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = ((u32)GP + (u32)R.g_chain), c0 = M32(cw); run_50a58(c, &R); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_50A58, c, (u32)GP + (u32)R.g_chain);
    area(k, "scratchpad", SPR, 0x4000);
    check_begin(k, c);
    run_50a58(c, &R);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_50a58_exit(RcCtx *c) { check_exit(&chk[RT_50A58], c); }

/* ---- func_L02_002A52D0 ---- */
int uya_stripb_a52d0_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_A52D0][lv_now()]++;
    if (!mode_ || refs_bad(RT_A52D0, ng, na, 1, 48)) return 0;
    const u32 ch = a[7];
    if (shape_bad(RT_A52D0, a[0] != a[29] || !all_eq(a, 7, 11, ch) || !all_eq(a, 14, 24, ch) || a[26] != ch || a[31] != ch || !all_eq(a, 34, 45, ch)
                            || a[28] != a[33] || a[28] != a[46] || a[28] != a[47] || a[30] != a[32] || (u32)GP + (u32)g[0] != a[3] + 4)) return 0;
    const Lay *L = lay_get(RT_A52D0, lay_a52d0, sizeof lay_a52d0 / sizeof *lay_a52d0);
    if (!L) return 0;
    A52d0Refs R = { g[0], a[0], a[1], a[2], a[3], a[4], a[5], a[6], ch, a[12], a[13], a[25], a[27], a[28], a[30] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = (ch), c0 = M32(cw); run_a52d0(c, &R, L); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_A52D0, c, ch);
    u32 sp = (u32)SP;
    area(k, "stack", sp - 0xA0, 0xA0);
    area(k, "saved view values / GS word", R.d1370, 0x18);
    area(k, "view", L->view + 0x200, 0x40);
    area(k, "D_0015EF74", R.texc, 4);
    area(k, "GIF packet", M32((u32)A0 + 0x1C), 0x4010);
    check_begin(k, c);
    run_a52d0(c, &R, L);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_a52d0_exit(RcCtx *c) { check_exit(&chk[RT_A52D0], c); }

/* ---- func_L06_00216B38 ---- */
int uya_stripb_16b38_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_16B38][lv_now()]++;
    if (!mode_ || refs_bad(RT_16B38, ng, na, 2, 4)) return 0;
    if (shape_bad(RT_16B38, g[0] != g[1])) return 0;
    L16b38Refs R = { g[0], a[0], a[1], a[2], a[3] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = ((u32)GP + (u32)R.g_chain), c0 = M32(cw); run_16b38(c, &R); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_16B38, c, (u32)GP + (u32)R.g_chain);
    area(k, "scratchpad", SPR, 0x4000);
    check_begin(k, c);
    run_16b38(c, &R);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_16b38_exit(RcCtx *c) { check_exit(&chk[RT_16B38], c); }

/* ---- func_L09_002C2B08 ---- */
int uya_stripb_c2b08_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_C2B08][lv_now()]++;
    if (!mode_ || refs_bad(RT_C2B08, ng, na, 1, 28)) return 0;
    const u32 ch = a[7];
    if (shape_bad(RT_C2B08, a[0] != a[24] || !all_eq(a, 7, 11, ch) || !all_eq(a, 13, 23, ch) || a[25] != ch || a[27] != ch || (u32)GP + (u32)g[0] != a[3] + 4)) return 0;
    const Lay *L = lay_get(RT_C2B08, lay_c2b08, sizeof lay_c2b08 / sizeof *lay_c2b08);
    if (!L) return 0;
    C2b08Refs R = { g[0], a[0], a[1], a[2], a[3], a[4], a[5], a[6], ch, a[12], a[26] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = (ch), c0 = M32(cw); run_c2b08(c, &R, L); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_C2B08, c, ch);
    u32 sp = (u32)SP;
    area(k, "stack", sp - 0xC0, 0xC0);
    area(k, "saved view values / GS word", R.d1370, 0x18);
    area(k, "view", L->view + 0x200, 0x40);
    area(k, "scratchpad", SPR, 0x4000);
    check_begin(k, c);
    run_c2b08(c, &R, L);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_c2b08_exit(RcCtx *c) { check_exit(&chk[RT_C2B08], c); }

/* ---- func_L12_002BD3D0 ---- */
int uya_stripb_bd3d0_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    get_mode();
    cen_calls[RT_BD3D0][lv_now()]++;
    if (!mode_ || refs_bad(RT_BD3D0, ng, na, 1, 7)) return 0;
    const u32 ch = a[0];
    if (shape_bad(RT_BD3D0, a[2] != ch || a[3] != ch || a[4] != ch || a[5] != ch || (u32)GP + (u32)g[0] != ch)) return 0;
    const Lay *L = lay_get(RT_BD3D0, lay_bd3d0, sizeof lay_bd3d0 / sizeof *lay_bd3d0);
    if (!L) return 0;
    Bd3d0Refs R = { g[0], ch, a[1] };
    if (mode_ >= 2) { int sd = sb_direct; sb_direct = 1; u32 s0 = native_stripd_begin(), cw = (ch), c0 = M32(cw); run_bd3d0(c, &R, L); native_stripd_end(s0, cw, c0, M32(cw)); sb_direct = sd; return 1; }
    Chk *k = chk_start(RT_BD3D0, c, ch);
    /* its callees keep state of their own (VU1_addGSregister's register cache, ...) that a
     * second run would see changed: every run puts back and compares all of RAM */
    k->always_full = 1;
    u32 sp = (u32)SP;
    area(k, "stack", sp - 0x50, 0x50);
    area(k, "scratchpad", SPR, 0x4000);
    check_begin(k, c);
    run_bd3d0(c, &R, L);
    check_native_done(k, c);
    return 0;
}
void uya_stripb_bd3d0_exit(RcCtx *c) { check_exit(&chk[RT_BD3D0], c); }

/* ---- level 1's strip pair, func_L01_00263338 (a0: the program's start / 8, a1: the entry) ----
 * Writes two 46-vertex strips at the chain pointer (gp, its only gp reference, three times): the first
 * from the template at 0x1CB3E0 (5 qwords) with its ST pairs, colours and positions taken from the
 * scratchpad through the index tables, ending with MSCAL a0 in the positions' padding and a FLUSH
 * qword; the second (its CTXT copy: the GIF tags' CTXT bit set) the same template with its own ST
 * pairs, its CNT closed without a start, and a REF 0x31 back to the first strip's colours and
 * positions (their UNPACKs and the MSCAL). The game's routine (translated) runs; the two strips are
 * told to native_stripd as semantic strips (the second's colours and positions are the first's), and
 * the range becomes records and a marker as the builders' do. Any other shape: the chain as written. */
static u32 l1_s0, l1_cw, l1_c0;
static int l1_on;
int uya_stripl1_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)a; (void)na;
    l1_on = 0;
    if (ng != 3 || g[0] != g[1] || g[0] != g[2]) { static int told; if (!told++) uya_log("native strip (level 1's pair): the routine has %d gp references (not 3, the chain pointer): its strips stay in the chain", ng); return 0; }
    l1_cw = (u32)c->r[28].d[0] + (u32)g[0];
    l1_c0 = M32(l1_cw);
    l1_s0 = native_stripd_begin();
    l1_on = 1;
    return 0;
}
void uya_stripl1_exit(RcCtx *c)
{
    (void)c;
    if (!l1_on) return;
    l1_on = 0;
    const u32 c0 = l1_c0, end = M32(l1_cw), n = 46;
    const u32 b1 = c0 + 16 * 79, b2 = b1 + 16 * 30;          /* strip 1: 5 + 24 + 13 + 36 + 1 qwords; strip 2: 5 + 24 + the REF */
    const u32 st1 = c0 + 0x60, col1 = c0 + 16 * 30, xyz1 = c0 + 16 * 43, st2 = b1 + 0x60, mscal = xyz1 + 4 * 3 * n;
    if (end != b2 || M32(c0) != 0x1000004Eu || M32(b1) != 0x1000001Cu || M32(mscal) >> 24 != 0x14 || M32(b2 - 16) != 0x30000031u || M32(b2 - 12) != col1 - 16) {
        static int told; if (told++ < 3) uya_log("native strip (level 1's pair): the range at %08X..%08X is not the routine's shape: it stays in the chain", c0, end);
        native_stripd_end(l1_s0, l1_cw, c0, c0);
        return;
    }
    const u32 pc = (M32(mscal) & 0xFFFF) * 8;
    native_stripd_sem(c0, b1, n, (const u32 *)RC_A(c0 + 0x10), (const u32 *)RC_A(c0 + 0x24), st1, col1, xyz1, 0, pc);
    native_stripd_sem(b1, b2, n, (const u32 *)RC_A(b1 + 0x10), (const u32 *)RC_A(b1 + 0x24), st2, col1, xyz1, 0, pc);
    native_stripd_end(l1_s0, l1_cw, c0, end);
}
