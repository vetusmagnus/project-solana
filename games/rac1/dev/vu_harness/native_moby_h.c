/* harness side of host/src/native/native_moby.c: every kicked packet compared word for word.
 * The only words allowed to differ (and counted apart) are ones the GS never reads: the
 * fourth word of an ST qword and the high REGS word of a GIF tag with at most 8 registers.
 * STRICT=1 allows none. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "harness.h"
#include "native_moby.h"

static NativeMobyKicks K;
static void moby_report(void);
static int ran;
static long n_unread_diff, n_strict_ok;

void native_reset(void) { native_moby_reset(); }

int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem)
{
    static int seeded;
    if (!seeded) {               /* the registers the game had when the capture started */
        u32 vf27[4];
        memcpy(vf27, v->vf[27], 16);
        native_moby_seed(vf27, ((const u32 *)v->vf[12])[3]);
        seeded = 1;
    }
    ran = native_moby_run(pc, (u8 *)mem, top, &K);
    return ran;
}

/* is word w of qword q of packet p one the GS does not read? */
static int unread_word(const u32 *p, u32 n, u32 q, int w)
{
    u32 t = 0;
    while (t < n) {
        const u32 *g = p + t * 4;
        u32 nloop = g[0] & 0x7FFF, flg = (g[1] >> 26) & 3, nreg = g[1] >> 28;
        u64 regs = ((u64)g[3] << 32) | g[2];
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if (q == t) return w == 3 && nreg <= 8;
        if (q < t + len) {
            if (flg != 0) return 0;
            u32 j = q - t - 1, reg = (u32)(regs >> (4 * (j % nreg))) & 15;
            return reg == 2 && w == 3;            /* ST: S T Q, the fourth word unused */
        }
        t += len;
        if (g[0] & 0x8000) break;
    }
    return 0;
}

int native_compare(const packet_t *k, int nk, int verbose)
{
    static int strict = -1;
    if (strict < 0) strict = getenv("STRICT") != NULL;
    if ((int)K.npk != nk) { if (verbose) fprintf(stderr, "   kicks: program %d, native %u\n", nk, K.npk); return 0; }
    int bad = 0, unread = 0;
    for (int i = 0; i < nk; i++) {
        const u32 *a = K.d + K.off[i] * 4;
        if (K.len[i] != k[i].n) { if (verbose) fprintf(stderr, "   kick %d: %u qwords, program %u\n", i, K.len[i], k[i].n); bad++; continue; }
        for (u32 q = 0; q < k[i].n; q++) for (int w = 0; w < 4; w++) {
            u32 x = a[q * 4 + w], y = k[i].d[q * 4 + w];
            if (x == y) continue;
            if (!strict && unread_word(k[i].d, k[i].n, q, w)) { if (verbose > 3 && unread < 4) fprintf(stderr, "   unread: kick %d qw %u word %d: %08X, program %08X\n", i, q, w, x, y); unread++; continue; }
            if (verbose > 1 && bad < 6) fprintf(stderr, "   kick %d qw %u word %d: %08X, program %08X\n", i, q, w, x, y);
            bad++;
        }
    }
    n_unread_diff += unread;
    if (!bad && !unread) n_strict_ok++;
    if (verbose > 2 && unread) fprintf(stderr, "   %d unread words differ\n", unread);
    {
        static int registered;
        if (!registered) { registered = 1; atexit(moby_report); }
    }
    return !bad;
}

static void moby_report(void) { fprintf(stderr, "native: %ld runs identical in every word; %ld words differing that the GS does not read\n", n_strict_ok, n_unread_diff); }
