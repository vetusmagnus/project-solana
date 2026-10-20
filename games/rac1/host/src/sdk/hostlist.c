/* The host draw list (steps B and C: the DMA chain walker replaced by a host list).
 *
 * The frame is drawn from it: the channel-1 walk (sdk/dma.c) takes every tag, and the data a CNT / NEXT
 * carries after it, from this list, not from the game's memory (an item the list does not hold is read
 * from the game's memory, counted and logged; none was in the tests). UYA_HL=0: the old walk of the
 * game's memory, for comparisons; UYA_HLCHECK=1: the game's memory compared with the list, item by item.
 *
 * The frame's items go into the list by their address as they are made, not when the chain is walked:
 *   the links between the producers' parts: host events from the splices' hooks (native_link.c, which
 *     know them from the chain pointer: uya_hl_link);
 *   the records' markers: as the native modules make them (uya_hl_put);
 *   what a hooked writer wrote itself (the return scan of native_2d.c, uya_texdma_exit: the producers,
 *     the natives' wrappers, the small helpers): the region it moved the chain pointer across, every
 *     qword of it, taken when it returns (the latest taking stands: a native writes its markers over
 *     what a call inside it wrote), except where the host put a link this frame (a link is the host's,
 *     not read back). A slot changed after the last taking that covers it shows in the check as
 *     "changed since taken".
 *
 * Check (UYA_HLCHECK=1): at each channel-1 send the list is followed from the chain's start (as the DMAC
 * follows tags: CNT past its data, NEXT to its address, REF / REFS to the next qword, CALL / RET, END)
 * and compared, item by item, with what the walker reads: the same address and the same four words.
 * A mismatch is logged (the first few, then counts every 300 sends): "not in the list" (a writer not
 * hooked yet), or "changed since taken" (written again after the last return that took it). */
#include "uya_host.h"
#include "uya_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void uya_log(const char *fmt, ...);

#define HL_Q (1u << 21)                 /* a slot a qword of the game's 32 MB */
static int hl_on = -1;
static u32 (*hl_val)[4];                /* the item taken at that qword (its tag and VIF words) */
static u32 *hl_stamp;                   /* when it was taken (0: never) */
static u32 *hl_gen, hl_cur = 1;         /* the frame (channel-1 send) it was taken in */
static u32 *hl_lk;                      /* the frame the host put a link there in */
static u32 hl_clock = 1;

int uya_hl_on(void)
{
    if (hl_on < 0) {
        const char *e = getenv("UYA_HL");
        hl_on = e ? atoi(e) != 0 : 1;
        if (hl_on) {
            hl_val = calloc(HL_Q, 16); hl_stamp = calloc(HL_Q, 4); hl_gen = calloc(HL_Q, 4); hl_lk = calloc(HL_Q, 4);
            if (!hl_val || !hl_stamp || !hl_gen || !hl_lk) hl_on = 0;
        }
        uya_log("host draw list: %s (UYA_HL)", hl_on ? "taken at the writers, the frame drawn from it" : "off: the game's memory walked");
    }
    return hl_on;
}

static unsigned long long n_taken, n_links;
static u32 hl_lastE;                    /* the end of the latest taking */
static void put(u32 a)
{
    u32 s = (a & 0x1FFFFF0u) >> 4;
    memcpy(hl_val[s], (const u32 *)(uintptr_t)(a & 0x1FFFFF0u), 16); hl_stamp[s] = ++hl_clock; hl_gen[s] = hl_cur; n_taken++;
}
/* a link the host knows (native_link.c): a NEXT at `at` to `to` */
void uya_hl_link(u32 at, u32 to)
{
    if (!uya_hl_on() || at < 0x100000u || at >= 0x2000000u) return;
    u32 s = (at & 0x1FFFFF0u) >> 4;
    hl_val[s][0] = 0x20000000u; hl_val[s][1] = to; hl_val[s][2] = 0; hl_val[s][3] = 0;
    hl_stamp[s] = ++hl_clock; hl_gen[s] = hl_cur; hl_lk[s] = hl_cur; n_links++;
}
/* an item the host knows (native_link.c): its four words at `at` */
void uya_hl_item(u32 at, u32 w0, u32 w1, u32 w2, u32 w3)
{
    if (!uya_hl_on() || at < 0x100000u || at >= 0x2000000u) return;
    u32 s = (at & 0x1FFFFF0u) >> 4;
    hl_val[s][0] = w0; hl_val[s][1] = w1; hl_val[s][2] = w2; hl_val[s][3] = w3;
    hl_stamp[s] = ++hl_clock; hl_gen[s] = hl_cur; hl_lk[s] = hl_cur; n_links++;
}

/* one item, as a native module makes it (a record's marker) */
void uya_hl_put(u32 a) { if (uya_hl_on() && a >= 0x100000u && a < 0x2000000u) put(a); }

/* a writer's region [T, E): its qwords into the list (not over the host's links of this frame) */
/* testing (UYA_HLDBG=a,b and UYA_HLDBGV=v): 1 if [T, E) meets [a, b) within vsyncs v..v+2 */
int uya_hl_dbg(u32 T, u32 E)
{
    static int on = -1; static u32 da, db; static long long dv;
    if (on < 0) { const char *e = getenv("UYA_HLDBG"), *v = getenv("UYA_HLDBGV"); on = e && sscanf(e, "%x,%x", &da, &db) == 2; dv = v ? atoll(v) : 1448; }
    return on && T < db && E > da && (long long)uya_frame_count() >= dv && (long long)uya_frame_count() < dv + 3;
}
void uya_hl_take(u32 T, u32 E)
{
    if (!uya_hl_on()) return;
    {   /* testing: UYA_HLDBG=a,b logs the takings that cover [a, b) (or are refused) */
        static int on = -1; static u32 da, db;
        static long long dv = -1;
        if (on < 0) { const char *e = getenv("UYA_HLDBG"), *v = getenv("UYA_HLDBGV"); on = e && sscanf(e, "%x,%x", &da, &db) == 2; dv = v ? atoll(v) : 1448; }
        if (on && ((T < db && E > da) || T >= E || E - T > 0x400000u) && (long long)uya_frame_count() >= dv && (long long)uya_frame_count() < dv + 3)
            uya_log("HLDBG take %08X..%08X%s", T, E, (T < 0x100000u || E > 0x2000000u || T >= E || E - T > 0x400000u || (T & 15)) ? " (refused)" : "");
    }
    if (T < 0x100000u || E > 0x2000000u || T >= E || E - T > 0x400000u || (T & 15)) return;
    for (; T < E; T += 16)   /* (every qword of it: a region also holds data its tags REF, which a parse of the tags would lose step on) */
        if (hl_lk[(T & 0x1FFFFF0u) >> 4] != hl_cur) put(T);
}

/* a taking at the chain pointer (the return scan's): the latest one's end kept for the send's tail, and a
 * short gap since it (what the routine running between two hooked calls wrote itself: space.c's fade REF
 * before func_00232A00) kept for the send */
static u32 gap[64][2], ngap;
void uya_hl_take_chain(u32 T, u32 E)
{
    uya_hl_take(T, E);
    if (!uya_hl_on() || E <= T) return;
    if (hl_lastE && T > hl_lastE && T - hl_lastE <= 0x1000u && ngap < 64) { gap[ngap][0] = hl_lastE; gap[ngap][1] = T; ngap++; }
    hl_lastE = E;
}

/* at a send (native_link.c, VU1_sendChain's entry): what the sending routine wrote itself just before,
 * between the latest taking and the chain pointer (FadeToBlack's fade REF), a short run, taken; and the
 * gaps kept since the last send */
void uya_hl_take_tail(u32 p)
{
    if (!uya_hl_on()) return;
    for (u32 i = 0; i < ngap; i++) if (gap[i][1] <= p) uya_hl_take(gap[i][0], gap[i][1]);
    ngap = 0;
    if (!hl_lastE || hl_lastE >= p || p - hl_lastE > 0x1000u) return;
    uya_hl_take(hl_lastE, p);
}

/* ---- drawing from the list (step C): UYA_HLDRAW=1 ----
 * The channel-1 walk takes each tag, and the data a CNT / NEXT / CALL / RET / END carries after it, from
 * the list instead of the game's memory (sdk/dma.c); the game's copy is compared with it, item by item and
 * qword by qword (a difference: counted, the first few logged), and an item the list does not hold is
 * read from the game's memory (counted). */
static int draw_mode = -1;
int uya_hl_draw_on(void)
{
    if (draw_mode < 0) { const char *e = getenv("UYA_HLDRAW"); draw_mode = (e ? atoi(e) != 0 : 1) && uya_hl_on(); }
    return draw_mode;
}
static unsigned long long d_items, d_miss, d_diff, d_qw;
static int check_mode = -1;
static int hl_check(void) { if (check_mode < 0) { const char *e = getenv("UYA_HLCHECK"), *f = getenv("UYA_HLFROM"); check_mode = ((e && atoi(e)) || f) && uya_hl_on(); if (check_mode) uya_log("host draw list: checked against the game's memory (UYA_HLCHECK)"); } return check_mode; }
const u32 *uya_hl_draw(u32 t, const u32 *g)
{
    if (!uya_hl_draw_on() || t < 0x100000u || t >= 0x2000000u || (t & 15)) return g;
    u32 s = t >> 4;
    d_items++;
    if (!hl_stamp[s]) { if (d_miss++ < 5) uya_log("host draw list (drawing): %08X not in the list: the game's memory read (vsync %u)", t, uya_frame_count()); return g; }
    const u32 *h = hl_val[s];
    u32 id = (h[0] >> 28) & 7, n = (id == 1 || id == 2 || id == 5 || id == 6 || id == 7) ? 1 + (h[0] & 0xFFFF) : 1;
    if (s + n > HL_Q) n = 1;
    d_qw += n;
    if (hl_check()) for (u32 k = 0; k < n; k++)
        if (!hl_stamp[s + k] || memcmp(hl_val[s + k], g + 4 * k, 16)) {
            if (d_diff++ < 10) uya_log("host draw list (drawing): %08X qword %u differs from the game's (vsync %u): list %08X %08X %08X %08X, game %08X %08X %08X %08X",
                                       t, k, uya_frame_count(), hl_val[s + k][0], hl_val[s + k][1], hl_val[s + k][2], hl_val[s + k][3], g[4 * k], g[4 * k + 1], g[4 * k + 2], g[4 * k + 3]);
            break;
        }
    return h;
}
void uya_hl_draw_stats(void)
{
    if (draw_mode > 0) uya_log("host draw list (drawing): %llu items read from it (%llu qwords); %llu not in it, %llu differing from the game's", d_items, d_qw, d_miss, d_diff);
}

/* ---- the check at a channel-1 send ---- */
#define HL_MAX 262144
static u32 (*hs)[5], nh;                /* the list followed: address and words */
static u32 (*ws)[5], nw;                /* what the walker read */
static u8 *wl;                          /* the list's item there as the walker read it: 0 the same, 1 none, 2 another */
static unsigned long long c_sends, c_items, c_bad_sends, c_missing, c_changed, c_order, c_putback;
void uya_hl_send_begin(u32 t)
{
    if (!hl_check()) return;
    if (!hs) { hs = malloc(sizeof *hs * HL_MAX); ws = malloc(sizeof *ws * HL_MAX); wl = malloc(HL_MAX); if (!hs || !ws || !wl) { hl_on = 0; return; } }
    nh = nw = 0;
    u32 stack[2], sp = 0;
    for (u32 g = 0; g < HL_MAX; g++) {
        if (t < 0x100000u || t >= 0x2000000u) break;
        u32 s = (t & 0x1FFFFF0u) >> 4;
        if (!hl_stamp[s]) { hs[nh][0] = t | 1; nh++; break; }   /* (not in the list: marked by bit 0) */
        const u32 *q = hl_val[s];
        hs[nh][0] = t; memcpy(hs[nh] + 1, q, 16); nh++;
        u32 id = (q[0] >> 28) & 7, qwc = q[0] & 0xFFFF, addr = q[1] & 0x7FFFFFF0u;
        if (id == 0 || id == 7) break;
        if (id == 1) t = t + 16 + 16 * qwc;
        else if (id == 2) t = addr;
        else if (id == 3 || id == 4) t += 16;
        else if (id == 5) { if (sp < 2) stack[sp++] = t + 16 + 16 * qwc; t = addr; }
        else { if (!sp) break; t = stack[--sp]; }
    }
}
void uya_hl_walk_item(u32 t, const u32 *q)
{
    if (check_mode <= 0 || !hs || nw >= HL_MAX) return;
    u32 s_ = (t & 0x1FFFFF0u) >> 4;
    wl[nw] = t < 0x100000u || t >= 0x2000000u ? 0 : !hl_stamp[s_] ? 1 : memcmp(hl_val[s_], q, 16) ? 2 : 0;
    ws[nw][0] = t; memcpy(ws[nw] + 1, q, 16); nw++;
}
void uya_hl_draw_stats(void);
void uya_hl_send_end(int suspended)
{
    if (hl_on <= 0) return;
    if (!suspended) hl_cur++;   /* (the next frame's items) */
    { static unsigned long long n; if (++n % 1500 == 0) uya_hl_draw_stats(); }
    if (check_mode <= 0 || !hs) return;
    c_sends++; c_items += nw;
    u32 k = 0;
    while (k < nw && k < nh && !memcmp(ws[k], hs[k], 20)) k++;
    int bad = k < nw;   /* (the walker read an item the list does not have there) */
    if (bad && k > 0 && ws[k][0] == ws[k - 1][0]) { bad = 0; c_putback++; }   /* (a record put its packet back, changed since: the walker reads it again; the list goes on as the record) */
    if (bad) {
        c_bad_sends++;
        const char *why;
        u32 s = (ws[k][0] & 0x1FFFFF0u) >> 4;
        if (k >= nh) why = "the list ends before it";
        else if (hs[k][0] & 1) { why = "not in the list"; c_missing++; }
        else if (hs[k][0] != ws[k][0]) { why = "another address in the list"; c_order++; }
        else { why = "changed since taken"; c_changed++; }
        static int told; static long long from = -1;
        if (from < 0) { const char *e = getenv("UYA_HLFROM"); from = e ? atoll(e) : 0; }   /* (testing: log from that vsync) */
        if (told < 40 && (long long)uya_frame_count() >= from) {
            told++;
            u32 uya_cw_owner(u32, u32 *); u32 st = 0, ow = uya_cw_owner(ws[k][0], &st);
            uya_log("host draw list: send %llu (vsync %u), item %u of %u at %08X: %s; walker %08X %08X %08X %08X, list %08X: %08X %08X %08X %08X (taken at %u, now %u)%s; written by %08X from %08X",
                    c_sends, uya_frame_count(), k, nw, ws[k][0], why, ws[k][1], ws[k][2], ws[k][3], ws[k][4],
                    k < nh ? hs[k][0] : 0, k < nh ? hs[k][1] : 0, k < nh ? hs[k][2] : 0, k < nh ? hs[k][3] : 0, k < nh ? hs[k][4] : 0,
                    ws[k][0] < 0x2000000u ? hl_stamp[s] : 0, hl_clock, suspended ? " (the walk stopped at an interrupt)" : "", ow, st);
        }
    }
    /* testing (UYA_HLFROM=v): every item the walker read, looked up by its address; the ones the list does not
     * hold as read counted by their writer (the chain-watch tree names it), printed 300 sends after v */
    {
        static long long from = -2; static int done;
        if (from == -2) { const char *e = getenv("UYA_HLFROM"); from = e ? atoll(e) : -1; }
        typedef struct { u32 ow, st, why; unsigned long long n; u32 ex; } Ce;
        static Ce ce[256]; static int nce; static unsigned long long sends0;
        if (from >= 0 && !done && (long long)uya_frame_count() >= from) {
            if (!sends0) sends0 = c_sends;
            for (u32 i = 0; i < nw; i++) {
                u32 a = ws[i][0], why = wl[i];
                if (!why) continue;
                u32 uya_cw_owner(u32, u32 *); u32 st = 0, ow = uya_cw_owner(a, &st);
                int j; for (j = 0; j < nce && (ce[j].ow != ow || ce[j].st != st || ce[j].why != why); j++) ;
                if (j == nce && nce < 256) { nce++; ce[j].ow = ow; ce[j].st = st; ce[j].why = why; ce[j].n = 0; ce[j].ex = a; }
                if (j < nce) ce[j].n++;
            }
            if (c_sends - sends0 >= 300) {
                done = 1;
                uya_log("host draw list census, sends %llu-%llu: %d kinds", sends0, c_sends, nce);
                for (int j = 0; j < nce; j++) uya_log("   %8llu  %s  written by %08X from %08X (e.g. %08X)", ce[j].n, ce[j].why == 1 ? "not in the list     " : "changed since taken ", ce[j].ow, ce[j].st, ce[j].ex);
            }
        }
    }
    if (c_sends % 300 == 0 || (c_sends <= 2048 && (c_sends & (c_sends - 1)) == 0))
        uya_log("host draw list: %llu sends, %llu items walked; sends that differ %llu (not in the list %llu, changed since taken %llu, another address %llu); records put back %llu; %llu items taken, %llu host links",
                c_sends, c_items, c_bad_sends, c_missing, c_changed, c_order, c_putback, n_taken, n_links);
}
