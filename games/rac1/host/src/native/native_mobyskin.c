/* The moby skin driver (the game's skinning and lighting of animated mobys, hand-written assembly
 * in the game: one region of two catalogue entries, level 0 func_L00_001EEBD8 (3436 bytes) and
 * func_L00_001EF944 (1548 bytes), 0x1EEBD8..0x1EFF50, 1246 instructions, one copy in every level)
 * together with the VU0 microprogram it drives (image 104691, 328 instruction pairs) as native C:
 * VU0 micro mode is not used for it.
 *
 * Called by MobyAnimProc (0x256918) once per animation record, after the joint evaluation
 * (0x252018) has left the record's skinning matrices in the scratchpad (SPR 0x0000 + j*64, four
 * float rows, row 3 the translation), as skin(a0 = the record in main memory, a1 = [0x15F6DC], an
 * 8 KB-aligned table of 512 x 16-byte "shared vertices"); it returns the next record in v0 (the
 * only register the caller reads; it restores the low doublewords of s0..s7, gp, sp, fp and ra
 * itself). What the game's code does, in order:
 *   - setup (0x1EEBD8): ra to SPR 0x1FF0; the scratchpad locals: 0x1F00 the list cursor (0x1C80),
 *     0x1F04 a1, 0x1F08 the packet buffer (0x2000, then 0x2800 in turn), 0x1F0C the output buffer
 *     (0x3000 / 0x3400), 0x1F10 the output qword count, 0x1F14 the next record, 0x1F18 the
 *     record's +0x14 word (the extra pass). DMA channel 9 (to the scratchpad): record + 0x40,
 *     (count >> 1) + 9 qwords (count = its halfword 0, the packets) to SPR 0x1C00: the light block
 *     (0x1C00..0x1C7F) and the packet list (8 bytes per packet from 0x1C80, zero terminated); it
 *     waits for the list's last entry (an sd 0 there first). The first packet goes to 0x2000 by
 *     channel 9 (entry word 0 = source >> 4 << 8 | qwords). The light block goes to VU0: vf25..27
 *     the three light direction rows (lanes: light 0, light 1, the point light), vf28..30 their
 *     colours, vf31 the moby's colour (65536.0 + a byte per lane); 0 - |record + 0xA0| (vf1, the
 *     back-light factors), the three rows and a sentinel qword (v0 = -1 in the low doubleword, v0's
 *     upper doubleword as it was) are stored to VU0 memory 0xFB..0xFF (vsqi from vi1 = 0xFB);
 *     vi10 = 0xFE (the mask of the third matrix address).
 *   - per packet (0x1EED14): ch9 idle; the next packet prefetched into the other buffer; the
 *     header: +0 the matrix uploads, +4 / +8 / +0xC the 2-joint, 3-joint and 1-joint vertex counts
 *     (ctc2 to vi3, vi4, vi5), +0x10 the shared-vertex copies, +0x14 the output vertex count n,
 *     +0x18 the vertex data, +0x1C the per-vertex colour scale bytes. The output block: n colour
 *     words (rounded up to a qword) then n positions (3 x s16, 6 bytes each, rounded up), the two
 *     padding qwords zeroed. The matrix uploads ({SPR matrix, VU0 address} byte pairs from +0x20):
 *     4 qwords each into VU0 memory. Then the vertices, one qword each (h0: bits 0..8 the shared
 *     slot, 9..15 the SPR matrix uploaded with it; b0..b7 VU0 matrix addresses, weights and slots;
 *     b8, b9 the normal's two table indices (SPR 0x3800, 8 bytes each); h5..h7 the position), fed
 *     to VU0 as vf1 = the halfwords 4..7 as words, vf2 = the two table entries, vf3 / vf4 = the
 *     bytes 0..3 / 4..7 as words, vf5..vf8 = the SPR matrix: `vcallms 0` with vertex 0's, then per
 *     vertex its inputs and a release (ctc2 0 to vi1 and vi2 in turn), the results of the vertex
 *     fed two releases earlier read back (qmfc2 of vf11 / vf12 after a vi1 release, vf9 / vf10
 *     after vi2; the first two reads only checked). Four vertices per loop pass, the last
 *     (m - 3) & 3 (m = the three counts' sum) and the pipeline's drain after it. A colour (the
 *     low halfword of each float lane, 8.7 fixed point) times the vertex's scale byte (pmulth),
 *     saturated (pmfhl.sh), >> 7, packed to bytes; a position (ftoi0, low halfwords) stored 6
 *     bytes apart with sdr / sdl (each 8-byte store's junk fourth halfword overwritten by the next
 *     vertex, the last one's in the zeroed padding when it fits); every skinned vertex also to the
 *     shared table: its position (8 bytes) at a1 + slot * 16 and its colour word at +8. The
 *     copies: halfwords after the upload pairs, each naming a table entry whose position and
 *     colour are appended. A sentinel check on every result read (lane y < 0: the VU0 has not
 *     written it yet) jumps to the spin stubs 0x1EF944.. (func_L00_001EF944), which read again.
 *   - the packet out (0x1EF864): ch9 idle if it was busy, ch8 idle, DMA channel 8 (from the
 *     scratchpad) [0x1F10] qwords from [0x1F0C] to the packet's hole in the moby chain (the list
 *     entry's word 1; MobyProc's REF tags in front of it carry the UNPACK codes); [0x1F0C] ^= 0x400.
 *   - the end (0x1EF904): ch8 idle; v0 = [0x1F14]; ra back; if [0x1F18] != 0 the extra pass.
 *   - the extra pass (0x1EFBE4, a moby with a fade: its second, sphere-mapped draw): ch9 the block
 *     after the record (its +0x14 word's upper half qwords: the turned matrix and its own list) to
 *     0x1C00, waited for by its last dword; vf28..30 * 0.25, vf31 = (65536.875 x 3, 65537); vf22..24
 *     the turned matrix. Per list entry: its packet (ch9, 0x2000 / 0x2800 in turn, the next one
 *     prefetched, each waited for by its last qword's first dword), the header (vertex count, the
 *     offsets of the position, colour and ST sections from 0x3000, the ST offset also the ch8
 *     qword count), per vertex (16 bytes: h0..h2 position, bytes 6/7 the normal's table indices,
 *     b8..b10 SPR joints, b11 the joint count + 1, b12..b14 weights): vf7 position, vf8 normal,
 *     vf9 weights, vf10..vf21 the joints' matrices, `qmfc2.i` of the previous vertex's results and
 *     `vcallmsr` of the kernel for its joint count (3: 0x888, 2: 0x908, 1: 0x968; one more call
 *     than vertices, the first vertex's results skipped): position (3 x sh), colour (ppach /
 *     ppacb, byte 3 the fade), ST (psraw 1, ppach) out; ch8 0x3000 to the entry's hole. Returns
 *     v0 = the block's end.
 *
 * The microprogram, as the driver runs it. The skin loop (0x000..0x880) is a software pipeline in
 * two halves, A (0x038.., released by vi1, results in vf11 / vf12, sentinel from VU0 memory 0xFF
 * into vf11) and B (0x110.., vi2, vf9 / vf10): each half takes one vertex in and finishes
 * earlier ones, waiting (an `ibeq viN, vi0` loop) for the EE's release before the next. Per
 * vertex, in its three stages:
 *   matrix: 1 joint R = mem[b2], after mem[b3..b3+3] := vf5..vf8 (the upload); 2 joints (b6 the
 *     upload slot) R = w0 mem[b2] + w1 mem[b3] (w = itof12(b4, b5) * 16.0 = b / 256), stored to
 *     mem[b7]; 3 joints R = w0 mem[b2] + w1 mem[b3] + w2 mem[b1 & 0xFE], weights b4..b6, stored
 *     to mem[b7] (no upload). VU0 memory 0x00..0xFA is a matrix cache whose schedule is in the
 *     vertex data;
 *   position = R0 x + R1 y + R2 z + R3 (ACC chain, itof0 inputs) -> ftoi0; normal n = R0 n0.x +
 *     R1 n0.y + R2 n0.z with n0 = (A0 B0, A1 B0, B1), A / B the two table entries (not
 *     normalised); Q = RSQRT(n.n) (mul, then add.x y and add.x z);
 *   light: d = L0 n.x + L1 n.y + L2 n.z (vf25..27), i = max(d, d * -|k|) (k = mem 0xFB), colour =
 *     (C0 i.x + C1 i.y + C2 i.z) * Q + base (vf28..30, vf31).
 * The halves of the 2-joint (0x1E8), 3-joint (0x438) and 1-joint (0x038) loops share the stage
 * code, so the pipeline carries from one loop to the next (2 -> 3 -> 1 -> the drain 0x6C8, two
 * halves with no input, vi11 2 -> 0, the E bit at 0x878): per packet 1 + n + 2 runs. The extra
 * kernel (0x888 / 0x908 / 0x968, the E bit at 0xA30): the joints blended with itof12(vf9), the
 * position and normal through them, vf2 = max(L n, 0) through vf25..27 then vf31 w + vf28 x +
 * vf29 y + vf30 z, vf3 = the turned matrix by n, xy + 1, ftoi12 (in the E bit's delay slot).
 *
 * This file does the same, word for word: the driver as the translated game code has it (its
 * GPRs in a local register file, all 128 bits, LO / HI, every load, store and DMA register write
 * in the routine's order: the emulated DMA moves the data at the CHCR write, as for the game's
 * routine), and in place of each `vcallms` / release the microprogram's halves as straight C
 * (vu_skin, vu_extra below), run synchronously exactly as host/src/gfx/vu.c runs the translated
 * microprogram (each release of a parked program runs it to its next wait loop): every VU0
 * operation with the translated microprogram's semantics (each product and sum through vu_fc,
 * results into temporaries before they are written, the pair rules of tools/vu_recomp.py: an
 * upper op reads its sources before the lower op of the same pair writes, a lower op before the
 * upper one writes; Q from DIV / RSQRT through the same pipeline model with the same cycle
 * counts, so even q_rdy matches; I from loi; the stale registers the first halves read - ACC,
 * vf13, vf17, vf18, vf23, vf24 - are the VU0's own, as the program finds them). The VU0 data
 * memory is used as the program uses it (the matrix cache, 0xFB..0xFF). No flags (the program
 * has no flag op). The square root and fmaxf are the C library's (the translated microprogram
 * is built with -fno-builtin: its vu_rsqrt and max are calls of sqrtf / fmaxf, an x87 fsqrt
 * rounding to nearest on 32-bit Linux, not the sqrtss this file would inline under MXCSR's
 * round-toward-zero). Under these semantics the sentinel is never seen: the spin stubs only
 * run if a result's lane y is negative, where the game's code would wait for ever; here that is
 * counted and logged and the driver goes on (the stubs' reads would give the same values).
 * What is not reproduced: vu.c's statistics (runs per image and start address) and its trace
 * hooks; the micro memory is checked for image 104691 at each call (else the game's routine runs).
 *
 * UYA_NMOBYSKIN: 0 the game's routine (translated EE code + VU0 micro mode); 1 check (this file
 * runs first, its results are put aside, the state is put back, the game's routine runs from the
 * same state and the two are compared at its end: the chain holes it wrote (addresses and sizes
 * as this file wrote them), the shared-vertex table, the scratchpad, VU0 data memory, the whole
 * VU0 register file and micro-mode state, the DMA ch8 / ch9 registers, every GPR (128 bits), LO
 * and HI); 2 this file (the default; UYA_NATIVE=0 makes it 0). */
#include "rc.h"
#include "vu.h"
#include "uya_host.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>

void uya_log(const char *fmt, ...);

#define SPR 0x70000000u
#define AI static inline __attribute__((always_inline))

/* the C library's sqrtf and fmaxf, as the translated microprogram calls them (see above) */
float sqrtf(float);
float fmaxf(float, float);
static float (*volatile lib_sqrtf)(float) = sqrtf;
static float (*volatile lib_fmaxf)(float, float) = fmaxf;
/* RSQRT as vu.h's vu_rsqrt, with the library's square root */
static f32 rsqrt_(f32 a, f32 b)
{
    f32 s = lib_sqrtf(__builtin_fabsf(b));
    if (s == 0.0f) return (vu_u(a) & 0x80000000u) ? -3.4028235e38f : 3.4028235e38f;
    return a / s;
}

/* ---- the MMI operations the driver uses, as tools/recomp.py translates them (s = rs, t = rt) ---- */
static const R128 ZQ = {{ 0, 0 }};
AI R128 pextlb(R128 s, R128 t) { R128 x; for (int i = 0; i < 8; i++) { x.b[2 * i] = t.b[i]; x.b[2 * i + 1] = s.b[i]; } return x; }
AI R128 pextub(R128 s, R128 t) { R128 x; for (int i = 0; i < 8; i++) { x.b[2 * i] = t.b[8 + i]; x.b[2 * i + 1] = s.b[8 + i]; } return x; }
AI R128 pextlh(R128 s, R128 t) { R128 x; for (int i = 0; i < 4; i++) { x.h[2 * i] = t.h[i]; x.h[2 * i + 1] = s.h[i]; } return x; }
AI R128 pextuh(R128 s, R128 t) { R128 x; for (int i = 0; i < 4; i++) { x.h[2 * i] = t.h[4 + i]; x.h[2 * i + 1] = s.h[4 + i]; } return x; }
AI R128 pextlw(R128 s, R128 t) { R128 x; x.w[0] = t.w[0]; x.w[1] = s.w[0]; x.w[2] = t.w[1]; x.w[3] = s.w[1]; return x; }
AI R128 ppach(R128 s, R128 t) { R128 x; for (int i = 0; i < 4; i++) { x.h[i] = t.h[2 * i]; x.h[4 + i] = s.h[2 * i]; } return x; }
AI R128 ppacb(R128 s, R128 t) { R128 x; for (int i = 0; i < 8; i++) { x.b[i] = t.b[2 * i]; x.b[8 + i] = s.b[2 * i]; } return x; }
AI R128 pcpyld(R128 s, R128 t) { R128 x; x.d[0] = t.d[0]; x.d[1] = s.d[0]; return x; }
AI R128 pcpyud(R128 s, R128 t) { R128 x; x.d[0] = s.d[1]; x.d[1] = t.d[1]; return x; }
AI R128 pcpyh(R128 t) { R128 x; for (int i = 0; i < 4; i++) { x.h[i] = t.h[0]; x.h[4 + i] = t.h[4]; } return x; }
AI R128 psllw(R128 t, int n) { R128 x; for (int i = 0; i < 4; i++) x.w[i] = t.w[i] << n; return x; }
AI R128 psrlw(R128 t, int n) { R128 x; for (int i = 0; i < 4; i++) x.w[i] = t.w[i] >> n; return x; }
AI R128 psraw(R128 t, int n) { R128 x; for (int i = 0; i < 4; i++) x.sw[i] = t.sw[i] >> n; return x; }
AI R128 psrah(R128 t, int n) { R128 x; for (int i = 0; i < 8; i++) x.sh[i] = t.sh[i] >> n; return x; }
/* pmulth: the even products to rd, all eight to LO / HI */
AI R128 pmulth(R128 s, R128 t, R128 *lo, R128 *hi)
{
    s32 p[8]; R128 x;
    for (int i = 0; i < 8; i++) p[i] = (s32)s.sh[i] * t.sh[i];
    x.sw[0] = p[0]; x.sw[1] = p[2]; x.sw[2] = p[4]; x.sw[3] = p[6];
    lo->sw[0] = p[0]; lo->sw[1] = p[1]; lo->sw[2] = p[4]; lo->sw[3] = p[5];
    hi->sw[0] = p[2]; hi->sw[1] = p[3]; hi->sw[2] = p[6]; hi->sw[3] = p[7];
    return x;
}
/* pmfhl.sh: LO / HI's words saturated to halfwords */
AI R128 pmfhl_sh(R128 lo, R128 hi)
{
    R128 x;
    x.sh[0] = rc_clamp16(lo.sw[0]); x.sh[1] = rc_clamp16(lo.sw[1]); x.sh[2] = rc_clamp16(hi.sw[0]); x.sh[3] = rc_clamp16(hi.sw[1]);
    x.sh[4] = rc_clamp16(lo.sw[2]); x.sh[5] = rc_clamp16(lo.sw[3]); x.sh[6] = rc_clamp16(hi.sw[2]); x.sh[7] = rc_clamp16(hi.sw[3]);
    return x;
}

/* ---- VU0 operations, as tools/vu_recomp.py translates them ---- */
#define VW(n, k) (((u32 *)v->vf[n])[k])
/* d = fs * b (every lane; d a VF register or the ACC) */
AI void v_mul(RcVu0 *v, f32 *d, int fs, f32 b)
{
    const f32 *a = v->vf[fs];
    f32 u0 = a[0] * b, u1 = a[1] * b, u2 = a[2] * b, u3 = a[3] * b;
    d[0] = vu_fc(u0); d[1] = vu_fc(u1); d[2] = vu_fc(u2); d[3] = vu_fc(u3);
}
/* d = ACC + fs * b */
AI void v_madd(RcVu0 *v, f32 *d, int fs, f32 b)
{
    const f32 *a = v->vf[fs];
    f32 u0 = v->acc[0] + vu_fc(a[0] * b), u1 = v->acc[1] + vu_fc(a[1] * b), u2 = v->acc[2] + vu_fc(a[2] * b), u3 = v->acc[3] + vu_fc(a[3] * b);
    d[0] = vu_fc(u0); d[1] = vu_fc(u1); d[2] = vu_fc(u2); d[3] = vu_fc(u3);
}
/* the .xyz forms (the extra kernel's light rows) */
AI void v_mul3(RcVu0 *v, f32 *d, int fs, f32 b)
{
    const f32 *a = v->vf[fs];
    f32 u0 = a[0] * b, u1 = a[1] * b, u2 = a[2] * b;
    d[0] = vu_fc(u0); d[1] = vu_fc(u1); d[2] = vu_fc(u2);
}
AI void v_madd3(RcVu0 *v, f32 *d, int fs, f32 b)
{
    const f32 *a = v->vf[fs];
    f32 u0 = v->acc[0] + vu_fc(a[0] * b), u1 = v->acc[1] + vu_fc(a[1] * b), u2 = v->acc[2] + vu_fc(a[2] * b);
    d[0] = vu_fc(u0); d[1] = vu_fc(u1); d[2] = vu_fc(u2);
}
/* vf[d] = vf[fs] * vf[ft], lane by lane */
AI void v_mulv(RcVu0 *v, int d, int fs, int ft)
{
    const f32 *a = v->vf[fs], *b = v->vf[ft];
    f32 u0 = a[0] * b[0], u1 = a[1] * b[1], u2 = a[2] * b[2], u3 = a[3] * b[3];
    v->vf[d][0] = vu_fc(u0); v->vf[d][1] = vu_fc(u1); v->vf[d][2] = vu_fc(u2); v->vf[d][3] = vu_fc(u3);
}
/* vf[d] = max(vf[fs], vf[ft]) lane by lane (n lanes from x) */
AI void v_max(RcVu0 *v, int d, int fs, int ft, int n)
{
    f32 u[4];
    for (int i = 0; i < n; i++) u[i] = lib_fmaxf(v->vf[fs][i], v->vf[ft][i]);
    for (int i = 0; i < n; i++) v->vf[d][i] = vu_fc(u[i]);
}
/* vf[d] = max(vf[fs], vf[ft].c) */
AI void v_max_bc(RcVu0 *v, int d, int fs, int ft, int c)
{
    f32 u[4];
    for (int i = 0; i < 4; i++) u[i] = lib_fmaxf(v->vf[fs][i], v->vf[ft][c]);
    for (int i = 0; i < 4; i++) v->vf[d][i] = vu_fc(u[i]);
}
/* add.x vf16, vf16, vf16.c (the normal's length, summed in x) */
AI void v_addx16(RcVu0 *v, int c) { f32 u0 = v->vf[16][0] + v->vf[16][c]; v->vf[16][0] = vu_fc(u0); }
/* mul.xy d, s, s.z */
AI void v_mulxy_z(RcVu0 *v, int d, int s) { f32 u0 = v->vf[s][0] * v->vf[s][2], u1 = v->vf[s][1] * v->vf[s][2]; v->vf[d][0] = vu_fc(u0); v->vf[d][1] = vu_fc(u1); }
/* itofN / ftoiN, every lane */
AI void v_itof(RcVu0 *v, int d, int s, f32 inv)
{
    u32 n0 = vu_itof(VW(s, 0), inv), n1 = vu_itof(VW(s, 1), inv), n2 = vu_itof(VW(s, 2), inv), n3 = vu_itof(VW(s, 3), inv);
    VW(d, 0) = n0; VW(d, 1) = n1; VW(d, 2) = n2; VW(d, 3) = n3;
}
AI void v_ftoi(RcVu0 *v, int d, int s, f32 scale, int n)
{
    s32 t[4];
    for (int i = 0; i < n; i++) t[i] = vu_ftoi(v->vf[s][i], scale);
    for (int i = 0; i < n; i++) VW(d, i) = (u32)t[i];
}
AI void v_move(RcVu0 *v, int d, int s) { memcpy(v->vf[d], v->vf[s], 16); }
AI void v_lq(RcVu0 *v, int d, u32 qa) { memcpy(v->vf[d], VU0_MEM(qa), 16); }
AI void v_sq(RcVu0 *v, int s, u32 qa) { memcpy(VU0_MEM(qa), v->vf[s], 16); }
#define Q_READ() vu_q(v, cyc)
#define RSQRT() do { cyc = vu_qwait(v, cyc); vu_qset(v, cyc, 13, rsqrt_(v->vf[0][3], v->vf[16][0])); } while (0)

/* ---- image 104691, the skin loop (0x000..0x880): from pc (0 or a wait loop it parked in) to its
 * next wait loop or its end. h is the half: 0 = A (vf11 / vf12 out, waits on vi1, sets vi2 for the
 * other half), 1 = B (vf9 / vf10, vi2, vi1). The cycle count (cyc) is the translation's: one per
 * pair, a stall at a Q write while one is in flight; it times Q. ---- */
#define OC (h ? 9 : 11)      /* the half's colour result (and sentinel) */
#define OP (h ? 10 : 12)     /* its position result */
#define VIO (h ? 1 : 2)      /* the handshake register it sets */
#define VIW (h ? 2 : 1)      /* the one it waits on (the EE clears it) */
static const u32 park_pc[4][2] = { { 0x780, 0x858 }, { 0x0F0, 0x1C8 }, { 0x2F0, 0x418 }, { 0x560, 0x6A8 } };   /* by loop: drain, 1, 2, 3 joints */
static unsigned long long st_halves, st_badpc;

static void vu_skin(RcVu0 *v, u32 pc)
{
    u32 cyc = 0;
    int h = 0, k = 0, bt;
    v->st_int = v->status; v->cl_int = v->clip;
    switch (pc) {
    case 0x000: goto entry;
    case 0x0F0: k = 1; h = 0; goto resume;
    case 0x1C8: k = 1; h = 1; goto resume;
    case 0x2F0: k = 2; h = 0; goto resume;
    case 0x418: k = 2; h = 1; goto resume;
    case 0x560: k = 3; h = 0; goto resume;
    case 0x6A8: k = 3; h = 1; goto resume;
    case 0x780: k = 0; h = 0; goto resume;
    case 0x858: k = 0; h = 1; goto resume;
    default:
        st_badpc++;
        { static int told; if (told++ < 4) uya_log("native mobyskin: VU0 resumed at 0x%04X (not a wait loop of image 104691)", pc); }
        goto done;
    }

resume:     /* the wait loop: ibeq viN, vi0 (+ its delay slot); b back (+ delay) and park again */
    cyc += 2;
    if (v->vi[VIW] == 0) {
        h ^= 1;
        switch (k) { case 1: goto j1; case 2: goto j2; case 3: goto j3; default: goto dcheck; }
    }
    cyc += 2; v->tpc = pc; v->susp = 1; goto done;

park:       /* a half ended and its handshake register is not clear: into the wait loop */
    cyc += 4; v->tpc = park_pc[k][h]; v->susp = 1; goto done;

entry:      /* 0x000: vf15, vf16, vf19, vf20 = 1, Q = 0 / 1, vi1 = 1, vi11 = 2 */
    cyc += 1; v_max_bc(v, 15, 0, 0, 3); v->vi[3] = (v->vi[3] - 1) & 0xFFFF;
    cyc += 1; v_max_bc(v, 16, 0, 0, 3); v->vi[1] = 1;
    cyc += 1; cyc = vu_qwait(v, cyc); vu_qset(v, cyc, 7, vu_div(v->vf[0][0], v->vf[0][3])); v_max_bc(v, 19, 0, 0, 3);
    cyc += 1; v_max_bc(v, 20, 0, 0, 3); bt = (s16)v->vi[3] >= 0;
    cyc += 1; v->vi[11] = 2;
    if (bt) goto j2;                                     /* 2-joint vertices first */
    cyc += 1; bt = v->vi[4] != 0;
    cyc += 1;
    if (bt) goto j3;
    goto j1;

j1:         /* the 1-joint half, 0x038 (A) / 0x110 (B) */
    st_halves++;
    cyc += 1; v_madd(v, v->acc, 23, v->vf[13][3]); bt = v->vi[5] == 0;      /* vertex k-1's position, last term */
    cyc += 1; v_madd(v, v->vf[17], 24, v->vf[0][3]); v->vi[8] = VW(3, 3) & 0xFFFF;
    if (bt) goto drain;                                  /* no 1-joint vertex left: the drain (0x6D8 / 0x7B0) */
    cyc += 1; v_itof(v, 13, 1, 1.000000000e+00f); v->vi[6] = VW(3, 2) & 0xFFFF;   /* vertex k: position */
    cyc += 1; v_addx16(v, 1); v_lq(v, OC, 255);          /* vertex k-1: |n|^2; the sentinel */
    cyc += 1; v_max(v, 20, 19, 20, 4); v_move(v, 14, 2); /* vertex k-2: i = max(d, d -|k|); vertex k: normal tables */
    cyc += 1; v_mul(v, v->acc, 25, v->vf[15][0]); v_sq(v, 5, v->vi[8] + 0);       /* vertex k-1: d = L n; vertex k: the upload */
    cyc += 1; v_madd(v, v->acc, 26, v->vf[15][1]); v_sq(v, 6, v->vi[8] + 1);
    cyc += 1; v_madd(v, v->vf[19], 27, v->vf[15][2]); v_sq(v, 7, v->vi[8] + 2);
    cyc += 1; v_addx16(v, 2); v_sq(v, 8, v->vi[8] + 3);
    cyc += 1; v_mulxy_z(v, 14, 14); v_lq(v, 21, v->vi[6] + 0);                       /* vertex k: n0; its matrix */
    cyc += 1; v_mul(v, v->acc, 28, v->vf[20][0]); v_lq(v, 22, v->vi[6] + 1);        /* vertex k-2: C i */
    cyc += 1; v_madd(v, v->acc, 29, v->vf[20][1]); v_lq(v, 23, v->vi[6] + 2);
    cyc += 1; v_madd(v, v->vf[20], 30, v->vf[20][2]); v_lq(v, 24, v->vi[6] + 3);
    cyc += 1; v_mul(v, v->acc, 21, v->vf[14][0]); v_lq(v, 27, 251);                 /* vertex k: n = R n0 */
    cyc += 1; v_madd(v, v->acc, 22, v->vf[14][1]); v->vi[VIO] = 1;
    cyc += 1; v_madd(v, v->vf[15], 23, v->vf[14][3]); v->vi[5] = (v->vi[5] - 1) & 0xFFFF;
    cyc += 1; v_mul(v, v->acc, 20, Q_READ());            /* vertex k-2: * Q */
    cyc += 1; v_mulv(v, 20, 19, 27); v_lq(v, 27, 254);   /* vertex k-1: d * k (vf27 read before the load) */
    cyc += 1; v_madd(v, v->vf[OC], 31, v->vf[0][3]);     /* vertex k-2: + base: its colour out */
    cyc += 1; v_ftoi(v, OP, 18, 1.0f, 4);                /* vertex k-2: its position out */
    cyc += 1; RSQRT(); v_mulv(v, 16, 15, 15);            /* vertex k-1: Q (RSQRT reads vf16.x before the mul writes) */
t1:         /* 0x0E0 / 0x1B8 */
    cyc += 1; v_mul(v, v->acc, 21, v->vf[13][1]); bt = v->vi[VIW] == 0;            /* vertex k: R0 x + R1 y .. */
    cyc += 1; v_madd(v, v->acc, 22, v->vf[13][2]); v_move(v, 18, 17);
    if (bt) { h ^= 1; goto j1; }
    k = 1; goto park;

j2:         /* the 2-joint half, 0x1E8 (A) / 0x310 (B) */
    st_halves++;
    cyc += 1; v_madd(v, v->acc, 23, v->vf[13][3]); v->vi[8] = VW(4, 2) & 0xFFFF;
    cyc += 1; v_madd(v, v->vf[17], 24, v->vf[0][3]); v->vi[6] = VW(3, 2) & 0xFFFF;
    cyc += 1; v_itof(v, 13, 1, 1.000000000e+00f); v->vi[7] = VW(3, 3) & 0xFFFF;
    cyc += 1; v_itof(v, 24, 4, 2.441406250e-04f); v->vi[9] = VW(4, 3) & 0xFFFF;   /* the weights (b4, b5); the blend slot b7 */
    cyc += 1; v_addx16(v, 1); v_move(v, 14, 2);
    cyc += 1; v_max(v, 20, 19, 20, 4); v_sq(v, 5, v->vi[8] + 0);                     /* the upload to b6 */
    cyc += 1; v_mul(v, v->acc, 25, v->vf[15][0]); v_sq(v, 6, v->vi[8] + 1);
    cyc += 1; v_madd(v, v->acc, 26, v->vf[15][1]); v_sq(v, 7, v->vi[8] + 2);
    cyc += 1; v_madd(v, v->vf[19], 27, v->vf[15][2]); v_sq(v, 8, v->vi[8] + 3);
    cyc += 1; v_addx16(v, 2); v_lq(v, OC, 255);
    cyc += 1; v_mulxy_z(v, 14, 14); v->i = vu_f(0x41800000u);                         /* loi 16.0 */
    cyc += 1; v_mul(v, v->vf[24], 24, v->i); v_lq(v, 25, v->vi[6] + 0);
    cyc += 1; v_mul(v, v->acc, 28, v->vf[20][0]); v_lq(v, 21, v->vi[7] + 0);
    cyc += 1; v_madd(v, v->acc, 29, v->vf[20][1]); v_lq(v, 26, v->vi[6] + 1);
    cyc += 1; v_madd(v, v->vf[20], 30, v->vf[20][2]); v_lq(v, 22, v->vi[7] + 1);
    cyc += 1; v_mul(v, v->acc, 25, v->vf[24][0]); v_lq(v, 27, v->vi[6] + 2);         /* R = w0 mem[b2] + w1 mem[b3] */
    cyc += 1; v_madd(v, v->vf[21], 21, v->vf[24][1]); v_lq(v, 23, v->vi[7] + 2);
    cyc += 1; v_mul(v, v->acc, 26, v->vf[24][0]); v_lq(v, 25, v->vi[6] + 3);
    cyc += 1; v_madd(v, v->vf[22], 22, v->vf[24][1]); v_lq(v, 15, v->vi[7] + 3);
    cyc += 1; v_mul(v, v->acc, 27, v->vf[24][0]); v->vi[VIO] = 1;
    cyc += 1; v_madd(v, v->vf[23], 23, v->vf[24][1]); v_lq(v, 27, 251);
    cyc += 1; v_mul(v, v->acc, 25, v->vf[24][0]); v_lq(v, 26, 253);
    cyc += 1; v_madd(v, v->vf[24], 15, v->vf[24][1]); v_lq(v, 25, 252);
    cyc += 1; v_mul(v, v->acc, 21, v->vf[14][0]); v_sq(v, 21, v->vi[9] + 0);         /* the blend to b7 */
    cyc += 1; v_madd(v, v->acc, 22, v->vf[14][1]); v_sq(v, 22, v->vi[9] + 1);
    cyc += 1; v_madd(v, v->vf[15], 23, v->vf[14][3]); v_sq(v, 23, v->vi[9] + 2);
    cyc += 1; v_mul(v, v->acc, 20, Q_READ()); v_sq(v, 24, v->vi[9] + 3);
    cyc += 1; v_mulv(v, 20, 19, 27); bt = v->vi[3] == 0;
    cyc += 1; v_madd(v, v->vf[OC], 31, v->vf[0][3]); v_lq(v, 27, 254);
    if (bt) goto t3;                                     /* the last 2-joint vertex: on with the 3-joint tail (0x540 / 0x688) */
    cyc += 1; v_ftoi(v, OP, 18, 1.0f, 4); v->vi[3] = (v->vi[3] - 1) & 0xFFFF;
    cyc += 1; RSQRT(); v_mulv(v, 16, 15, 15);
    cyc += 1; v_mul(v, v->acc, 21, v->vf[13][1]); bt = v->vi[VIW] == 0;
    cyc += 1; v_madd(v, v->acc, 22, v->vf[13][2]); v_move(v, 18, 17);
    if (bt) { h ^= 1; goto j2; }
    k = 2; goto park;

j3:         /* the 3-joint half, 0x438 (A) / 0x580 (B) */
    st_halves++;
    cyc += 1; v_madd(v, v->acc, 23, v->vf[13][3]); v->vi[6] = VW(3, 2) & 0xFFFF;
    cyc += 1; v_madd(v, v->vf[17], 24, v->vf[0][3]); v->vi[7] = VW(3, 3) & 0xFFFF;
    cyc += 1; v_itof(v, 13, 1, 1.000000000e+00f); v->vi[8] = VW(3, 1) & 0xFFFF;
    cyc += 1; v_itof(v, 24, 4, 2.441406250e-04f); v->vi[9] = VW(4, 3) & 0xFFFF;
    cyc += 1; v_addx16(v, 1); v_move(v, 14, 2);
    cyc += 1; v_max(v, 20, 19, 20, 4);
    cyc += 1; v_mul(v, v->acc, 25, v->vf[15][0]); v->vi[8] = (v->vi[8] & v->vi[10]) & 0xFFFF;   /* b1 & 0xFE */
    cyc += 1; v_madd(v, v->acc, 26, v->vf[15][1]); v->vi[4] = (v->vi[4] - 1) & 0xFFFF;
    cyc += 1; v_madd(v, v->vf[19], 27, v->vf[15][2]); v_lq(v, OC, 255);
    cyc += 1; v_addx16(v, 2); v->i = vu_f(0x41800000u);
    cyc += 1; v_mul(v, v->vf[24], 24, v->i); v->vi[VIO] = 1;
    cyc += 1; v_mulxy_z(v, 14, 14); v_lq(v, 15, v->vi[6] + 0);
    cyc += 1; v_mul(v, v->acc, 28, v->vf[20][0]); v_lq(v, 25, v->vi[7] + 0);
    cyc += 1; v_madd(v, v->acc, 29, v->vf[20][1]); v_lq(v, 21, v->vi[8] + 0);
    cyc += 1; v_madd(v, v->vf[20], 30, v->vf[20][2]); v_lq(v, 26, v->vi[6] + 1);
    cyc += 1; v_mul(v, v->acc, 15, v->vf[24][0]); v_lq(v, 27, v->vi[7] + 1);         /* R = w0 mem[b2] + w1 mem[b3] + w2 mem[b1 & 0xFE] */
    cyc += 1; v_madd(v, v->acc, 25, v->vf[24][1]); v_lq(v, 22, v->vi[8] + 1);
    cyc += 1; v_madd(v, v->vf[21], 21, v->vf[24][2]); v_lq(v, 15, v->vi[6] + 2);
    cyc += 1; v_mul(v, v->acc, 26, v->vf[24][0]); v_lq(v, 25, v->vi[7] + 2);
    cyc += 1; v_madd(v, v->acc, 27, v->vf[24][1]); v_lq(v, 23, v->vi[8] + 2);
    cyc += 1; v_madd(v, v->vf[22], 22, v->vf[24][2]); v_lq(v, 26, v->vi[6] + 3);
    cyc += 1; v_mul(v, v->acc, 15, v->vf[24][0]); v_lq(v, 27, v->vi[7] + 3);
    cyc += 1; v_madd(v, v->acc, 25, v->vf[24][1]); v_lq(v, 15, v->vi[8] + 3);
    cyc += 1; v_madd(v, v->vf[23], 23, v->vf[24][2]);
    cyc += 1; v_mul(v, v->acc, 26, v->vf[24][0]); v_lq(v, 25, 252);
    cyc += 1; v_madd(v, v->acc, 27, v->vf[24][1]); v_lq(v, 26, 253);
    cyc += 1; v_madd(v, v->vf[24], 15, v->vf[24][2]); v_lq(v, 27, 251);
    cyc += 1; v_mul(v, v->acc, 21, v->vf[14][0]); v_sq(v, 21, v->vi[9] + 0);
    cyc += 1; v_madd(v, v->acc, 22, v->vf[14][1]); v_sq(v, 22, v->vi[9] + 1);
    cyc += 1; v_madd(v, v->vf[15], 23, v->vf[14][3]); v_sq(v, 23, v->vi[9] + 2);
    cyc += 1; v_mul(v, v->acc, 20, Q_READ()); v_sq(v, 24, v->vi[9] + 3);
    cyc += 1; v_mulv(v, 20, 19, 27);
    cyc += 1; v_madd(v, v->vf[OC], 31, v->vf[0][3]); v_lq(v, 27, 254);
t3:         /* 0x540 / 0x688 */
    cyc += 1; v_ftoi(v, OP, 18, 1.0f, 4); bt = v->vi[4] == 0;
    cyc += 1; RSQRT(); v_mulv(v, 16, 15, 15);
    if (bt) goto t1;                                     /* no 3-joint vertex left: on with the 1-joint tail (0x0E0 / 0x1B8) */
    cyc += 1; v_mul(v, v->acc, 21, v->vf[13][1]); bt = v->vi[VIW] == 0;
    cyc += 1; v_madd(v, v->acc, 22, v->vf[13][2]); v_move(v, 18, 17);
    if (bt) { h ^= 1; goto j3; }
    k = 3; goto park;

dcheck:     /* the drain, 0x6C8 (A) / 0x7A0 (B): its two halves done, the end */
    cyc += 1; bt = v->vi[11] == 0;
    cyc += 1;
    if (bt) goto end;
drain:      /* 0x6D8 / 0x7B0: the last two vertices' stages, no input */
    st_halves++;
    cyc += 1;
    cyc += 1; v_addx16(v, 1); v_lq(v, OC, 255);
    cyc += 1; v_max(v, 20, 19, 20, 4);
    cyc += 1; v_mul(v, v->acc, 25, v->vf[15][0]);
    cyc += 1; v_madd(v, v->acc, 26, v->vf[15][1]);
    cyc += 1; v_madd(v, v->vf[19], 27, v->vf[15][2]);
    cyc += 1; v_addx16(v, 2);
    cyc += 1;
    cyc += 1; v_mul(v, v->acc, 28, v->vf[20][0]);
    cyc += 1; v_madd(v, v->acc, 29, v->vf[20][1]);
    cyc += 1; v_madd(v, v->vf[20], 30, v->vf[20][2]);
    cyc += 1; v_lq(v, 27, 251);
    cyc += 1; v->vi[VIO] = 1;
    cyc += 1; v->vi[11] = (v->vi[11] - 1) & 0xFFFF;
    cyc += 1; v_mul(v, v->acc, 20, Q_READ());
    cyc += 1; v_mulv(v, 20, 19, 27); v_lq(v, 27, 254);
    cyc += 1; v_madd(v, v->vf[OC], 31, v->vf[0][3]);
    cyc += 1; v_ftoi(v, OP, 18, 1.0f, 4);
    cyc += 1; RSQRT();
    cyc += 1; bt = v->vi[VIW] == 0;
    cyc += 1; v_move(v, 18, 17);
    if (bt) { h ^= 1; goto dcheck; }
    k = 0; goto park;

end:        /* 0x878: the E bit (+ its delay slot) */
    cyc += 2; v->tpc = 0x888;
done:
    vu_settle(v);
}

/* ---- image 104691, the extra pass's kernel: 0x888 (3 joints), 0x908 (2), 0x968 (1) to the E bit
 * at 0xA30 (+ the delay slot 0xA38). No Q, so no cycle count. ---- */
static void vu_extra(RcVu0 *v, u32 pc)
{
    v->st_int = v->status; v->cl_int = v->clip;
    if (pc == 0x968) {                                   /* 1 joint: its matrix as it is */
        v_mulxy_z(v, 4, 8); v_move(v, 18, 10);           /* n0.xy = A * B0 */
        { f32 u2 = v->vf[0][2] + v->vf[8][3]; v->vf[5][2] = vu_fc(u2); } v_move(v, 19, 11);   /* n0.z = B1 */
        v_itof(v, 1, 7, 1.000000000e+00f); v_move(v, 20, 12);
        v_move(v, 21, 13);
    } else if (pc == 0x908 || pc == 0x888) {
        v_itof(v, 9, 9, 2.441406250e-04f);               /* the weights */
        v_mulxy_z(v, 4, 8);
        { f32 u2 = v->vf[0][2] + v->vf[8][3]; v->vf[5][2] = vu_fc(u2); }
        v_itof(v, 1, 7, 1.000000000e+00f);
        if (pc == 0x888) {                               /* 3 joints: vf10..13 w.x + vf14..17 w.y + vf18..21 w.z */
            v_mul(v, v->acc, 10, v->vf[9][0]); v_madd(v, v->acc, 14, v->vf[9][1]); v_madd(v, v->vf[18], 18, v->vf[9][2]);
            v_mul(v, v->acc, 11, v->vf[9][0]); v_madd(v, v->acc, 15, v->vf[9][1]); v_madd(v, v->vf[19], 19, v->vf[9][2]);
            v_mul(v, v->acc, 12, v->vf[9][0]); v_madd(v, v->acc, 16, v->vf[9][1]); v_madd(v, v->vf[20], 20, v->vf[9][2]);
            v_mul(v, v->acc, 13, v->vf[9][0]); v_madd(v, v->acc, 17, v->vf[9][1]); v_madd(v, v->vf[21], 21, v->vf[9][2]);
        } else {                                         /* 2 joints */
            v_mul(v, v->acc, 10, v->vf[9][0]); v_madd(v, v->vf[18], 14, v->vf[9][1]);
            v_mul(v, v->acc, 11, v->vf[9][0]); v_madd(v, v->vf[19], 15, v->vf[9][1]);
            v_mul(v, v->acc, 12, v->vf[9][0]); v_madd(v, v->vf[20], 16, v->vf[9][1]);
            v_mul(v, v->acc, 13, v->vf[9][0]); v_madd(v, v->vf[21], 17, v->vf[9][1]);
        }
    } else {
        st_badpc++;
        { static int told; if (told++ < 4) uya_log("native mobyskin: vcallmsr to 0x%04X (not a kernel of image 104691)", pc); }
        vu_settle(v);
        return;
    }
    /* 0x990: the normal and the position through the matrix */
    v_mul(v, v->acc, 18, v->vf[4][0]); v_madd(v, v->acc, 19, v->vf[4][1]); v_madd(v, v->vf[4], 20, v->vf[5][2]);
    v_mul(v, v->acc, 18, v->vf[1][0]); v_madd(v, v->acc, 19, v->vf[1][1]); v_madd(v, v->acc, 20, v->vf[1][2]); v_madd(v, v->vf[1], 21, v->vf[0][3]);
    /* the lights (xyz) and the turned matrix (the sphere map) */
    v_mul3(v, v->acc, 25, v->vf[4][0]); v_madd3(v, v->acc, 26, v->vf[4][1]); v_madd3(v, v->vf[2], 27, v->vf[4][2]);
    v_mul(v, v->acc, 22, v->vf[4][0]); v_madd(v, v->acc, 23, v->vf[4][1]); v_madd(v, v->vf[3], 24, v->vf[4][2]);
    v_max(v, 2, 2, 0, 3);                                /* 0x9F8 */
    v_ftoi(v, 1, 1, 1.0f, 4);                            /* 0xA00 */
    { f32 u0 = v->vf[3][0] + v->vf[0][3], u1 = v->vf[3][1] + v->vf[0][3]; v->vf[3][0] = vu_fc(u0); v->vf[3][1] = vu_fc(u1); }   /* 0xA10 */
    v_mul(v, v->acc, 31, v->vf[0][3]); v_madd(v, v->acc, 28, v->vf[2][0]); v_madd(v, v->acc, 29, v->vf[2][1]); v_madd(v, v->vf[2], 30, v->vf[2][2]);
    v_ftoi(v, 3, 3, 4096.0f, 2);                         /* 0xA38, the E bit's delay slot */
    v->tpc = 0x0A40;
    vu_settle(v);
}

/* the EE's COP2 transfers as tools/recomp.py translates them */
AI void lqc2(int n, u32 a) { __builtin_memcpy(VF(n), (void *)RC_QA(a), 16); }
AI void qmtc2(int n, R128 x) { __builtin_memcpy(VF(n), &x, 16); }
AI R128 qmfc2(int n) { R128 x; __builtin_memcpy(&x, VF(n), 16); return x; }
/* vsqi vfn, (vi1++) */
AI void vsqi_vi1(int n)
{
    f32 *p = (f32 *)(uintptr_t)(0x11004000u + ((rc_vu0.vi[1] & 0xFF) << 4));
    p[0] = VF(n)[0]; p[1] = VF(n)[1]; p[2] = VF(n)[2]; p[3] = VF(n)[3];
    rc_vu0.vi[1] = (rc_vu0.vi[1] + 1) & 0xFFFF;
}

/* ---- the EE's side of the coroutine, as host/src/gfx/vu.c does it for the translated program ---- */
/* vcallms / vcallmsr: a new program replaces a parked one */
static void vcallms(u32 addr)
{
    rc_vu0.susp = 0;
    addr &= 0xFF8;
    if (addr == 0) vu_skin(&rc_vu0, 0);
    else vu_extra(&rc_vu0, addr);
}
/* a VI register written by the EE (or qmfc2.i): a program parked in a wait loop carries on */
static void vu_resume(void)
{
    if (!rc_vu0.susp) return;
    rc_vu0.susp = 0;
    vu_skin(&rc_vu0, rc_vu0.tpc & 0xFF8);
}
AI void ctc2_vi(int k, u32 x) { rc_vu0.vi[k] = x & 0xFFFF; vu_resume(); }

/* ---- check mode's record of the chain holes: each ch8 transfer as it starts (and, while a check
 * runs, what it overwrites, to put it back) ---- */
#define MAXH 1024
static struct { u32 addr, len; u8 *before, *after; } hole[MAXH];
static int nhole, rec_holes, hole_over;
static void hole_note(void)
{
    u32 madr = M32(0x1000D010u), qwc = M32(0x1000D020u) & 0xFFFF, a = madr & 0x01FFFFF0u, len = qwc * 16;
    if (madr & 0x80000000u || a < 0x100000u || a + len > 0x2000000u) return;   /* (not main memory: not kept) */
    if (nhole == MAXH) { hole_over = 1; return; }
    hole[nhole].addr = a; hole[nhole].len = len;
    hole[nhole].before = realloc(hole[nhole].before, len ? len : 1);
    hole[nhole].after = realloc(hole[nhole].after, len ? len : 1);
    if (hole[nhole].before) memcpy(hole[nhole].before, (void *)RC_A(a), len);
    nhole++;
}
static void sw_(u32 a, u32 x)
{
    if (a == 0x1000D000u && (x & 0x100) && rec_holes) hole_note();
    rc_sw(a, x);
}

/* path counts (logged by check mode; a path at 0 is untested) */
static unsigned long long st_rec, st_pkt, st_vtx[3], st_small, st_copy, st_extra, st_xpkt, st_xv[3], st_spin, st_bail_img, st_bail_susp;
static void note_packet(u64 n2, u64 n3, u64 n1)
{
    st_pkt++;
    st_vtx[1] += (u32)n2; st_vtx[2] += (u32)n3; st_vtx[0] += (u32)n1;
    if ((u32)n1 + (u32)n2 + (u32)n3 < 3) st_small++;
}
/* a result read with lane y < 0: the game's code would spin in 0x1EF944.. for ever */
static void spin(u32 at, R128 x)
{
    st_spin++;
    static int told;
    if (told++ < 4) uya_log("native mobyskin: the VU0 result read at 0x%06X is %08X %08X (lane y < 0): the game's routine waits there for ever; going on", at, x.w[0], x.w[1]);
}

/* ---- the driver, 0x1EEBD8..0x1EFF4C, with the translation's register file ---- */
enum { ZERO, AT, V0, V1, A0, A1, A2, A3, T0, T1, T2, T3, T4, T5, T6, T7, S0, S1, S2, S3, S4, S5, S6, S7, T8, T9, K0, K1, GP, SP, FP, RA };
#define D(n) r[n].d[0]
typedef struct { R128 r[32], lo, hi; } Out;

static void run(RcCtx *c, Out *o)
{
    R128 r[32], lo = c->lo, hi = c->hi;
    memcpy(r, c->r, sizeof r);
    st_rec++;
    /* ==== setup, 0x1EEBD8: ra to SPR 0x1FF0, the locals, ch9 the record's tail to 0x1C00, the first packet to 0x2000, the light block to VU0 ==== */
    D(AT) = SE32(0x70000000);
    rc_sq(&r[RA], (u32)(D(AT) + 8176));
    D(AT) = SE32(M32((u32)(D(A0) + 20)));
    D(A2) = SE32(0x70000000);
    D(V0) = M16((u32)(D(A0) + 0));
    D(A3) = SE32((u32)D(A0) + 64);
    sw_((u32)(D(A2) + 7960), (u32)D(AT));
    D(T7) = SE32((u32)D(A2) + 8192);
    sw_((u32)(D(A2) + 7944), (u32)D(T7));
    D(T7) = SE32((u32)D(A2) + 12288);
    sw_((u32)(D(A2) + 7948), (u32)D(T7));
    D(T7) = SE32((u32)D(A2) + 7168);
    D(S0) = SE32((u32)D(T7) + 128);
    D(T0) = SE32((u32)D(V0) >> 1);
    sw_((u32)(D(A2) + 7936), (u32)D(S0));
    D(T6) = SE32(0x10010000);
    sw_((u32)(D(T6) + -11248), (u32)D(A3));
    D(T0) = SE32((u32)D(T0) + 9);
    sw_((u32)(D(T6) + -11232), (u32)D(T0));
    D(V0) = SE32((u32)D(V0) << 3);
    sw_((u32)(D(T6) + -11136), (u32)D(T7));
    D(V0) = SE32((u32)D(V0) + (u32)D(A2));
    rc_sd((u32)(D(V0) + 7288), 0ULL);
    D(T1) = SE32(256);
    sw_((u32)(D(T6) + -11264), (u32)D(T1));
    D(S1) = SE32((u32)D(T0) << 4);
    D(S1) = SE32((u32)D(S1) + (u32)D(A3));
    sw_((u32)(D(A2) + 7940), (u32)D(A1));
    sw_((u32)(D(A2) + 7956), (u32)D(S1));
    D(A3) = SE32((u32)D(S0));
    /* wait for the record's tail: the list's last entry (zeroed above) */
L_001EEC50:;
    D(T1) = M64((u32)(D(V0) + 7288));
    { int bt = D(T1) == 0ULL; if (bt) { goto L_001EEC50; } }
    D(T0) = SE32(M32((u32)(D(A3) + 0)));
    D(T1) = D(T0) & 0xFF;
    D(T2) = SE32((u32)D(T0) >> 8);
    sw_((u32)(D(T6) + -11232), (u32)D(T1));
    D(T2) = SE32((u32)D(T2) << 4);
    sw_((u32)(D(T6) + -11248), (u32)D(T2));
    D(T1) = SE32(8192);
    sw_((u32)(D(T6) + -11136), (u32)D(T1));
    D(T1) = SE32(256);
    sw_((u32)(D(T6) + -11264), (u32)D(T1));
    lqc2(1, (u32)(D(T7) + 96));
    D(T4) = SE32(1799);
    D(A1) = SE32(M32((u32)(D(A2) + 7940)));
    r[T4] = pcpyld(r[T4], ZQ);
    lqc2(25, (u32)(D(T7) + 0));
    D(AT) = SE32(251);
    lqc2(26, (u32)(D(T7) + 16));
    D(V0) = SE32(-1);
    { f32 t0 = __builtin_fabsf(VF(1)[0]); f32 t1 = __builtin_fabsf(VF(1)[1]); f32 t2 = __builtin_fabsf(VF(1)[2]); f32 t3 = __builtin_fabsf(VF(1)[3]); VF(1)[0] = t0; VF(1)[1] = t1; VF(1)[2] = t2; VF(1)[3] = t3; }
    lqc2(27, (u32)(D(T7) + 32));
    D(V1) = SE32(254);
    lqc2(28, (u32)(D(T7) + 48));
    D(T5) = SE32(0xE000000);
    lqc2(29, (u32)(D(T7) + 64));
    D(T5) = SE32((u32)D(T5) + 3584);
    lqc2(30, (u32)(D(T7) + 80));
    lqc2(31, (u32)(D(T7) + 112));
    { f32 t0 = rc_fc(VF(0)[0] - VF(1)[0]); f32 t1 = rc_fc(VF(0)[1] - VF(1)[1]); f32 t2 = rc_fc(VF(0)[2] - VF(1)[2]); f32 t3 = rc_fc(VF(0)[3] - VF(1)[3]); VF(1)[0] = t0; VF(1)[1] = t1; VF(1)[2] = t2; VF(1)[3] = t3; }
    ctc2_vi(1, (u32)D(AT));   /* 0x1EECF0 */
    qmtc2(2, r[V0]);
    ctc2_vi(10, (u32)D(V1));   /* 0x1EECF8 */
    vsqi_vi1(1);
    vsqi_vi1(25);
    vsqi_vi1(26);
    vsqi_vi1(27);
    vsqi_vi1(2);
    /* ==== per packet, 0x1EED14: ch9 idle (this packet in) ==== */
L_001EED14:;
    D(T1) = SE32(M32((u32)(D(T6) + -11264)));
    D(T1) = D(T1) & 0x100;
    { int bt = D(T1) != 0ULL; if (bt) { goto L_001EED14; } }
    /* the next list entry: its packet prefetched into the other buffer (t9 ^ 0x800) */
L_001EED34:;
    D(T0) = SE32(M32((u32)(D(A3) + 8)));
    D(T9) = SE32(M32((u32)(D(A2) + 7944)));
    D(T1) = D(T0) & 0xFF;
    D(T2) = SE32((u32)D(T0) >> 8);
    { int bt = D(T0) == 0ULL; D(T7) = D(T9) ^ 0x800; if (bt) { goto L_001EED74; } }
    sw_((u32)(D(T6) + -11232), (u32)D(T1));
    D(T2) = SE32((u32)D(T2) << 4);
    sw_((u32)(D(T6) + -11248), (u32)D(T2));
    sw_((u32)(D(T6) + -11136), (u32)D(T7));
    D(T1) = SE32(256);
    sw_((u32)(D(T6) + -11264), (u32)D(T1));
    /* the header (t9 = this packet): counts to vi3 / vi4 / vi5, the output block's layout, its padding qwords zeroed */
L_001EED74:;
    sw_((u32)(D(A2) + 7944), (u32)D(T7));
    D(A0) = SE32(M32((u32)(D(A2) + 7948)));
    D(T2) = SE32(M32((u32)(D(T9) + 0)));
    D(SP) = SE32((u32)D(T9) + 32);
    D(T6) = SE32(M32((u32)(D(T9) + 4)));
    D(T7) = SE32(M32((u32)(D(T9) + 8)));
    D(S0) = SE32(M32((u32)(D(T9) + 12)));
    D(T3) = SE32(M32((u32)(D(T9) + 20)));
    D(T0) = SE32((u32)D(T6) + (u32)D(T7));
    D(AT) = SE32(M32((u32)(D(T9) + 24)));
    D(T0) = SE32((u32)D(T0) + (u32)D(S0));
    D(V0) = SE32(M32((u32)(D(T9) + 28)));
    D(T0) = SE32((u32)D(T0) + -3);
    ctc2_vi(3, (u32)D(T6));   /* 0x1EEDBC */
    D(AT) = SE32((u32)D(AT) + (u32)D(T9));
    ctc2_vi(4, (u32)D(T7));   /* 0x1EEDC4 */
    D(V0) = SE32((u32)D(V0) + (u32)D(T9));
    ctc2_vi(5, (u32)D(S0));   /* 0x1EEDCC */
    note_packet(D(T6), D(T7), D(S0));
    D(FP) = D(T0) & 0x3;
    D(T6) = SE32((u32)D(T3) << 1);
    D(T7) = SE32((u32)D(T3) << 2);
    D(T6) = SE32((u32)D(T6) + (u32)D(T7));
    D(T7) = SE32((u32)D(T7) + 15);
    D(T6) = SE32((u32)D(T6) + 15);
    D(T7) = D(T7) & 0xFFF0;
    D(T6) = D(T6) & 0xFFF0;
    D(T0) = D(T0) & 0xFC;
    D(V1) = SE32((u32)D(A0) + (u32)D(T7));
    D(T0) = SE32((u32)D(T0) << 2);
    D(GP) = SE32((u32)D(V1) + (u32)D(T6));
    D(T8) = SE32((u32)D(V0) + (u32)D(T0));
    D(T6) = SE32((u32)D(GP) - (u32)D(A0));
    rc_sq(&ZQ, (u32)(D(V1) + -16));
    D(T6) = SE32((u32)D(T6) >> 4);
    rc_sq(&ZQ, (u32)(D(GP) + -16));
    { int bt = D(T2) == 0ULL; sw_((u32)(D(A2) + 7952), (u32)D(T6)); if (bt) { goto L_001EEE5C; } }
    /* the packet's matrix uploads: {SPR matrix, VU0 address} byte pairs, 4 qwords each */
L_001EEE1C:;
    D(T6) = M8((u32)(D(SP) + 0));
    D(T7) = M8((u32)(D(SP) + 1));
    D(T6) = SE32((u32)D(T6) << 6);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    ctc2_vi(1, (u32)D(T7));   /* 0x1EEE2C */
    D(T2) = SE32((u32)D(T2) + -1);
    lqc2(1, (u32)(D(T6) + 0));
    lqc2(2, (u32)(D(T6) + 16));
    lqc2(3, (u32)(D(T6) + 32));
    lqc2(4, (u32)(D(T6) + 48));
    vsqi_vi1(1);
    vsqi_vi1(2);
    vsqi_vi1(3);
    D(SP) = SE32((u32)D(SP) + 2);
    { int bt = D(T2) != 0ULL; vsqi_vi1(4); if (bt) { goto L_001EEE1C; } }
    /* the pipeline's start: vertices 0..2 (a qword each); vertex 0 in, vcallms 0; vertex 1 in, release vi1 */
L_001EEE5C:;
    rc_lq(&r[T0], (u32)(D(AT) + 0));
    rc_lq(&r[T1], (u32)(D(AT) + 16));
    rc_lq(&r[T2], (u32)(D(AT) + 32));
    r[S1] = pextub(r[T4], r[T0]);
    r[S1] = pextlh(r[T5], r[S1]);
    r[S1] = psllw(r[S1], 3);
    D(T7) = M64((u32)(D(S1) + 0));
    r[T9] = pextuh(r[T0], ZQ);
    D(S1) = D(S1) >> 32;
    D(T6) = SE32((u32)D(T0) >> 3);
    D(S1) = M64((u32)(D(S1) + 0));
    r[GP] = pextlb(ZQ, r[T0]);
    r[T9] = psraw(r[T9], 16);
    r[S1] = pcpyld(r[S1], r[T7]);
    D(T6) = D(T6) & 0x1FC0;
    r[T7] = pextlb(ZQ, r[GP]);
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S1]);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    qmtc2(3, r[T7]);
    r[S0] = pextub(r[T4], r[T1]);
    qmtc2(4, r[GP]);
    r[S0] = pextlh(r[T5], r[S0]);
    r[S0] = psllw(r[S0], 3);
    lqc2(5, (u32)(D(T6) + 0));
    r[S1] = pextub(r[T4], r[T2]);
    lqc2(6, (u32)(D(T6) + 16));
    r[S1] = pextlh(r[T5], r[S1]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S1] = psllw(r[S1], 3);
    lqc2(8, (u32)(D(T6) + 48));
    r[T9] = pextuh(r[T1], ZQ);
    vcallms(0x000);
    r[GP] = pextlb(ZQ, r[T1]);
    D(SP) = M64((u32)(D(S0) + 0));
    D(T6) = SE32((u32)D(T1) >> 3);
    D(S0) = D(S0) >> 32;
    D(S0) = M64((u32)(D(S0) + 0));
    D(T6) = D(T6) & 0x1FC0;
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    D(T7) = M64((u32)(D(S1) + 0));
    r[S0] = pcpyld(r[S0], r[SP]);
    D(S1) = D(S1) >> 32;
    D(S1) = M64((u32)(D(S1) + 0));
    r[T9] = psraw(r[T9], 16);
    r[S1] = pcpyld(r[S1], r[T7]);
    r[T7] = pextlb(ZQ, r[GP]);
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S0]);
    qmtc2(3, r[T7]);
    qmtc2(4, r[GP]);
    lqc2(5, (u32)(D(T6) + 0));
    lqc2(6, (u32)(D(T6) + 16));
    lqc2(7, (u32)(D(T6) + 32));
    lqc2(8, (u32)(D(T6) + 48));
    ctc2_vi(1, 0u);   /* 0x1EEFBC */
    D(AT) = SE32((u32)D(AT) + 48);
    r[T9] = pextuh(r[T2], ZQ);
    rc_lq(&r[A3], (u32)(D(AT) + 0));
    r[GP] = pextlb(ZQ, r[T2]);
    rc_lq(&r[T0], (u32)(D(AT) + 16));
    rc_lq(&r[T1], (u32)(D(AT) + 32));
    D(T6) = SE32((u32)D(T2) >> 3);
    rc_lq(&r[T2], (u32)(D(AT) + 48));
    D(T6) = D(T6) & 0x1FC0;
    r[S6] = qmfc2(11);
    r[T9] = psraw(r[T9], 16);
    { int bt = (s64)D(S6) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF014, r[S6]); }
    /* vertex 2 in, release vi2 (the first two reads are only checked) */
L_001EF01C:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S1]);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    qmtc2(3, r[T7]);
    r[S0] = pextub(r[T4], r[A3]);
    qmtc2(4, r[GP]);
    r[S0] = pextlh(r[T5], r[S0]);
    lqc2(5, (u32)(D(T6) + 0));
    r[S0] = psllw(r[S0], 3);
    lqc2(6, (u32)(D(T6) + 16));
    r[S1] = pextub(r[T4], r[T0]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S1] = pextlh(r[T5], r[S1]);
    lqc2(8, (u32)(D(T6) + 48));
    r[S1] = psllw(r[S1], 3);
    ctc2_vi(2, 0u);   /* 0x1EF05C */
    r[T9] = pextuh(r[A3], ZQ);
    D(SP) = M64((u32)(D(S0) + 0));
    r[GP] = pextlb(ZQ, r[A3]);
    D(S0) = D(S0) >> 32;
    D(T6) = SE32((u32)D(A3) >> 3);
    D(S0) = M64((u32)(D(S0) + 0));
    D(T6) = D(T6) & 0x1FC0;
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    D(T7) = M64((u32)(D(S1) + 0));
    r[S0] = pcpyld(r[S0], r[SP]);
    D(S1) = D(S1) >> 32;
    D(S1) = M64((u32)(D(S1) + 0));
    r[SP] = qmfc2(9);
    r[T9] = psraw(r[T9], 16);
    r[S1] = pcpyld(r[S1], r[T7]);
    { int bt = (s64)D(SP) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF0B4, r[SP]); }
    /* vertex 3 in, release vi1; vertex 0's results (vf11 / vf12) */
L_001EF0BC:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S0]);
    qmtc2(3, r[T7]);
    r[T9] = pextuh(r[T0], ZQ);
    qmtc2(4, r[GP]);
    r[GP] = pextlb(ZQ, r[T0]);
    lqc2(5, (u32)(D(T6) + 0));
    lqc2(6, (u32)(D(T6) + 16));
    lqc2(7, (u32)(D(T6) + 32));
    lqc2(8, (u32)(D(T6) + 48));
    r[T9] = psraw(r[T9], 16);
    ctc2_vi(1, 0u);   /* 0x1EF0FC */
    D(T6) = SE32((u32)D(T0) >> 3);
    rc_sq(&ZQ, (u32)(D(V1) + 0));
    ;
    D(T6) = D(T6) & 0x1FC0;
    r[S7] = qmfc2(11);
    r[T7] = pextlb(ZQ, r[GP]);
    r[S4] = qmfc2(12);
    r[GP] = pextub(ZQ, r[GP]);
    { int bt = (s64)D(S7) < 0; if (bt) spin(0x1EF15C, r[S7]); }
    /* vertex 4 in, release vi2; vertex 1's (vf9 / vf10) */
L_001EF164:;
    qmtc2(1, r[T9]);
    qmtc2(2, r[S1]);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    qmtc2(3, r[T7]);
    r[S0] = pextub(r[T4], r[T1]);
    qmtc2(4, r[GP]);
    r[S0] = pextlh(r[T5], r[S0]);
    r[S0] = psllw(r[S0], 3);
    lqc2(5, (u32)(D(T6) + 0));
    r[S1] = pextub(r[T4], r[T2]);
    lqc2(6, (u32)(D(T6) + 16));
    r[S1] = pextlh(r[T5], r[S1]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S1] = psllw(r[S1], 3);
    lqc2(8, (u32)(D(T6) + 48));
    r[T9] = pextuh(r[T1], ZQ);
    ctc2_vi(2, 0u);   /* 0x1EF1AC */
    r[GP] = pextlb(ZQ, r[T1]);
    D(SP) = M64((u32)(D(S0) + 0));
    D(T6) = SE32((u32)D(T1) >> 3);
    D(S0) = D(S0) >> 32;
    D(S0) = M64((u32)(D(S0) + 0));
    D(T6) = D(T6) & 0x1FC0;
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    D(T7) = M64((u32)(D(S1) + 0));
    r[S0] = pcpyld(r[S0], r[SP]);
    D(S1) = D(S1) >> 32;
    D(S1) = M64((u32)(D(S1) + 0));
    r[SP] = qmfc2(9);
    r[T9] = psraw(r[T9], 16);
    r[S5] = qmfc2(10);
    r[S1] = pcpyld(r[S1], r[T7]);
    { int bt = (s64)D(SP) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF20C, r[SP]); }
    /* vertex 5 in, release vi1 */
L_001EF214:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S0]);
    qmtc2(3, r[T7]);
    r[S4] = ppach(ZQ, r[S4]);
    qmtc2(4, r[GP]);
    r[S5] = ppach(ZQ, r[S5]);
    lqc2(5, (u32)(D(T6) + 0));
    r[S7] = ppach(r[SP], r[S7]);
    lqc2(6, (u32)(D(T6) + 16));
    lqc2(7, (u32)(D(T6) + 32));
    lqc2(8, (u32)(D(T6) + 48));
    ctc2_vi(1, 0u);   /* 0x1EF254 */
    D(AT) = SE32((u32)D(AT) + 64);
    { int bt = D(V0) == D(T8); if (bt) { goto L_001EF504; } }
    /* ==== the loop, 4 vertices a pass (halves B, A, B, A): vertex k in, vertex k-2's results out; colours by the scale bytes (pmulth, pmfhl.sh, >> 7, bytes), positions 6 bytes apart, both to the shared table ==== */
L_001EF26C:;
    rc_lq(&r[T3], (u32)(D(V0) + 0));
    r[T9] = pextuh(r[T2], ZQ);
    rc_lq(&r[A3], (u32)(D(AT) + 0));
    r[GP] = pextlb(ZQ, r[T2]);
    rc_lq(&r[T0], (u32)(D(AT) + 16));
    r[T6] = pextlb(ZQ, r[T3]);
    rc_lq(&r[T1], (u32)(D(AT) + 32));
    r[S7] = pmulth(r[S7], r[T6], &lo, &hi);
    D(T6) = SE32((u32)D(T2) >> 3);
    rc_lq(&r[T2], (u32)(D(AT) + 48));
    D(T6) = D(T6) & 0x1FC0;
    r[S6] = qmfc2(11);
    r[T9] = psraw(r[T9], 16);
    r[S2] = qmfc2(12);
    r[S7] = pmfhl_sh(lo, hi);
    { int bt = (s64)D(S6) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF2AC, r[S6]); }
L_001EF2B4:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S1]);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    qmtc2(3, r[T7]);
    r[S0] = pextub(r[T4], r[A3]);
    qmtc2(4, r[GP]);
    r[S0] = pextlh(r[T5], r[S0]);
    lqc2(5, (u32)(D(T6) + 0));
    r[S0] = psllw(r[S0], 3);
    lqc2(6, (u32)(D(T6) + 16));
    r[S1] = pextub(r[T4], r[T0]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S1] = pextlh(r[T5], r[S1]);
    lqc2(8, (u32)(D(T6) + 48));
    r[S1] = psllw(r[S1], 3);
    ctc2_vi(2, 0u);   /* 0x1EF2F4 */
    r[T9] = pextuh(r[A3], ZQ);
    D(SP) = M64((u32)(D(S0) + 0));
    r[GP] = pextlb(ZQ, r[A3]);
    D(S0) = D(S0) >> 32;
    D(T6) = SE32((u32)D(A3) >> 3);
    D(S0) = M64((u32)(D(S0) + 0));
    r[S7] = psrah(r[S7], 7);
    D(T6) = D(T6) & 0x1FC0;
    D(A3) = D(A3) & 0x1FF;
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    D(A3) = SE32((u32)D(A3) << 4);
    D(T7) = M64((u32)(D(S1) + 0));
    r[S0] = pcpyld(r[S0], r[SP]);
    D(S1) = D(S1) >> 32;
    D(A3) = SE32((u32)D(A3) + (u32)D(A1));
    D(S1) = M64((u32)(D(S1) + 0));
    r[S2] = ppach(ZQ, r[S2]);
    r[SP] = qmfc2(9);
    r[T9] = psraw(r[T9], 16);
    r[S3] = qmfc2(10);
    r[S1] = pcpyld(r[S1], r[T7]);
    { int bt = (s64)D(SP) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF34C, r[SP]); }
L_001EF354:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S0]);
    r[S3] = ppach(ZQ, r[S3]);
    qmtc2(3, r[T7]);
    r[T9] = pextuh(r[T0], ZQ);
    qmtc2(4, r[GP]);
    r[GP] = pextlb(ZQ, r[T0]);
    lqc2(5, (u32)(D(T6) + 0));
    r[S6] = ppach(r[SP], r[S6]);
    lqc2(6, (u32)(D(T6) + 16));
    r[T3] = pextub(ZQ, r[T3]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S6] = pmulth(r[S6], r[T3], &lo, &hi);
    lqc2(8, (u32)(D(T6) + 48));
    r[T9] = psraw(r[T9], 16);
    ctc2_vi(1, 0u);   /* 0x1EF394 */
    rc_sdr(D(S4), (u32)(D(V1) + 0));
    D(T6) = SE32((u32)D(T0) >> 3);
    rc_sdr(D(S5), (u32)(D(V1) + 6));
    r[S6] = pmfhl_sh(lo, hi);
    rc_sdl(D(S5), (u32)(D(V1) + 13));
    D(T0) = D(T0) & 0x1FF;
    rc_sdr(D(S2), (u32)(D(V1) + 12));
    r[S6] = psrah(r[S6], 7);
    rc_sdl(D(S2), (u32)(D(V1) + 19));
    r[S6] = ppacb(r[S6], r[S7]);
    rc_sdr(D(S3), (u32)(D(V1) + 18));
    D(T0) = SE32((u32)D(T0) << 4);
    rc_sq(&r[S6], (u32)(D(A0) + 0));
    D(T0) = SE32((u32)D(T0) + (u32)D(A1));
    sw_((u32)(D(A3) + 8), (u32)D(S6));
    D(S6) = D(S6) >> 32;
    rc_sd((u32)(D(A3) + 0), D(S4));
    D(T6) = D(T6) & 0x1FC0;
    r[S7] = qmfc2(11);
    r[T7] = pextlb(ZQ, r[GP]);
    r[S4] = qmfc2(12);
    r[GP] = pextub(ZQ, r[GP]);
    { int bt = (s64)D(S7) < 0; sw_((u32)(D(T0) + 8), (u32)D(S6)); if (bt) spin(0x1EF3F4, r[S7]); }
L_001EF3FC:;
    qmtc2(1, r[T9]);
    r[S6] = pcpyud(r[S6], ZQ);
    qmtc2(2, r[S1]);
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    qmtc2(3, r[T7]);
    r[S0] = pextub(r[T4], r[T1]);
    qmtc2(4, r[GP]);
    r[S0] = pextlh(r[T5], r[S0]);
    rc_sd((u32)(D(T0) + 0), D(S5));
    r[S0] = psllw(r[S0], 3);
    lqc2(5, (u32)(D(T6) + 0));
    r[S1] = pextub(r[T4], r[T2]);
    lqc2(6, (u32)(D(T6) + 16));
    r[S1] = pextlh(r[T5], r[S1]);
    lqc2(7, (u32)(D(T6) + 32));
    r[S1] = psllw(r[S1], 3);
    lqc2(8, (u32)(D(T6) + 48));
    r[T9] = pextuh(r[T1], ZQ);
    ctc2_vi(2, 0u);   /* 0x1EF444 */
    r[GP] = pextlb(ZQ, r[T1]);
    D(SP) = M64((u32)(D(S0) + 0));
    D(T6) = SE32((u32)D(T1) >> 3);
    D(V0) = SE32((u32)D(V0) + 16);
    D(S0) = D(S0) >> 32;
    D(T1) = D(T1) & 0x1FF;
    D(S0) = M64((u32)(D(S0) + 0));
    D(T1) = SE32((u32)D(T1) << 4);
    D(T6) = D(T6) & 0x1FC0;
    D(T1) = SE32((u32)D(T1) + (u32)D(A1));
    D(T6) = SE32((u32)D(T6) + (u32)D(A2));
    sw_((u32)(D(T1) + 8), (u32)D(S6));
    D(T7) = M64((u32)(D(S1) + 0));
    r[S0] = pcpyld(r[S0], r[SP]);
    D(S1) = D(S1) >> 32;
    rc_sd((u32)(D(T1) + 0), D(S2));
    D(S1) = M64((u32)(D(S1) + 0));
    D(V1) = SE32((u32)D(V1) + 24);
    r[SP] = qmfc2(9);
    r[T9] = psraw(r[T9], 16);
    r[S5] = qmfc2(10);
    r[S1] = pcpyld(r[S1], r[T7]);
    { int bt = (s64)D(SP) < 0; r[T7] = pextlb(ZQ, r[GP]); if (bt) spin(0x1EF4A4, r[SP]); }
L_001EF4AC:;
    qmtc2(1, r[T9]);
    r[GP] = pextub(ZQ, r[GP]);
    qmtc2(2, r[S0]);
    D(A0) = SE32((u32)D(A0) + 16);
    qmtc2(3, r[T7]);
    r[S4] = ppach(ZQ, r[S4]);
    qmtc2(4, r[GP]);
    r[S5] = ppach(ZQ, r[S5]);
    lqc2(5, (u32)(D(T6) + 0));
    r[S7] = ppach(r[SP], r[S7]);
    lqc2(6, (u32)(D(T6) + 16));
    D(S6) = D(S6) >> 32;
    lqc2(7, (u32)(D(T6) + 32));
    D(T9) = D(T2) & 0x1FF;
    lqc2(8, (u32)(D(T6) + 48));
    D(T9) = SE32((u32)D(T9) << 4);
    ctc2_vi(1, 0u);   /* 0x1EF4EC */
    D(T9) = SE32((u32)D(T9) + (u32)D(A1));
    sw_((u32)(D(T9) + 8), (u32)D(S6));
    D(AT) = SE32((u32)D(AT) + 64);
    { int bt = D(V0) != D(T8); rc_sd((u32)(D(T9) + 0), D(S3)); if (bt) { goto L_001EF26C; } }
    /* ==== the last (m - 3) & 3 vertices (fp) and the pipeline's drain ==== */
L_001EF504:;
    D(T3) = M64((u32)(D(V0) + 0));
    D(T2) = SE32(M32((u32)(D(AT) + -12)));
    D(S0) = SE32(M32((u32)(D(A2) + 7944)));
    r[T3] = pextlb(ZQ, r[T3]);
    D(T2) = SE32((u32)D(T2) << 4);
    D(S0) = D(S0) ^ 0x800;
    D(A3) = D(T2) & 0x1FF0;
    D(T2) = SE32((u32)D(T2) >> 16);
    D(A3) = SE32((u32)D(A3) + (u32)D(A1));
    D(T2) = D(T2) & 0x1FF0;
    r[S6] = qmfc2(11);
    D(T0) = SE32((u32)D(T2) + (u32)D(A1));
    r[S2] = qmfc2(12);
    { int bt = (s64)D(S6) < 0; D(S1) = SE32(M32((u32)(D(S0) + 0))); if (bt) spin(0x1EF544, r[S6]); }
L_001EF54C:;
    rc_sdr(D(S4), (u32)(D(V1) + 0));
    r[S7] = pmulth(r[S7], r[T3], &lo, &hi);
    rc_sdr(D(S5), (u32)(D(V1) + 6));
    D(T1) = SE32((u32)D(S0) + 32);
    D(T7) = SE32(M32((u32)(D(S0) + 16)));
    D(S1) = SE32((u32)D(S1) + (u32)D(S1));
    rc_sdl(D(S5), (u32)(D(V1) + 13));
    D(GP) = SE32(0x10010000);
    rc_sd((u32)(D(A3) + 0), D(S4));
    r[S7] = pmfhl_sh(lo, hi);
    rc_sd((u32)(D(T0) + 0), D(S5));
    r[S7] = psrah(r[S7], 7);
    D(T9) = SE32(M32((u32)(D(GP) + -11264)));
    r[S7] = ppacb(ZQ, r[S7]);
    rc_sd((u32)(D(A0) + 0), D(S7));
    D(T7) = SE32((u32)D(T7) << 2);
    ctc2_vi(2, 0u);   /* 0x1EF58C */
    D(S1) = SE32((u32)D(S1) + 6);
    sw_((u32)(D(A3) + 8), (u32)D(S7));
    D(S7) = D(S7) >> 32;
    sw_((u32)(D(T0) + 8), (u32)D(S7));
    D(S1) = D(S1) & 0xF8;
    D(T3) = M64((u32)(D(V0) + 8));
    D(T1) = SE32((u32)D(T1) + (u32)D(S1));
    D(T2) = SE32(M32((u32)(D(AT) + -8)));
    D(T6) = SE32((u32)D(A1) >> 13);
    r[T3] = pextlb(ZQ, r[T3]);
    D(T2) = SE32((u32)D(T2) << 4);
    D(T1) = SE32((u32)D(T1) + 8);
    D(A3) = D(T2) & 0x1FF0;
    D(T2) = SE32((u32)D(T2) >> 16);
    r[S2] = ppach(ZQ, r[S2]);
    r[SP] = qmfc2(9);
    D(A3) = SE32((u32)D(A3) + (u32)D(A1));
    r[S3] = qmfc2(10);
    D(T2) = D(T2) & 0x1FF0;
    { int bt = (s64)D(SP) < 0; D(T0) = SE32((u32)D(T2) + (u32)D(A1)); if (bt) spin(0x1EF5E4, r[SP]); }
L_001EF5EC:;
    D(GP) = SE32(M32((u32)(D(GP) + -12288)));
    r[S6] = ppach(r[SP], r[S6]);
    rc_sdr(D(S2), (u32)(D(V1) + 12));
    r[S6] = pmulth(r[S6], r[T3], &lo, &hi);
    rc_sdl(D(S2), (u32)(D(V1) + 19));
    r[S3] = ppach(ZQ, r[S3]);
    D(V1) = SE32((u32)D(V1) + 18);
    r[T6] = pcpyh(r[T6]);
    D(S1) = M64((u32)(D(T1) + -8));
    rc_sd((u32)(D(A3) + 0), D(S2));
    r[S7] = pmfhl_sh(lo, hi);
    r[S7] = psrah(r[S7], 7);
    r[S7] = ppacb(ZQ, r[S7]);
    ctc2_vi(1, 0u);   /* 0x1EF62C */
    r[S1] = pextlh(r[T6], r[S1]);
    rc_sd((u32)(D(A0) + 8), D(S7));
    D(A0) = SE32((u32)D(A0) + 12);
    r[S0] = psrlw(r[S1], 3);
    { int bt = D(FP) == 0ULL; sw_((u32)(D(A3) + 8), (u32)D(S7)); if (bt) { goto L_001EF7BC; } }
    rc_sdr(D(S3), (u32)(D(V1) + 0));
    D(S7) = D(S7) >> 32;
    rc_sd((u32)(D(T0) + 0), D(S3));
    D(FP) = SE32((u32)D(FP) + -1);
    D(V1) = SE32((u32)D(V1) + 6);
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(FP) == 0ULL; sw_((u32)(D(T0) + 8), (u32)D(S7)); if (bt) { goto L_001EF7BC; } }
    D(FP) = SE32((u32)D(FP) + -1);
    r[S6] = qmfc2(11);
    r[S2] = qmfc2(12);
    { int bt = (s64)D(S6) < 0; if (bt) spin(0x1EF694, r[S6]); }
L_001EF69C:;
    r[S2] = ppach(ZQ, r[S2]);
    ;
    ;
    ctc2_vi(2, 0u);   /* 0x1EF6E4 */
    D(T3) = M64((u32)(D(V0) + 16));
    D(T2) = SE32(M32((u32)(D(AT) + -4)));
    r[T3] = pextlb(ZQ, r[T3]);
    D(T2) = SE32((u32)D(T2) << 4);
    D(A3) = D(T2) & 0x1FF0;
    D(T2) = SE32((u32)D(T2) >> 16);
    r[SP] = qmfc2(9);
    D(A3) = SE32((u32)D(A3) + (u32)D(A1));
    r[S3] = qmfc2(10);
    D(T2) = D(T2) & 0x1FF0;
    { int bt = (s64)D(SP) < 0; D(T0) = SE32((u32)D(T2) + (u32)D(A1)); if (bt) spin(0x1EF73C, r[SP]); }
L_001EF744:;
    r[S6] = ppach(r[SP], r[S6]);
    rc_sdr(D(S2), (u32)(D(V1) + 0));
    r[S6] = pmulth(r[S6], r[T3], &lo, &hi);
    r[S3] = ppach(ZQ, r[S3]);
    D(V1) = SE32((u32)D(V1) + 6);
    rc_sd((u32)(D(A3) + 0), D(S2));
    r[S7] = pmfhl_sh(lo, hi);
    r[S7] = psrah(r[S7], 7);
    r[S7] = ppacb(ZQ, r[S7]);
    rc_sd((u32)(D(A0) + 0), D(S7));
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(FP) == 0ULL; sw_((u32)(D(A3) + 8), (u32)D(S7)); if (bt) { goto L_001EF7BC; } }
    rc_sdr(D(S3), (u32)(D(V1) + 0));
    D(S7) = D(S7) >> 32;
    rc_sdl(D(S3), (u32)(D(V1) + 7));
    D(V1) = SE32((u32)D(V1) + 6);
    rc_sd((u32)(D(T0) + 0), D(S3));
    D(A0) = SE32((u32)D(A0) + 4);
    sw_((u32)(D(T0) + 8), (u32)D(S7));
    /* ==== the copies (header +0x10): shared-table entries appended to the two sections ==== */
L_001EF7BC:;
    st_copy += (u32)D(T7) >> 2;
    { int bt = D(T7) == 0ULL; D(T7) = SE32((u32)D(T7) + (u32)D(A0)); if (bt) { goto L_001EF864; } }
L_001EF7C4:;
    rc_lq(&r[A3], (u32)(D(S0) + 0));
    D(S0) = D(S0) >> 32;
    D(S1) = M64((u32)(D(T1) + 0));
    D(V1) = SE32((u32)D(V1) + 6);
    r[T0] = pcpyud(r[A3], ZQ);
    rc_sdr(D(A3), (u32)(D(V1) + -6));
    rc_sdl(D(A3), (u32)(D(V1) + 1));
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(A0) == D(T7); sw_((u32)(D(A0) + -4), (u32)D(T0)); if (bt) { goto L_001EF864; } }
    rc_lq(&r[A3], (u32)(D(S0) + 0));
    r[S0] = pcpyud(r[S0], ZQ);
    D(T1) = SE32((u32)D(T1) + 8);
    D(V1) = SE32((u32)D(V1) + 6);
    r[T0] = pcpyud(r[A3], ZQ);
    rc_sdr(D(A3), (u32)(D(V1) + -6));
    rc_sdl(D(A3), (u32)(D(V1) + 1));
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(A0) == D(T7); sw_((u32)(D(A0) + -4), (u32)D(T0)); if (bt) { goto L_001EF864; } }
    rc_lq(&r[A3], (u32)(D(S0) + 0));
    r[S1] = pextlh(r[T6], r[S1]);
    D(S0) = D(S0) >> 32;
    D(V1) = SE32((u32)D(V1) + 6);
    r[T0] = pcpyud(r[A3], ZQ);
    rc_sdr(D(A3), (u32)(D(V1) + -6));
    rc_sdl(D(A3), (u32)(D(V1) + 1));
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(A0) == D(T7); sw_((u32)(D(A0) + -4), (u32)D(T0)); if (bt) { goto L_001EF864; } }
    rc_lq(&r[A3], (u32)(D(S0) + 0));
    r[S0] = psrlw(r[S1], 3);
    D(V1) = SE32((u32)D(V1) + 6);
    r[T0] = pcpyud(r[A3], ZQ);
    rc_sdr(D(A3), (u32)(D(V1) + -6));
    rc_sdl(D(A3), (u32)(D(V1) + 1));
    D(A0) = SE32((u32)D(A0) + 4);
    { int bt = D(A0) != D(T7); sw_((u32)(D(A0) + -4), (u32)D(T0)); if (bt) { goto L_001EF7C4; } }
    /* ==== the packet out: ch9 idle if it was busy, ch8 idle, ch8 (fromSPR) to the hole (entry word 1) ==== */
L_001EF864:;
    D(A3) = SE32(M32((u32)(D(A2) + 7936)));
    D(T6) = SE32(0x10010000);
    D(T7) = SE32(M32((u32)(D(A2) + 7952)));
    D(T9) = D(T9) & 0x100;
    { int bt = D(T9) == 0ULL; D(GP) = D(GP) & 0x100; if (bt) { goto L_001EF89C; } }
L_001EF87C:;
    D(T1) = SE32(M32((u32)(D(T6) + -11264)));
    D(T1) = D(T1) & 0x100;
    { int bt = D(T1) != 0ULL; if (bt) { goto L_001EF87C; } }
L_001EF89C:;
    { int bt = D(GP) == 0ULL; D(T0) = SE32(M32((u32)(D(A3) + 4))); if (bt) { goto L_001EF8C4; } }
L_001EF8A4:;
    D(T1) = SE32(M32((u32)(D(T6) + -12288)));
    D(T1) = D(T1) & 0x100;
    { int bt = D(T1) != 0ULL; if (bt) { goto L_001EF8A4; } }
L_001EF8C4:;
    D(T2) = SE32(M32((u32)(D(A2) + 7948)));
    D(A3) = SE32((u32)D(A3) + 8);
    sw_((u32)(D(T6) + -12256), (u32)D(T7));
    sw_((u32)(D(T6) + -12272), (u32)D(T0));
    sw_((u32)(D(T6) + -12160), (u32)D(T2));
    D(T1) = SE32(256);
    sw_((u32)(D(T6) + -12288), (u32)D(T1));
    D(T0) = SE32(M32((u32)(D(A3) + 0)));
    D(T2) = D(T2) ^ 0x400;
    sw_((u32)(D(A2) + 7936), (u32)D(A3));
    { int bt = D(T0) != 0ULL; sw_((u32)(D(A2) + 7948), (u32)D(T2)); if (bt) { goto L_001EED34; } }
    /* ==== the end: ch8 idle, v0 = the next record, ra back; the extra pass if [0x1F18] != 0 ==== */
L_001EF904:;
    D(T1) = SE32(M32((u32)(D(T6) + -12288)));
    D(T1) = D(T1) & 0x100;
    { int bt = D(T1) != 0ULL; if (bt) { goto L_001EF904; } }
    D(A0) = SE32(M32((u32)(D(A2) + 7960)));
    D(V0) = SE32(M32((u32)(D(A2) + 7956)));
    D(AT) = SE32(0x70000000);
    { int bt = D(A0) != 0ULL; rc_lq(&r[RA], (u32)(D(AT) + 8176)); if (bt) { goto L_001EFBE4; } }
    goto out;
    /* ==== the extra pass, 0x1EFBE4: the block after the record to 0x1C00; the light colours * 0.25, vf31 = 65536.875 / 65537 ==== */
L_001EFBE4:;
    st_extra++;
    D(T9) = SE32(0x10010000);
    D(V1) = SE32((u32)D(A0) >> 16);
    sw_((u32)(D(T9) + -11248), (u32)D(V0));
    D(T8) = SE32(0x70000000);
    sw_((u32)(D(T9) + -11232), (u32)D(V1));
    D(T7) = SE32((u32)D(T8) + 7168);
    sw_((u32)(D(T9) + -11136), (u32)D(T7));
    D(V1) = SE32((u32)D(V1) << 4);
    D(T7) = SE32((u32)D(T7) + (u32)D(V1));
    D(GP) = SE32((u32)D(V0) + (u32)D(V1));
    D(T7) = SE32((u32)D(T7) + -8);
    D(S0) = SE32((u32)D(T8) + 8192);
    rc_sd((u32)(D(T7) + 0), D(T7));
    D(AT) = SE32(256);
    sw_((u32)(D(T9) + -11264), (u32)D(AT));
    D(SP) = D(A0) & 0xFFFF;
    D(FP) = SE32((u32)D(T8) + 7232);
    D(T4) = SE32(1799);
    D(AT) = SE32(0x3E800000);
    D(V1) = SE32(0x47800000);
    qmtc2(1, r[AT]);
    D(V0) = D(V1) | 0x70;
    qmtc2(2, r[V0]);
    D(V1) = D(V1) | 0x80;
    qmtc2(3, r[V1]);
    { f32 t0 = rc_fc(VF(28)[0] * VF(1)[0]); f32 t1 = rc_fc(VF(28)[1] * VF(1)[0]); f32 t2 = rc_fc(VF(28)[2] * VF(1)[0]); VF(28)[0] = t0; VF(28)[1] = t1; VF(28)[2] = t2; }
    { f32 t0 = rc_fc(VF(29)[0] * VF(1)[0]); f32 t1 = rc_fc(VF(29)[1] * VF(1)[0]); f32 t2 = rc_fc(VF(29)[2] * VF(1)[0]); VF(29)[0] = t0; VF(29)[1] = t1; VF(29)[2] = t2; }
    { f32 t0 = rc_fc(VF(30)[0] * VF(1)[0]); f32 t1 = rc_fc(VF(30)[1] * VF(1)[0]); f32 t2 = rc_fc(VF(30)[2] * VF(1)[0]); VF(30)[0] = t0; VF(30)[1] = t1; VF(30)[2] = t2; }
    { f32 t3 = VF(3)[0]; VF(31)[3] = t3; }
    { f32 t0 = rc_fc(VF(0)[0] + VF(2)[0]); f32 t1 = rc_fc(VF(0)[1] + VF(2)[0]); f32 t2 = rc_fc(VF(0)[2] + VF(2)[0]); VF(31)[0] = t0; VF(31)[1] = t1; VF(31)[2] = t2; }
    /* wait for the block (its last dword, set to its own address above, becomes 0) */
L_001EFC5C:;
    D(AT) = M64((u32)(D(T7) + 0));
    { int bt = D(AT) != 0ULL; if (bt) { goto L_001EFC5C; } }
    D(AT) = SE32(M32((u32)(D(FP) + 0)));
    sw_((u32)(D(T9) + -11136), (u32)D(S0));
    D(V0) = D(AT) & 0xFF;
    D(AT) = SE32((u32)D(AT) >> 8);
    sw_((u32)(D(T9) + -11232), (u32)D(V0));
    D(AT) = SE32((u32)D(AT) << 4);
    sw_((u32)(D(T9) + -11248), (u32)D(AT));
    D(V0) = SE32((u32)D(V0) << 4);
    D(S1) = SE32((u32)D(S0) + (u32)D(V0));
    D(S1) = SE32((u32)D(S1) + -16);
    D(AT) = SE32(256);
    rc_sd((u32)(D(S1) + 0), 0ULL);
    sw_((u32)(D(T9) + -11264), (u32)D(AT));
    D(S0) = D(S0) ^ 0x800;
    lqc2(22, (u32)(D(T8) + 7168));
    lqc2(23, (u32)(D(T8) + 7184));
    lqc2(24, (u32)(D(T8) + 7200));
    r[T5] = pextlw(r[T5], r[T5]);
    /* ---- per list entry ---- */
L_001EFCC8:;
    { int bt = D(S1) == 0ULL; D(S0) = D(S0) ^ 0x800; if (bt) { goto L_001EFF48; } }
    /* wait for this packet (its last qword's first dword, zeroed); the next one prefetched */
L_001EFCD0:;
    D(A0) = M64((u32)(D(S1) + 0));
    D(T9) = SE32(0x10010000);
    D(AT) = SE32(M32((u32)(D(FP) + 8)));
    D(V1) = D(S0) ^ 0x800;
    { int bt = D(A0) == 0ULL; D(T7) = SE32(M32((u32)(D(FP) + 4))); if (bt) { goto L_001EFCD0; } }
    { int bt = D(AT) == 0ULL; D(S1) = SE32(0); if (bt) { goto L_001EFD2C; } }
    D(V0) = D(AT) & 0xFF;
    D(AT) = SE32((u32)D(AT) >> 8);
    sw_((u32)(D(T9) + -11136), (u32)D(V1));
    D(AT) = SE32((u32)D(AT) << 4);
    sw_((u32)(D(T9) + -11232), (u32)D(V0));
    D(V0) = SE32((u32)D(V0) << 4);
    sw_((u32)(D(T9) + -11248), (u32)D(AT));
    D(S1) = SE32((u32)D(V1) + (u32)D(V0));
    D(S1) = SE32((u32)D(S1) + -16);
    rc_sd((u32)(D(S1) + 0), 0ULL);
    D(AT) = SE32(256);
    sw_((u32)(D(T9) + -11264), (u32)D(AT));
    /* the header: vertex count, position / colour / ST offsets from 0x3000 (ST: also the ch8 qwords) */
L_001EFD2C:;
    st_xpkt++;
    D(AT) = SE32((u32)D(S0) + 16);
    D(T9) = SE32(M32((u32)(D(S0) + 0)));
    D(V1) = SE32((u32)D(T8) + 12288);
    D(A0) = SE32(M32((u32)(D(S0) + 4)));
    D(A1) = SE32(M32((u32)(D(S0) + 8)));
    D(A3) = SE32((u32)D(T9) << 4);
    D(T6) = SE32(M32((u32)(D(S0) + 12)));
    D(A3) = SE32((u32)D(A3) + (u32)D(AT));
    D(A0) = SE32((u32)D(A0) + (u32)D(V1));
    D(A1) = SE32((u32)D(A1) + (u32)D(V1));
    D(A2) = SE32((u32)D(T6) + (u32)D(V1));
    rc_sq(&ZQ, (u32)(D(A0) + -16));
    rc_sq(&ZQ, (u32)(D(A1) + -16));
    D(T6) = SE32((u32)D(T6) >> 4);
    rc_sq(&ZQ, (u32)(D(A2) + -16));
    D(V0) = SE32(-1);
    rc_sq(&ZQ, (u32)(D(A3) + 0));
    D(FP) = SE32((u32)D(FP) + 8);
    /* per vertex (and once more): position, normal, weights, its joints' matrices to VU0; the previous vertex's results out; vcallmsr */
L_001EFD74:;
    D(A3) = M16((u32)(D(AT) + 6));
    D(V0) = SE32((u32)D(V0) + 1);
    D(A2) = M64((u32)(D(AT) + 0));
    r[T0] = pextlb(r[T4], r[A3]);
    D(T1) = M8((u32)(D(AT) + 11));
    r[T0] = pextlh(r[T5], r[T0]);
    D(T2) = SE32(M32((u32)(D(AT) + 8)));
    r[T0] = psllw(r[T0], 3);
    D(T3) = SE32(M32((u32)(D(AT) + 12)));
    D(A3) = M64((u32)(D(T0) + 0));
    r[A2] = pextlh(r[A2], ZQ);
    D(T0) = D(T0) >> 32;
    D(T1) = SE32((u32)D(T1) + -2);
    D(T0) = M64((u32)(D(T0) + 0));
    r[A2] = psraw(r[A2], 16);
    r[T2] = pextlb(ZQ, r[T2]);
    qmtc2(7, r[A2]);
    r[A3] = pcpyld(r[T0], r[A3]);
    { int bt = (s64)D(T1) < 0; qmtc2(8, r[A3]); if (bt) { goto L_001EFE34; } }
    r[T3] = pextlb(ZQ, r[T3]);
    r[T2] = psllw(r[T2], 3);
    r[T3] = pextlh(ZQ, r[T3]);
    r[T2] = pextlh(r[T5], r[T2]);
    r[T3] = psllw(r[T3], 4);
    r[T2] = psllw(r[T2], 3);
    qmtc2(9, r[T3]);
    lqc2(10, (u32)(D(T2) + 0));
    lqc2(11, (u32)(D(T2) + 16));
    lqc2(12, (u32)(D(T2) + 32));
    D(T3) = SE32(289);
    lqc2(13, (u32)(D(T2) + 48));
    D(T2) = D(T2) >> 32;
    lqc2(14, (u32)(D(T2) + 0));
    lqc2(15, (u32)(D(T2) + 16));
    lqc2(16, (u32)(D(T2) + 32));
    lqc2(17, (u32)(D(T2) + 48));
    { int bt = D(T1) == 0ULL; r[T2] = pcpyud(r[T2], ZQ); if (bt) { goto L_001EFE5C; } }
    lqc2(18, (u32)(D(T2) + 0));
    lqc2(19, (u32)(D(T2) + 16));
    lqc2(20, (u32)(D(T2) + 32));
    D(T3) = SE32(273);
    lqc2(21, (u32)(D(T2) + 48)); goto L_001EFE5C;
    /* one joint */
L_001EFE34:;
    D(T2) = D(T2) & 0xFF;
    D(T3) = SE32(0x70000000);
    D(T2) = SE32((u32)D(T2) << 6);
    D(T2) = D(T2) | D(T3);
    lqc2(10, (u32)(D(T2) + 0));
    lqc2(11, (u32)(D(T2) + 16));
    lqc2(12, (u32)(D(T2) + 32));
    lqc2(13, (u32)(D(T2) + 48));
    D(T3) = SE32(301);
    /* the kernel's start (vi27 = CMSAR0); qmfc2.i of the previous results, vcallmsr */
L_001EFE5C:;
    st_xv[(u32)D(T3) == 301 ? 0 : (u32)D(T3) == 289 ? 1 : 2]++;
    rc_vu0.cmsar0 = (u32)D(T3) & 0xFFFF;
    vu_resume(); r[A2] = qmfc2(1);
    r[A3] = qmfc2(2);
    D(AT) = SE32((u32)D(AT) + 16);
    r[T0] = qmfc2(3);
    r[A2] = ppach(ZQ, r[A2]);
    { int bt = D(V0) == 0ULL; vcallms((rc_vu0.cmsar0 & 0xFFFF) * 8); if (bt) { goto L_001EFF20; } }
    M16((u32)(D(A0) + 0)) = (u16)D(A2);
    D(A2) = D(A2) >> 16;
    M16((u32)(D(A0) + 2)) = (u16)D(A2);
    D(A2) = D(A2) >> 16;
    M16((u32)(D(A0) + 4)) = (u16)D(A2);
    D(A0) = SE32((u32)D(A0) + 6);
    r[A3] = ppach(ZQ, r[A3]);
    r[A3] = ppacb(ZQ, r[A3]);
    sw_((u32)(D(A1) + 0), (u32)D(A3));
    r[T0] = psraw(r[T0], 1);
    r[T0] = ppach(ZQ, r[T0]);
    M8((u32)(D(A1) + 3)) = (u8)D(SP);
    sw_((u32)(D(V1) + 0), (u32)D(T0));
    D(V1) = SE32((u32)D(V1) + 4);
    { int bt = D(V0) != D(T9); D(A1) = SE32((u32)D(A1) + 4); if (bt) { goto L_001EFD74; } }
    D(T9) = SE32(0x10010000);
    sw_((u32)(D(T9) + -12256), (u32)D(T6));
    sw_((u32)(D(T9) + -12272), (u32)D(T7));
    D(AT) = SE32(12288);
    sw_((u32)(D(T9) + -12160), (u32)D(AT));
    D(AT) = SE32(256);
    sw_((u32)(D(T9) + -12288), (u32)D(AT));
    /* ch8 idle (a short delay loop between polls) */
L_001EFEEC:;
    D(AT) = SE32(M32((u32)(D(T9) + -12288)));
    D(AT) = D(AT) & 0x100;
    { int bt = D(AT) == 0ULL; D(AT) = SE32(16); if (bt) { goto L_001EFCC8; } }
L_001EFEFC:;
    D(AT) = SE32((u32)D(AT) + -1);
    { int bt = D(AT) != 0ULL; if (bt) { goto L_001EFEFC; } }
    goto L_001EFEEC;
    /* the first vertex: no results yet */
L_001EFF20:;
    /* (eight pmaxw to $zero: no effect) */
    goto L_001EFD74;
    /* the end: v0 = the block's end */
L_001EFF48:;
    D(V0) = SE32((u32)D(GP) + 0); goto out;
out:
    memcpy(o->r, r, sizeof r);
    o->lo = lo; o->hi = hi;
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
/* image 104691 in VU0 micro memory? (vu.c runs whatever image is there; this file is that one) */
typedef struct { u32 vma, size; const u32 *words; } VuBlock;
typedef struct { const char *name; int vu; void (*fn)(RcVu0 *, u32); int nblk; const VuBlock *blk; } VuImage;
extern const VuImage vu_images[];
extern const int vu_image_count;
static int image_in_place(void)
{
    static int idx = -2;
    if (idx == -2) {
        idx = -1;
        for (int i = 0; i < vu_image_count; i++)
            if (vu_images[i].vu == 0 && !strcmp(vu_images[i].name, "104691")) { idx = i; break; }
        if (idx < 0) uya_log("native mobyskin: the build has no VU0 image 104691: the game's routine runs");
    }
    if (idx < 0) return 0;
    const VuImage *im = &vu_images[idx];
    for (int b = 0; b < im->nblk; b++)
        if (im->blk[b].vma + im->blk[b].size > 0x1000 || memcmp((const u8 *)(uintptr_t)0x11000000u + im->blk[b].vma, im->blk[b].words, im->blk[b].size)) return 0;
    return 1;
}

static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;
static int n_told;

/* check mode: the state the routine reads and writes, put aside and compared */
#define TAB 0x4000u      /* the shared-vertex table: 16 KB from a1 rounded down to 8 KB (its 512 entries, and the copies' addresses) */
typedef struct { u8 spr[0x4000], vu0m[0x1000], tab[TAB], d8[0xA0], d9[0xA0]; RcVu0 vu0; int valid; } Snap;
static Snap A, N, G;
static struct { u32 tab, ntab; Out o; } K;
static void grab(Snap *s)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    memcpy(s->vu0m, (void *)(uintptr_t)0x11004000u, 0x1000);
    if (K.ntab) memcpy(s->tab, (void *)RC_A(K.tab), K.ntab);
    memcpy(s->d8, (void *)(uintptr_t)0x1000D000u, 0xA0);
    memcpy(s->d9, (void *)(uintptr_t)0x1000D400u, 0xA0);
    s->vu0 = rc_vu0;
    s->valid = 1;
}
static void put_back(const Snap *s)
{
    for (int k = nhole - 1; k >= 0; k--)                  /* the holes, last first */
        if (hole[k].before) memcpy((void *)RC_A(hole[k].addr), hole[k].before, hole[k].len);
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    memcpy((void *)(uintptr_t)0x11004000u, s->vu0m, 0x1000);
    if (K.ntab) memcpy((void *)RC_A(K.tab), s->tab, K.ntab);
    memcpy((void *)(uintptr_t)0x1000D000u, s->d8, 0xA0);
    memcpy((void *)(uintptr_t)0x1000D400u, s->d9, 0xA0);
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
        uya_log("native mobyskin check: run %llu: %s differs at %08X (+0x%X; %u qwords differ): game %08X %08X %08X %08X, native %08X %08X %08X %08X",
                n_runs, what, base + o, k, nq, gq[0], gq[1], gq[2], gq[3], xq[0], xq[1], xq[2], xq[3]);
    }
    return 1;
}
/* the VU0 state: every field of RcVu0 (registers, micro-mode state, the pipeline model's leftovers) */
static int vu0_differs(const RcVu0 *g, const RcVu0 *n)
{
    if (!memcmp(g, n, sizeof *g)) return 0;
    if (n_told < 10) {
        n_told++;
        const u32 *gw = (const u32 *)g, *nw = (const u32 *)n;
        u32 k = 0;
        while (gw[k] == nw[k]) k++;
        const char *what = "?";
        u32 off = k * 4;
        if (off < offsetof(RcVu0, acc)) what = "vf";
        else if (off < offsetof(RcVu0, q)) what = "acc";
        else if (off < offsetof(RcVu0, vi)) what = "q/i/p/r";
        else if (off < offsetof(RcVu0, clip)) what = "vi";
        else if (off < offsetof(RcVu0, cmsar0)) what = "clip/status/mac";
        else if (off < offsetof(RcVu0, st_int)) what = "cmsar0/tpc/susp";
        else if (off < offsetof(RcVu0, fp_n)) what = "st_int/cl_int";
        else if (off < offsetof(RcVu0, q_pend)) what = "flag pipelines";
        else what = "Q pipeline (q_pend, q_rdy, q_new)";
        u32 r0 = off / 16;
        if (off < offsetof(RcVu0, acc))
            uya_log("native mobyskin check: run %llu: VU0 %s differs, first vf%u: game %08X %08X %08X %08X, native %08X %08X %08X %08X", n_runs, what, r0,
                    gw[r0 * 4], gw[r0 * 4 + 1], gw[r0 * 4 + 2], gw[r0 * 4 + 3], nw[r0 * 4], nw[r0 * 4 + 1], nw[r0 * 4 + 2], nw[r0 * 4 + 3]);
        else
            uya_log("native mobyskin check: run %llu: VU0 %s differs (word +0x%X: game %08X, native %08X; tpc %04X / %04X, susp %d / %d)", n_runs, what, off,
                    gw[k], nw[k], g->tpc, n->tpc, g->susp, n->susp);
    }
    return 1;
}

int uya_mobyskin_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    (void)g; (void)a;
    if (mode_ < 0) {
        const char *e = getenv("UYA_NMOBYSKIN"), *n = getenv("UYA_NATIVE");
        mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2;
        uya_log("native mobyskin: %s", mode_ == 0 ? "off (the game's routine and VU0 micro mode)" : mode_ == 1 ? "check against the game's routine" : "on");
    }
    if (!mode_) return 0;
    /* the routine's own gp-relative accesses (it uses gp as a temporary: sq 0 at -16(gp), the
     * CHCR reads at 0x1EF57C / 0x1EF5EC): 3, no address pairs */
    if (ng != 3 || na != 0) { static int told; if (!told++) uya_log("native mobyskin: the level's routine has %d / %d data references (not 3 / 0): the game's routine runs", ng, na); return 0; }
    if (rc_vu0.susp) {   /* another VU0 program parked: the game's first ctc2 would resume it */
        st_bail_susp++;
        static int told; if (!told++) uya_log("native mobyskin: a VU0 program is parked at 0x%04X on entry: the game's routine runs", rc_vu0.tpc);
        return 0;
    }
    if (!image_in_place()) {
        st_bail_img++;
        static int told; if (!told++) uya_log("native mobyskin: VU0 micro memory does not hold image 104691: the game's routine runs");
        return 0;
    }
    if (mode_ >= 2) {
        Out o;
        run(c, &o);
        memcpy(c->r, o.r, sizeof o.r);
        c->lo = o.lo; c->hi = o.hi;
        return 1;
    }
    /* check: run here, keep the results, put the state back for the game's routine */
    {
        u32 a1 = (u32)c->r[5].d[0], t = a1 & ~0x1FFFu;
        uintptr_t p = RC_A(t);
        K.tab = t; K.ntab = (p >= 0x100000u && p + TAB <= 0x2000000u) ? TAB : 0;
    }
    grab(&A);
    nhole = 0; hole_over = 0; rec_holes = 1;
    run(c, &K.o);
    rec_holes = 0;
    grab(&N);
    for (int k = 0; k < nhole; k++) if (hole[k].after) memcpy(hole[k].after, (void *)RC_A(hole[k].addr), hole[k].len);
    put_back(&A);
    return 0;
}

void uya_mobyskin_exit(RcCtx *c)
{
    if (mode_ != 1 || !N.valid) return;
    N.valid = 0;
    n_runs++;
    grab(&G);
    int bad = 0;
    for (int k = 0; k < 32; k++)
        if (memcmp(&c->r[k], &K.o.r[k], 16)) {
            bad = 1;
            if (n_told < 10) {
                n_told++;
                uya_log("native mobyskin check: run %llu: GPR %d %016llX %016llX, native %016llX %016llX", n_runs, k,
                        (unsigned long long)c->r[k].d[1], (unsigned long long)c->r[k].d[0], (unsigned long long)K.o.r[k].d[1], (unsigned long long)K.o.r[k].d[0]);
            }
            break;
        }
    if (memcmp(&c->lo, &K.o.lo, 16) || memcmp(&c->hi, &K.o.hi, 16)) {
        bad = 1;
        if (n_told < 10) { n_told++; uya_log("native mobyskin check: run %llu: LO / HI differ", n_runs); }
    }
    for (int k = 0; k < nhole; k++)
        if (hole[k].after) {
            char what[32]; snprintf(what, sizeof what, "hole %d", k);
            bad |= differs(what, hole[k].addr, (const u8 *)RC_A(hole[k].addr), hole[k].after, hole[k].len);
        }
    if (hole_over) { static int told; if (!told++) uya_log("native mobyskin check: more than %d holes in a call: the others are not compared", MAXH); }
    bad |= differs("scratchpad", SPR, G.spr, N.spr, 0x4000);
    bad |= differs("VU0 memory", 0x11004000u, G.vu0m, N.vu0m, 0x1000);
    if (K.ntab) bad |= differs("shared-vertex table", K.tab, G.tab, N.tab, K.ntab);
    bad |= differs("DMA ch8 registers", 0x1000D000u, G.d8, N.d8, 0xA0);
    bad |= differs("DMA ch9 registers", 0x1000D400u, G.d9, N.d9, 0xA0);
    bad |= vu0_differs(&G.vu0, &N.vu0);
    if (bad) n_bad++; else n_ok++;
    if (n_runs % 500 == 0 || n_runs == 100 || n_runs == 10)
        uya_log("native mobyskin check: %llu runs, %llu identical, %llu differ; records %llu, packets %llu (under 3 skinned vertices %llu), "
                "vertices 1 joint %llu, 2 joints %llu, 3 joints %llu, copies %llu; VU0 halves %llu; extra pass %llu (packets %llu, vertices 1 / 2 / 3 joints %llu / %llu / %llu); "
                "sentinel seen %llu; odd VU0 start %llu; the game's routine ran: image missing %llu, VU0 busy %llu",
                n_runs, n_ok, n_bad, st_rec, st_pkt, st_small, st_vtx[0], st_vtx[1], st_vtx[2], st_copy, st_halves, st_extra, st_xpkt,
                st_xv[0], st_xv[1], st_xv[2], st_spin, st_badpc, st_bail_img, st_bail_susp);
}
