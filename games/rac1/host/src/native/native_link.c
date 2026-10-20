/* The chain's links as host events (step B of the host draw list, sdk/hostlist.c).
 *
 * The frame's order comes from one idiom, used by every subsystem (src/game/draw.c, shrubfunc.c,
 * tfragfunc.c, mobyfunc.c, skyfunc.c in the decomp):
 *   the reserver leaves a qword R at the chain pointer (DrawShrubs, DrawTfrag, SetupSkyGifPaging,
 *     SetupGifPaging at their entry; DrawMobysSetup, func_0020E0C8, just before it returns);
 *   the producers draw after it;
 *   the splicer takes the qword p at the chain pointer, appends the textures' uploads after it, and
 *     links: R -> p + 16 (the uploads first), after the uploads a NEXT -> R + 16 (then the draws), and
 *     p -> E (then what follows the uploads), E the chain pointer it leaves (DmaShrubTextures,
 *     DmaTfragTextures, DmaMobyTextures, DoSkyGifPaging, DoGifPaging).
 * These hooks know R, p and E from the chain pointer alone (when the reserver and the splicer run), so
 * the three links are the host's own: handed to the host draw list (uya_hl_link), and checked against
 * the words the game's routine wrote (a difference: counted and logged, the links not handed over).
 * Each hook also runs the return scan around its routine (native_2d.c: the conversions, the captures). */
#include "rc.h"
#include "uya_host.h"
#include "uya_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_log(const char *fmt, ...);
int uya_texdma_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na);
void uya_texdma_exit(RcCtx *c);
int uya_hl_on(void);
void uya_hl_link(u32 at, u32 to);
void uya_hl_item(u32 at, u32 w0, u32 w1, u32 w2, u32 w3);

enum { K_SHRUB, K_TFRAG, K_MOBY, K_SKY, K_GIF, K_TIE, K_MSHADOW, K_N };
static const char *const kname[K_N] = { "shrubs", "tfrag", "mobys", "sky", "GIF paging", "ties", "moby shadows" };
static const u32 cws[3] = { 0x161000u, 0x161240u, 0x161280u };   /* the chain pointer: the executable's, the levels' */
static u32 res_w[K_N][3];            /* the chain words at the reservation (R where it moved) */
static u32 spl_w[K_N][3], spl_ra[K_N];
static unsigned long long n_ok[K_N], n_bad[K_N], n_end;
static int cw_i = -1;                 /* the chain word the last matching splice used */

static int on(void)
{
    static int m = -1;
    if (m < 0) { const char *e = getenv("UYA_NLINK"); m = e ? atoi(e) != 0 : 1; uya_log("native chain links: %s (UYA_NLINK)", m ? "the splices' links as host events" : "off"); }
    return m && uya_hl_on();
}
static int ram(u32 a) { return a >= 0x100000u && a < 0x2000000u && !(a & 15); }
static int is_next(u32 at, u32 to) { return M32(at) == 0x20000000u && M32(at + 4) == to && M32(at + 8) == 0 && M32(at + 12) == 0; }

static void reserve_at_entry(int k) { for (int i = 0; i < 3; i++) res_w[k][i] = M32(cws[i]); }
static void reserve_at_exit(int k) { for (int i = 0; i < 3; i++) res_w[k][i] = M32(cws[i]) - 16; }
static void splice_enter(int k, RcCtx *c) { for (int i = 0; i < 3; i++) spl_w[k][i] = M32(cws[i]); spl_ra[k] = (u32)c->r[31].d[0]; }
/* the splicer's exit: R is the reservation of whichever subsystem's matches (the catalogue files some
 * twins under one name: DmaTieTextures' level copies under DmaShrubTextures'), or p - 16 (nothing
 * drawn between the reservation and the splice); only links that are all as the model says are
 * handed over */
static void splice_exit(int k, RcCtx *c)
{
    if (spl_ra[k] != (u32)c->r[31].d[0]) return;
    spl_ra[k] = 0;
    int cand = -1, kk = -1;
    for (int i = 0; i < 3 && kk < 0; i++) {
        u32 p = spl_w[k][i], E = M32(cws[i]);
        if (!ram(p) || !ram(E) || E <= p || E - p > 0x400000u) continue;
        if (cand < 0) cand = i;
        if (!is_next(p, E)) continue;
        for (int j = 0; j <= K_N && kk < 0; j++) {
            u32 R = j < K_N ? res_w[j][i] : p - 16;
            if (ram(R) && is_next(R, p + 16) && is_next(E - 16, R + 16)) {
                uya_hl_link(R, p + 16); uya_hl_link(E - 16, R + 16); uya_hl_link(p, E);
                kk = j < K_N ? j : k; cw_i = i;
            }
        }
    }
    if (kk >= 0) n_ok[kk]++;
    else if (k == K_MSHADOW && cand < 0) {   /* (no shadows: func_0020DEB0 leaves the slot a CNT with nothing) */
        for (int i = 0; i < 3; i++) {
            u32 R = res_w[K_MSHADOW][i];
            if (spl_w[k][i] == M32(cws[i]) && ram(R) && M32(R) == 0x10000000u && !M32(R + 4) && !M32(R + 8) && !M32(R + 12)) {
                uya_hl_item(R, 0x10000000u, 0, 0, 0); n_ok[k]++; break;
            }
        }
    } else if (cand >= 0) {
        u32 p = spl_w[k][cand], E = M32(cws[cand]);
        if (n_bad[k]++ < 8)
            uya_log("native chain links, %s splice: p %08X E %08X: no reservation matches; the game wrote %08X %08X at E-16 (R + 16), %08X %08X at p",
                    kname[k], p, E, M32(E - 16), M32(E - 12), M32(p), M32(p + 4));
    }
    unsigned long long t = 0; for (int j = 0; j < K_N; j++) t += n_ok[j] + n_bad[j];
    if (cand >= 0 && (t == 1 || t % 5000 == 0)) {
        char b[400]; int o = 0;
        for (int j = 0; j < K_N; j++) o += snprintf(b + o, sizeof b - o, "%s%s %llu / %llu", j ? ", " : "", kname[j], n_ok[j], n_bad[j]);
        uya_log("native chain links: splices as host links / differing from the game's: %s", b);
    }
}
/* MobyProc leaves the moby shadows' slot (D_00160040) for func_0020DEB0 (native_mobyproc.c calls this) */
void uya_lk_mshadow_reserve(u32 slot) { for (int i = 0; i < 3; i++) res_w[K_MSHADOW][i] = slot; }
/* DrawTies_2 reserves just before each TieProc pass (native_tieproc.c's wrapper calls this) */
void uya_lk_tie_reserve(void) { for (int i = 0; i < 3; i++) res_w[K_TIE][i] = M32(cws[i]) - 16; }

#define RESERVER(name, k, when) \
    int uya_lk_##name##_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) \
    { uya_texdma_enter(c, g, ng, a, na); if (on() && when == 0) reserve_at_entry(k); return 0; } \
    void uya_lk_##name##_exit(RcCtx *c) { uya_texdma_exit(c); if (on() && when == 1) reserve_at_exit(k); }
#define SPLICER(name, k) \
    int uya_lk_##name##_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na) \
    { uya_texdma_enter(c, g, ng, a, na); if (on()) splice_enter(k, c); return 0; } \
    void uya_lk_##name##_exit(RcCtx *c) { uya_texdma_exit(c); if (on()) splice_exit(k, c); }

RESERVER(rshrub, K_SHRUB, 0)   /* DrawShrubs, func_00229E50 */
RESERVER(rtfrag, K_TFRAG, 0)   /* DrawTfrag, func_002346C0 */
RESERVER(rmoby, K_MOBY, 1)     /* DrawMobysSetup, func_0020E0C8 (reserves last) */
RESERVER(rsky, K_SKY, 0)       /* SetupSkyGifPaging, func_0022C7E0 */
RESERVER(rgif, K_GIF, 0)       /* SetupGifPaging, func_001F4630 */
SPLICER(sshrub, K_SHRUB)       /* DmaShrubTextures, func_00229C08 */
SPLICER(stfrag, K_TFRAG)       /* DmaTfragTextures, func_002344D8 */
SPLICER(smoby, K_MOBY)         /* DmaMobyTextures, func_0020DC40 */
SPLICER(ssky, K_SKY)           /* DoSkyGifPaging, func_0022C870 */
SPLICER(sgif, K_GIF)           /* DoGifPaging, func_001F4748 */
SPLICER(smshadow, K_MSHADOW)   /* the moby shadows, func_0020DEB0 */
SPLICER(stie, K_TIE)           /* DmaTieTextures, func_00236958 (the executable's; the levels' copies are filed as func_00229C08) */

/* VU1_sendChain (func_002349B8): the END at the chain pointer, then the send (inside it: the walk, and the
 * host list's check, happen before it returns, so the END is the host's at its entry) */
int uya_lk_end_enter(RcCtx *c, const s32 *g, int ng, const u32 *a, int na)
{
    if (!on()) return 0;
    /* its chain word: from its own references (a gp offset of the chain word's, or its address), else
     * the one the last matching splice used */
    u32 cw = 0;
    for (int k = 0; k < ng && !cw; k++) if (g[k] == -0x5D00 || g[k] == -0x5AC0 || g[k] == -0x5A80) cw = (u32)c->r[28].d[0] + (u32)g[k];
    for (int k = 0; k < na && !cw; k++) for (int i = 0; i < 3; i++) if (a[k] == cws[i]) cw = a[k];
    if (!cw && cw_i >= 0) cw = cws[cw_i];
    if (!cw) return 0;
    u32 p = M32(cw);
    void uya_hl_take_tail(u32); uya_hl_take_tail(p);   /* (the sender's own last writes: sdk/hostlist.c) */
    if (ram(p)) { uya_hl_item(p, 0x70000000u, 0, 0, 0); n_end++; }
    return 0;
}
void uya_lk_end_exit(RcCtx *c) { (void)c; }
