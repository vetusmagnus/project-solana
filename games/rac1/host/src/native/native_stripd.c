/* The strip effect (VU1 image 57843) drawn without the VIF: what a strip producer (native_strip.c's
 * setup, one strip, the strip list; native_stripb.c's level builders) wrote into the chain, as
 * records, drawn where the chain meets their marker.
 *
 * A producer's run writes a range of the chain made of these items:
 *   MPG:     a REF to a microprogram packet in the resident .vutext (VIF NOPs);
 *   setup:   CNT 14 (TTE FLUSH, STCYCL 4/4; NOP NOP NOP UNPACK V4-32 12 at VU 932 + 12 qwords; BASE 0,
 *            OFFSET 0x1D2, MSCALF 0, NOP);
 *   GIF:     CNT n with TTE NOP, DIRECT n and its n qwords (copied), or REF n with TTE NOP, DIRECT n
 *            (read when the chain is walked, as the REF is): GS register blocks, the GS reset, a
 *            texture upload, TEXFLUSH;
 *   strip:   (native_strip.c's writers) CNT n (TTE STCYCL 4/4, UNPACK V4-32 1 at TOPS: the strip's GIF
 *            tag) + the tag, UNPACK V4-32 1 at VU 940 + a qword, STCOL + 4 words, STCYCL 3/1, STMOD 0;
 *            STMASK + mask, NOP, UNPACK V2-32 (masked) n at TOPS + 1 + the ST pairs; NOP NOP NOP
 *            UNPACK V4-8 n at TOPS + 2 + the colours; STMASK + mask, NOP, UNPACK V3-32 (masked) n at
 *            TOPS + 3 + the positions (each array then zero words, VIF NOPs, up to a qword); MSCALF
 *            0x60 (0x70 clipped), FLUSH, 0, 0;
 *   strip (semantic): a block a level builder told of (native_stripd_sem: native_stripb.c's record
 *            point): the same template, then its three arrays in the builder's order (ST, colours,
 *            positions; or a grid cell's positions, colours, ST), each where the VIF takes it: in the
 *            range (copied) or in REF'd memory outside it (read when the chain is walked), with the
 *            UNPACK code (and STMASK) before it; MSCALF;
 *   marker:  a strip marker a producer it called left (its run, drawn in its place).
 * When every item of the range is one of these, the items are recorded, a marker CNT (qwc 0, VIF NOP
 * NATIVE_STRIP_MARK, the run's number) takes the range's place and the chain pointer comes back to
 * just after it. At the marker each item does what the VIF did with it, in the same order: the MPG
 * (vu_mpg), the unpacks into VU1 memory by the VIF's cycle, mask, column and mode
 * (uya_vif1_op_unpack), the registers, the starts (the program runs as from the VIF), the GIF data
 * (native_gif). Any other shape: the range stays in the chain and goes through the VIF.
 *
 * UYA_NSTRIPDIRECT: 0 the chain, 2 records (the default). */
#include "rc.h"
#include "uya_host.h"
#include "native_moby.h"
#include "native_stripd.h"
#include "uya_runtime.h"
#include <stdlib.h>
#include <string.h>

void uya_log(const char *fmt, ...);
void native_gif(int path, const u32 *d, u32 qwc);

#define W(a) (*(const u32 *)RC_A(a))
enum { I_SETUP, I_GIF, I_GIFREF, I_MPG, I_STRIP, I_SEM, I_RUN };
#define RING (1u << 22)                 /* words of recorded items (16 MB) */
#define NREC 65536                      /* items */
#define NRUN 16384                      /* markers */
static u32 *ring; static u32 ring_at;
static struct { u32 kind, off, nw, a, b; } item[NREC];
static u32 item_at;
static struct { u32 first, n; } run[NRUN];
static u32 run_at;
static int mode = -1;
static unsigned long long n_runs, n_items, n_strips, n_sems, n_kept, n_drawn, n_unknown;
static const char *why_kept;

/* the semantic strips a producer told of, pending until its range is converted (a stack: a
 * producer's end takes those from its begin on) */
#define NSEM 4096
typedef struct { u32 b0, b1, n, gif[4], v940[4], arr[3], pc; } Sem;
static Sem sem[NSEM];
static u32 sem_n;

static u32 a4(u32 w) { return (w + 3) & ~3u; }
/* a VIF word that does nothing here: NOP, FLUSHE, FLUSH, FLUSHA (everything runs in order on the host) */
static int vnop(u32 w) { u32 c = (w >> 24) & 0x7F; return (c == 0x00 || c == 0x10 || c == 0x11 || c == 0x13) && !(w & 0x00FFFFFFu); }
static u32 *room(u32 nw)
{
    if (!ring) ring = malloc(RING * 4);
    if (ring_at + nw > RING) ring_at = 0;
    u32 *p = ring + ring_at;
    ring_at += nw;
    return p;
}
static u32 *add(u32 kind, u32 nw, u32 x, u32 y)
{
    u32 *p = room(nw);
    u32 k = item_at++ % NREC;
    item[k].kind = kind; item[k].off = (u32)(p - ring); item[k].nw = nw; item[k].a = x; item[k].b = y;
    n_items++;
    return p;
}
/* a native_strip.c strip block at b: its length in qwords, 0 if not the template */
static u32 strip_len(u32 b, u32 end)
{
    if (b + 0x60 > end) return 0;
    u32 t = W(b), n = W(b + 0x10) & 0x7FFF;
    if ((t & 0xFFFF0000u) != 0x10000000u || W(b + 4) || W(b + 8) != 0x01000404u || W(b + 0x0C) != 0x6C01C000u) return 0;
    if (W(b + 0x20) != 0x6C0143ACu || W(b + 0x34) != 0x31000000u || W(b + 0x48) != 0x01000103u || W(b + 0x4C) != 0x05000000u) return 0;
    if (!n || n > 255) return 0;
    u32 i = 20;
    if (W(b + 4 * i) != 0x20000000u || W(b + 4 * i + 8) || W(b + 4 * i + 12) != (0x7400C001u | n << 16)) return 0;
    i += 4 + a4(2 * n);
    if (b + 4 * i + 16 > end || W(b + 4 * i) || W(b + 4 * i + 4) || W(b + 4 * i + 8) || W(b + 4 * i + 12) != (0x6E00C002u | n << 16)) return 0;
    i += 4 + a4(n);
    if (b + 4 * i + 16 > end || W(b + 4 * i) != 0x20000000u || W(b + 4 * i + 8) || W(b + 4 * i + 12) != (0x7800C003u | n << 16)) return 0;
    i += 4 + a4(3 * n);
    if (b + 4 * i + 16 > end) return 0;
    u32 m = W(b + 4 * i);
    if ((m != 0x1500000Cu && m != 0x1500000Eu) || W(b + 4 * i + 4) != 0x11000000u || W(b + 4 * i + 8) || W(b + 4 * i + 12)) return 0;
    u32 q = i / 4 + 1;
    if ((t & 0xFFFF) != q - 1) return 0;
    return q;
}
/* an array of a semantic strip: its UNPACK code just before its data (and an STMASK qword before
 * that when masked); words of data it takes; 0 if not one of the strip's three */
static u32 arr_words(u32 d, u32 n, u32 *code, u32 *mask, int *masked)
{
    *code = W(d - 4);
    u32 c = (*code >> 24) & 0x7F, num = (*code >> 16) & 0xFF;
    if (num != n || (*code & 0xC000u) != 0xC000u) return 0;
    *masked = W(d - 16) == 0x20000000u && !W(d - 8);
    *mask = *masked ? W(d - 12) : 0;
    if (!*masked && (W(d - 16) || W(d - 12) || W(d - 8))) return 0;
    if (c == 0x74) return 2 * n;   /* V2-32 (masked) */
    if (c == 0x6E) return n;       /* V4-8 */
    if (c == 0x78) return 3 * n;   /* V3-32 (masked) */
    return 0;
}
/* a semantic strip as an item: the template's VIF words and data, then per array (in order) its
 * STMASK flag and mask, code, words, and where its data is (in the item: copied; else an address
 * read at the walk) */
#define SEM_HDR 20
static int sem_item(const Sem *s, u32 ch0, u32 end, int rec)
{
    u32 b = s->b0;
    if (W(b + 8) != 0x01000404u || W(b + 0x0C) != 0x6C01C000u || W(b + 0x20) != 0x6C0143ACu || W(b + 0x34) != 0x31000000u ||
        W(b + 0x48) != 0x01000103u || W(b + 0x4C) != 0x05000000u || !s->n || s->n > 255) return 0;
    u32 code[3], mask[3], nw[3], tot = SEM_HDR + 18; int masked[3];
    for (int k = 0; k < 3; k++) {
        if (!s->arr[k] || !(nw[k] = arr_words(s->arr[k], s->n, &code[k], &mask[k], &masked[k]))) return 0;
        if (s->arr[k] >= ch0 && s->arr[k] < end) tot += nw[k];
    }
    if (!rec) return 1;
    u32 *p = add(I_SEM, tot, 0, 0);
    p[0] = W(b + 8); p[1] = W(b + 0x0C); memcpy(p + 2, s->gif, 16);
    p[6] = W(b + 0x20); memcpy(p + 7, s->v940, 16);
    memcpy(p + 11, (const void *)RC_A(b + 0x38), 16);   /* STCOL's 4 words */
    p[15] = W(b + 0x48); p[16] = W(b + 0x4C); p[17] = s->pc; p[18] = s->n; p[19] = 0;
    u32 at = SEM_HDR + 18;
    for (int k = 0; k < 3; k++) {
        u32 *a = p + SEM_HDR + 6 * k;
        a[0] = (u32)masked[k]; a[1] = mask[k]; a[2] = code[k]; a[3] = nw[k];
        if (s->arr[k] >= ch0 && s->arr[k] < end) { a[4] = 0; a[5] = at; memcpy(p + at, (const void *)RC_A(s->arr[k]), nw[k] * 4); at += nw[k]; }
        else { a[4] = 1; a[5] = s->arr[k]; }
    }
    n_sems++;
    return 1;
}
/* the range's items: 1 if every one is one of the above (rec: recorded too; the first pass only
 * checks) */
static int scan(u32 ch0, u32 end, u32 s0, int rec)
{
    u32 b = ch0;
    while (b < end) {
        u32 t = W(b), id = (t >> 28) & 7, qwc = t & 0xFFFF;
        const Sem *s = 0;
        for (u32 k = s0; k < sem_n; k++) if (sem[k].b0 == b) { s = &sem[k]; break; }
        if (s) {
            if (s->b1 <= b || s->b1 > end || !sem_item(s, ch0, end, rec)) { why_kept = "a builder's strip"; return 0; }
            b = s->b1;
            continue;
        }
        if (id == 1 && qwc == 0 && W(b + 8) == NATIVE_STRIP_MARK) {     /* a marker of a producer it called */
            if (rec) add(I_RUN, 0, W(b + 12), 0);
            b += 16;
            continue;
        }
        if (id == 3 && !W(b + 8) && !W(b + 12) && W(b + 4) >= 0x100080u && W(b + 4) < 0x112310u) {   /* MPG */
            if (rec) add(I_MPG, 0, W(b + 4), qwc);
            b += 16;
            continue;
        }
        if (id == 1 && qwc == 0 && vnop(W(b + 8)) && vnop(W(b + 12)) && !W(b + 4)) {   /* CNT 0 with NOPs / FLUSHes */
            b += 16;
            continue;
        }
        if (id == 3 && vnop(W(b + 8)) && W(b + 12) == (0x50000000u | qwc) && qwc) {   /* REF n, DIRECT n */
            if (rec) add(I_GIFREF, 0, W(b + 4), qwc);
            b += 16;
            continue;
        }
        if (id == 1 && !W(b + 4) && vnop(W(b + 8)) && W(b + 12) == (0x50000000u | qwc) && qwc && b + 16 * (qwc + 1) <= end) {   /* CNT n, DIRECT n */
            if (rec) { u32 *p = add(I_GIF, 4 * qwc, 0, qwc); memcpy(p, (const void *)RC_A(b + 16), 16 * qwc); }
            b += 16 * (qwc + 1);
            continue;
        }
        if (t == 0x1000000Eu && W(b + 8) == 0x11000000u && W(b + 12) == 0x01000404u) {   /* the setup */
            if (b + 0xF0 > end || W(b + 0x10) || W(b + 0x14) || W(b + 0x18) || W(b + 0x1C) != 0x6C0C43A4u ||
                W(b + 0xE0) != 0x03000000u || W(b + 0xE4) != 0x020001D2u || W(b + 0xE8) != 0x15000000u || W(b + 0xEC)) { why_kept = "the setup's template"; return 0; }
            if (rec) { u32 *p = add(I_SETUP, 0xF0 / 4, 0, 0); memcpy(p, (const void *)RC_A(b), 0xF0); }
            b += 0xF0;
            continue;
        }
        u32 q = strip_len(b, end);
        if (!q) { why_kept = "a strip block"; return 0; }
        if (rec) { u32 *p = add(I_STRIP, q * 4, 0, 0); memcpy(p, (const void *)RC_A(b), q * 16); n_strips++; }
        b += 16 * q;
    }
    return b == end;
}
u32 native_stripd_begin(void) { return sem_n; }
void native_stripd_sem(u32 b0, u32 b1, u32 n, const u32 gif[4], const u32 v940[4], u32 st, u32 col, u32 xyz, int grid, u32 pc)
{
    if (sem_n >= NSEM) return;
    if (W(b0 + 8) == NATIVE_STRIP_MARK) return;   /* (a producer it called made it a marker already) */
    Sem *s = &sem[sem_n++];
    s->b0 = b0; s->b1 = b1; s->n = n; memcpy(s->gif, gif, 16); memcpy(s->v940, v940, 16); s->pc = pc;
    if (grid) { s->arr[0] = xyz; s->arr[1] = col; s->arr[2] = st; }   /* a grid cell: positions, colours, ST */
    else { s->arr[0] = st; s->arr[1] = col; s->arr[2] = xyz; }
}
void native_stripd_end(u32 s0, u32 chain_word, u32 ch0, u32 end)
{
    if (mode < 0) { const char *e = getenv("UYA_NSTRIPDIRECT"); mode = e ? atoi(e) : 2; uya_log("native strip direct draws (UYA_NSTRIPDIRECT): %s", mode ? "on" : "off"); }
    if (mode && end > ch0 && end - ch0 <= 0x100000u) {
        why_kept = 0;
        if (!scan(ch0, end, s0, 0)) {
            n_kept++; static int told;
            if (told++ < 8) {
                uya_log("native strip direct: a range at %08X (%u qwords) stays in the chain (%s)", ch0, (end - ch0) / 16, why_kept ? why_kept : "its end");
                if (told <= 1) for (u32 q = 0; q < 48 && ch0 + 16 * q < end; q++) uya_log("   %08X: %08X %08X %08X %08X", ch0 + 16 * q, W(ch0 + 16 * q), W(ch0 + 16 * q + 4), W(ch0 + 16 * q + 8), W(ch0 + 16 * q + 12));
            }
        }
        else {
            u32 id = run_at++, r = id % NRUN;
            run[r].first = item_at;
            scan(ch0, end, s0, 1);
            run[r].n = item_at - run[r].first;
            rc_sw(ch0, 0x10000000u); rc_sw(ch0 + 4, 0); rc_sw(ch0 + 8, NATIVE_STRIP_MARK); rc_sw(ch0 + 12, id);   /* (the run's number: its slot is id % NRUN) */
            rc_sw(chain_word, ch0 + 16);
            n_runs++;
        }
    }
    sem_n = s0;
}
void native_stripd_convert(u32 chain_word, u32 ch0, u32 end) { native_stripd_end(sem_n, chain_word, ch0, end); }

static void apply_run(u32 id, int depth);
static void apply_item(u32 i, int depth)
{
    const u32 *p = ring + item[i].off;
    extern const char *uya_gs_src;
    switch (item[i].kind) {
    case I_MPG:
        if (!uya_vif1_op_mpg_packet((const u32 *)RC_A(item[i].a), item[i].b)) { static int told; if (!told++) uya_log("native strip direct: the MPG packet at %08X has other codes", item[i].a); }
        break;
    case I_GIF: uya_gs_src = "PATH2 (VIF1 DIRECT)"; native_gif(1, p, item[i].b); break;
    case I_GIFREF: uya_gs_src = "PATH2 (VIF1 DIRECT)"; native_gif(1, (const u32 *)RC_A(item[i].a), item[i].b); break;
    case I_RUN: apply_run(item[i].a, depth + 1); break;
    case I_SETUP:                                       /* p: the setup's CNT and its data (0xF0 bytes) */
        uya_vif1_op_reg(p[3]);                          /* STCYCL 4/4 (FLUSH: nothing to wait for) */
        uya_vif1_op_unpack(p[7], p + 8, 12 * 4);        /* VU 932..943 */
        uya_vif1_op_reg(p[56]); uya_vif1_op_reg(p[57]);   /* BASE 0, OFFSET 0x1D2 */
        uya_vif1_op_mscal((p[58] & 0xFFFF) * 8);        /* MSCALF 0 */
        break;
    case I_STRIP: {
        u32 n = p[4] & 0x7FFF, w = 20;
        uya_vif1_op_reg(p[2]);                          /* STCYCL 4/4 */
        uya_vif1_op_unpack(p[3], p + 4, 4);             /* the GIF tag at TOPS */
        uya_vif1_op_unpack(p[8], p + 9, 4);             /* VU 940 */
        uya_vif1_op_stcol(p + 14);                      /* STCOL */
        uya_vif1_op_reg(p[18]); uya_vif1_op_reg(p[19]);   /* STCYCL 3/1, STMOD 0 */
        uya_vif1_op_stmask(p[w + 1]); uya_vif1_op_unpack(p[w + 3], p + w + 4, 2 * n);   /* ST */
        w += 4 + a4(2 * n);
        uya_vif1_op_unpack(p[w + 3], p + w + 4, n);     /* colours */
        w += 4 + a4(n);
        uya_vif1_op_stmask(p[w + 1]); uya_vif1_op_unpack(p[w + 3], p + w + 4, 3 * n);   /* positions */
        w += 4 + a4(3 * n);
        uya_vif1_op_mscal((p[w] & 0xFFFF) * 8);         /* MSCALF 0x60 / 0x70 (FLUSH after it) */
        break;
    }
    case I_SEM:
        uya_vif1_op_reg(p[0]);                          /* STCYCL 4/4 */
        uya_vif1_op_unpack(p[1], p + 2, 4);             /* the GIF tag at TOPS */
        uya_vif1_op_unpack(p[6], p + 7, 4);             /* VU 940 */
        uya_vif1_op_stcol(p + 11);                      /* STCOL */
        uya_vif1_op_reg(p[15]); uya_vif1_op_reg(p[16]);   /* STCYCL 3/1, STMOD 0 */
        for (int k = 0; k < 3; k++) {
            const u32 *a = p + SEM_HDR + 6 * k;
            if (a[0]) uya_vif1_op_stmask(a[1]);
            uya_vif1_op_unpack(a[2], a[4] ? (const u32 *)RC_A(a[5]) : p + a[5], a[3]);
        }
        uya_vif1_op_mscal(p[17]);                       /* MSCALF 0x60 / 0x70 */
        break;
    }
}
static void apply_run(u32 id, int depth)
{
    if (!ring || id >= run_at || run_at - id > NRUN || depth > 8) { if (n_unknown++ < 3) uya_log("native strip direct: marker %u not known (runs %u, vsync %u): skipped", id, run_at, uya_frame_count()); return; }
    u32 r = id % NRUN;
    for (u32 k = 0; k < run[r].n; k++) apply_item((run[r].first + k) % NREC, depth);
}
int native_strip_tag(const u32 *q)
{
    apply_run(q[3], 0);
    n_drawn++;
    if (n_drawn % 2000 == 0) uya_log("native strip direct: %llu ranges drawn (%llu items: %llu strips, %llu builders' strips), %llu kept in the chain, %llu markers not known", n_drawn, n_items, n_strips, n_sems, n_kept, n_unknown);
    return 1;
}
#undef W
