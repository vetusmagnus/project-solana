/* VIF0 and VIF1: the command streams DMA channels 0 and 1 (and the VIF FIFOs)
 * carry to the vector units.
 *
 * UNPACK writes VU data memory (every vn/vl format, signed/unsigned, the
 * STMASK/STROW/STCOL masking, the STCYCL write/skip cycles, the STMOD
 * offset/difference modes and VIF1's double buffering with BASE/OFFSET/TOPS
 * and the +TOPS flag). MPG uploads microcode (host/src/gfx/vu.c); MSCAL,
 * MSCALF and MSCNT run the translated microprogram there (tools/vu_recomp.py)
 * to completion: the VU programs end with XGKICKs that hand their GIF packets
 * to the GS. DIRECT/DIRECTHL carry GIF packets for the GS (PATH2). The flushes
 * have nothing to wait for, since everything before them has already run. */
#include "uya_host.h"
#include "uya_runtime.h"
#include "vu.h"
#include "../native/native_moby.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_gif_packet(const u32 *data, u32 qwc);
void vu_mpg(int vu, u32 addr, const u32 *w, u32 ninstr);
void vu_run(int vu, u32 pc);

typedef struct {
    int id;
    u32 cl, wl, mode, mask, row[4], col[4];
    u32 base, ofst, tops, top, itops, itop, dbf, mark;
    u32 code, need, got;         /* the command whose data is still coming */
    u32 *buf, cap;
} Vif;

static u32 buf0[0x1000], buf1[0x10000 * 4 + 16];
static Vif vif[2] = { { .id = 0, .cl = 1, .wl = 1, .buf = buf0, .cap = sizeof buf0 / 4 },
                      { .id = 1, .cl = 1, .wl = 1, .buf = buf1, .cap = sizeof buf1 / 4 } };
static unsigned long long cmd_count[128], cmd_words[128], frames;
static u32 mpg_src[32]; static unsigned long long mpg_src_n[32];   /* (UYA_VIFSTATS: MPGs by the address of their data) */

static const char *cmd_name(u32 c)
{
    switch (c) {
    case 0x00: return "NOP"; case 0x01: return "STCYCL"; case 0x02: return "OFFSET"; case 0x03: return "BASE";
    case 0x04: return "ITOP"; case 0x05: return "STMOD"; case 0x06: return "MSKPATH3"; case 0x07: return "MARK";
    case 0x10: return "FLUSHE"; case 0x11: return "FLUSH"; case 0x13: return "FLUSHA"; case 0x14: return "MSCAL";
    case 0x15: return "MSCALF"; case 0x17: return "MSCNT"; case 0x20: return "STMASK"; case 0x30: return "STROW";
    case 0x31: return "STCOL"; case 0x4A: return "MPG"; case 0x50: return "DIRECT"; case 0x51: return "DIRECTHL";
    }
    return (c & 0x60) == 0x60 ? "UNPACK" : "?";
}

u32 vu_vif_top(int vu) { return vif[vu & 1].top; }
u32 vu_vif_itop(int vu) { return vif[vu & 1].itop; }

/* VIF1's state, for the moby runs drawn without the VIF (host/src/native/native_mobyseg.c): read at a
 * run's start (TOPS, the double buffer, ITOPS; need: a command still waiting for data means the run
 * goes through the VIF), set at its end to the values the run's left-out codes would have left, known
 * from MobyProc's templates (no VIF code goes through here). The command still waiting for data
 * (code, need, got) is not set. uya_vif1_direct_n: testing (the check mode counts the GS-state
 * blocks against it). */
unsigned long long uya_vif1_direct_n;
void uya_vif1_get_state(UyaVifState *s)
{
    const Vif *v = &vif[1];
    s->cl = v->cl; s->wl = v->wl; s->mode = v->mode; s->mask = v->mask;
    memcpy(s->row, v->row, 16); memcpy(s->col, v->col, 16);
    s->base = v->base; s->ofst = v->ofst; s->tops = v->tops; s->top = v->top;
    s->itops = v->itops; s->itop = v->itop; s->dbf = v->dbf; s->mark = v->mark; s->need = v->need;
}
void uya_vif1_set_state(const UyaVifState *s)
{
    Vif *v = &vif[1];
    v->cl = s->cl; v->wl = s->wl; v->mode = s->mode; v->mask = s->mask;
    memcpy(v->row, s->row, 16); memcpy(v->col, s->col, 16);
    v->base = s->base; v->ofst = s->ofst; v->tops = s->tops; v->top = s->top;
    v->itops = s->itops; v->itop = s->itop; v->dbf = s->dbf; v->mark = s->mark;
}

/* vectors of an UNPACK of `num` writes that take data (the rest are filled) */
static u32 data_vectors(const Vif *v, u32 num)
{
    u32 cl = v->cl, wl = v->wl ? v->wl : 1;
    if (wl <= cl) return num;
    return (num / wl) * cl + (num % wl < cl ? num % wl : cl);
}

static u32 unpack_words(const Vif *v, u32 cmd, u32 num)
{
    u32 vn = (cmd >> 2) & 3, vl = cmd & 3;
    u32 bits = vl == 3 ? 16 : (32 >> vl) * (vn + 1);
    return (bits * data_vectors(v, num) + 31) / 32;
}

/* element k (of the format's element size) of the data */
static inline u32 elem(const u32 *d, u32 nwords, u32 vl, u32 k, int usn)
{
    switch (vl) {
    case 0: return k < nwords ? d[k] : 0;
    case 1: { if (k / 2 >= nwords) return 0; u16 h = ((const u16 *)d)[k]; return usn ? h : (u32)(s32)(s16)h; }
    case 2: { if (k / 4 >= nwords) return 0; u8 b = ((const u8 *)d)[k]; return usn ? b : (u32)(s32)(s8)b; }
    }
    return 0;
}


/* the usual UNPACK: no mask, every write takes data (WL <= CL); the format is
 * a constant here (see unpack()), so elem() comes down to a load */
static inline __attribute__((always_inline)) void unpack_plain(Vif *v, u8 *mem, u32 qmask, u32 addr, u32 cl, u32 wl,
                                                                const u32 *d, u32 nwords, u32 num, const u32 vn, const u32 vl, u32 usn)
{
    for (u32 k = 0; k < num; k++) {
        u32 qa = wl == cl ? addr + k : addr + (k / wl) * cl + k % wl, val[4];
        u32 e = k * (vn + 1);
        switch (vn) {
        case 0: val[0] = val[1] = val[2] = val[3] = elem(d, nwords, vl, e, usn); break;
        case 1: val[0] = val[2] = elem(d, nwords, vl, e, usn); val[1] = val[3] = elem(d, nwords, vl, e + 1, usn); break;
        default: for (int c = 0; c < 4; c++) val[c] = elem(d, nwords, vl, e + c, usn); break;
        }
        u32 *dst = (u32 *)(mem + ((qa & qmask) << 4));
        if (v->mode == 0) { dst[0] = val[0]; dst[1] = val[1]; dst[2] = val[2]; dst[3] = val[3]; }
        else if (v->mode == 1) for (int c = 0; c < 4; c++) dst[c] = val[c] + v->row[c];
        else if (v->mode == 2) for (int c = 0; c < 4; c++) { v->row[c] += val[c]; dst[c] = v->row[c]; }
        else for (int c = 0; c < 4; c++) dst[c] = val[c];
    }
}

static void unpack(Vif *v, u32 code, const u32 *d, u32 nwords)
{
    u32 cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    u32 vn = (cmd >> 2) & 3, vl = cmd & 3, m = (cmd >> 4) & 1, usn = (imm >> 14) & 1;
    u32 qmask = v->id ? 0x3FF : 0xFF;
    u32 addr = imm & 0x3FF;
    if (v->id && (imm & 0x8000)) addr += v->tops;
    u8 *mem = (u8 *)(uintptr_t)(v->id ? 0x1100C000u : 0x11004000u);
    u32 cl = v->cl, wl = v->wl ? v->wl : 1, di = 0;
    if (!m && wl <= cl && vl != 3) {
        switch (vn * 4 + vl) {
#define U(n, l) case n * 4 + l: unpack_plain(v, mem, qmask, addr, cl, wl, d, nwords, num, n, l, usn); return;
        U(0, 0) U(0, 1) U(0, 2) U(1, 0) U(1, 1) U(1, 2) U(2, 0) U(2, 1) U(2, 2) U(3, 0) U(3, 1) U(3, 2)
#undef U
        }
    }
    for (u32 k = 0; k < num; k++) {
        u32 pos = k % wl, qa, has;
        if (wl <= cl) { qa = addr + (k / wl) * cl + pos; has = 1; }
        else { qa = addr + k; has = pos < cl; }
        u32 val[4] = { 0, 0, 0, 0 };
        if (has) {
            if (vl == 3) {                              /* V4-5: RGBA 5:5:5:1 */
                u32 h = di / 2 < nwords ? ((const u16 *)d)[di] : 0;
                val[0] = (h & 0x1F) << 3; val[1] = ((h >> 5) & 0x1F) << 3; val[2] = ((h >> 10) & 0x1F) << 3; val[3] = ((h >> 15) & 1) << 7;
            } else {
                u32 e = di * (vn + 1);
                switch (vn) {
                case 0: val[0] = val[1] = val[2] = val[3] = elem(d, nwords, vl, e, usn); break;
                case 1: val[0] = val[2] = elem(d, nwords, vl, e, usn); val[1] = val[3] = elem(d, nwords, vl, e + 1, usn); break;
                case 2: for (int c = 0; c < 4; c++) val[c] = elem(d, nwords, vl, e + c, usn); break;   /* w: the next element */
                case 3: for (int c = 0; c < 4; c++) val[c] = elem(d, nwords, vl, e + c, usn); break;
                }
            }
            di++;
        }
        u32 *dst = (u32 *)(mem + ((qa & qmask) << 4));
        u32 r = pos < 3 ? pos : 3;
        for (int c = 0; c < 4; c++) {
            u32 mk = m ? (v->mask >> ((r * 4 + c) * 2)) & 3 : (has ? 0 : 1);
            switch (mk) {
            case 0:
                if (v->mode == 1) dst[c] = val[c] + v->row[c];
                else if (v->mode == 2) { v->row[c] += val[c]; dst[c] = v->row[c]; }
                else dst[c] = val[c];
                break;
            case 1: dst[c] = v->row[c]; break;
            case 2: dst[c] = v->col[r]; break;
            default: break;
            }
        }
    }
}

extern int uya_vif_log;
static void start(Vif *v, u32 pc)
{
    {   /* testing: UYA_STARTTRACE=a,b: every VU1 program start from vsync a to b, with TOPS, TOP and a hash of VU1 data memory */
        static long a = -2, b;
        if (a == -2) { const char *e = getenv("UYA_STARTTRACE"); a = -1; if (e) sscanf(e, "%ld,%ld", &a, &b); }
        long f = (long)uya_frame_count();
        if (v->id && a >= 0 && f >= a && f <= b) {
            const u8 *m = (const u8 *)(uintptr_t)0x1100C000u; u32 h = 2166136261u;
            for (u32 k = 0; k < 0x4000; k++) h = (h ^ m[k]) * 16777619u;
            uya_log("START v%ld pc %04X tops %03X top %03X dbf %u mem %08X", f, pc, v->tops, v->top, v->dbf, h);
        }
    }
    if (v->id) {
        v->top = v->tops;
        if (v->dbf) { v->tops = v->base; v->dbf = 0; }
        else { v->tops = (v->base + v->ofst) & 0x3FF; v->dbf = 1; }
    }
    v->itop = v->itops;
    vu_run(v->id, pc);
    if (uya_vif_log) { extern const char *uya_vu_cur_image(void); uya_log("    (ran image %s from %04X, tops %03X)", uya_vu_cur_image(), pc, v->top); }
}

static void bad_cmd(Vif *v, u32 code)
{
    static u32 seen[2][4];
    u32 c = (code >> 24) & 0x7F;
    if (seen[v->id][c / 32] & (1u << (c % 32))) return;
    seen[v->id][c / 32] |= 1u << (c % 32);
    uya_log("VIF%d: command 0x%02X (%08X) not handled", v->id, c, code);
}

/* run a command once all its data (d, n words) is there */
int uya_vif_log;   /* testing: UYA_DMALOG=n also lists send n's VIF commands */

static u32 hist[64][3], hist_n;   /* testing: the last VIF1 commands (code, TOPS, first data word) */
void uya_vif1_dump_hist(void)
{
    uya_log("VIF1: BASE %03X OFFSET %03X TOPS %03X TOP %03X DBF %u ITOP %03X; last commands:", vif[1].base, vif[1].ofst, vif[1].tops, vif[1].top, vif[1].dbf, vif[1].itop);
    for (u32 i = 0; i < 64; i++) { u32 k = (hist_n + i) & 63; if (hist[k][0] || hist[k][1]) uya_log("   %-8s %08X  tops %03X  data0 %08X", cmd_name((hist[k][0] >> 24) & 0x7F), hist[k][0], hist[k][1], hist[k][2]); }
}

/* ---- VIF1 as a stub ----
 * With the native producers (ties, shrubs, terrain, mobys, particles, strips) drawing from records,
 * what gameplay's chain still gives VIF1 is NOPs, FLUSHes, MARKs, MPGs (microprogram uploads: micro
 * memory for the programs the records start) and DIRECT data (texture uploads and the game's own GIF
 * data: native_gif). Those are the stub's. Any other command (UNPACK, MSCAL, STCYCL, STMOD, STMASK,
 * BASE, OFFSET...) belongs to the full VIF: it still runs (a chain with no native producer, as the
 * boot screens, menus and movies may give, keeps working), and is counted and logged (once per
 * command, with the vsync and the chain tag) so what still needs it shows. UYA_VIFSTUB: 1 (default)
 * as said; 2 the full VIF's commands are dropped (testing: gameplay needs none); 0 no counting. */
unsigned long long uya_vif1_full_n;
extern u32 uya_dma_tag_ring[16][5], uya_dma_tag_n;
static int stub_mode = -1;
static inline int stub_own(u32 cmd) { return cmd == 0x00 || cmd == 0x07 || cmd == 0x10 || cmd == 0x11 || cmd == 0x13 || cmd == 0x4A || cmd == 0x50 || cmd == 0x51; }
static int stub_full(u32 code)
{
    u32 cmd = (code >> 24) & 0x7F;
    if (stub_mode < 0) { const char *e = getenv("UYA_VIFSTUB"); stub_mode = e ? atoi(e) : 1; }
    if (!stub_mode || stub_own(cmd)) return 0;
    uya_vif1_full_n++;
    {   /* testing: UYA_STUBTRACE=from,n: the chain tags before the first n full-VIF commands from vsync `from` */
        static long from = -2; static int left;
        if (from == -2) { const char *e = getenv("UYA_STUBTRACE"); from = -1; if (e) sscanf(e, "%ld,%d", &from, &left); }
        if (from >= 0 && left > 0 && (long)uya_frame_count() >= from) {
            left--;
            uya_log("VIF1 stub trace: %s (%08X) at vsync %u; the last chain tags:", cmd_name(cmd), code, uya_frame_count());
            for (u32 k = 0; k < 16; k++) { const u32 *t = uya_dma_tag_ring[(uya_dma_tag_n + k) & 15]; uya_log("   tag %08X: %08X %08X  %08X %08X", t[0], t[1], t[2], t[3], t[4]); }
            u32 a0 = uya_dma_tag_ring[(uya_dma_tag_n - 1) & 15][0];
            for (u32 k = 1; k <= 12 && a0 < 0x2000000u; k++) { const u32 *q = (const u32 *)RC_A(a0 + 16 * k); uya_log("   then %08X: %08X %08X %08X %08X", a0 + 16 * k, q[0], q[1], q[2], q[3]); }
        }
    }
    static u8 told[128];
    if (!told[cmd & 0x7F]) {
        told[cmd & 0x7F] = 1;
        const u32 *t = uya_dma_tag_ring[(uya_dma_tag_n - 1) & 15];
        uya_log("VIF1 stub: %s (%08X) went to the full VIF at vsync %u (chain tag at %08X: %08X %08X %08X %08X)%s", cmd_name(cmd), code, uya_frame_count(), t[0], t[1], t[2], t[3], t[4], stub_mode == 2 ? "; dropped (UYA_VIFSTUB=2)" : "");
    }
    return stub_mode == 2;
}
static void exec(Vif *v, u32 code, const u32 *d, u32 n)
{
    u32 cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
    if (v->id && stub_full(code)) return;
    if (v->id) { cmd_count[cmd]++; cmd_words[cmd] += n; hist[hist_n & 63][0] = code; hist[hist_n & 63][1] = v->tops; hist[hist_n & 63][2] = n ? d[0] : 0; hist_n++; }
    if (uya_vif_log) uya_log("    VIF%d %-8s %08X (%u data words)", v->id, cmd_name(cmd), code, n);
    switch (cmd) {
    case 0x00: case 0x06: case 0x10: case 0x11: case 0x13: break;
    case 0x01: v->cl = imm & 0xFF; v->wl = (imm >> 8) & 0xFF; break;
    case 0x02: v->ofst = imm & 0x3FF; v->dbf = 0; v->tops = v->base; break;
    case 0x03: v->base = imm & 0x3FF; break;
    case 0x04: v->itops = imm & 0x3FF; break;
    case 0x05: v->mode = imm & 3; break;
    case 0x07: v->mark = imm; break;
    case 0x14: case 0x15: start(v, imm * 8); break;
    case 0x17: start(v, v->id ? rc_vu1.tpc : rc_vu0.tpc); break;
    case 0x20: v->mask = d[0]; break;
    case 0x30: memcpy(v->row, d, 16); break;
    case 0x31: memcpy(v->col, d, 16); break;
    case 0x4A:
        if (v->id) {   /* (the census: by the data's address, a REF's into .vutext) */
            u32 a = (u32)(uintptr_t)d; int k;
            for (k = 0; k < 32 && mpg_src_n[k] && mpg_src[k] != a; k++) ;
            if (k < 32) { mpg_src[k] = a; mpg_src_n[k]++; }
        }
        vu_mpg(v->id, imm * 8, d, num ? num : 256); break;
    case 0x50: case 0x51:
        if (v->id) {
            uya_vif1_direct_n++;
            extern const char *uya_gs_src; uya_gs_src = "PATH2 (VIF1 DIRECT)";
            static int left = -1;      /* testing: UYA_BADTEX=n logs DIRECT packets with a TEX0 whose top half looks like a pointer */
            if (left < 0) { const char *e = getenv("UYA_BADTEXV"); left = e ? atoi(e) : 0; }
            if (left > 0)
                for (u32 k = 0; k + 3 < n; k += 4)
                    if ((d[k + 2] & 0xFF) == 6 && (d[k + 1] >> 16) >= 0xFF00) {
                        left--;
                        uya_log("DIRECT packet at %08X (%u qwords) has TEX0 %08X%08X at qword %u:", (u32)(uintptr_t)d, n / 4, d[k + 1], d[k], k / 4);
                        for (u32 j = 0; j < n && j < 64; j += 4) uya_log("   %08X: %08X %08X %08X %08X", (u32)(uintptr_t)(d + j), d[j], d[j + 1], d[j + 2], d[j + 3]);
                        break;
                    }
            { void native_gif(int path, const u32 *d, u32 qwc); native_gif(1, d, n / 4); }
        }
        else bad_cmd(v, code);
        break;
    default:
        if ((cmd & 0x60) == 0x60) unpack(v, code, d, n);
        else bad_cmd(v, code);
        break;
    }
}

/* words of data that follow a command */
static u32 data_words(Vif *v, u32 code)
{
    u32 cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
    switch (cmd) {
    case 0x20: return 1;
    case 0x30: case 0x31: return 4;
    case 0x4A: return (num ? num : 256) * 2;
    case 0x50: case 0x51: return (imm ? imm : 0x10000) * 4;
    }
    if ((cmd & 0x60) == 0x60) return unpack_words(v, cmd, num ? num : 256);
    return 0;
}

static void feed(Vif *v, const u32 *w, u32 n)
{
    u32 i = 0;
    while (i < n) {
        if (v->need) {
            u32 take = v->need < n - i ? v->need : n - i;
            if (!v->got && take == v->need) {            /* all of it here: no copy */
                v->need = 0;
                exec(v, v->code, w + i, take);
                i += take;
                continue;
            }
            for (u32 k = 0; k < take; k++) if (v->got + k < v->cap) v->buf[v->got + k] = w[i + k];
            v->got += take; v->need -= take; i += take;
            if (!v->need) { u32 g = v->got < v->cap ? v->got : v->cap; v->got = 0; exec(v, v->code, v->buf, g); }
            continue;
        }
        u32 code = w[i++];
        u32 nd = data_words(v, code);
        if (!nd) { exec(v, code, 0, 0); continue; }
        v->code = code; v->need = nd; v->got = 0;
    }
}

void uya_vif0_words(const u32 *w, u32 n) { feed(&vif[0], w, n); }
void uya_vif0_data(const u32 *w, u32 qwc) { feed(&vif[0], w, qwc * 4); }
void uya_vif1_words(const u32 *w, u32 n) { feed(&vif[1], w, n); }
void uya_vif1_data(const u32 *w, u32 qwc) { feed(&vif[1], w, qwc * 4); }

void uya_gfx_frame_stats(void)
{
    frames++;
    {   /* testing: UYA_VIFSTATS=a,b: VIF1 commands (with their data words) and VU programs from vsync a to b */
        static long a = -2, b; static int st; static unsigned long long f0; void vu_stats(void); void vu_stats_reset(void);
        if (a == -2) { const char *e = getenv("UYA_VIFSTATS"); a = -1; if (e) sscanf(e, "%ld,%ld", &a, &b); }
        long v = (long)uya_frame_count();
        if (a >= 0 && st == 0 && v >= a) { st = 1; f0 = frames; memset(cmd_count, 0, sizeof cmd_count); memset(cmd_words, 0, sizeof cmd_words); memset(mpg_src_n, 0, sizeof mpg_src_n); vu_stats_reset(); }
        if (st == 1 && v >= b) {
            st = 2;
            double n = (double)(frames - f0); if (n < 1) n = 1;
            uya_log("VIF1 commands from vsync %ld to %ld (%.0f sends): count, data words, per send:", a, b, n);
            for (int c = 0; c < 128; c++)
                if (cmd_count[c]) uya_log("  %-8s %02X %10.1f %12.1f", cmd_name(c), c, (double)cmd_count[c] / n, (double)cmd_words[c] / n);
            for (int k = 0; k < 32 && mpg_src_n[k]; k++) uya_log("  MPG data at %08X: %.1f per send", mpg_src[k], (double)mpg_src_n[k] / n);
            vu_stats();
        }
    }
    if (frames == 3 || frames == 600 || frames == 3000) {
        void vu_stats(void);
        uya_log("VIF1 command counts after %llu sends:", frames);
        for (int c = 0; c < 128; c++)
            if (cmd_count[c]) uya_log("  %-8s %llu", cmd_name(c), cmd_count[c]);
        vu_stats();
    }
}

/* ---- VIF1 operations for records (host/src/native: a producer drawn without the chain) ----
 * The same effects as the VIF commands, in the record's order, without VIF codes going through the
 * VIF: a register set (STCYCL, OFFSET, BASE, ITOP, STMOD: the code word), STROW, an UNPACK of data
 * (at the code's address, by the current cycle, mode and mask), a program's start (MSCAL: TOP and
 * the double buffer as start() leaves them). They are not counted as VIF commands (UYA_VIFSTATS
 * counts what is still in the chain). */
void uya_vif1_op_reg(u32 code)
{
    Vif *v = &vif[1];
    u32 imm = code & 0xFFFF;
    switch ((code >> 24) & 0x7F) {
    case 0x01: v->cl = imm & 0xFF; v->wl = (imm >> 8) & 0xFF; break;
    case 0x02: v->ofst = imm & 0x3FF; v->dbf = 0; v->tops = v->base; break;
    case 0x03: v->base = imm & 0x3FF; break;
    case 0x04: v->itops = imm & 0x3FF; break;
    case 0x05: v->mode = imm & 3; break;
    default: break;
    }
}
void uya_vif1_op_strow(const u32 row[4]) { memcpy(vif[1].row, row, 16); }
void uya_vif1_op_stcol(const u32 col[4]) { memcpy(vif[1].col, col, 16); }
void uya_vif1_op_stmask(u32 mask) { vif[1].mask = mask; }
void uya_vif1_op_unpack(u32 code, const u32 *d, u32 nwords) { unpack(&vif[1], code, d, nwords); }
void uya_vif1_op_mscal(u32 pc) { start(&vif[1], pc); }
/* a microprogram packet (as the game REFs it: FLUSH, FLUSHE, FLUSHA, NOP and MPG codes) uploaded to VU1's
 * micro memory as the VIF would; 0 if it has other codes (nothing done past them) */
int uya_vif1_op_mpg_packet(const u32 *w, u32 qwc)
{
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
