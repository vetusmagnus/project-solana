/* testing: who writes the game's GIF packets (UYA_CWATCH).
 *
 * A build made with UYA_RC_CWATCH=1 in the environment (tools/recomp.py) wraps every jal/jalr in the
 * translated code with rc_cw_pre / rc_cw_post: a call that moved the VIF1 chain pointer (the word at
 * UYA_CWATCH=addr, hex: 161000 in the executable, 161240 or 161280 in the levels; 0: the one the 2D hooks find) owns the qwords it
 * moved it over, unless a call inside it already owns them (the innermost writer). Where the chain meets
 * a VIF1 DIRECT, uya_cw_direct counts the packet against its owner (the called address and the call's
 * place); UYA_CWATCH_AT=a,b (vsyncs) prints the census at b. Off (a normal build) it costs nothing. */
#include "rc.h"
#include "uya_host.h"
#include "uya_runtime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void uya_log(const char *fmt, ...);
static int on = -1;
static u32 cwa, from, to; static int autow;
static u32 *stamp; static u32 (*own)[2];
static u32 clock_;
static void init(void)
{
    const char *e = getenv("UYA_CWATCH"), *w = getenv("UYA_CWATCH_AT");
    on = 0;
    if (!e) return;
    cwa = (u32)strtoul(e, 0, 16);   /* (0: the chain word the 2D hooks last found) */
    autow = !cwa;
    from = 1000; to = 1400;
    if (w) sscanf(w, "%u,%u", &from, &to);
    stamp = calloc(1u << 21, 4); own = calloc(1u << 21, 8);
    if (!stamp || !own) return;
    on = 1;
    uya_log("chain watch: the chain pointer at %08X, census over vsyncs %u-%u", cwa, from, to);
}
extern u32 uya_chain_word_seen;
u32 rc_cw_pre(void)
{
    if (on < 0) init();
    if (!on) return 0;
    if (autow && uya_chain_word_seen != cwa) { cwa = uya_chain_word_seen; uya_log("chain watch: the chain word is %08X", cwa); }
    if (!cwa) return 0;
    return ++clock_;
}
void rc_cw_post(u32 s, u32 target, u32 site, u32 before)
{
    if (!on || !cwa) return;
    u32 now = M32(cwa);
    if (now <= before || now - before > 0x100000u || before < 0x100000u || now > 0x2000000u) return;
    u32 t = ++clock_;
    for (u32 q = before >> 4; q < now >> 4; q++)
        if (stamp[q] < s) { stamp[q] = t; own[q][0] = target; own[q][1] = site; }
}
u32 rc_cw_word(void) { if (on < 0) init(); return on && cwa ? M32(cwa) : 0; }

typedef struct { u32 target, site, tag0, tag1, tag2, tag3; unsigned long long pk, qw, kicks; } Ent;
static Ent ent[512]; static int nent;
void uya_cw_direct(const u32 *d, u32 qwc, unsigned long long kicks)
{
    if (on < 0) init();
    if (!on) return;
    u32 f = uya_frame_count();
    if (f < from || !cwa) return;
    u32 a = (u32)(uintptr_t)d & 0x1FFFFFFu;
    u32 tg = a < 0x2000000u ? own[a >> 4][0] : 0, st = a < 0x2000000u ? own[a >> 4][1] : 0;
    if (!tg) {   /* (a REF's data: the owner of its tag) */
        extern u32 uya_dma_tag_ring[16][5], uya_dma_tag_n;
        u32 t = uya_dma_tag_ring[(uya_dma_tag_n - 1) & 15][0] & 0x1FFFFFFu;
        if (t < 0x2000000u && own[t >> 4][0]) { tg = own[t >> 4][0]; st = own[t >> 4][1] | 1; }
    }
    if (f < to) {
        int k;
        for (k = 0; k < nent && (ent[k].target != tg || ent[k].site != st); k++) ;
        if (k == nent && nent < 512) { nent++; ent[k].target = tg; ent[k].site = st; ent[k].tag0 = d[0]; ent[k].tag1 = d[1]; ent[k].tag2 = d[2]; ent[k].tag3 = d[3]; }
        if (k < nent) { ent[k].pk++; ent[k].qw += qwc; ent[k].kicks += kicks; }
        return;
    }
    static int done;
    if (done++) return;
    double nv = to - from;
    for (int i = 0; i < nent; i++)
        for (int j = i + 1; j < nent; j++)
            if (ent[j].kicks > ent[i].kicks || (ent[j].kicks == ent[i].kicks && ent[j].qw > ent[i].qw)) { Ent x = ent[i]; ent[i] = ent[j]; ent[j] = x; }
    uya_log("chain watch census, vsyncs %u-%u (per vsync): writer (called address, from), packets, qwords, drawn kicks, first GIF tag", from, to);
    for (int i = 0; i < nent; i++)
        uya_log("  %08X from %08X: %.2f packets %.1f qw %.1f kicks  tag %08X %08X %08X %08X", ent[i].target, ent[i].site,
                ent[i].pk / nv, ent[i].qw / nv, ent[i].kicks / nv, ent[i].tag1, ent[i].tag0, ent[i].tag3, ent[i].tag2);
}
