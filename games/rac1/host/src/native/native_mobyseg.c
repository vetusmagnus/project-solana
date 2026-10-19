/* Mobys straight from the native MobyProc (native_mobyproc.c), as shrubs and ties are drawn from
 * theirs (native_shrub.c, native_tie.c): records made where MobyProc writes each item of its chain,
 * drawn where the chain walker meets the run's marker, without the run's tags and VIF codes.
 *
 * MobyProc still writes its whole chain (every tag; every hole the skin driver, the colour patch and
 * the shadow splice fill later). Beside it, at the sites where it writes them, it records:
 *   R_MSCAL  an MSCAL (the chain start's 0x0000; each packet's 0x0050 / 0x0070);
 *   R_SETUP  the chain start's REF to the program's setup block and the per-buffer constants (two
 *            STCYCL 4/4 + UNPACK V4-32 x6, absolute 0x123 and 0x287: their chain addresses);
 *   R_MTX    a matrix (STCYCL 4/4 + UNPACK V4-32 x4, absolute 0x129 / 0x28D: its chain address);
 *   R_PKT    a packet: its hole (chain address), its counts word, its class packet (address, qwc);
 *            main pass: positions UNPACK V3-16 signed +TOPS 0 (REF, hole + colour qwords), colours
 *            UNPACK V4-8 unsigned +TOPS 0x061 (NEXT, the hole's start); extra pass: positions as
 *            main, colours (REF, hole + (colour + position qwords)), ST UNPACK V2-16 masked signed
 *            +TOPS 0x0C2 (NEXT, the hole's start); then the class packet (REF, its own VIF codes);
 *   R_GIF    a GS-state block (CNT + DIRECT 2): its 2 qwords' chain address, sent as PATH2.
 * MobyProc checks its templates, its GS block's tag and the program's setup block against the values
 * these formats assume, once per call (direct_ok there); anything else, and any record this file
 * cannot draw (a packet's counts outside the shapes above, a class packet whose codes are not ones
 * listed below), and the call gets no markers: its whole chain draws as it is.
 *
 * A run (the stretch only the moby program takes: from the chain's start to the CNT+FLUSHA before
 * pass 2's shadow slot, or to the closing CNT+FLUSHA when there is no pass 2; pass 2's from the slot
 * + 0x10 to the closing CNT+FLUSHA) gets a marker over its first qword at MobyProc's end: a NEXT of 0
 * qwords to the run's end, VIF NOPs NATIVE_MOBY_MARK and the segment's serial. The original qword is
 * kept here. The holes are read when the marker is met (the next frame, after DrawMobysCleanUp filled
 * them), as the REF and NEXT tags would have read them.
 *
 * Drawing a segment writes into VU1 memory (0x1100C000: the program's working memory, which other
 * VU1 programs share; the moby program reads words earlier packets left) exactly the words the VIF
 * would have written, with fixed-format writers for the formats above (vif.c's results: the V3-16 w
 * lane is the next halfword while it lies within the command's data, else 0; V4-8 zero-extended;
 * V2-16 under STMASK 0x20202020 {s, t, STCOL 0x1000, t}; V4-32 copies); TOPS / TOP / the double
 * buffer from the known setup (BASE 0, OFFSET 0x164; each MSCAL flips as vif.c's start() does); and
 * runs the native moby program at each MSCAL (host/src/gfx/vu.c uya_moby_vu1_run). A class packet is
 * decoded once (the codes STCYCL, STMASK, STROW, STCOL, STMOD, NOP, FLUSH*, MSKPATH3 and UNPACK of
 * every format with or without the mask, +TOPS or absolute, as vif.c does them; it must leave the
 * state as the setup block left it) into a list of qword writes kept by (address, qwc); each use
 * compares its stream with the copy kept, except the source words of plain V4-32 qwords, which are
 * read again at each draw (the game patches texture words); a change: decoded again.
 * VIF1's state after the run is set to what the left-out codes leave: the setup block's values (or,
 * pass 2's run, the values it started with: its first matrix's STCYCL 4/4), TOPS / TOP / DBF / ITOP
 * after its MSCALs. Before drawing, the run is checked against what these writers assume (CHCR TTE
 * on; no VIF command waiting for data; pass 2's run starting from the setup block's state; the setup
 * block unchanged; the words after each unpack's data in the hole NOP codes; every class packet
 * decodable; image 13859 in VU1 micro memory for the run's starts); if not, its first qword is put
 * back and the walker reads it again: the run goes through the chain.
 *
 * UYA_NMOBYDIRECT: 0 off (the chain); 1 check: the chain draws as before; the run's first
 * CNT carries a check marker in its address word (NATIVE_MOBY_CHKMARK, the serial), and at each
 * start of the moby program from the chain the records are followed to that start and what they
 * predict (the start's pc, TOP, the VIF state, every VU1 lane written since the last start) is
 * compared with what the VIF did (the program runs once, from the chain); 2 direct (the default). Only with the
 * native MobyProc (UYA_NMOBYPROC=2, its only caller) and the native moby program (UYA_NATIVE != 0).
 * No float math here. */
#include "native_moby.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void uya_gif_packet(const u32 *data, u32 qwc);
void native_gif(int path, const u32 *d, u32 qwc);

typedef UyaVifState VS;
#define VU1MEM ((u8 *)(uintptr_t)0x1100C000u)
#define EP(a) ((const u8 *)(uintptr_t)(a))
#define STD_MASK 0x20202020u
#define STD_COL 0x1000u

const u32 native_moby_setup_words[16] = {
    0x01000404u, 0x20000000u, STD_MASK, 0x31000000u,       /* STCYCL 4/4; STMASK; STCOL */
    STD_COL, STD_COL, STD_COL, STD_COL,
    0x03000000u, 0x02000164u, 0x05000000u, 0x30000000u,    /* BASE 0; OFFSET 0x164; STMOD 0; STROW */
    0, 0, 0, 0 };

static int dmode = -1;
int native_mobyseg_mode(void)
{
    if (dmode < 0) {
        const char *e = getenv("UYA_NMOBYDIRECT"), *n = getenv("UYA_NATIVE");
        dmode = e ? atoi(e) : 2;
        if (dmode < 0 || dmode > 2) dmode = 0;
        if (dmode && n && !atoi(n)) { uya_log("native moby direct draws: UYA_NATIVE=0 (the moby program runs from the chain): off"); dmode = 0; }
        uya_log("native moby direct draws (UYA_NMOBYDIRECT): %s", dmode == 0 ? "off (the chain)" : dmode == 1 ? "check (the chain draws; the records compared at each moby program start)" : "on");
    }
    return dmode;
}

/* a chain or data address as the chain walker reads it (host/src/sdk/dma.c phys(): the low 4 bits
 * dropped, the KSEG aliases folded): RAM above 1 MB only (bytes of it from there), else 0 */
static u32 pa(u32 a, u32 bytes)
{
    u32 k = a >> 28;
    if (k != 0 && k != 2 && k != 3) return 0;
    a &= 0x01FFFFF0u;
    if (a < 0x00100000u || a + bytes > 0x02000000u) return 0;
    return a;
}

/* ---- the class packets: decoded once, kept by (address, qwc) ---- */
typedef struct { u32 qa, fl, v[4], src; } MW;   /* fl: bit 0 +TOPS, bits 1..4 the lanes written; src: a plain V4-32 qword's source */
typedef struct { u32 a, qwc, p; int ok, why; u32 *raw; u8 *vol; MW *w; u32 nw; } MCls;
#define DPN 8192
#define DPH 16384
#define CLS_MAXQ 0x400u
static MCls *cls; static u32 ncls;
static int clh[DPH];
static unsigned long long c_dec, c_hit, c_redec, c_bad, c_full;
static MW wtmp[16 * CLS_MAXQ];

static void cls_drop(void)
{
    for (u32 i = 0; i < ncls; i++) { free(cls[i].raw); free(cls[i].vol); free(cls[i].w); }
    ncls = 0;
    for (u32 h = 0; h < DPH; h++) clh[h] = -1;
}
static inline u32 elem(const u32 *d, u32 nwords, u32 vl, u32 k, u32 usn)       /* vif.c's */
{
    switch (vl) {
    case 0: return k < nwords ? d[k] : 0;
    case 1: { if (k / 2 >= nwords) return 0; u16 h; memcpy(&h, (const u8 *)d + 2 * k, 2); return usn ? h : (u32)(s32)(s16)h; }
    case 2: { if (k / 4 >= nwords) return 0; u8 b = ((const u8 *)d)[k]; return usn ? b : (u32)(s32)(s8)b; }
    }
    return 0;
}
/* the packet's codes from the setup block's state (STCYCL 4/4, STMASK 0x20202020, STROW 0, STCOL
 * 0x1000, STMOD 0) into c->w, as vif.c's unpack() would write them; it must leave that state.
 * 0, or < 0 (c->why: the command it stopped at) */
static int decode(MCls *c)
{
    free(c->raw); free(c->vol); free(c->w);
    c->raw = 0; c->vol = 0; c->w = 0; c->nw = 0; c->ok = 0; c->why = 0;
    u32 nw = c->qwc * 4, i = 0, n = 0;
    c->p = pa(c->a, c->qwc * 16);
    if (!c->p || c->qwc > CLS_MAXQ) return -1;
    const u32 *w = (const u32 *)(uintptr_t)c->p;
    c->raw = malloc(nw * 4 + 4); c->vol = calloc(nw + 1, 1);
    if (!c->raw || !c->vol) return -2;
    memcpy(c->raw, w, nw * 4);
    u32 cl = 4, wl = 4, mode = 0, mask = STD_MASK, row[4] = { 0, 0, 0, 0 }, col[4] = { STD_COL, STD_COL, STD_COL, STD_COL };
    while (i < nw) {
        u32 code = w[i++], cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
        c->why = (int)code;
        if ((cmd & 0x60) == 0x60) {
            if (!num) num = 256;
            u32 vn = (cmd >> 2) & 3, vl = cmd & 3, m = (cmd >> 4) & 1, usn = (imm >> 14) & 1, addr = imm & 0x3FF, flg = (imm >> 15) & 1;
            u32 wl1 = wl ? wl : 1;
            if (wl1 > cl) return -3;                                   /* (the fill mode: not taken) */
            u32 bits = vl == 3 ? 16 : (32 >> vl) * (vn + 1), nd = (bits * num + 31) / 32;
            if (nd > nw - i) return -4;                                /* (its data goes on past the packet) */
            const u32 *d = w + i;
            for (u32 k = 0; k < num; k++) {
                u32 pos = k % wl1, qa = addr + (k / wl1) * cl + pos, val[4] = { 0, 0, 0, 0 };
                if (vl == 3) {                                         /* V4-5 */
                    u32 h = 0;
                    if (k / 2 < nd) { u16 x; memcpy(&x, (const u8 *)d + 2 * k, 2); h = x; }
                    val[0] = (h & 0x1F) << 3; val[1] = ((h >> 5) & 0x1F) << 3; val[2] = ((h >> 10) & 0x1F) << 3; val[3] = ((h >> 15) & 1) << 7;
                } else {
                    u32 e = k * (vn + 1);
                    switch (vn) {
                    case 0: val[0] = val[1] = val[2] = val[3] = elem(d, nd, vl, e, usn); break;
                    case 1: val[0] = val[2] = elem(d, nd, vl, e, usn); val[1] = val[3] = elem(d, nd, vl, e + 1, usn); break;
                    default: for (u32 q = 0; q < 4; q++) val[q] = elem(d, nd, vl, e + q, usn); break;   /* (V3: w the next element) */
                    }
                }
                u32 r = pos < 3 ? pos : 3, lanes = 0, out[4] = { 0, 0, 0, 0 };
                for (u32 q = 0; q < 4; q++) {
                    u32 mk = m ? (mask >> ((r * 4 + q) * 2)) & 3 : 0;
                    switch (mk) {
                    case 0:
                        if (mode == 1) out[q] = val[q] + row[q];
                        else if (mode == 2) { row[q] += val[q]; out[q] = row[q]; }
                        else out[q] = val[q];
                        lanes |= 1u << q; break;
                    case 1: out[q] = row[q]; lanes |= 1u << q; break;
                    case 2: out[q] = col[r]; lanes |= 1u << q; break;
                    default: break;
                    }
                }
                if (!lanes) continue;
                if (n == sizeof wtmp / sizeof wtmp[0]) return -5;
                MW *o = &wtmp[n++];
                o->qa = qa; o->fl = flg | lanes << 1; memcpy(o->v, out, 16); o->src = 0;
                if (!m && vl == 0 && vn == 3 && mode != 1 && mode != 2) {   /* a plain V4-32 qword: read at each draw */
                    o->src = c->p + (i + 4 * k) * 4;
                    memset(c->vol + i + 4 * k, 1, 4);
                }
            }
            i += nd;
        } else switch (cmd) {
        case 0x00: case 0x06: case 0x10: case 0x11: case 0x13: break;          /* NOP, MSKPATH3, FLUSHE, FLUSH, FLUSHA */
        case 0x01: cl = imm & 0xFF; wl = (imm >> 8) & 0xFF; break;             /* STCYCL */
        case 0x05: mode = imm & 3; break;                                       /* STMOD */
        case 0x20: if (i + 1 > nw) return -6; mask = w[i++]; break;            /* STMASK */
        case 0x30: if (i + 4 > nw) return -6; memcpy(row, w + i, 16); i += 4; break;   /* STROW */
        case 0x31: if (i + 4 > nw) return -6; memcpy(col, w + i, 16); i += 4; break;   /* STCOL */
        default: return -7;                    /* (MSCAL, MSCALF, MSCNT, MPG, DIRECT, BASE, OFFSET, ITOP, MARK, unknown) */
        }
    }
    c->why = 0;
    if (cl != 4 || wl != 4 || mode != 0 || mask != STD_MASK || row[0] | row[1] | row[2] | row[3] ||
        col[0] != STD_COL || col[1] != STD_COL || col[2] != STD_COL || col[3] != STD_COL) return -8;   /* (the state it leaves is not the setup's) */
    c->w = malloc((n ? n : 1) * sizeof *c->w);
    if (!c->w) return -2;
    memcpy(c->w, wtmp, n * sizeof *c->w);
    c->nw = n; c->ok = 1;
    return 0;
}
/* the class packet at (a, qwc), decoded, still as its stream was (but for the words read at each
 * draw), or 0 */
static const MCls *cls_get(u32 a, u32 qwc)
{
    static const MCls empty = { .ok = 1 };
    if (!qwc) return &empty;                         /* (a REF of 0 qwords sends nothing) */
    if (!cls) { cls = calloc(DPN, sizeof *cls); if (!cls) return 0; for (u32 h = 0; h < DPH; h++) clh[h] = -1; }
    u32 h = ((a >> 4) * 2654435761u ^ qwc * 40503u) & (DPH - 1);
    while (clh[h] >= 0) {
        MCls *c = &cls[clh[h]];
        if (c->a == a && c->qwc == qwc) {
            const u32 *w = c->p ? (const u32 *)(uintptr_t)c->p : 0;
            int same = w && c->raw;
            for (u32 j = 0, nw = qwc * 4; same && j < nw; j++) if (!c->vol[j] && c->raw[j] != w[j]) same = 0;
            if (same) { c_hit++; return c->ok ? c : 0; }
            c_redec++;
            int r = decode(c);
            if (r) { c_bad++; static int told; if (told++ < 8) uya_log("native moby direct: class packet %08X (%u qwords) changed and cannot be decoded now (%d, at %08X)", a, qwc, r, (u32)c->why); }
            return c->ok ? c : 0;
        }
        h = (h + 1) & (DPH - 1);
    }
    if (ncls == DPN) { c_full++; return 0; }       /* (emptied between calls: native_mobyseg_call_begin) */
    MCls *c = &cls[ncls];
    memset(c, 0, sizeof *c);
    c->a = a; c->qwc = qwc;
    clh[h] = (int)ncls++;
    c_dec++;
    int r = decode(c);
    if (r) { c_bad++; static int told; if (told++ < 8) uya_log("native moby direct: class packet %08X (%u qwords) cannot be decoded (%d, at %08X): its calls go through the chain", a, qwc, r, (u32)c->why); }
    return c->ok ? c : 0;
}

/* ---- records and segments ---- */
enum { R_MSCAL = 1, R_SETUP, R_MTX, R_PKT, R_GIF };
#define DSEG 4096
#define DREC (1u << 18)
typedef struct { u32 serial, first, count, start, end, at, orig[4]; int state; } Seg;   /* state: 1 recorded, 2 marked, 3 put back */
static Seg dseg[DSEG];
static u32 (*drec)[4], drec_next, seg_serial;
static struct { int on, fail; u32 rec0, nseg, seg[2]; int open; } CALL;
static unsigned long long n_calls, n_marked, n_failed, n_runs_marked;
static const char *fail_why;
static Seg *seg_of(u32 serial) { return &dseg[serial % DSEG]; }
static const u32 *rec_at(const Seg *g, u32 k) { return drec[(g->first + k) % DREC]; }
static int recs_kept(const Seg *g) { return drec_next - g->first <= DREC; }

static void rec4(u32 a, u32 b, u32 c, u32 d)
{
    if (!CALL.on || CALL.fail) return;
    if (!CALL.open) { CALL.fail = 1; fail_why = "a record outside a run"; return; }
    Seg *g = seg_of(CALL.seg[CALL.nseg - 1]);
    if (g->count >= DREC / 4) { CALL.fail = 1; fail_why = "a run of too many records"; return; }
    u32 *r = drec[drec_next++ % DREC];
    r[0] = a; r[1] = b; r[2] = c; r[3] = d;
    g->count++;
}
void native_mobyseg_call_begin(void)
{
    if (!drec) drec = malloc((size_t)DREC * 16);
    memset(&CALL, 0, sizeof CALL);
    if (!drec) return;
    if (ncls > DPN * 3 / 4) cls_drop();           /* (between calls: no record holds a class's index) */
    CALL.on = 1; CALL.rec0 = drec_next;
    n_calls++;
}
static void call_drop(void)
{
    for (u32 s = 0; s < CALL.nseg; s++) seg_of(CALL.seg[s])->state = 0;
    drec_next = CALL.rec0;
    CALL.on = 0;
}
u32 native_mobyseg_begin(u32 start)
{
    if (!CALL.on || CALL.fail) return 0;
    if (CALL.nseg == 2) { CALL.fail = 1; fail_why = "more than two runs"; return 0; }
    u32 s = ++seg_serial & 0xFFFFFFu;
    if (!s) s = ++seg_serial & 0xFFFFFFu;
    Seg *g = seg_of(s);
    memset(g, 0, sizeof *g);
    g->serial = s; g->first = drec_next; g->state = 1; g->at = start; g->start = pa(start, 16);
    if (!g->start) { CALL.fail = 1; fail_why = "a run outside RAM"; }
    CALL.seg[CALL.nseg++] = s; CALL.open = 1;
    return s;
}
void native_mobyseg_close(u32 end)
{
    if (!CALL.on || !CALL.open) return;
    seg_of(CALL.seg[CALL.nseg - 1])->end = end;
    CALL.open = 0;
}
void native_mobyseg_mscal(u32 pc) { rec4(R_MSCAL, pc, 0, 0); }
void native_mobyseg_setup(u32 ref, u32 c0, u32 c1)
{
    if (!pa(ref, 64) || !pa(c0, 96) || !pa(c1, 96)) { CALL.fail = 1; fail_why = "the chain start outside RAM"; return; }
    rec4(R_SETUP, ref, c0, c1);
}
void native_mobyseg_mtx(u32 rows, u32 vuaddr)
{
    if (!pa(rows, 64)) { CALL.fail = 1; fail_why = "a matrix outside RAM"; return; }
    rec4(R_MTX, rows, vuaddr, 0);
}
void native_mobyseg_gif(u32 at, u32 n)
{
    if (!pa(at, n * 16)) { CALL.fail = 1; fail_why = "a GS block outside RAM"; return; }
    rec4(R_GIF, at, n, 0);
}
/* the counts word's shape: n vertices (byte 3; 0: 256), pq position qwords (byte 1), cq colour (and
 * ST) qwords (byte 2), each section at least the words its unpack takes */
static int pkt_shape(u32 counts, u32 *num, u32 *pq, u32 *cq)
{
    u32 n = counts >> 24;
    *num = n ? n : 256; *pq = (counts >> 8) & 0xFF; *cq = (counts >> 16) & 0xFF;
    return (48 * *num + 31) / 32 <= *pq * 4 && *num <= *cq * 4;
}
int native_mobyseg_pkt(int extra, u32 hole, u32 counts, u32 cls_a, u32 qwc)
{
    if (!CALL.on || CALL.fail) return 1;
    u32 num, pq, cq;
    if (!pkt_shape(counts, &num, &pq, &cq)) { CALL.fail = 1; fail_why = "a packet's counts outside the known shapes"; return 0; }
    if (!pa(hole, (pq + cq * (extra ? 2 : 1)) * 16)) { CALL.fail = 1; fail_why = "a hole outside RAM"; return 0; }
    if (!cls_get(cls_a, qwc)) { CALL.fail = 1; fail_why = "a class packet that cannot be decoded"; return 0; }
    rec4(R_PKT | (extra ? 0x100u : 0) | qwc << 16, hole, counts, cls_a);
    return 1;
}
static void call_log(int mode)
{
    if (n_calls % 5000 == 0) uya_log("native moby direct: %llu calls, %llu with %s (%llu runs), %llu through the chain; class packets %u kept (decoded %llu, hits %llu, decoded again %llu, not decodable %llu, cache full %llu)",
                                     n_calls, n_marked, mode == 2 ? "markers" : "check markers", n_runs_marked, n_failed, ncls, c_dec, c_hit, c_redec, c_bad, c_full);
}
int native_mobyseg_call_end(int ok, int mode)
{
    if (!CALL.on) return 0;
    call_log(mode);
    if (!ok || CALL.fail || CALL.open) {
        static int told;
        if (told++ < 8) uya_log("native moby direct: a call goes through the chain (%s)", !ok ? "MobyProc" : CALL.open ? "a run not closed" : fail_why ? fail_why : "?");
        call_drop(); n_failed++;
        return 0;
    }
    /* every run with records: its first qword a CNT (q0, or pass 2's first matrix), its records kept */
    u32 nm = 0;
    for (u32 s = 0; s < CALL.nseg; s++) {
        Seg *g = seg_of(CALL.seg[s]);
        if (!g->count) { g->state = 0; continue; }
        const u32 *q = (const u32 *)(uintptr_t)g->start;
        if (((q[0] >> 28) & 7) != 1 || (q[0] >> 31) || !g->end || !recs_kept(g)) { call_drop(); n_failed++; static int told; if (told++ < 8) uya_log("native moby direct: a call goes through the chain (a run's first qword %08X %08X)", q[0], q[1]); return 0; }
    }
    for (u32 s = 0; s < CALL.nseg; s++) {
        Seg *g = seg_of(CALL.seg[s]);
        if (!g->count) continue;
        u32 *q = (u32 *)(uintptr_t)g->start;
        memcpy(g->orig, q, 16);
        if (mode == 2) { q[0] = 0x20000000u; q[1] = g->end; q[2] = NATIVE_MOBY_MARK; q[3] = g->serial; }
        else q[1] = (u32)NATIVE_MOBY_CHKMARK << 16 | (g->serial & 0xFFFF);
        g->state = 2; nm++;
    }
    CALL.on = 0;
    n_marked++; n_runs_marked += nm;
    return (int)nm;
}

/* ---- the writers: VU1 memory as vif.c's unpacks leave it, for the formats MobyProc's templates have ---- */
typedef struct { u8 *mem; u8 *lanes; u16 *touch; u32 nt; u32 *who; u32 k; } Sink;   /* lanes (check): written per qword; who: the record */
static inline u32 *dq(Sink *s, u32 q, u32 lanes)
{
    q &= 0x3FF;
    if (s->lanes) { if (!s->lanes[q]) s->touch[s->nt++] = (u16)q; s->lanes[q] |= (u8)lanes; s->who[q] = s->k; }
    return (u32 *)(s->mem + q * 16);
}
/* UNPACK V4-32 (cl = wl = 4, STMOD 0): n qwords from p to q */
static void put_v4_32(Sink *s, u32 q, const u8 *p, u32 n) { for (u32 i = 0; i < n; i++) memcpy(dq(s, q + i, 15), p + 16 * i, 16); }
/* UNPACK V3-16 signed: x y z sign-extended; w the next halfword while it lies within the command's
 * (48 n + 31) / 32 data words, else 0 (vif.c's elem()) */
static void put_v3_16(Sink *s, u32 q, const u8 *p, u32 num)
{
    u32 nw = (48 * num + 31) / 32;
    for (u32 k = 0; k < num; k++) {
        u32 *d = dq(s, q + k, 15);
        for (u32 c = 0; c < 4; c++) { u32 e = 3 * k + c; s16 h = 0; if (e / 2 < nw) memcpy(&h, p + 2 * e, 2); d[c] = (u32)(s32)h; }
    }
}
/* UNPACK V4-8 unsigned */
static void put_v4_8(Sink *s, u32 q, const u8 *p, u32 num)
{
    for (u32 k = 0; k < num; k++) { u32 *d = dq(s, q + k, 15); for (u32 c = 0; c < 4; c++) d[c] = p[4 * k + c]; }
}
/* UNPACK V2-16 signed, masked with STMASK 0x20202020 (x y w data, z STCOL 0x1000), cl = wl = 4 */
static void put_st(Sink *s, u32 q, const u8 *p, u32 num)
{
    for (u32 k = 0; k < num; k++) {
        s16 sv, tv; memcpy(&sv, p + 4 * k, 2); memcpy(&tv, p + 4 * k + 2, 2);
        u32 *d = dq(s, q + k, 15);
        d[0] = (u32)(s32)sv; d[1] = (u32)(s32)tv; d[2] = STD_COL; d[3] = (u32)(s32)tv;
    }
}
static void put_cls(Sink *s, const MCls *c, u32 tops)
{
    for (u32 i = 0; i < c->nw; i++) {
        const MW *w = &c->w[i];
        u32 lanes = w->fl >> 1, *d = dq(s, ((w->fl & 1) ? tops : 0) + w->qa, lanes);
        if (w->src) memcpy(d, EP(w->src), 16);
        else for (u32 q = 0; q < 4; q++) if (lanes & (1u << q)) d[q] = w->v[q];
    }
}
/* vif.c start(): TOP, the double buffer, ITOP */
static void vstart(VS *v)
{
    v->top = v->tops;
    if (v->dbf) { v->tops = v->base; v->dbf = 0; }
    else { v->tops = (v->base + v->ofst) & 0x3FF; v->dbf = 1; }
    v->itop = v->itops;
}
/* the state the setup block leaves (its codes checked equal to native_moby_setup_words) */
static void setup_state(VS *v)
{
    v->cl = 4; v->wl = 4; v->mask = STD_MASK;
    for (int c = 0; c < 4; c++) { v->col[c] = STD_COL; v->row[c] = 0; }
    v->base = 0; v->ofst = 0x164; v->dbf = 0; v->tops = v->base; v->mode = 0;
}
static int setup_state_is(const VS *v, int cycle)
{
    return v->mode == 0 && v->mask == STD_MASK && !(v->row[0] | v->row[1] | v->row[2] | v->row[3]) &&
           v->col[0] == STD_COL && v->col[1] == STD_COL && v->col[2] == STD_COL && v->col[3] == STD_COL && (!cycle || (v->cl == 4 && v->wl == 4));
}
/* one record but R_MSCAL on state v into s (c: R_PKT's class packet; send: R_GIF to the GS) */
static void apply(const u32 *r, VS *v, Sink *s, const MCls *c, int send)
{
    switch (r[0] & 0xFF) {
    case R_SETUP:                                    /* the setup block; STCYCL 4/4 + V4-32 x6 to 0x123, 0x287 */
        setup_state(v);
        put_v4_32(s, 0x123, EP(pa(r[2], 96)), 6);
        put_v4_32(s, 0x287, EP(pa(r[3], 96)), 6);
        break;
    case R_MTX:                                      /* STCYCL 4/4 + V4-32 x4, absolute */
        v->cl = 4; v->wl = 4;
        put_v4_32(s, r[2], EP(pa(r[1], 64)), 4);
        break;
    case R_PKT: {
        u32 num, pq, cq;
        pkt_shape(r[2], &num, &pq, &cq);
        const u8 *h = EP(pa(r[1], 16));
        put_v3_16(s, v->tops + 0x000, h + cq * 16, num);                         /* positions (REF) */
        if (r[0] & 0x100) {
            put_v4_8(s, v->tops + 0x061, h + (cq + pq) * 16, num);               /* colours (REF) */
            put_st(s, v->tops + 0x0C2, h, num);                                  /* ST (NEXT) */
        } else put_v4_8(s, v->tops + 0x061, h, num);                             /* colours (NEXT) */
        if (c) put_cls(s, c, v->tops);                                           /* the class packet (REF) */
        break;
    }
    case R_GIF:
        if (send) { extern const char *uya_gs_src; uya_gs_src = "PATH2 native moby GS state"; native_gif(1, (const u32 *)(uintptr_t)pa(r[1], r[2] * 16), r[2]); }
        break;
    }
}
/* the words after an unpack's data in its transfer, which the VIF takes as codes: NOPs (or codes
 * that change nothing vif.c keeps) */
static int noop_words(const u8 *p, u32 from, u32 to)
{
    for (u32 j = from; j < to; j++) {
        u32 w; memcpy(&w, p + 4 * j, 4);
        u32 c = (w >> 24) & 0x7F;
        if (c != 0x00 && c != 0x06 && c != 0x10 && c != 0x11 && c != 0x13) return 0;
    }
    return 1;
}
static int pkt_padding_ok(const u32 *r)
{
    u32 num, pq, cq;
    pkt_shape(r[2], &num, &pq, &cq);
    const u8 *h = EP(pa(r[1], 16));
    if (!noop_words(h + cq * 16, (48 * num + 31) / 32, pq * 4)) return 0;
    if (r[0] & 0x100) return noop_words(h + (cq + pq) * 16, num, cq * 4) && noop_words(h, num, cq * 4);
    return noop_words(h, num, cq * 4);
}

/* ---- the run checked before it is drawn ---- */
enum { W_TTE, W_NEED, W_STATE, W_SETUP, W_PAD, W_CLS, W_IMAGE, W_RING, W_N };
static const char *why_name[W_N] = { "CHCR TTE off", "a VIF command waiting for data", "pass 2's VIF state not the setup's", "the setup block changed",
                                     "a hole's words after its data not NOPs", "a class packet not decodable", "VU1 micro memory without image 13859",
                                     "its records overwritten" };
static unsigned long long n_why[W_N];
static const MCls **pkc; static u32 pkc_n;
/* 0 or W_* + 1; the class packets of the run's packets in pkc[] */
static int prepass(const Seg *g, const VS *v, int tte)
{
    if (!tte) return W_TTE + 1;
    if (v->need) return W_NEED + 1;
    if (g->count && (rec_at(g, 0)[0] & 0xFF) != R_MSCAL && !setup_state_is(v, (rec_at(g, 0)[0] & 0xFF) != R_MTX)) return W_STATE + 1;
    if (pkc_n < g->count) { free(pkc); pkc = malloc(g->count * sizeof *pkc); pkc_n = pkc ? g->count : 0; if (!pkc) return W_CLS + 1; }
    int img[3] = { 0, 0, 0 };
    for (u32 k = 0, np = 0; k < g->count; k++) {
        const u32 *r = rec_at(g, k);
        switch (r[0] & 0xFF) {
        case R_SETUP: if (memcmp(EP(pa(r[1], 64)), native_moby_setup_words, 64)) return W_SETUP + 1; break;
        case R_PKT:
            if (!pkt_padding_ok(r)) return W_PAD + 1;
            if (!(pkc[np++] = cls_get(r[3], r[0] >> 16))) return W_CLS + 1;
            break;
        case R_MSCAL: {
            int i = r[1] == 0 ? 0 : r[1] == 0x50 ? 1 : 2;
            if (!img[i]) { if (!uya_vu1_image_is(r[1], "13859")) return W_IMAGE + 1; img[i] = 1; }
            break;
        }
        }
    }
    return 0;
}

/* ---- direct: the walker meets a marker ---- */
static unsigned long long n_drawn, n_back, n_badmark, n_starts, n_nostart;
static void put_back(Seg *g, int why)
{
    memcpy((void *)(uintptr_t)g->start, g->orig, 16);
    g->state = 3; n_back++; n_why[why]++;
    static int told; if (told++ < 8) uya_log("native moby direct: run %u goes through the chain (%s)", g->serial, why_name[why]);
}
int native_moby_tag(const u32 *q, u32 t, int tte)
{
    if ((q[0] & 0x7000FFFFu) != 0x20000000u || q[2] != NATIVE_MOBY_MARK || (q[3] >> 24)) return 0;
    Seg *g = seg_of(q[3]);
    if (g->serial != q[3] || g->start != pa(t, 16) || g->state != 2 || g->end != q[1]) {
        n_badmark++;
        static int told; if (told++ < 8) uya_log("native moby direct: marker %u at %08X not known (serial %u, state %d): its run is skipped", q[3], t, g->serial, g->state);
        return 0;
    }
    if (!recs_kept(g)) { put_back(g, W_RING); return 2; }
    VS v; uya_vif1_get_state(&v);
    int w = prepass(g, &v, tte);
    if (w) { put_back(g, w - 1); return 2; }
    Sink s = { VU1MEM, 0, 0, 0, 0, 0 };
    for (u32 k = 0, np = 0; k < g->count; k++) {
        const u32 *r = rec_at(g, k);
        if ((r[0] & 0xFF) == R_MSCAL) {
            vstart(&v);
            n_starts++;
            if (!uya_moby_vu1_run(VU1MEM, r[1], v.top)) n_nostart++;
        } else apply(r, &v, &s, (r[0] & 0xFF) == R_PKT ? pkc[np++] : 0, 1);
    }
    uya_vif1_set_state(&v);
    if (++n_drawn % 2000 == 0) {
        uya_log("native moby direct: %llu runs drawn (programs %llu, without native code %llu), %llu through the chain (TTE %llu, waiting VIF %llu, pass 2 state %llu, setup %llu, hole words %llu, class packets %llu, image %llu, records overwritten %llu); markers not known %llu",
                n_drawn, n_starts, n_nostart, n_back, n_why[W_TTE], n_why[W_NEED], n_why[W_STATE], n_why[W_SETUP], n_why[W_PAD], n_why[W_CLS], n_why[W_IMAGE], n_why[W_RING], n_badmark);
    }
    return 1;
}

/* ---- check (UYA_NMOBYDIRECT=1): the records followed beside the chain's draws ---- */
static u8 E[1024 * 16], EL[1024]; static u16 ET[1024]; static u32 EW[1024];
static struct { int on; u32 serial, k; VS v; unsigned long long gif0; u32 ngif; } C;
static unsigned long long k_starts, k_bad, k_runs, k_left, k_extra, k_wouldback[W_N], k_gifs_unchecked;
static int k_told;
#define KDIFF(...) do { k_bad++; if (k_told++ < 10) uya_log(__VA_ARGS__); } while (0)
void native_mobyseg_chk_begin(u32 serial16, u32 t)
{
    if (dmode != 1) return;
    Seg *g = &dseg[serial16 % DSEG];
    if ((g->serial & 0xFFFF) != serial16 || g->state != 2 || g->start != pa(t, 16) || !recs_kept(g)) return;
    if (C.on) { k_left++; if (k_told++ < 10) uya_log("native moby direct check: run %u ended with records not followed (at %u)", C.serial, C.k); }
    memset(&C, 0, sizeof C);
    C.on = 1; C.serial = g->serial;
    uya_vif1_get_state(&C.v);
    C.gif0 = uya_vif1_direct_n;
    int w = prepass(g, &C.v, 1);
    if (w) k_wouldback[w - 1]++;
    for (u32 i = 0; i < 1024; i++) EL[i] = 0;
    k_runs++;
}
void native_mobyseg_chk(u32 pc, const u8 *vumem, u32 top)
{
    if (!C.on) return;
    Seg *g = seg_of(C.serial);
    if (g->serial != C.serial || !recs_kept(g)) { C.on = 0; return; }
    Sink s = { E, EL, ET, 0, EW, 0 };
    u32 want = ~0u;
    while (C.k < g->count) {
        const u32 *r = rec_at(g, C.k);
        s.k = C.k++;
        if ((r[0] & 0xFF) == R_MSCAL) { vstart(&C.v); want = r[1]; break; }
        if ((r[0] & 0xFF) == R_GIF) C.ngif++;
        apply(r, &C.v, &s, (r[0] & 0xFF) == R_PKT ? cls_get(r[3], r[0] >> 16) : 0, 0);
    }
    k_starts++;
    if (want == ~0u) { k_extra++; KDIFF("native moby direct check: run %u: a program start (0x%04X, TOP %03X) the records do not have (%u records)", C.serial, pc, top, g->count); C.on = 0; return; }
    if (want != pc || C.v.top != top) KDIFF("native moby direct check: run %u: start 0x%04X TOP %03X, the records 0x%04X TOP %03X", C.serial, pc, top, want, C.v.top);
    VS v; uya_vif1_get_state(&v);
    if (v.cl != C.v.cl || v.wl != C.v.wl || v.mode != C.v.mode || v.mask != C.v.mask || memcmp(v.row, C.v.row, 16) || memcmp(v.col, C.v.col, 16) ||
        v.base != C.v.base || v.ofst != C.v.ofst || v.tops != C.v.tops || v.top != C.v.top || v.itop != C.v.itop || v.dbf != C.v.dbf)
        KDIFF("native moby direct check: run %u: VIF1 state differs at record %u: VIF CL %u WL %u MODE %u MASK %08X BASE %03X OFFSET %03X TOPS %03X TOP %03X ITOP %03X DBF %u; the records %u %u %u %08X %03X %03X %03X %03X %03X %u",
              C.serial, C.k, v.cl, v.wl, v.mode, v.mask, v.base, v.ofst, v.tops, v.top, v.itop, v.dbf, C.v.cl, C.v.wl, C.v.mode, C.v.mask, C.v.base, C.v.ofst, C.v.tops, C.v.top, C.v.itop, C.v.dbf);
    unsigned long long ng = uya_vif1_direct_n - C.gif0;
    if (ng != C.ngif) KDIFF("native moby direct check: run %u: %u GS blocks (DIRECT) before start %u, the records %u", C.serial, (u32)ng, C.k, C.ngif);
    for (u32 i = 0; i < s.nt; i++) {
        u32 q = ET[i];
        const u32 *a = (const u32 *)(vumem + q * 16), *b = (const u32 *)(E + q * 16);
        for (u32 c = 0; c < 4; c++)
            if ((EL[q] >> c & 1) && a[c] != b[c]) {
                const u32 *r = rec_at(g, EW[q]);
                k_bad++;
                if (k_told++ < 10) uya_log("native moby direct check: run %u: VU1 qword %03X lane %u: VU %08X, the records %08X (record %u: kind %u, %08X %08X %08X)",
                                           C.serial, q, c, a[c], b[c], EW[q], r[0] & 0xFF, r[1], r[2], r[3]);
                break;
            }
        EL[q] = 0;
    }
    C.gif0 = uya_vif1_direct_n; C.ngif = 0;
    /* the run's end: no start left (GS blocks after the last start are not compared) */
    u32 k2 = C.k;
    while (k2 < g->count && (rec_at(g, k2)[0] & 0xFF) != R_MSCAL) k2++;
    if (k2 == g->count) { k_gifs_unchecked += g->count - C.k; C.on = 0; }
    if (k_starts % 20000 == 0)
        uya_log("native moby direct check: %llu program starts in %llu runs, %llu differences; runs not followed to their end %llu, starts the records do not have %llu; records after the last start %llu; "
                "runs the direct path would send through the chain: TTE %llu, waiting VIF %llu, pass 2 state %llu, setup %llu, hole words %llu, class packets %llu, image %llu",
                k_starts, k_runs, k_bad, k_left, k_extra, k_gifs_unchecked, k_wouldback[W_TTE], k_wouldback[W_NEED], k_wouldback[W_STATE], k_wouldback[W_SETUP], k_wouldback[W_PAD], k_wouldback[W_CLS], k_wouldback[W_IMAGE]);
}
