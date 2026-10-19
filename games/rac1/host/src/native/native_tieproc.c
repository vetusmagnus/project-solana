/* TieProc (the game's tie culling, level of detail and chain building, hand-written assembly in
 * the game: decomp text 0x236F00, 5332 bytes, one copy in every level) as native C.
 *
 * What the game's routine does, in order:
 *   - setup: saves s0..s7, sp, fp, ra in the scratchpad (0x3FA8..); keeps its state there: the
 *     class count and the class table (0x3F00 / 0x3F04, table 0x1E1A00 in the executable), the clip
 *     list pointer (0x3F08, list at 0x3A00), the texture-distance list pointer (0x3F14, list at
 *     0x3200), the light list pointer (0x3F18, list at 0x3600), the instance base and the
 *     per-instance distance cap (0x3F1C / 0x3F28); clears the instance slots (0x2000..0x2FFF);
 *     copies the occlusion bitmap (128 bytes) to 0x3000, the sway sine table (256 signed bytes)
 *     to 0x3500 and the sway constants (2 qwords) to 0x3E80; the sway clock to 0x3EA0, the sway
 *     subroutine's return address to 0x3F24; loads the camera (rotation rows, position * 1024 and
 *     its 1/1024 back in vf24.w, the view-projection matrix), the frustum constants and the guard
 *     band scale; the eight chain tag templates (gp) to 0x3E00..0x3E70; writes the chain's start: a
 *     REF to the tie VU program (MPG) and 6 qwords of VIF constants.
 *   - per class (a TieData, 0x1C0 bytes): skipped when its flags (+0x24) have bit 0 or 3. Else
 *     its counters (+0x1C) are cleared, its sway mode ((flags & 6) >> 1) kept in 0x3F20, and its
 *     instances (+0x28, count +0x26, 0x20 bytes each) DMAed to the scratchpad slots 128 at a time.
 *     Per batch:
 *       - the cull pass, per instance: its distance cap (min of its word 0x14 and 0x3F28; zero
 *         makes the game's routine retry the same instance for ever); its centre to camera space;
 *         dropped if its occlusion bits are clear, beyond its draw distance, behind the near plane
 *         or outside a side plane. A sphere crossing the frustum's edge is tested again with the
 *         class's bounding box (8 corners at +0xA0, through the instance's matrix) in clip space:
 *         dropped if all are outside one plane; else its slot's word 8 says whether it is inside
 *         the guard band (1) or needs the clip program (0). A kept instance's slot: its distance,
 *         depth and so on (16 bytes) and the chain address of its 6 qwords (word 0xC).
 *       - if any were kept: a NEXT tag over their 6-qword blocks, then the draw pass, per kept
 *         slot: its light list entry (its index, into 0x3600) unless its byte 0x1B is 0 and its
 *         half 0x1E is 0xFFFF; the class's nearest distance kept (vf17.x); its level of detail
 *         from its distance against the class's three (+0x10): LOD0 (near), LOD0 morphing to
 *         LOD1, LOD1 morphing to LOD2, LOD2 (far), counted in the class (+0x1C, +0x1D, +0x1E);
 *         the morph factor and fog in its block's last 2 qwords. An instance needing the clip
 *         program goes to the clip list (0x3A00, 16 bytes each, up to 0x3E00) and leaves its LOD
 *         list; the others get their matrix (the instance's, with the sway applied when the class
 *         has a sway mode and it is near enough) through the view-projection matrix in their
 *         block's first 4 qwords. The blocks go through the scratchpad in 4 KB halves, DMAed to
 *         the chain (the packet pointer) when a half fills.
 *       - per level of detail with instances: per packet of its table (+0/+4/+8, counts
 *         +0x20/+0x21/+0x22, 16 bytes each): REF tags to the packet's GS data, its vertices,
 *         its colours and an MSCAL; then per instance in the list REFs to its block and to its
 *         colours (the instance matrix + 0x40), double-buffered in VU memory (t9).
 *     A class's nearest distance (as an integer) goes into the texture-distance list (0x3200).
 *   - the end: a closing CNT; if the clip list has entries, a second chain part for them: the clip
 *     VU program's MPG and 9 constant qwords; each entry's matrix (sway too) through the clip-space
 *     matrix (camera + 0xC0), written straight into its block in the chain; the packets and REFs
 *     as above, per run of entries with the same packet table, with one more REF (bytes 0xC/0xD);
 *     a closing CNT. The light list closed (0xFFFF); the packet pointer saved.
 *
 * This file does the same, word for word: every store (scratchpad, chain, class counters, DMA
 * registers), every VU0 macro operation with its result and its MAC/status flags kept in the VU0
 * registers (other code may read what the routine leaves; the routine reads the MAC flags
 * itself), the VU's float arithmetic as the translated game code has it (each product and sum
 * clamped to +-MAX), in the routine's order. Where a GPR's upper 64 bits reach a VU register
 * (qmtc2) they are carried too (v0h, v1h, a0h, t3h). The data addresses differ per level:
 * tools/recomp.py passes the level's own (its gp offsets and its address pairs, read from the
 * level's bytes) to the hook it puts at the routine's start.
 *
 * UYA_NTIEPROC: 0 the game's routine (translated); 1 check (this file runs first, its results
 * are put aside, the game's routine runs from the same state, and the two are compared at its
 * end: scratchpad, chain, class counters, the packet pointer, the VU0 registers); 2 this file,
 * direct (default): a packet with a tie mesh is drawn by native_tie.c from the level data and the
 * routine's own results, a marker in the chain where its REFs and MSCALs were (the clip list's
 * part still goes into the chain); 3 this file, every draw into the chain (what check mode
 * compares). */
#include "rc.h"
#include "uya_host.h"
#include "native_tie.h"
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
    s32 g_cnt, g_inst, g_cap, g_pkt, g_clock, g_vf21, g_t[8], g_pkt2;   /* gp offsets */
    u32 classes, occl, sine, sway, ret1, cam, frust, mpg_qwc, mpg_addr, consts,
        ret_l0, ret_l1, ret_l2, mpg2_qwc, mpg2_addr, consts2, cam2, ret2;
} TieRefs;

/* ---- the VU0 macro operations, as tools/recomp.py translates them ---- */
enum { ADD, SUB, MUL, MADD, MSUB, MAX, MIN };
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
        case ADD: r[i] = fc(a + b); break;
        case SUB: r[i] = fc(a - b); break;
        case MUL: r[i] = fc(a * b); break;
        case MADD: r[i] = fc(VACC[i] + fc(a * b)); break;
        case MSUB: r[i] = fc(VACC[i] - fc(a * b)); break;
        case MAX: r[i] = fmaxf(a, b); break;
        default: r[i] = fminf(a, b); break;
        }
    }
    if (fd == 0) { if (op < MAX) rc_vu0_flags(r, m); return; }   /* vf0 stays; the flags are set */
    f32 *d = fd < 0 ? VACC : VF(fd);
    for (int i = 0; i < 4; i++) if (m & (8u >> i)) d[i] = r[i];
    if (op < MAX) rc_vu0_flags(d, m);
}
/* d = rows a, b, c by v's x, y, z (ACC, then the last into d) */
static void mat3(int d, u32 m, int a, int b, int c, int v)
{
    fm(ACC, m, MUL, a, v, 0); fm(ACC, m, MADD, b, v, 1); fm(d, m, MADD, c, v, 2);
}
/* d = rows a, b, c by v's x, y, z, + row t (by vf0.w) */
static void aff(int d, int a, int b, int c, int t, int v)
{
    fm(ACC, XYZW, MUL, a, v, 0); fm(ACC, XYZW, MADD, b, v, 1); fm(ACC, XYZW, MADD, c, v, 2); fm(d, XYZW, MADD, t, 0, 3);
}
static void clipw(int s)
{
    float w = __builtin_fabsf(VF(s)[3]), x = VF(s)[0], y = VF(s)[1], z = VF(s)[2];
    u32 fl = (x > w) | ((x < -w) << 1) | ((y > w) << 2) | ((y < -w) << 3) | ((z > w) << 4) | ((z < -w) << 5);
    rc_vu0.clip = ((rc_vu0.clip << 6) | fl) & 0xFFFFFF;
}
static u32 and4(u32 v) { return v & (v >> 6) & ((v >> 12) & (v >> 18)); }
static void lqc2(int d, u32 a) { memcpy(VF(d), (void *)RC_QA(a), 16); }
static void sqc2(int s, u32 a) { memcpy((void *)RC_QA(a), VF(s), 16); }
/* qmtc2 from a GPR: its low doubleword and its upper one */
static void qmtc2(int d, u64 lo, u64 hi) { u64 w[2] = { lo, hi }; memcpy(VF(d), w, 16); }
static u64 hi_of(int s) { u64 w[2]; memcpy(w, VF(s), 16); return w[1]; }
static void itof12(int d) { for (int k = 0; k < 4; k++) VF(d)[k] = rc_itof(*(s32 *)&VF(d)[k], 12); }

/* ---- the chain ---- */
static int busy(u32 ch) { return (M32(ch) & 0x100) != 0; }
static void wait_idle(u32 ch) { for (int k = 0; k < 1000000 && busy(ch); k++) ; }
static void sq_(u32 a, const R128 *v) { rc_sq(v, a); }
static void sb_(u32 a, u32 v) { M8(a) = (u8)v; }
static void sh_(u32 a, u32 v) { M16(a) = (u16)v; }
static const R128 ZQ = {{ 0, 0 }};
/* a 4 KB half of chain words filled: to the chain at fp */
static void flush(u32 *sp, u32 fp)
{
    u32 at = *sp & 0xFFF;
    u32 base = *sp - at;
    wait_idle(0x1000D000u);
    rc_sw(0x1000D080u, base);
    rc_sw(0x1000D010u, fp - at);
    rc_sw(0x1000D020u, at >> 4);
    rc_sw(0x1000D000u, 0x100);
    *sp = (*sp - at) ^ 0x1000;
}
/* a REF to an MPG packet (qwc at q, data after it) and n constant qwords */
static void emit_start(u32 qwc_at, u32 data, u32 consts, int n, u32 *sp, u32 *fp)
{
    sq_(*sp, &ZQ);
    u32 v0 = M32(qwc_at);
    rc_sw(*sp + 4, data); rc_sw(*sp, 0x30000000u); *fp += 0x10; sh_(*sp, v0); *sp += 0x10;
    R128 v[9];
    for (int k = 0; k < n; k++) rc_lq(&v[k], consts + 16 * (u32)k);
    for (int k = 0; k < n; k++) sq_(*sp + 16 * (u32)k, &v[k]);
    *sp += 16 * (u32)n; *fp += 16 * (u32)n;
}
/* ---- the chain start as a record (direct mode; UYA_TSTARTREC: 0 the chain, 1 check, 2 records) ----
 * The start's 6 chain qwords (from the level's constants) are, in the game's template: REF 9 (VIF
 * STMOD 0, UNPACK V4-32 9 at VU 966), CNT 0 (MSCAL 0x40), CNT 2 (STROW + the row; BASE 0, OFFSET 0x2C,
 * STMOD 0, STCYCL 4/4), CNT 0 (FLUSHA), after a REF to the program's MPG packet. With that template,
 * one marker takes their place, and at the marker the record does the same, in the same order: the
 * MPG (the packet's MPGs), STMOD, the unpack (the REF's data, read when the chain is walked, as the
 * REF would), the start (TOP, the double buffer; the program at 0x40 only loads registers the direct
 * ties do not use), STROW, BASE, OFFSET, STMOD, STCYCL. Check mode applies the record, then the
 * original qwords through the VIF from the same state, and compares VIF1's state and VU1 data memory. */
void vu_mpg(int vu, u32 addr, const u32 *w, u32 ninstr);
#define TS_N 64
static struct { u32 q[36]; u32 mpg_addr, mpg_qwc, clip; } ts_rec[TS_N];
static u32 ts_next;
static int ts_mode = -1;
static unsigned long long ts_n, ts_bad, ts_mpgbad;
static int start_template_ok(u32 c)
{
    return M32(c) == 0x30000009u && M32(c + 8) == 0x05000000u && M32(c + 12) == 0x6C0903C6u &&
           M32(c + 0x10) == 0x10000000u && M32(c + 0x18) == 0x15000008u && M32(c + 0x1C) == 0 &&
           M32(c + 0x20) == 0x10000002u && M32(c + 0x28) == 0 && M32(c + 0x2C) == 0x30000000u &&
           M32(c + 0x40) == 0x03000000u && M32(c + 0x44) == 0x0200002Cu && M32(c + 0x48) == 0x05000000u && M32(c + 0x4C) == 0x01000404u &&
           M32(c + 0x50) == 0x10000000u && M32(c + 0x58) == 0x13000000u && M32(c + 0x5C) == 0;
}
static void emit_start_rec(u32 qwc_at, u32 data, u32 consts, u32 *sp, u32 *fp)
{
    u32 i = ts_next++ % TS_N;
    for (int k = 0; k < 24; k++) ts_rec[i].q[k] = M32(consts + 4 * (u32)k);
    ts_rec[i].mpg_addr = data; ts_rec[i].mpg_qwc = M16(qwc_at); ts_rec[i].clip = 0;
    sq_(*sp, &ZQ);
    rc_sw(*sp, 0x10000000u); rc_sw(*sp + 8, NATIVE_TSTART_MARK); rc_sw(*sp + 12, i);
    *sp += 0x10; *fp += 0x10;
}
/* the MPG packet's uploads (FLUSH, FLUSHE, FLUSHA, NOP codes and MPGs only; 0 if it has anything else) */
static int apply_mpg(u32 addr, u32 qwc)
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
/* the clip part's start (9 qwords: CNT 2 with VIF DIRECT 2 and its A+D qwords; REF 10 (STMOD 0, UNPACK
 * V4-32 10 at VU 966); CNT 0 (MSCAL 0x40); CNT 2 (STROW + row; BASE 0, OFFSET 0x2C, STMOD 0, STCYCL 4/4);
 * CNT 0 (FLUSHA)), as a record when the clip part goes direct (the GPU renderer): the program's 0x40
 * (224979) still runs, at the marker: it leaves registers and VU memory other programs read */
static int cstart_template_ok(u32 c)
{
    return M32(c) == 0x10000002u && M32(c + 8) == 0 && M32(c + 12) == 0x50000002u &&
           M32(c + 0x30) == 0x3000000Au && M32(c + 0x38) == 0x05000000u && M32(c + 0x3C) == 0x6C0A03C6u &&
           M32(c + 0x40) == 0x10000000u && M32(c + 0x48) == 0x15000008u && M32(c + 0x4C) == 0 &&
           M32(c + 0x50) == 0x10000002u && M32(c + 0x58) == 0 && M32(c + 0x5C) == 0x30000000u &&
           M32(c + 0x70) == 0x03000000u && M32(c + 0x74) == 0x0200002Cu && M32(c + 0x78) == 0x05000000u && M32(c + 0x7C) == 0x01000404u &&
           M32(c + 0x80) == 0x10000000u && M32(c + 0x88) == 0x13000000u && M32(c + 0x8C) == 0;
}
static void emit_cstart_rec(u32 qwc_at, u32 data, u32 consts, u32 *sp, u32 *fp)
{
    u32 i = ts_next++ % TS_N;
    for (int k = 0; k < 36; k++) ts_rec[i].q[k] = M32(consts + 4 * (u32)k);
    ts_rec[i].mpg_addr = data; ts_rec[i].mpg_qwc = M16(qwc_at); ts_rec[i].clip = 1;
    sq_(*sp, &ZQ);
    rc_sw(*sp, 0x10000000u); rc_sw(*sp + 8, NATIVE_TSTART_MARK); rc_sw(*sp + 12, i);
    *sp += 0x10; *fp += 0x10;
}
void native_gif(int path, const u32 *d, u32 qwc);
static void apply_cstart(u32 i)
{
    const u32 *c = ts_rec[i].q;
    if (!apply_mpg(ts_rec[i].mpg_addr, ts_rec[i].mpg_qwc)) { if (ts_mpgbad++ < 3) uya_log("native TieProc: the clip start's MPG packet at %08X has other codes", ts_rec[i].mpg_addr); }
    { extern const char *uya_gs_src; uya_gs_src = "PATH2 (VIF1 DIRECT)"; native_gif(1, c + 4, 2); }   /* DIRECT 2 */
    uya_vif1_op_reg(c[14]);                                          /* STMOD 0 */
    uya_vif1_op_unpack(c[15], (const u32 *)RC_A(c[13]), 10 * 4);     /* VU 966..975 from the REF's data */
    uya_vif1_op_mscal((c[18] & 0xFFFF) * 8);                         /* MSCAL 0x40: the clip program's setup */
    uya_vif1_op_strow(c + 24);                                       /* STROW */
    uya_vif1_op_reg(c[28]); uya_vif1_op_reg(c[29]); uya_vif1_op_reg(c[30]); uya_vif1_op_reg(c[31]);   /* BASE, OFFSET, STMOD, STCYCL */
}
static void apply_start(u32 i)
{
    if (ts_rec[i].clip) { apply_cstart(i); return; }
    const u32 *c = ts_rec[i].q;
    if (!apply_mpg(ts_rec[i].mpg_addr, ts_rec[i].mpg_qwc)) { if (ts_mpgbad++ < 3) uya_log("native TieProc: the start's MPG packet at %08X has other codes", ts_rec[i].mpg_addr); }
    uya_vif1_op_reg(c[2]);                                           /* STMOD 0 */
    uya_vif1_op_unpack(c[3], (const u32 *)RC_A(c[1]), 9 * 4);        /* VU 966..974 from the REF's data */
    uya_vif1_op_mscal((c[6] & 0xFFFF) * 8);                          /* MSCAL 0x40 */
    uya_vif1_op_strow(c + 12);                                       /* STROW */
    uya_vif1_op_reg(c[16]); uya_vif1_op_reg(c[17]); uya_vif1_op_reg(c[18]); uya_vif1_op_reg(c[19]);   /* BASE, OFFSET, STMOD, STCYCL */
}
void uya_dma_vif1_replay(const u32 *q, int tte);
int native_tie_start_tag(const u32 *q)
{
    u32 i = q[3] % TS_N;
    if (ts_mode != 1) { apply_start(i); ts_n++; return 1; }
    /* check: the record, then the chain's qwords from the same state; the chain's result stays */
    static u8 m0[0x4000], ma[0x4000]; u8 *vu = (u8 *)(uintptr_t)0x1100C000u;
    UyaVifState s0, sa, sb;
    uya_vif1_get_state(&s0); memcpy(m0, vu, sizeof m0);
    apply_start(i);
    uya_vif1_get_state(&sa); memcpy(ma, vu, sizeof ma);
    uya_vif1_set_state(&s0); memcpy(vu, m0, sizeof m0);
    const u32 *c = ts_rec[i].q;
    u32 mq[4] = { 0x30000000u | ts_rec[i].mpg_qwc, ts_rec[i].mpg_addr, 0, 0 };
    uya_dma_vif1_replay(mq, 1);
    if (ts_rec[i].clip) { uya_dma_vif1_replay(c, 1); uya_dma_vif1_replay(c + 12, 1); uya_dma_vif1_replay(c + 16, 1); uya_dma_vif1_replay(c + 20, 1); uya_dma_vif1_replay(c + 32, 1); }
    else { uya_dma_vif1_replay(c, 1); uya_dma_vif1_replay(c + 4, 1); uya_dma_vif1_replay(c + 8, 1); uya_dma_vif1_replay(c + 20, 1); }
    uya_vif1_get_state(&sb);
    ts_n++;
    sa.need = sb.need;
    int dv = memcmp(&sa, &sb, sizeof sa) != 0, dm = memcmp(ma, vu, sizeof ma) != 0;
    if (dv || dm) {
        if (ts_bad++ < 10) {
            u32 q0 = 0; while (q0 < 0x400 && !memcmp(ma + q0 * 16, vu + q0 * 16, 16)) q0++;
            uya_log("native TieProc start check: run %llu differs: VIF state %s (TOPS %03X/%03X TOP %03X/%03X DBF %u/%u), VU memory %s (first qword %03X)",
                    ts_n, dv ? "differs" : "same", sa.tops, sb.tops, sa.top, sb.top, sa.dbf, sb.dbf, dm ? "differs" : "same", q0);
        }
    }
    if (ts_n % 1000 == 0) uya_log("native TieProc start check: %llu starts, %llu differ", ts_n, ts_bad);
    return 1;
}

/* a CNT with a VIF FLUSHA, closing a part */
static void emit_close(u32 *sp, u32 *fp)
{
    sq_(*sp, &ZQ); rc_sw(*sp, 0x10000000u); rc_sw(*sp + 8, 0x13000000u); *sp += 0x10; *fp += 0x10;
}
/* one packet of a LOD's table (s4): REFs to its GS data (5 qwords each, relocated by base), its
 * vertices, (the clip part: one more), its colours, an MSCAL, its two strip lists; v1 = the VU
 * address of the next unpack */
static void emit_packet(const R128 *T, u32 s4, u32 a3, u32 base, int clip, u32 *sp, u32 *fp)
{
    u32 at = M8(s4 + 4), a0 = a3 + 0x10;
    sq_(*sp, &T[0]); *fp += 0x10; rc_sw(*sp + 4, a3); *sp += 0x10;
    u32 v1 = 1;
    do {
        u32 a1 = M32(a0); a0 += 4;
        sq_(*sp, &T[0]); sb_(*sp, 5); a1 += base; sb_(*sp + 0xE, 5); *fp += 0x10; sh_(*sp + 0xC, v1); v1 += 5;
        rc_sw(*sp + 4, a1); at -= 1; *sp += 0x10;
    } while (at != 0);
    u32 v0 = M8(s4 + 7);
    sq_(*sp, &T[1]); v1 += 0x4000; rc_sw(*sp + 4, a3 + 0x20); sb_(*sp, v0); sb_(*sp + 0xE, v0 << 2); *fp += 0x10; sh_(*sp + 0xC, v1); *sp += 0x10;
    if (clip) {
        at = M8(s4 + 0xC); v0 = M8(s4 + 0xD); *fp += 0x10;
        sq_(*sp, &T[7]); sb_(*sp, v0); sb_(*sp + 0xE, v0 << 2); rc_sw(*sp + 4, (at << 4) + a3); *sp += 0x10;
    }
    at = M8(s4 + 8); v0 = M8(s4 + 9); *fp += 0x10;
    sq_(*sp, &T[2]); sb_(*sp, v0); sb_(*sp + 0xE, v0 << 1); rc_sw(*sp + 4, (at << 4) + a3); *sp += 0x10;
    sq_(*sp, &ZQ); rc_sw(*sp, 0x10000000u); rc_sw(*sp + 0xC, 0x14000006u); *fp += 0x10; *sp += 0x10;
    v0 = M8(s4 + 0xB); *fp += 0x20; at = M8(s4 + 0xA);
    sq_(*sp, &T[3]); v1 = (v0 + 3) >> 2; sq_(*sp + 0x10, &T[3]);
    sb_(*sp + 0xE, v0); at = (at << 4) + a3; sb_(*sp + 0x1E, v0); sb_(*sp, v1); sb_(*sp + 0x10, v1);
    rc_sw(*sp + 4, at); at += v1 << 4; rc_sw(*sp + 0x14, at); sh_(*sp + 0x1C, 0x40F8); *sp += 0x20;
}
/* an instance's draw: REFs to its block (blk) and its colours (mtx + 0x40), VU buffer t9 */
static int emit_inst(const R128 *T, u32 blk, u32 mtx, u32 *t9, u32 *sp, u32 *fp)
{
    sq_(*sp, &T[4]); rc_sw(*sp + 4, blk); *fp += 0x30;
    sq_(*sp + 0x10, &T[5]); rc_sw(*sp + 0x14, mtx + 0x40); sh_(*sp + 0x1C, *t9 + 0x4346);
    int full = (*sp & 0xE00) == 0xE00;
    sq_(*sp + 0x20, &T[6]); *sp += 0x30;
    *t9 ^= 0x40;
    return full;
}

/* ---- direct: a packet with a mesh goes to native_tie.c, a marker in the chain where a run of
 * them starts (no REFs, no MSCALs: t9, which follows the VU's buffers, moves only for the chain's
 * instances) ---- */
static int direct, seg_open;
static unsigned long long st_direct, st_chain, st_cdirect, st_cchain;
static void mark(u32 *sp, u32 *fp)
{
    u32 w[4] = { 0x10000000u, 0, NATIVE_TIE_MARK, native_tie_seg_begin() };
    memcpy((void *)(uintptr_t)*sp, w, 16);
    *sp += 0x10; *fp += 0x10;
    seg_open = 1;
}
/* the chain's templates and constants as the direct draws read them (native_tie.c): else the chain */
static int direct_ok(const R128 *T, const TieRefs *R, u32 gp)
{
    static const u32 want[7][2] = { { 0x05000000u, 0x6C010000u }, { 0x14000004u, 0x6E004000u }, { 0x00000000u, 0x6D000032u },
        { 0x05000000u, 0x6E0040CCu }, { 0x00000000u, 0x6C0680C6u }, { 0x05000001u, 0x6E404346u }, { 0x05000000u, 0x14000000u } };
    int ok = 1;
    for (int k = 0; k < 7; k++) ok &= T[k].w[2] == want[k][0] && T[k].w[3] == want[k][1] && (T[k].w[0] >> 28) == (k == 6 ? 1u : 3u);
    u32 c = R->consts;
    ok &= M32(c) == 0x30000009u && M32(c + 8) == 0x05000000u && M32(c + 12) == 0x6C0903C6u && M32(c + 0x2C) == 0x30000000u &&
          M32(c + 0x40) == 0x03000000u && M32(c + 0x44) == 0x0200002Cu && M32(c + 0x4C) == 0x01000404u;
    if (!ok) { static int told; if (!told++) uya_log("native TieProc: the chain's templates are not as expected: the draws go into the chain"); return 0; }
    u32 k966[36], row[4];
    memcpy(k966, (void *)(uintptr_t)M32(c + 4), sizeof k966);
    memcpy(row, (void *)(uintptr_t)(c + 0x30), sizeof row);
    u32 sig = R->classes * 31u + M32(gp + (u32)R->g_inst) * 7u + M32(R->classes);
    native_tie_direct_frame(sig, k966, row);
    return 1;
}

/* path counts (logged by check mode; a path at 0 is untested) */
static unsigned long long st_inst, st_retry, st_occl, st_far, st_near, st_side, st_cross, st_box, st_guard,
    st_lod[4], st_clip, st_clipfull, st_sway, st_swayfar, st_pass2, st_skip;

/* the sway (the class's sway mode in at, the matrix's address in a3): rows vf1..vf3 bent by the
 * sine table, by the instance's distance (vf4) and its address; returns to the caller (the game's
 * routine jumps to 0x3F24's address) */
static void sway(u32 at, u32 a3, u64 a0h)
{
    lqc2(9, SPR + 0x3E90); lqc2(8, SPR + 0x3E80);
    sqc2(12, SPR + 0x3EB0);
    fm(11, X, ADD, 0, 0, 3);
    u32 a1 = M32(SPR + 0x3EA0);
    at >>= 1;
    fm(12, XYZ, MUL, 4, 4, BV);
    u32 a2 = SPR | 0x3500;
    fm(ACC, X, ADD, 12, 12, 1);
    fm(12, X, MADD, 11, 12, 2);
    fm(0, Y, SUB, 9, 12, 0);              /* vf9.y - distance^2: its sign in the MAC flags */
    u32 a0 = rc_vu0.mac;
    fm(12, X, MUL, 12, 9, 2);
    a0 &= 0x40;
    fm(12, W, SUB, 0, 12, 0);
    st_sway++;
    if (a0 == 0) {
        fm(5, XYZW, MUL, 0, 0, 0);
        a3 = (a3 >> 9) & 0x1FF;
        fm(6, XYZW, MUL, 0, 0, 0);
        { f32 t[4] = { VF(0)[1], VF(0)[2], VF(0)[3], VF(0)[0] }; memcpy(VF(7), t, 16); }   /* vmr32 */
        fm(5, X, ADD, 5, 0, 3);
        u32 v0 = a3 << 6;
        fm(6, Y, ADD, 6, 0, 3);
        v0 = v0 + (a3 << 1) + a3;
        u32 v1 = (a3 << 7) - (a3 << 2) - a3;
        v0 += a1;
        v1 += a1;
        if (at) v1 += a1;
        s32 s;
        s = (s32)((u32)(s32)(s8)M8(a2 + (v0 & 0xFF)) << 5); qmtc2(10, (u64)(s64)s, a0h); itof12(10);
        s = (s32)((u32)(s32)(s8)M8(a2 + ((v0 >> 1) & 0xFF)) << 5); qmtc2(11, (u64)(s64)s, a0h); itof12(11);
        fm(7, X, MUL, 10, 11, BV);
        u32 a4 = v1 + 0x40;
        fm(7, W, SUB, 0, 9, 0);
        fm(7, X, MUL, 7, 9, 0);
        fm(7, X, ADD, 7, 7, 3);
        s = (s32)((u32)(s32)(s8)M8(a2 + (a4 & 0xFF)) << 5); qmtc2(10, (u64)(s64)s, a0h); itof12(10);
        s = (s32)((u32)(s32)(s8)M8(a2 + ((v1 >> 1) & 0xFF)) << 5); qmtc2(11, (u64)(s64)s, a0h); itof12(11);
        fm(7, X, MUL, 7, 8, 0);
        fm(ACC, X, MUL, 10, 8, 1);
        fm(10, X, MADD, 7, 8, 2);
        fm(10, X, MUL, 10, 12, 3);
        fm(ACC, X, MUL, 11, 8, 1);
        fm(11, X, MADD, 7, 8, 3);
        fm(11, X, MUL, 11, 12, 3);
        if (!at) { fm(10, X, MUL, 10, 9, 3); fm(11, X, MUL, 11, 9, 3); }
        fm(7, X, ADD, 0, 10, 0);
        fm(7, Y, ADD, 0, 11, 0);
        mat3(1, XYZ, 5, 6, 7, 1);
        mat3(2, XYZ, 5, 6, 7, 2);
        mat3(3, XYZ, 5, 6, 7, 3);
    } else st_swayfar++;
    lqc2(12, SPR + 0x3EB0);
}

static void run(RcCtx *c, const TieRefs *R)
{
    const u32 gp = (u32)c->r[28].d[0];
    u64 r1hi = c->r[1].d[1], t3h = c->r[11].d[1];
    /* the registers it saves */
    rc_sd(SPR + 0x3FF8, c->r[31].d[0]); rc_sd(SPR + 0x3FF0, c->r[30].d[0]); rc_sd(SPR + 0x3FE8, c->r[29].d[0]);
    for (int k = 23; k >= 16; k--) rc_sd(SPR + 0x3FA8 + 8 * (u32)(k - 16), c->r[k].d[0]);
    {
        u32 n = M32(gp + (u32)R->g_cnt), ib = M32(gp + (u32)R->g_inst), cap = M32(gp + (u32)R->g_cap);
        rc_sw(SPR + 0x3F00, n); rc_sw(SPR + 0x3F04, R->classes); rc_sw(SPR + 0x3F08, SPR + 0x3A00);
        rc_sw(SPR + 0x3F14, SPR + 0x3200); rc_sw(SPR + 0x3F18, SPR + 0x3600); rc_sw(SPR + 0x3F1C, ib); rc_sw(SPR + 0x3F28, cap);
    }
    u32 fp = M32(gp + (u32)R->g_pkt), sp = SPR, t9 = 0;
    for (u32 a = SPR + 0x2000; a < SPR + 0x3000; a += 0x10) sq_(a, &ZQ);
    for (u32 k = 0; k < 0x80; k += 16) { R128 v; rc_lq(&v, R->occl + k); sq_(SPR + 0x3000 + k, &v); }
    for (u32 k = 0; k < 0x100; k += 16) { R128 v; rc_lq(&v, R->sine + k); sq_(SPR + 0x3500 + k, &v); }
    for (u32 k = 0; k < 0x20; k += 16) { R128 v; rc_lq(&v, R->sway + k); sq_(SPR + 0x3E80 + k, &v); }
    rc_sw(SPR + 0x3EA0, M32(gp + (u32)R->g_clock));
    rc_sw(SPR + 0x3F24, R->ret1);
    /* the camera */
    qmtc2(1, 0x44800000u, M64(R->sway + 8));
    lqc2(24, R->cam + 0x140); lqc2(25, R->cam); lqc2(26, R->cam + 0x10); lqc2(27, R->cam + 0x20);
    lqc2(28, R->cam + 0x80); lqc2(29, R->cam + 0x90); lqc2(30, R->cam + 0xA0); lqc2(31, R->cam + 0xB0);
    for (int k = 0; k < 4; k++) lqc2(12 + k, R->cam + 0x100 + 16 * (u32)k);
    fm(24, W, MUL, 0, 1, 0);
    lqc2(18, R->frust + 0x220); lqc2(19, R->frust + 0xA0); lqc2(20, R->frust + 0xB0); lqc2(22, R->frust + 0x1E0); lqc2(23, R->frust + 0x1F0);
    qmtc2(1, 0x3A800000u, r1hi);
    fm(19, XY, MUL, 19, 1, 0); fm(23, XY, MUL, 23, 1, 0);
    lqc2(21, gp + (u32)R->g_vf21);
    int dok;
    { R128 v[8]; for (int k = 0; k < 8; k++) rc_lq(&v[k], gp + (u32)R->g_t[k]); for (int k = 0; k < 8; k++) sq_(SPR + 0x3E00 + 16 * (u32)k, &v[k]); dok = direct && direct_ok(v, R, gp); }
    seg_open = 0;
    {   /* testing: UYA_TSTARTLOG=1: the chain start's words, once */
        static int left = -1; if (left < 0) left = getenv("UYA_TSTARTLOG") ? 1 : 0;
        if (left > 0) {
            left--;
            uya_log("TieProc start: MPG qwc %u at %08X; consts at %08X:", M16(R->mpg_qwc), R->mpg_addr, R->consts);
            for (int k = 0; k < 6; k++) uya_log("   %08X %08X %08X %08X", M32(R->consts + 16 * k), M32(R->consts + 16 * k + 4), M32(R->consts + 16 * k + 8), M32(R->consts + 16 * k + 12));
            u32 ra = M32(R->consts + 4);
            for (int k = 0; k < 9; k++) uya_log("   ref %08X: %08X %08X %08X %08X", ra + 16 * k, M32(ra + 16 * k), M32(ra + 16 * k + 4), M32(ra + 16 * k + 8), M32(ra + 16 * k + 12));
            if (R->consts2) for (int k = 0; k < 9; k++) uya_log("   clip consts %d: %08X %08X %08X %08X", k, M32(R->consts2 + 16 * k), M32(R->consts2 + 16 * k + 4), M32(R->consts2 + 16 * k + 8), M32(R->consts2 + 16 * k + 12));
        }
    }
    if (ts_mode < 0) { const char *e = getenv("UYA_TSTARTREC"); ts_mode = e ? atoi(e) : 2; }
    if (dok && ts_mode > 0 && start_template_ok(R->consts)) emit_start_rec(R->mpg_qwc, R->mpg_addr, R->consts, &sp, &fp);
    else emit_start(R->mpg_qwc, R->mpg_addr, R->consts, 6, &sp, &fp);
    /* the upper halves of v0, v1, a0 (the last 128-bit loads into them: the constants' copy) */
    u64 v0h = M64(R->consts + 8), v1h = M64(R->consts + 0x18), a0h = M64(R->consts + 0x28);

    /* the classes */
    for (;;) {
        u32 n = M32(SPR + 0x3F00), tp = M32(SPR + 0x3F04);
        if (n == 0) break;
        u32 t8 = M32(tp);
        rc_sw(SPR + 0x3F00, n - 1); rc_sw(SPR + 0x3F04, tp + 4);
        u32 fl = M16(t8 + 0x24);
        qmtc2(17, 0x50000000u, v1h);
        u32 s6 = M32(t8 + 0x28), s7 = M16(t8 + 0x26);
        lqc2(16, t8 + 0x10);
        if (fl & 9) st_skip++;
        else {
            rc_sw(t8 + 0x1C, 0); rc_sw(SPR + 0x3F20, (fl & 6) >> 1);
            /* the instances, 128 at a time */
            while (s7 != 0) {
                u32 t3 = M32(SPR + 0x3F28);
                rc_sw(0x1000D480u, 0x2000);
                u32 nb = (s32)s7 < 0x80 ? s7 : 0x80;
                rc_sw(0x1000D410u, s6); rc_sw(0x1000D420u, nb << 1); rc_sw(0x1000D400u, 0x100);
                s7 -= nb;
                u32 s4 = SPR + 0x2000, s5 = nb << 5;
                rc_sw(SPR + 0x3F0C, s6);
                s6 += s5; s5 += s4; s4 -= 0x20;
                u32 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
                u32 t4 = M8(t8 + 0x1C), t5 = M8(t8 + 0x1D), t6 = M16(t8 + 0x1E);
                /* the cull pass */
                for (;;) {
                    u32 w14 = M32(s4 + 0x34);
                    s4 += 0x20;
                    lqc2(1, s4);
                    if (s4 == s5) break;
                    st_inst++;
                    /* pminw v0, v0, t3 */
                    s32 m0 = (s32)w14 < (s32)t3 ? (s32)w14 : (s32)t3;
                    {
                        s32 a = (s32)(u32)v0h, b = (s32)(u32)(v0h >> 32), x = (s32)(u32)t3h, y = (s32)(u32)(t3h >> 32);
                        v0h = (u64)(u32)(a < x ? a : x) | (u64)(u32)(b < y ? b : y) << 32;
                    }
                    u32 ob = M8(s4 + 0x19);
                    fm(2, XYZ, SUB, 1, 24, BV);
                    u32 om = M8(s4 + 0x18);
                    fm(3, X, ADD, 0, 1, 3);
                    qmtc2(4, (u64)(u32)m0 << 32, v0h);
                    fm(3, Y, SUB, 0, 1, 3);
                    if (m0 == 0) { st_retry++; s4 -= 0x20; continue; }   /* (the game's routine: for ever) */
                    fm(ACC, XYZ, MUL, 25, 2, 0);
                    fm(ACC, XYZ, MADD, 26, 2, 1);
                    M64(s4) = 0;
                    fm(2, XYZ, MADD, 27, 2, 2);
                    rc_sw(s4 + 0x14, 0);
                    fm(19, Y, SUB, 4, 1, 3);
                    u32 ov = M8(SPR + ob + 0x3000);
                    fm(8, XY, MUL, 22, 1, 3);
                    fm(1, XY, ADD, 3, 2, 2);
                    om &= ov;
                    fm(5, XY, MUL, 20, 2, 2);
                    if (om == 0) { st_occl++; continue; }
                    VF(2)[0] = __builtin_fabsf(VF(2)[0]); VF(2)[1] = __builtin_fabsf(VF(2)[1]);
                    fm(3, XY, SUB, 19, 1, BV);
                    fm(6, XY, SUB, 2, 8, BV);
                    fm(8, XY, ADD, 2, 8, BV);
                    fm(4, XY, SUB, 1, 23, BV);
                    fm(1, Z, MAX, 0, 2, BV);
                    u32 f3x = VW(3, 0), f3y = VW(3, 1);
                    fm(7, XY, SUB, 5, 6, BV);
                    fm(8, XY, SUB, 5, 8, BV);
                    u32 f4x = VW(4, 0), f4y = VW(4, 1);
                    a0h = hi_of(4);
                    if (f3y & 0x80000000u) { st_far++; continue; }                /* beyond its draw distance */
                    { f32 y = VF(1)[1], z = VF(1)[2]; VF(1)[0] = y; VF(1)[1] = z; }
                    if (!(f3x & 0x80000000u)) { st_near++; continue; }           /* behind the near plane */
                    u32 a2 = fp + s3;
                    u32 f7x = VW(7, 0), f7y = VW(7, 1);
                    v0h = hi_of(7);
                    u32 f8x = VW(8, 0), f8y = VW(8, 1);
                    v1h = hi_of(8);
                    if (f7y & 0x80000000u) { st_side++; continue; }              /* outside a side plane */
                    if (f7x & 0x80000000u) { st_side++; continue; }
                    int crossing = ((f8y | f4y) & 0x80000000u) || ((f8x | (f4x ^ 0x80000000u)) & 0x80000000u);
                    if (!crossing) {
                        sqc2(1, s4); s3 += 0x60; rc_sw(s4 + 0xC, a2 + 0x10);
                        continue;
                    }
                    /* crossing the frustum's edge: the class's box through the instance's matrix */
                    st_cross++;
                    u32 t7 = M32(s4 + 0x10);
                    fm(9, XYZW, MUL, 1, 0, 3);
                    lqc2(4, t7 + 0x30); lqc2(1, t7); lqc2(2, t7 + 0x10); lqc2(3, t7 + 0x20);
                    lqc2(5, t8 + 0xA0); lqc2(6, t8 + 0xB0);
                    fm(1, XYZ, MUL, 1, 4, 3); fm(2, XYZ, MUL, 2, 4, 3); fm(3, XYZ, MUL, 3, 4, 3);
                    fm(4, XYZ, SUB, 4, 24, BV);
                    mat3(1, XYZW, 12, 13, 14, 1);
                    fm(4, XYZ, MUL, 4, 24, 3);
                    mat3(2, XYZW, 12, 13, 14, 2);
                    lqc2(7, t8 + 0xC0);
                    mat3(3, XYZW, 12, 13, 14, 3);
                    lqc2(8, t8 + 0xD0);
                    aff(4, 12, 13, 14, 15, 4);
                    aff(5, 1, 2, 3, 4, 5);
                    aff(6, 1, 2, 3, 4, 6);
                    clipw(5);
                    aff(7, 1, 2, 3, 4, 7); clipw(6);
                    aff(8, 1, 2, 3, 4, 8); clipw(7);
                    fm(5, XY, MUL, 5, 21, BV); fm(6, XY, MUL, 6, 21, BV); clipw(8); fm(7, XY, MUL, 7, 21, BV); fm(8, XY, MUL, 8, 21, BV);
                    lqc2(10, t8 + 0xE0); lqc2(11, t8 + 0xF0);
                    u32 raw0 = rc_vu0.clip;
                    clipw(5); clipw(6); clipw(7); clipw(8);
                    aff(5, 1, 2, 3, 4, 10);
                    u32 sc0 = rc_vu0.clip;
                    lqc2(7, t8 + 0x100); lqc2(8, t8 + 0x110);
                    aff(6, 1, 2, 3, 4, 11); clipw(5);
                    aff(7, 1, 2, 3, 4, 7); clipw(6);
                    aff(8, 1, 2, 3, 4, 8); clipw(7);
                    fm(5, XY, MUL, 5, 21, BV); fm(6, XY, MUL, 6, 21, BV); clipw(8); fm(7, XY, MUL, 7, 21, BV); fm(8, XY, MUL, 8, 21, BV);
                    u32 raw1 = rc_vu0.clip;
                    clipw(5); clipw(6); clipw(7); clipw(8);
                    u32 sc1 = rc_vu0.clip;
                    if (and4(raw0) & and4(raw1) & 0x3F) { st_box++; continue; } /* all eight outside one plane */
                    sqc2(9, s4);
                    u32 inside = (sc0 | sc1) == 0;
                    st_guard += inside;
                    rc_sw(s4 + 8, inside); s3 += 0x60; rc_sw(s4 + 0xC, fp + (s3 - 0x60) + 0x10);
                }
                if (s3 == 0) continue;
                /* a NEXT over the blocks, then the draw pass */
                s4 = SPR + 0x1FE0;
                fp += 0x10;
                sq_(sp, &ZQ); rc_sw(sp, 0x20000000u); rc_sw(sp + 4, fp + s3); sp += 0x10;
                seg_open = 0;
                for (;;) {
                    u64 w01 = M64(s4 + 0x20);
                    s4 += 0x20;
                    lqc2(1, s4);
                    if (s4 == s5) break;
                    u32 t7 = M32(s4 + 0x10);
                    if (w01 == 0) continue;
                    fm(2, XYZ, SUB, 16, 1, 1);
                    u32 b1b = M8(s4 + 0x1B);
                    fm(3, X, MUL, 1, 24, 3);
                    u32 h1e = M16(s4 + 0x1E);
                    fm(ACC, Y, ADD, 18, 0, BV);
                    fm(4, Y, MADD, 1, 18, 0);
                    if (b1b != 0 || h1e != 0xFFFF) {
                        /* the light list */
                        u32 idx = (M32(SPR + 0x3F0C) + (s4 & 0xFFF) - M32(SPR + 0x3F1C)) >> 5;
                        u32 lp = M32(SPR + 0x3F18);
                        if (lp != SPR + 0x39FE) { sh_(lp, idx); rc_sw(SPR + 0x3F18, lp + 2); }
                    }
                    u32 f2x = VW(2, 0), f2y = VW(2, 1), f2z = VW(2, 2), f2w = VW(2, 3);
                    v0h = f2z | (u64)f2w << 32;
                    fm(17, X, MIN, 3, 17, BV);
                    v1h = f2x | (u64)f2w << 32;     /* prot3w v1, v0 */
                    /* the level of detail */
                    int lod = !(f2x >> 31) ? 0 : (f2z >> 31) ? 3 : !(f2y >> 31) ? 1 : 2;
                    st_lod[lod]++;
                    u32 t2, tab, cnt, *head;
                    if (lod == 1 || lod == 2) {
                        /* morphing: LOD0 to LOD1 (1), LOD1 to LOD2 (2) */
                        if (lod == 2) {
                            fm(3, Z, SUB, 16, 16, 1); t5++; fm(3, Y, SUB, 0, 2, BV);
                            fm(4, Y, MIN, 4, 18, 2);
                            rc_sw(s4 + 0x1C, s1); s1 = s4; head = &s1;
                            rc_vu0.q = fc(rc_fdiv(VF(3)[1], VF(3)[2]));
                            tab = M32(t8 + 4); cnt = M8(t8 + 0x21);
                        } else {
                            fm(3, Y, SUB, 16, 16, 0); t4++; fm(3, X, SUB, 0, 2, BV);
                            fm(4, Y, MIN, 4, 18, 2);
                            rc_sw(s4 + 0x1C, s0); s0 = s4; head = &s0;
                            rc_vu0.q = fc(rc_fdiv(VF(3)[0], VF(3)[1]));
                            tab = M32(t8); cnt = M8(t8 + 0x20);
                        }
                        fm(4, W, MAX, 18, 4, 1);
                        { u32 q = 0x43800000u; u32 w[4] = { q, q, q, q }; memcpy(VF(5), w, 16); }
                        fm(3, Y, ADD, 0, 0, BV);
                        fm(3, W, MUL, 5, 0, BQ);
                        fm(3, X, ADD, 0, 0, BQ);
                        fm(4, XYZ, ADD, 0, 0, BV);
                        t2 = M32(s4 + 8);
                        fm(3, Z, SUB, 5, 3, 3);
                        sqc2(4, sp + 0x50);
                        if (t2 == 0) *head = M32(s4 + 0x1C);
                        sqc2(3, sp + 0x40);
                    } else {
                        /* LOD0 (0) or LOD2 (3), no morph */
                        R128 q = {{ 0, 0x43800000ull }};
                        fm(4, Y, MIN, 4, 18, 2);
                        if (lod == 0) { rc_sw(s4 + 0x1C, s0); s0 = s4; head = &s0; tab = M32(t8); cnt = M8(t8 + 0x20); }
                        else { rc_sw(s4 + 0x1C, s2); s2 = s4; head = &s2; tab = M32(t8 + 8); cnt = M8(t8 + 0x22); }
                        fm(3, XYZ, ADD, 0, 0, BV);
                        fm(3, W, MAX, 18, 4, 1);
                        t2 = M32(s4 + 8);
                        if (lod == 0) t4++; else t6++;
                        sq_(sp + 0x40, &q);
                        if (t2 == 0) *head = M32(s4 + 0x1C);
                        sqc2(3, sp + 0x50);
                    }
                    if (t2 == 0) {
                        /* needs the clip program: the clip list */
                        sp += 0x60; fp += 0x60;
                        u32 at = M32(SPR + 0x3F08);
                        if (at != SPR + 0x3E00) {
                            st_clip++;
                            u32 a2 = M32(SPR + 0x3F20) | t7, v0 = M32(s4 + 0xC), a1 = M32(t8 + 0x2C);
                            rc_sw(at, v0); rc_sw(at + 4, a2); rc_sw(at + 8, tab); rc_sw(at + 0xC, (cnt << 24) | (a1 >> 4));
                            at += 0x10;
                        } else st_clipfull++;
                        rc_sw(SPR + 0x3F08, at);
                        if ((sp & 0xE00) == 0xE00) flush(&sp, fp);
                        continue;
                    }
                    /* the matrix */
                    lqc2(4, t7 + 0x30); lqc2(1, t7); lqc2(2, t7 + 0x10); lqc2(3, t7 + 0x20);
                    fm(1, XYZ, MUL, 1, 4, 3);
                    u32 swm = M32(SPR + 0x3F20);
                    fm(2, XYZ, MUL, 2, 4, 3);
                    sp += 0x60;
                    fm(3, XYZ, MUL, 3, 4, 3);
                    fm(4, XYZ, SUB, 4, 24, BV);
                    if (swm) sway(swm, t7, a0h);
                    mat3(1, XYZW, 28, 29, 30, 1);
                    fm(4, XYZ, MUL, 4, 24, 3);
                    fp += 0x60;
                    mat3(2, XYZW, 28, 29, 30, 2);
                    mat3(3, XYZW, 28, 29, 30, 3);
                    aff(4, 28, 29, 30, 31, 4);
                    sqc2(1, sp - 0x60); sqc2(2, sp - 0x50); sqc2(3, sp - 0x40); sqc2(4, sp - 0x30);
                    if ((sp & 0xE00) == 0xE00) flush(&sp, fp);
                }
                /* the class's counters; the draws per level of detail */
                sb_(t8 + 0x1C, t4); sb_(t8 + 0x1D, t5); sh_(t8 + 0x1E, t6);
                R128 T[7];
                for (int k = 0; k < 7; k++) rc_lq(&T[k], SPR + 0x3E00 + 16 * (u32)k);
                t3h = T[3].d[1];
                const u32 heads[3] = { s0, s1, s2 };
                for (int l = 0; l < 3; l++) {
                    if (heads[l] == 0) continue;
                    u32 p = M32(t8 + 4 * (u32)l), pe = p + (M8(t8 + 0x20 + (u32)l) << 4), a3 = M32(p);
                    rc_sw(SPR + 0x3F10, heads[l]);
                    for (;;) {
                        u32 q = M32(SPR + 0x3F10);
                        if (p == pe) break;
                        int slot = dok ? native_tie_direct_class(p, M32(t8 + 0x2C)) : -1;
                        if (slot >= 0) {
                            /* direct: the packet and its instances to native_tie.c */
                            if (!seg_open) { mark(&sp, &fp); if ((sp & 0xE00) == 0xE00) flush(&sp, fp); }
                            native_tie_seg_packet(slot, a3, M32(t8 + 0x2C));
                            a3 = M32(p + 0x10); p += 0x10;
                            do {
                                native_tie_seg_inst(M32(q + 0xC), M32(q + 0x10));
                                q = M32(q + 0x1C);
                                st_direct++;
                            } while (q != 0);
                            continue;
                        }
                        seg_open = 0;
                        if (dok) st_chain++;
                        emit_packet(T, p, a3, M32(t8 + 0x2C), 0, &sp, &fp);
                        a3 = M32(p + 0x10); p += 0x10;
                        do {
                            u32 blk = M32(q + 0xC), mtx = M32(q + 0x10);
                            q = M32(q + 0x1C);
                            if (emit_inst(T, blk, mtx, &t9, &sp, &fp)) flush(&sp, fp);
                        } while (q != 0);
                    }
                }
            }
        }
        /* the class's nearest distance, to the texture-distance list */
        { s32 k = rc_ftoi(VF(17)[0], 0); memcpy(&VF(17)[0], &k, 4); }
        u32 lp = M32(SPR + 0x3F14);
        rc_sw(SPR + 0x3F14, lp + 4);
        rc_sw(lp, VW(17, 0));
    }

    /* the end of the main part */
    emit_close(&sp, &fp);
    {
        u32 at = sp & 0xFFF;
        wait_idle(0x1000D000u);
        if (at) {
            rc_sw(0x1000D080u, sp - at); rc_sw(0x1000D010u, fp - at); rc_sw(0x1000D020u, at >> 4); rc_sw(0x1000D000u, 0x100);
            sp = (sp - at) ^ 0x1000;
        }
    }
    u32 s0 = M32(SPR + 0x3F08);
    t9 = 0;
    if (dok) { static unsigned long long nr; if (++nr % 300 == 0) uya_log("native TieProc: %llu runs; instances drawn directly %llu, packets left in the chain %llu; clip parts direct %llu, in the chain %llu", nr, st_direct, st_chain, st_cdirect, st_cchain); }
    if (s0 != SPR + 0x3A00) {
        /* the clip list's part */
        emit_start(R->mpg2_qwc, R->mpg2_addr, R->consts2, 9, &sp, &fp);
        a0h = M64(R->consts2 + 0x28);
        lqc2(28, R->cam2 + 0xC0); lqc2(29, R->cam2 + 0xD0); lqc2(30, R->cam2 + 0xE0); lqc2(31, R->cam2 + 0xF0);
        R128 T[8];
        for (int k = 0; k < 8; k++) rc_lq(&T[k], SPR + 0x3E00 + 16 * (u32)k);
        rc_sw(SPR + 0x3F24, R->ret2);
        wait_idle(0x1000D000u);
        /* the matrices, through the clip-space matrix, into their blocks */
        u32 s3 = SPR + 0x3A00;
        do {
            st_pass2++;
            u32 s6 = M32(s3 + 4), at = s6 & 0xF;
            s6 &= ~0xFu;
            lqc2(4, s6 + 0x30); lqc2(1, s6); lqc2(2, s6 + 0x10); lqc2(3, s6 + 0x20);
            fm(1, XYZ, MUL, 1, 4, 3);
            u32 s7 = M32(s3);
            fm(2, XYZ, MUL, 2, 4, 3);
            s3 += 0x10;
            fm(3, XYZ, MUL, 3, 4, 3);
            fm(4, XYZ, SUB, 4, 24, BV);
            if (at) sway(at, s6, a0h);
            mat3(1, XYZW, 28, 29, 30, 1);
            fm(4, XYZ, MUL, 4, 24, 3);
            mat3(2, XYZW, 28, 29, 30, 2);
            mat3(3, XYZW, 28, 29, 30, 3);
            aff(4, 28, 29, 30, 31, 4);
            sqc2(1, s7); sqc2(2, s7 + 0x10); sqc2(3, s7 + 0x20); sqc2(4, s7 + 0x30);
        } while (s3 != s0);
        /* direct, with the GPU renderer: the clip part's draws to native_tie.c (as meshes the GPU
         * cuts at the frustum), when every one of them can go there; else all into the chain. The
         * clip program's MPG and setup stay in the chain */
        int cdir = 0;
        {
            u32 c2 = R->consts2, c1 = R->consts;
            if (dok && T[7].w[2] == 0 && T[7].w[3] == 0x6E00001Eu && (T[7].w[0] >> 28) == 3 &&
                M32(c2 + 0x30) == 0x3000000Au && M32(c2 + 0x38) == 0x05000000u && M32(c2 + 0x3C) == 0x6C0A03C6u &&
                M32(c2 + 0x5C) == 0x30000000u && !memcmp((void *)(uintptr_t)(c2 + 0x60), (void *)(uintptr_t)(c1 + 0x30), 16) &&
                M32(c2 + 0x70) == 0x03000000u && M32(c2 + 0x74) == 0x0200002Cu && M32(c2 + 0x7C) == 0x01000404u) {
                u32 k[40];
                memcpy(k, (void *)(uintptr_t)M32(c2 + 0x34), sizeof k);
                cdir = native_tie_direct_clip_frame(k);
                const char *why = cdir ? 0 : "not the GPU renderer, or UYA_NCLIPMESH=0";
                for (u32 e = SPR + 0x3A00; cdir && e != s0; e += 0x10) {
                    if (!native_tie_direct_clip_inst((const u32 *)(uintptr_t)M32(e))) { cdir = 0; why = "an instance's fog or the screen scale"; }
                    u32 p = M32(e + 8), s5 = M32(e + 0xC), pe = p + ((s5 >> 24) << 4);
                    for (; cdir && p != pe; p += 0x10) if (native_tie_direct_clip_class(p, (s5 << 8) >> 4) < 0) { cdir = 0; why = "a packet without its mesh"; }
                }
                if (why) { static int told; if (told++ < 3) uya_log("native TieProc: the clip part goes into the chain (%s)", why); }
            } else if (dok) { static int told; if (!told++) uya_log("native TieProc: the clip part's templates are not as expected: it goes into the chain"); }
            if (dok) { if (cdir) st_cdirect++; else st_cchain++; }
            if (cdir && ts_mode > 0 && cstart_template_ok(R->consts2)) {
                /* the clip start's 10 qwords (still in the scratchpad: nothing was flushed since) become a record */
                sp -= 0xA0; fp -= 0xA0;
                emit_cstart_rec(R->mpg2_qwc, R->mpg2_addr, R->consts2, &sp, &fp);
            }
            if (cdir) { mark(&sp, &fp); if ((sp & 0xE00) == 0xE00) flush(&sp, fp); }
        }
        /* the draws, per run of entries with the same packet table */
        s3 = SPR + 0x3A00;
        for (;;) {
            u32 s5 = M32(s3 + 0xC);
            if (s3 == s0) break;
            u32 p = M32(s3 + 8);
            u32 base = (s5 << 8) >> 4;
            u32 pe = p + ((s5 >> 24) << 4), a3 = M32(p);
            u32 run_n = 0, run_at = s3;
            for (;;) {
                u32 nx = M32(s3 + 0x18);
                s3 += 0x10; run_n++;
                if (s3 == s0 || nx != p) break;
            }
            while (p != pe) {
                if (cdir) {
                    native_tie_seg_clip_packet(native_tie_direct_clip_class(p, base), a3, base);
                    a3 = M32(p + 0x10); p += 0x10;
                    for (u32 k = 0, a2 = run_at; k < run_n; k++, a2 += 0x10) native_tie_seg_inst(M32(a2), (M32(a2 + 4) >> 4) << 4);
                    continue;
                }
                emit_packet(T, p, a3, base, 1, &sp, &fp);
                a3 = M32(p + 0x10); p += 0x10;
                s32 a1 = (s32)run_n;
                u32 a2 = run_at;
                do {
                    u32 mtx = (M32(a2 + 4) >> 4) << 4, blk = M32(a2);
                    a1--; a2 += 0x10;
                    if (emit_inst(T, blk, mtx, &t9, &sp, &fp)) flush(&sp, fp);
                } while (a1 > 0);
            }
        }
        emit_close(&sp, &fp);
        {
            u32 at = sp & 0xFFF;
            wait_idle(0x1000D000u);
            rc_sw(0x1000D080u, sp - at); rc_sw(0x1000D010u, fp - at); rc_sw(0x1000D020u, at >> 4); rc_sw(0x1000D000u, 0x100);
        }
    }
    sh_(M32(SPR + 0x3F18), 0xFFFF);
    wait_idle(0x1000D000u);
    rc_sw(gp + (u32)R->g_pkt2, fp);
}

/* ---- the hook (tools/recomp.py puts its calls in the level's routine) ---- */
static int mode_ = -1;
static unsigned long long n_runs, n_ok, n_bad;
static TieRefs refs_of(const s32 *g, const u32 *a)
{
    TieRefs R;
    R.g_cnt = g[0]; R.g_inst = g[1]; R.g_cap = g[2]; R.g_pkt = g[3]; R.g_clock = g[4]; R.g_vf21 = g[5];
    for (int k = 0; k < 8; k++) R.g_t[k] = g[6 + k];
    R.g_pkt2 = g[14];
    R.classes = a[0]; R.occl = a[1]; R.sine = a[2]; R.sway = a[3]; R.ret1 = a[4]; R.cam = a[5]; R.frust = a[6];
    R.mpg_qwc = a[7]; R.mpg_addr = a[8]; R.consts = a[9]; R.ret_l0 = a[10]; R.ret_l1 = a[11]; R.ret_l2 = a[12];
    R.mpg2_qwc = a[13]; R.mpg2_addr = a[14]; R.consts2 = a[15]; R.cam2 = a[16]; R.ret2 = a[17];
    return R;
}

/* check mode: the state the routine reads and writes, put aside and compared */
#define WIN 0x100000u   /* the chain window compared, from the packet pointer */
#define MAXCLS 1024
static struct { u8 spr[0x4000]; u8 *win; u32 fp0, fp1; u32 ncls, cls[MAXCLS], cnt[MAXCLS]; RcVu0 vu0; u32 pkt; int valid; } A, N;
static u32 chk_gp_pkt;
static void grab(typeof(A) *s, u32 fp0, const TieRefs *R, u32 ncls, u32 gp)
{
    memcpy(s->spr, (void *)(uintptr_t)SPR, 0x4000);
    if (!s->win) s->win = malloc(WIN);
    u32 n = fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - fp0;
    memcpy(s->win, (void *)(uintptr_t)fp0, n);
    s->fp0 = fp0;
    s->ncls = ncls < MAXCLS ? ncls : MAXCLS;
    for (u32 k = 0; k < s->ncls; k++) { s->cls[k] = M32(R->classes + 4 * k); s->cnt[k] = M32(s->cls[k] + 0x1C); }
    s->vu0 = rc_vu0;
    s->pkt = M32(gp);
    s->valid = 1;
}
static void put_back(const typeof(A) *s, u32 gp)
{
    memcpy((void *)(uintptr_t)SPR, s->spr, 0x4000);
    u32 n = s->fp0 + WIN <= 0x2000000u ? WIN : 0x2000000u - s->fp0;
    memcpy((void *)(uintptr_t)s->fp0, s->win, n);
    for (u32 k = 0; k < s->ncls; k++) M32(s->cls[k] + 0x1C) = s->cnt[k];
    rc_vu0 = s->vu0;
    rc_sw(gp, s->pkt);
}
static TieRefs chk_R;
static u32 chk_ncls;

int uya_tieproc_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (mode_ < 0) { const char *e = getenv("UYA_NTIEPROC"), *n = getenv("UYA_NATIVE"); mode_ = e ? atoi(e) : (n && !atoi(n)) ? 0 : 2; uya_log("native TieProc: %s", mode_ == 0 ? "off (the game's routine)" : mode_ == 1 ? "check against the game's routine" : mode_ == 3 ? "on, the draws into the chain" : "on, the draws direct"); }
    if (!mode_) return 0;
    if (ng != 15 || na != 18) { static int told; if (!told++) uya_log("native TieProc: the level's routine has %d / %d data references (not 15 / 18): the game's routine runs", ng, na); return 0; }
    TieRefs R = refs_of(g, a);
    if (mode_ >= 2) { direct = mode_ == 2; run(c, &R); return 1; }
    direct = 0;
    /* check: run here, keep the results, put the state back for the game's routine */
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

void uya_tieproc_exit(RcCtx *c)
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
        bad = 1; snprintf(why, sizeof why, "chain differs at +0x%X (of 0x%X)", k, G.fp1 - G.fp0);
    } else if (memcmp(G.spr, N.spr, 0x4000)) {
        u32 k = 0; while (G.spr[k] == N.spr[k]) k++;
        bad = 1; snprintf(why, sizeof why, "scratchpad differs at 0x%04X", k);
    } else if (memcmp(G.cnt, N.cnt, 4 * G.ncls)) {
        u32 k = 0; while (G.cnt[k] == N.cnt[k]) k++;
        bad = 1; snprintf(why, sizeof why, "class %u's counters %08X, native %08X", k, G.cnt[k], N.cnt[k]);
    } else if (memcmp(G.vu0.vf, N.vu0.vf, sizeof G.vu0.vf) || memcmp(G.vu0.acc, N.vu0.acc, sizeof G.vu0.acc) || G.vu0.clip != N.vu0.clip ||
               G.vu0.q != N.vu0.q || G.vu0.mac != N.vu0.mac || G.vu0.status != N.vu0.status) {
        int r = 0; while (r < 32 && !memcmp(G.vu0.vf[r], N.vu0.vf[r], 16)) r++;
        bad = 1; snprintf(why, sizeof why, "VU0 registers differ (first vf%d; clip %06X / %06X, mac %04X / %04X, status %03X / %03X)", r, G.vu0.clip, N.vu0.clip, G.vu0.mac, N.vu0.mac, G.vu0.status, N.vu0.status);
    }
    if (bad) { n_bad++; if (n_bad <= 10) uya_log("native TieProc check: run %llu differs: %s", n_runs, why); }
    else n_ok++;
    if (n_runs % 500 == 0)
        uya_log("native TieProc check: %llu runs, %llu identical, %llu differ; instances %llu: retried %llu, occluded %llu, far %llu, near %llu, side %llu, crossing %llu (box out %llu, in the guard band %llu); "
                "LOD0 %llu, LOD0-1 %llu, LOD1-2 %llu, LOD2 %llu; to the clip list %llu (full %llu), clip part %llu; sway %llu (too far %llu); classes skipped %llu",
                n_runs, n_ok, n_bad, st_inst, st_retry, st_occl, st_far, st_near, st_side, st_cross, st_box, st_guard,
                st_lod[0], st_lod[1], st_lod[2], st_lod[3], st_clip, st_clipfull, st_pass2, st_sway, st_swayfar, st_skip);
}
