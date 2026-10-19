/* gs.c: a software Graphics Synthesizer, enough for the menu.
 *
 * Input is GIF data (PATH2 from VIF1 DIRECT, PATH3 from GIF DMA). It keeps
 * the 4 MB local memory with the real swizzled layouts, so a texture written
 * in one format and read in another (fonts, CLUTs) works like on the PS2.
 *
 * Done: every primitive type, flat and Gouraud colour, ST/Q and UV texturing
 * (32/24/16-bit, 8/4-bit with CLUT, 8H/4HL/4HH), nearest and bilinear
 * filtering, mipmapping, wrap modes, texture functions, fog, alpha test,
 * destination alpha test, depth test, alpha blending, colour clamp,
 * FBMSK/ZMSK, scissor, host-to-local and local-to-local transfers, CLUT
 * loading (CSM1), and the display circuits (PMODE/DISPFB/DISPLAY) for showing
 * a frame.
 * Not done: antialiasing, dithering, CSM2, local-to-host transfers. */
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "uya_host.h"
#include "uya_runtime.h"
#include "gs_swizzle.h"
#include "gs_types.h"
#include "gs_gl.h"
static inline unsigned long long rdtsc_(void) { unsigned lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((unsigned long long)hi << 32) | lo; }
unsigned long long uya_t_gs, uya_tex_px;
int uya_timing = -1;   /* UYA_FPSLOG: time the GS and the VUs (rdtsc around each primitive, packet and VU run) */
static int timing(void) { if (uya_timing < 0) uya_timing = getenv("UYA_FPSLOG") != 0; return uya_timing; }

#define BITS(v, lo, n) ((u32)(((v) >> (lo)) & ((1ull << (n)) - 1)))

static u32 *vram;                     /* 1M words = 4 MB */

/* ---- addressing ---- */
static inline u32 a32(u32 bp, u32 bw, u32 x, u32 y)
{
    u32 b = bp + ((y >> 5) * bw + (x >> 6)) * 32 + gs_block32[(y >> 3) & 3][(x >> 3) & 7];
    return ((b & 0x3FFF) << 6) | gs_col32[y & 7][x & 7];
}
static inline u32 a32z(u32 bp, u32 bw, u32 x, u32 y)
{
    u32 b = bp + ((y >> 5) * bw + (x >> 6)) * 32 + (gs_block32[(y >> 3) & 3][(x >> 3) & 7] ^ 0x18);
    return ((b & 0x3FFF) << 6) | gs_col32[y & 7][x & 7];
}
static inline u32 a16(u32 bp, u32 bw, u32 x, u32 y, const uint8_t (*bt)[4], u32 z)
{
    u32 b = bp + ((y >> 6) * bw + (x >> 6)) * 32 + (bt[(y >> 3) & 7][(x >> 4) & 3] ^ z);
    return ((b & 0x3FFF) << 7) | gs_col16[y & 7][x & 15];       /* halfword index */
}
static inline u32 a8(u32 bp, u32 bw, u32 x, u32 y)
{
    u32 b = bp + ((y >> 6) * (bw >> 1) + (x >> 7)) * 32 + gs_block8[(y >> 4) & 3][(x >> 4) & 7];
    return ((b & 0x3FFF) << 8) | gs_col8[y & 15][x & 15];       /* byte index */
}
static inline u32 a4(u32 bp, u32 bw, u32 x, u32 y)
{
    u32 b = bp + ((y >> 7) * (bw >> 1) + (x >> 7)) * 32 + gs_block4[(y >> 4) & 7][(x >> 5) & 3];
    return ((b & 0x3FFF) << 9) | gs_col4[y & 15][x & 31];       /* nibble index */
}

static inline u16 *vram16(void) { return (u16 *)vram; }
static inline u8 *vram8(void) { return (u8 *)vram; }

/* raw pixel read/write in any storage format (colour formats return the stored bits) */
static u32 px_read(u32 psm, u32 bp, u32 bw, u32 x, u32 y)
{
    switch (psm) {
    case 0x00: case 0x01: return vram[a32(bp, bw, x, y)];
    case 0x02: return vram16()[a16(bp, bw, x, y, gs_block16, 0)];
    case 0x0A: return vram16()[a16(bp, bw, x, y, gs_block16s, 0)];
    case 0x13: return vram8()[a8(bp, bw, x, y)];
    case 0x14: { u32 n = a4(bp, bw, x, y); return (vram8()[n >> 1] >> ((n & 1) * 4)) & 15; }
    case 0x1B: return vram[a32(bp, bw, x, y)] >> 24;
    case 0x24: return (vram[a32(bp, bw, x, y)] >> 24) & 15;
    case 0x2C: return vram[a32(bp, bw, x, y)] >> 28;
    case 0x30: case 0x31: return vram[a32z(bp, bw, x, y)];
    case 0x32: return vram16()[a16(bp, bw, x, y, gs_block16, 0x18)];
    case 0x3A: return vram16()[a16(bp, bw, x, y, gs_block16s, 0x18)];
    }
    return 0;
}
static void px_write(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32 v)
{
    u32 i;
    switch (psm) {
    case 0x00: vram[a32(bp, bw, x, y)] = v; break;
    case 0x01: i = a32(bp, bw, x, y); vram[i] = (vram[i] & 0xFF000000) | (v & 0xFFFFFF); break;
    case 0x02: vram16()[a16(bp, bw, x, y, gs_block16, 0)] = (u16)v; break;
    case 0x0A: vram16()[a16(bp, bw, x, y, gs_block16s, 0)] = (u16)v; break;
    case 0x13: vram8()[a8(bp, bw, x, y)] = (u8)v; break;
    case 0x14: i = a4(bp, bw, x, y); vram8()[i >> 1] = (u8)((vram8()[i >> 1] & ((i & 1) ? 0x0F : 0xF0)) | ((v & 15) << ((i & 1) * 4))); break;
    case 0x1B: i = a32(bp, bw, x, y); vram[i] = (vram[i] & 0x00FFFFFF) | (v << 24); break;
    case 0x24: i = a32(bp, bw, x, y); vram[i] = (vram[i] & 0xF0FFFFFF) | ((v & 15) << 24); break;
    case 0x2C: i = a32(bp, bw, x, y); vram[i] = (vram[i] & 0x0FFFFFFF) | ((v & 15) << 28); break;
    case 0x30: vram[a32z(bp, bw, x, y)] = v; break;
    case 0x31: i = a32z(bp, bw, x, y); vram[i] = (vram[i] & 0xFF000000) | (v & 0xFFFFFF); break;
    case 0x32: vram16()[a16(bp, bw, x, y, gs_block16, 0x18)] = (u16)v; break;
    case 0x3A: vram16()[a16(bp, bw, x, y, gs_block16s, 0x18)] = (u16)v; break;
    }
}
static u32 psm_bits(u32 psm)
{
    switch (psm) {
    case 0x01: case 0x31: return 24;
    case 0x02: case 0x0A: case 0x32: case 0x3A: return 16;
    case 0x13: return 8;
    case 0x14: return 4;
    default: return 32;    /* 32-bit, and the H formats take whole words of GS memory */
    }
}

/* bits per pixel of transfer data (host <-> local): the H formats send 8 or
 * 4 bits a pixel like PSMT8/PSMT4, though they sit in 32-bit words in GS
 * memory (psm_bits above, for the memory they take) */
static u32 trx_bits(u32 psm)
{
    switch (psm) {
    case 0x1B: return 8;
    case 0x24: case 0x2C: return 4;
    default: return psm_bits(psm);
    }
}

/* ---- registers ---- */

static gs_ctx_t ctx[2];
static u64 prim, prmode, prmodecont = 1, rgbaq, st, uv, fog, texa, fogcol, colclamp, pabe, dthe;
/* the frame's two draw passes in host form (target, depth, alpha test, blend, scissor, offset),
 * decoded when the game's GIF sets them: what every draw reads, the native renderers' material
 * draws (nd_mesh_draw, below) and the GIF's primitives alike */
typedef struct {
    u32 fbp, fbw, fpsm, fbmsk, zbp, zpsm; u8 zmsk;
    u8 ate, atst, aref, afail, zte, ztst, date, datm;
    u8 ba, bb, bc, bd, fix, fba;
    int sx0, sx1, sy0, sy1;
    float xoff, yoff;
} gs_pass_t;
static gs_pass_t gpass[2] = { { .zpsm = 0x30 }, { .zpsm = 0x30 } };   /* (as decoded from registers at 0) */
static void pass_decode(int i)
{
    const gs_ctx_t *c = &ctx[i]; gs_pass_t *q = &gpass[i];
    q->fbp = (u32)((c->frame) & 511) * 32; q->fbw = (u32)(c->frame >> 16) & 63; q->fpsm = (u32)(c->frame >> 24) & 63; q->fbmsk = (u32)(c->frame >> 32);
    q->zbp = (u32)((c->zbuf) & 511) * 32; q->zpsm = ((u32)(c->zbuf >> 24) & 15) | 0x30; q->zmsk = (u8)((c->zbuf >> 32) & 1);
    q->ate = (u8)(c->test & 1); q->atst = (u8)((c->test >> 1) & 7); q->aref = (u8)(c->test >> 4); q->afail = (u8)((c->test >> 12) & 3);
    q->zte = (u8)((c->test >> 16) & 1); q->ztst = (u8)((c->test >> 17) & 3); q->date = (u8)((c->test >> 14) & 1); q->datm = (u8)((c->test >> 15) & 1);
    q->ba = (u8)(c->alpha & 3); q->bb = (u8)((c->alpha >> 2) & 3); q->bc = (u8)((c->alpha >> 4) & 3); q->bd = (u8)((c->alpha >> 6) & 3); q->fix = (u8)(c->alpha >> 32);
    q->fba = (u8)(c->fba & 1);
    q->sx0 = (int)(c->scissor & 2047); q->sx1 = (int)((c->scissor >> 16) & 2047); q->sy0 = (int)((c->scissor >> 32) & 2047); q->sy1 = (int)((c->scissor >> 48) & 2047);
    q->xoff = (float)(u32)(c->xyoffset & 0xFFFF) / 16.0f; q->yoff = (float)(u32)((c->xyoffset >> 32) & 0xFFFF) / 16.0f;
}
static u64 bitbltbuf, trxpos, trxreg, texclut;
static float qreg = 1.0f;
static u32 clut[256];                  /* loaded CLUT entries (raw 32-bit or 16-bit values) */
static u32 clut_hash;
static u32 cbp0, cbp1;

static vtx_t vq[3];
static int vcount;                     /* vertices queued for the current primitive */

static unsigned long long stat_prims, stat_pixels;
static int tc_nopin = -1;
static unsigned long long gs_thread_busy, n_tc_reuse, n_tc_stale, n_mark_trx, n_mark_draw, n_drain_wait, n_tc_build, n_trx_px, t_tcb, t_trx, t_draw;
extern const char *uya_gs_src;
static const char *gs_src_int = "?";   /* the source of the data the GS is working on */
static int mtgs;                         /* the GS runs on its own thread */
static const u32 *gif_cur_qw;            /* testing: the GIF qword being processed */

static int trxlog_on(void);
/* ---- CLUT load (CSM1 layout) ---- */
static void clut_read(u64 t0);
static int clut_from_upload(u64 t0);
/* GL: exchange the loaded palette with a saved one (gs_gl.c keeps a batch's) */
void gs_clut_swap(u32 *pal, u32 *hash)
{
    u32 t[256]; memcpy(t, clut, sizeof t); memcpy(clut, pal, sizeof t); memcpy(pal, t, sizeof t);
    u32 h = clut_hash; clut_hash = *hash; *hash = h;
}
static int sv_on; static void survey_clut(u64 t0);   /* (UYA_MEMSURVEY, below) */
static void clut_poison(void);
static u64 clut_pend; static int clut_pending;   /* GL: a load not done yet */
static void clut_flush(void)
{
    if (!clut_pending) return;
    clut_pending = 0;
    gsgl_cpu_read(BITS(clut_pend, 37, 14), 4);
    clut_read(clut_pend);
    clut_poison();
}
static void clut_load(u64 t0)
{
    u32 psm = BITS(t0, 20, 6), cbp = BITS(t0, 37, 14), cpsm = BITS(t0, 51, 4), csa = BITS(t0, 56, 5);
    u32 cld = BITS(t0, 61, 3);
    int load = 0;
    switch (cld) {
    case 1: load = 1; break;
    case 2: load = 1; cbp0 = cbp; break;
    case 3: load = 1; cbp1 = cbp; break;
    case 4: if (cbp != cbp0) { load = 1; cbp0 = cbp; } break;
    case 5: if (cbp != cbp1) { load = 1; cbp1 = cbp; } break;
    }
    if (trxlog_on()) uya_log("TEX0 %016llX: CLD %u CBP %04X CSA %u PSM %X CPSM %X -> %s", (unsigned long long)t0, cld, cbp, csa, psm, cpsm, load ? "load" : "keep");
    if (!load) return;
    if (__builtin_expect(sv_on > 0, 0)) survey_clut(t0);
    if (gsgl_on) {
        int pal = psm == 0x13 || psm == 0x1B || psm == 0x14 || psm == 0x24 || psm == 0x2C;
        gsgl_clut_changed();                 /* (a batch decoding later uses the old palette) */
        /* GL: a load from memory a GPU target owns waits until a primitive
         * uses the palette: games point CBP at the frame buffer to force the
         * next load (CLD 4/5), and a full 8-bit load replaces it anyway */
        /* (a pending load from a GPU target that another load replaces is
         * dropped, not read back: the force-load trick's frame-buffer data is
         * never used; reading it cost a GPU readback a frame in the menu) */
        clut_pending = 0;
        if (!pal) return;
        if (clut_from_upload(t0)) return;   /* (the game's upload of it: GS memory not needed) */
        if (gsgl_tex_gpu(0x00, cbp, 1, 16, 16)) { clut_pend = t0; clut_pending = 1; return; }
        gsgl_cpu_read(cbp, 4);
    }
    if (!clut_from_upload(t0)) { clut_read(t0); clut_poison(); }
}
/* testing: UYA_NTEXPOISON=1: a palette that is not from an upload is made solid magenta */
static void clut_hash_set(void);
static void clut_poison(void)
{
    static int poison = -1; if (poison < 0) { const char *x = getenv("UYA_NTEXPOISON"); poison = x && atoi(x); }
    if (!(poison & 2)) return;   /* (1: textures, 2: palettes, 3: both) */
    for (int i = 0; i < 256; i++) clut[i] = 0x80FF00FFu;
    clut_hash_set();
}
static void clut_hash_set(void)
{
    u32 h = 2166136261u;
    for (int i = 0; i < 256; i++) h = (h ^ clut[i]) * 16777619u;
    clut_hash = h;
}
/* an 8-bit texture's 256-colour CT32 palette from the game's upload of it (a 16x16 PSMCT32 image,
 * kept as it came: entry i is its pixel (x, y) as clut_read finds it in GS memory); 0 if there is
 * none intact (UYA_NTEXCHECK=1 compares it with GS memory) */
static const struct urec_s *urec_find_(u32 psm, u32 tbp, u32 tbw, u32 tw, u32 th);
static const u8 *urec_px_(const struct urec_s *r);
static u32 urec_w_(const struct urec_s *r);
static int ntex_check;
static void urec_why_(u32 bp);
static unsigned long long n_pal_up, n_pal_mem;   /* palettes loaded from uploads, from GS memory (UYA_FPSLOG) */
static int clut_from_upload(u64 t0)
{
    u32 psm = BITS(t0, 20, 6), cbp = BITS(t0, 37, 14), cpsm = BITS(t0, 51, 4);
    if ((psm != 0x13 && psm != 0x1B) || cpsm != 0) return 0;
    const struct urec_s *r = urec_find_(0x00, cbp, 1, 16, 16);
    if (!r) {
        static int why = -1, left = 40; if (why < 0) why = getenv("UYA_NTEXWHY") != 0;   /* testing: why a palette is not from its upload */
        static long from = -2; if (from == -2) { const char *e = getenv("UYA_NTEXWHY_FROM"); from = e ? atol(e) : 0; }
        if (why && left > 0 && (long)uya_frame_count() >= from) { left--; urec_why_(cbp); }
        return 0;
    }
    static unsigned long long nup, nchk, nbad;
    const u8 *px = urec_px_(r); u32 w = urec_w_(r);
    for (u32 i = 0; i < 256; i++) {
        u32 x = (i & 7) | ((i & 0x10) >> 1), y = ((i >> 3) & 1) | ((i >> 5) << 1);
        memcpy(&clut[i], px + (y * w + x) * 4, 4);
    }
    nup++; n_pal_up++;
    if (ntex_check > 0 && !(gsgl_on && gsgl_tex_gpu(0x00, cbp, 1, 16, 16))) {
        u32 a[256]; memcpy(a, clut, sizeof a);
        if (gsgl_on) gsgl_cpu_read(cbp, 4);
        clut_read(t0);
        nchk++;
        if (memcmp(a, clut, sizeof a)) { if (nbad++ < 20) uya_log("NTEXCHECK: palette %04X from its upload differs from GS memory", cbp); }
        if ((nchk & 4095) == 0) uya_log("NTEXCHECK: %llu palettes from uploads checked, %llu differ (%llu from uploads)", nchk, nbad, nup);
        memcpy(clut, a, sizeof a);
    }
    clut_hash_set();
    return 1;
}
/* the palette into clut[] (CSM1) */
static void clut_read(u64 t0)
{
    n_pal_mem++;
    u32 psm = BITS(t0, 20, 6), cbp = BITS(t0, 37, 14), cpsm = BITS(t0, 51, 4), csa = BITS(t0, 56, 5);
    u32 cp = (cpsm == 0) ? 0x00 : (cpsm == 0xA ? 0x0A : 0x02);
    if ((psm == 0x13 || psm == 0x1B) && cp == 0x00) {   /* the same as below, the 32-bit reads inline */
        for (u32 i = 0; i < 256; i++) {
            u32 x = (i & 7) | ((i & 0x10) >> 1), y = ((i >> 3) & 1) | ((i >> 5) << 1);
            clut[i] = vram[a32(cbp, 1, x, y)];
        }
    } else if (psm == 0x13 || psm == 0x1B) {          /* 8-bit: 256 entries, 16x16 with index bits 3 and 4 swapped */
        for (u32 i = 0; i < 256; i++) {
            u32 x = (i & 7) | ((i & 0x10) >> 1), y = ((i >> 3) & 1) | ((i >> 5) << 1);
            clut[i] = px_read(cp, cbp, 1, x, y);
        }
    } else if (psm == 0x14 || psm == 0x24 || psm == 0x2C) {   /* 4-bit: 16 entries, 8x2, at CSA */
        u32 base = (cp == 0x00 ? (csa & 15) : csa) * 16;
        for (u32 i = 0; i < 16; i++) clut[(base + i) & 255] = px_read(cp, cbp, 1, i & 7, i >> 3);
    }
    clut_hash_set();   /* (a hash of the palette, for the texture cache) */
    if (trxlog_on()) uya_log("   clut hash %08X", clut_hash);
}

static void prim_reset(void) { vcount = 0; }
/* testing: UYA_TRXLOG=from,to logs transfers, CLUT loads and texture builds in that vsync range */
static int trxlog_on(void)
{
    static int st = -1; static unsigned a, b;
    if (st < 0) { const char *e = getenv("UYA_TRXLOG"); st = e && sscanf(e, "%u,%u", &a, &b) == 2; }
    if (!st) return 0;
    unsigned f = uya_frame_count();
    static unsigned lastf;
    if (f >= a && f <= b && f != lastf) { lastf = f; uya_log("---- vsync %u", f); }
    return f >= a && f <= b;
}

/* ---- texture sampling ---- */
typedef struct { u8 r, g, b, a; } rgba_t;

static inline rgba_t c32(u32 v) { rgba_t c = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)}; return c; }
static inline rgba_t c16(u32 v, int aem, u8 ta0, u8 ta1)
{
    rgba_t c;
    c.r = (u8)((v & 31) << 3); c.g = (u8)(((v >> 5) & 31) << 3); c.b = (u8)(((v >> 10) & 31) << 3);
    c.a = (v & 0x8000) ? ta1 : ((aem && !(v & 0x7FFF)) ? 0 : ta0);
    return c;
}

/* the texture at mip level `lvl` (0: TEX0's own) */
/* where mip level `lvl` of the context's texture is (0: TEX0's own) */
static void tex_level(const gs_ctx_t *c, int lvl, u32 *tbp_, u32 *tbw_, u32 *tw_, u32 *th_)
{
    u64 t0 = c->tex0;
    u32 tbp = BITS(t0, 0, 14), tbw = BITS(t0, 14, 6), psm = BITS(t0, 20, 6);
    u32 tw = 1u << BITS(t0, 26, 4), th = 1u << BITS(t0, 30, 4);
    if (lvl > 0) {
        if (BITS(c->tex1, 9, 1)) {            /* MTBA: levels 1-3 follow level 0 */
            u32 b = tbp, w = tw, h = th, bw = tbw;
            for (int k = 0; k < lvl && k < 3; k++) {
                u32 bits = psm_bits(psm); b += (w * h * bits / 8 + 255) / 256; w >>= 1; h >>= 1; bw = bw > 1 ? bw >> 1 : 1;
            }
            tbp = b; tbw = bw;
        } else {
            u64 m = lvl <= 3 ? c->miptbp1 : c->miptbp2;
            int k = (lvl - 1) % 3;
            tbp = BITS(m, k * 20, 14); tbw = BITS(m, k * 20 + 14, 6);
        }
        tw >>= lvl; th >>= lvl;
        if (!tw) tw = 1;
        if (!th) th = 1;
    }
    if (tw > 1024) tw = 1024;
    if (th > 1024) th = 1024;
    *tbp_ = tbp; *tbw_ = tbw; *tw_ = tw; *th_ = th;
}

/* one texel, decoded to RGBA (CLUT and TEXA applied) */
static inline u32 rgba_u32(rgba_t r) { return (u32)r.r | ((u32)r.g << 8) | ((u32)r.b << 16) | ((u32)r.a << 24); }
static inline rgba_t u32_rgba(u32 v) { rgba_t r = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) }; return r; }
static rgba_t texel_raw(u64 t0, u32 tbp, u32 tbw, u32 x, u32 y)
{
    u32 psm = BITS(t0, 20, 6);
    u8 ta0 = (u8)BITS(texa, 0, 8), ta1 = (u8)BITS(texa, 32, 8);
    int aem = (int)BITS(texa, 15, 1);
    u32 cpsm = BITS(t0, 51, 4), csa = BITS(t0, 56, 5);
    u32 v = px_read(psm, tbp, tbw, x & 2047, y & 2047);
    rgba_t r;
    switch (psm) {
    case 0x00: return c32(v);
    case 0x01: r = c32(v); r.a = (aem && !(v & 0xFFFFFF)) ? 0 : ta0; return r;
    case 0x02: case 0x0A: return c16(v, aem, ta0, ta1);
    case 0x13: case 0x1B: v = clut[v & 255]; break;
    case 0x14: case 0x24: case 0x2C: v = clut[((cpsm == 0 ? (csa & 15) : csa) * 16 + v) & 255]; break;
    default: return c32(v);
    }
    return cpsm == 0 ? c32(v) : c16(v, aem, ta0, ta1);
}

/* ---- texture cache ----
 * Textures are decoded (swizzle, CLUT, TEXA) into plain RGBA arrays once and
 * reused until GS memory under them changes: transfers and draws mark the
 * 8 KB pages they write (page_gen), and an entry is checked against its pages
 * once per primitive. */
static u32 page_gen[512], dirty_epoch = 1;
static void mark_blocks(u32 bp, u32 nblocks)
{
    u32 p0 = bp / 32, p1 = (bp + (nblocks ? nblocks : 1) - 1) / 32;
    if (p1 > 511) p1 = 511;
    for (u32 p = p0; p <= p1 && p < 512; p++) page_gen[p]++;
    dirty_epoch++;
}
/* ---- the game's texture uploads, kept as they came (the native texture path) ----
 * The game streams its textures to the GS several times a frame: 8-bit
 * textures (PSMT8) and their palettes go up, are drawn with, and the next set
 * goes over the same memory (Kerwan: blocks 0x3717 to 0x3AFF). At a level's
 * load it also uploads, once, the palettes (16x16 PSMCT32) and the small mip
 * levels. Each PSMT8 and PSMCT32 upload is kept here as the game sent it (the
 * pixels in order), and a texture that is a kept upload, or its top left corner
 * (the game uploads square images of non-square textures), is decoded from it
 * (tc_get), and a palette from its upload (clut_from_upload), instead of being
 * read back out of GS memory. An upload stays usable while nothing
 * wrote over its blocks since: other uploads and local copies (blk_gen, by
 * 256-byte block), draws (pg_draw_gen, by page, counted only for draws whose
 * frame or Z buffer reaches the memory uploads use) and, with GL, GPU targets.
 * UYA_NTEX=0 decodes everything from GS memory (to compare); UYA_NTEXCHECK=1
 * decodes both ways and logs any difference. */
static u32 blk_gen[0x4000];
static u32 pg_draw_gen[512];
static u32 up_lo = 0x4000, up_hi;     /* the blocks uploads were kept for (drawing there invalidates them) */
static u32 up_win_ver;                /* changes when that range grows */
static int ntex_on = -1, ntex_check = -1;
/* the blocks a rectangle of a buffer covers (callback per block) */
static void rect_blocks(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32 w, u32 h, void (*fn)(u32 blk, void *), void *arg)
{
    u32 bwd, bht;
    switch (psm) {
    case 0x00: case 0x01: case 0x1B: case 0x24: case 0x2C: bwd = 8; bht = 8; break;
    case 0x02: case 0x0A: bwd = 16; bht = 8; break;
    case 0x13: bwd = 16; bht = 16; break;
    case 0x14: bwd = 32; bht = 16; break;
    default: {   /* (other formats: every block of the area) */
        u32 n = (((bw ? bw : 1) * 64 * (y + h) * 32 / 8 + 255) / 256) + 64;
        for (u32 i = 0; i < n && bp + i < 0x4000; i++) fn(bp + i, arg);
        return;
    }
    }
    if (!w || !h) return;
    for (u32 ty = y & ~(bht - 1); ty < y + h; ty += bht)
        for (u32 tx = x & ~(bwd - 1); tx < x + w; tx += bwd) {
            u32 X = tx & 2047, Y = ty & 2047, b;
            switch (psm) {
            case 0x02: b = a16(bp, bw, X, Y, gs_block16, 0) >> 7; break;
            case 0x0A: b = a16(bp, bw, X, Y, gs_block16s, 0) >> 7; break;
            case 0x13: b = a8(bp, bw, X, Y) >> 8; break;
            case 0x14: b = a4(bp, bw, X, Y) >> 9; break;
            default: b = a32(bp, bw, X, Y) >> 6; break;
            }
            fn(b & 0x3FFF, arg);
        }
}
static void blk_bump(u32 b, void *a) { (void)a; blk_gen[b]++; }
typedef struct urec_s {
    u32 bp, bw, w, h, hash, nblk, complete, psm;   /* psm: 0x13 (PSMT8) or 0x00 (PSMCT32) */
    u32 blkcap;
    u16 *blk; u32 *gen;                /* its blocks and their generations after it */
    u32 pg0, pg1, *pgen;               /* its pages and their draw generations */
    u8 *px; u32 cap;                   /* the pixels, w * h of 1 (PSMT8) or 4 (PSMCT32) bytes */
} urec_t;
/* (enough that the level's resident uploads, made once at its load: palettes, mip levels 2 and 3,
 * shrub billboards, stay while the streamed ones come and go) */
#define UREC_N 2048
static urec_t urec[UREC_N];
static int urec_head, urec_cur = -1;   /* the next slot; the upload being received */
static int16_t urec_at[0x4000];        /* block -> the record that starts there, + 1 */
static void urec_collect(u32 b, void *a)
{
    urec_t *r = a;
    if (r->nblk < r->blkcap) { r->blk[r->nblk] = (u16)b; r->nblk++; }
}
/* an upload starts (TRXDIR 0) or a local copy is made (TRXDIR 2): its blocks change */
static void urec_transfer(u32 dir)
{
    u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6), dpsm = BITS(bitbltbuf, 56, 6);
    u32 dx = BITS(trxpos, 32, 11), dy = BITS(trxpos, 48, 11), w = BITS(trxreg, 0, 12), h = BITS(trxreg, 32, 12);
    rect_blocks(dpsm, dbp, dbw, dx, dy, w, h, blk_bump, 0);
    urec_cur = -1;
    if (ntex_on < 0) { const char *e = getenv("UYA_NTEX"); ntex_on = !e || atoi(e) != 0; e = getenv("UYA_NTEXCHECK"); ntex_check = e && atoi(e); }
    if (!ntex_on || dir != 0 || (dpsm != 0x13 && dpsm != 0x00) || dx || dy || !w || !h || w > 1024 || h > 1024 || !dbw) return;
    /* the next slot whose record is no longer what memory holds (its first block written since,
     * or another upload starts there now), so the level's resident uploads stay while the
     * streamed ones, written over every pass, take turns; the oldest if every record is live */
    int slot = urec_head;
    for (int k = 0; k < UREC_N; k++) {
        int s_ = (urec_head + k) % UREC_N;
        const urec_t *x = &urec[s_];
        if (!x->complete || !x->nblk || x->bp >= 0x4000 || urec_at[x->bp] != s_ + 1 || blk_gen[x->blk[0]] != x->gen[0]) { slot = s_; break; }
    }
    urec_t *r = &urec[slot];
    if (r->bp < 0x4000 && urec_at[r->bp] == slot + 1) urec_at[r->bp] = 0;
    urec_cur = slot; urec_head = (slot + 1) % UREC_N;
    u32 bpp = dpsm == 0x13 ? 1 : 4;
    u32 need = dpsm == 0x13 ? ((w + 15) / 16 + 1) * ((h + 15) / 16 + 1) : ((w + 7) / 8 + 1) * ((h + 7) / 8 + 1);
    if (need > 4096) need = 4096;
    if (r->blkcap < need) { free(r->blk); free(r->gen); r->blk = malloc(need * 2); r->gen = malloc(need * 4); r->blkcap = need; }
    if (!r->pgen) r->pgen = malloc(512 * 4);
    if (r->cap < w * h * bpp) { free(r->px); r->px = malloc(w * h * bpp); r->cap = w * h * bpp; }
    r->bp = dbp; r->bw = dbw; r->w = w; r->h = h; r->psm = dpsm; r->hash = 2166136261u; r->complete = 0; r->nblk = 0;
    rect_blocks(dpsm, dbp, dbw, 0, 0, w, h, urec_collect, r);
    for (u32 i = 0; i < r->nblk; i++) r->gen[i] = blk_gen[r->blk[i]];
    r->pg0 = 511; r->pg1 = 0;
    for (u32 i = 0; i < r->nblk; i++) { u32 pg = r->blk[i] / 32; if (pg < r->pg0) r->pg0 = pg; if (pg > r->pg1) r->pg1 = pg; }
    for (u32 pg = r->pg0; pg <= r->pg1; pg++) r->pgen[pg - r->pg0] = pg_draw_gen[pg];
    if (dbp < up_lo || dbp + r->nblk > up_hi) {
        if (dbp < up_lo) up_lo = dbp;
        if (r->pg1 * 32 + 32 > up_hi) up_hi = r->pg1 * 32 + 32;
        up_win_ver++;
    }
    urec_at[dbp] = (int16_t)(urec_cur + 1);
}
/* the upload's pixels as they arrive (n pixels from (x, y): a byte each for PSMT8, 4 for PSMCT32, in order) */
static inline void urec_data(u32 x, u32 y, const u8 *d, u32 n)
{
    urec_t *r = &urec[urec_cur];
    u32 bpp = r->psm == 0x13 ? 1 : 4;
    memcpy(r->px + (y * r->w + x) * bpp, d, n * bpp);
    u32 hsh = r->hash;
    for (u32 i = 0; i < n * bpp; i++) hsh = (hsh ^ d[i]) * 16777619u;
    r->hash = hsh;
    if (y == r->h - 1 && x + n == r->w) r->complete = 1;
}
/* the kept upload a texture level (PSMT8 or PSMCT32) is, or is the top left corner of (the same
 * buffer width: texel (x, y) is then the upload's pixel (x, y)), if it is still what memory holds */
static const urec_t *urec_find(u32 psm, u32 tbp, u32 tbw, u32 tw, u32 th)
{
    if (ntex_on <= 0 || tbp >= 0x4000) return 0;
    int i = urec_at[tbp] - 1;
    if (i < 0) return 0;
    const urec_t *r = &urec[i];
    if (!r->complete || r->psm != psm || r->bp != tbp || r->bw != tbw || r->w < tw || r->h < th) return 0;
    for (u32 k = 0; k < r->nblk; k++) if (blk_gen[r->blk[k]] != r->gen[k]) return 0;
    for (u32 pg = r->pg0; pg <= r->pg1; pg++) if (pg_draw_gen[pg] != r->pgen[pg - r->pg0]) return 0;
    return r;
}
static const struct urec_s *urec_find_(u32 psm, u32 tbp, u32 tbw, u32 tw, u32 th) { return urec_find(psm, tbp, tbw, tw, th); }
static void urec_why_(u32 bp)
{
    int i = bp < 0x4000 ? urec_at[bp] - 1 : -1;
    if (i < 0) { uya_log("NTEXWHY: palette %04X: no upload starts there (vsync %u)", bp, uya_frame_count()); return; }
    const urec_t *r = &urec[i];
    u32 k = 0; while (k < r->nblk && blk_gen[r->blk[k]] == r->gen[k]) k++;
    u32 pg = r->pg0; while (pg <= r->pg1 && pg_draw_gen[pg] == r->pgen[pg - r->pg0]) pg++;
    uya_log("NTEXWHY: palette %04X: upload psm %X bw %u %ux%u complete %u; block %u of %u written since (%04X); page %u drawn since (vsync %u)",
            bp, r->psm, r->bw, r->w, r->h, r->complete, k, r->nblk, k < r->nblk ? r->blk[k] : 0, pg <= r->pg1 ? pg : 0, uya_frame_count());
}
static const u8 *urec_px_(const struct urec_s *r) { return r->px; }
static u32 urec_w_(const struct urec_s *r) { return r->w; }
/* a draw's target reaches the uploads' memory: those pages change (vertex_kick) */
static void urec_drawn(u32 fbp, u32 fpsm, u32 fbw, u32 zbp, u32 zpsm, int z, u32 h)
{
    u32 n = (((fbw ? fbw : 1) * 64 * h * (fpsm == 0x02 || fpsm == 0x0A ? 16 : 32) / 8) + 255) / 256;
    for (u32 pg = fbp / 32; pg <= (fbp + n - 1) / 32 && pg < 512; pg++) if (pg * 32 + 32 > up_lo && pg * 32 < up_hi) pg_draw_gen[pg]++;   /* (n blocks from fbp: the last is fbp + n - 1) */
    if (z) {
        n = (((fbw ? fbw : 1) * 64 * h * ((zpsm & 0xF) == 2 || (zpsm & 0xF) == 0xA ? 16 : 32) / 8) + 255) / 256;
        for (u32 pg = zbp / 32; pg <= (zbp + n - 1) / 32 && pg < 512; pg++) if (pg * 32 + 32 > up_lo && pg * 32 < up_hi) pg_draw_gen[pg]++;
    }
}

/* Content of GS memory by page, for the texture cache: games stream textures
 * through the same memory every frame (Kerwan: ~370 rebuilds a vsync), so an
 * entry whose pages were written again is still good if the bytes are the
 * same. A page is hashed once per change (page_gen). */
static u32 page_hg[512] = { [0 ... 511] = ~0u };
static u64 page_h[512];
static u64 pages_content(u32 p0, u32 p1)
{
    u64 h = 0x9E3779B97F4A7C15ull ^ ((u64)p0 << 32) ^ p1;
    for (u32 p = p0; p <= p1; p++) {
        if (page_hg[p] != page_gen[p]) {
            const u64 *w = (const u64 *)(vram + p * 2048);
            u64 a = 0, b = 0, c = 0, d = 0;
            for (u32 i = 0; i < 1024; i += 4) {
                a = (a ^ w[i]) * 0x100000001B3ull; b = (b ^ w[i + 1]) * 0x9E3779B97F4A7C15ull;
                c = (c ^ w[i + 2]) * 0xC2B2AE3D27D4EB4Full; d = (d ^ w[i + 3]) * 0x165667B19E3779F9ull;
            }
            page_h[p] = a ^ (b << 1) ^ (c << 2) ^ (d << 3) ^ (a >> 29) ^ (c >> 31);
            page_hg[p] = page_gen[p];
        }
        h = (h ^ page_h[p]) * 0x100000001B3ull + p;
    }
    return h;
}
/* blocks a w x h area of a buffer (base bp, width bw in 64-pixel units) can touch */
static u32 area_blocks(u32 psm, u32 bw, u32 h)
{
    u32 bits = psm_bits(psm);
    if (bits == 24) bits = 32;
    u32 w = (bw ? bw : 1) * 64;
    return (w * h * bits / 8 + 255) / 256 + 64;
}
typedef struct { u64 k1, k2, ch; u32 epoch, sum, p0, p1, w, h, used, ser; u32 *px; u32 up; } tcache_t;   /* up: decoded from an upload (ch is its content) */
#define TC_BUDGET ((size_t)256 << 20)   /* bytes of decoded textures kept */
static size_t tc_bytes;
static tcache_t tc[TC_N];
static u32 tc_clock, prim_serial;
static struct { u32 serial; const tcache_t *e; } tc_memo[8];
static unsigned long long n_tc_ntex, n_tc_uplook, n_tc_memlook, ntex_why[10];   /* textures decoded from the game's uploads; lookups by upload, by GS memory */
/* a texture level (tw x th, the upload's top left corner) from the game's upload of it, as
 * decode_tex would: PSMT8 through the palette, PSMCT32 as it is */
static void urec_decode(u32 *out, u64 t0, const urec_t *r, u32 tw, u32 th)
{
    if (r->psm == 0x00) {
        for (u32 y = 0; y < th; y++) memcpy(out + y * tw, r->px + (size_t)y * r->w * 4, tw * 4);
        return;
    }
    u32 cpsm = BITS(t0, 51, 4);
    u8 ta0 = (u8)BITS(texa, 0, 8), ta1 = (u8)BITS(texa, 32, 8);
    int aem = (int)BITS(texa, 15, 1);
    u32 pal[256];
    for (u32 i = 0; i < 256; i++) { u32 v = clut[i]; pal[i] = rgba_u32(cpsm == 0 ? c32(v) : c16(v, aem, ta0, ta1)); }
    for (u32 y = 0; y < th; y++) {
        const u8 *px = r->px + (size_t)y * r->w;
        for (u32 x = 0; x < tw; x++) out[y * tw + x] = pal[px[x]];
    }
}
/* a whole texture level to RGBA, a swizzle block at a time */
static void decode_tex(u32 *out, u64 t0, u32 tbp, u32 tbw, u32 tw, u32 th)
{
    u32 psm = BITS(t0, 20, 6), cpsm = BITS(t0, 51, 4), csa = BITS(t0, 56, 5);
    u8 ta0 = (u8)BITS(texa, 0, 8), ta1 = (u8)BITS(texa, 32, 8);
    int aem = (int)BITS(texa, 15, 1);
    u32 pal[256];
    if (psm == 0x13 || psm == 0x1B || psm == 0x14 || psm == 0x24 || psm == 0x2C) {
        u32 off = (psm == 0x13 || psm == 0x1B) ? 0 : (cpsm == 0 ? (csa & 15) : csa) * 16;
        u32 n = (psm == 0x13 || psm == 0x1B) ? 256 : 16;
        for (u32 i = 0; i < n; i++) { u32 v = clut[(off + i) & 255]; pal[i] = rgba_u32(cpsm == 0 ? c32(v) : c16(v, aem, ta0, ta1)); }
    }
    for (u32 y = 0; y < th; y++) {
        u32 *row = out + y * tw;
        u32 yy = y & 2047;
        switch (psm) {
        case 0x13: {
            const u8 *col = gs_col8[yy & 15];
            for (u32 x0 = 0; x0 < tw; x0 += 16) {
                u32 base = a8(tbp, tbw, x0, yy) - col[0], n = tw - x0 < 16 ? tw - x0 : 16;
                for (u32 i = 0; i < n; i++) row[x0 + i] = pal[vram8()[base + col[i]]];
            }
        } break;
        case 0x14: {
            const u16 *col = gs_col4[yy & 15];
            for (u32 x0 = 0; x0 < tw; x0 += 32) {
                u32 base = a4(tbp, tbw, x0, yy) - col[0], n = tw - x0 < 32 ? tw - x0 : 32;
                for (u32 i = 0; i < n; i++) { u32 nb = base + col[i]; row[x0 + i] = pal[(vram8()[nb >> 1] >> ((nb & 1) * 4)) & 15]; }
            }
        } break;
        case 0x00: case 0x01: case 0x1B: case 0x24: case 0x2C: {
            const u8 *col = gs_col32[yy & 7];
            for (u32 x0 = 0; x0 < tw; x0 += 8) {
                u32 base = a32(tbp, tbw, x0, yy) - col[0], n = tw - x0 < 8 ? tw - x0 : 8;
                for (u32 i = 0; i < n; i++) {
                    u32 v = vram[base + col[i]];
                    switch (psm) {
                    case 0x00: row[x0 + i] = v; break;
                    case 0x01: { rgba_t r = c32(v); r.a = (aem && !(v & 0xFFFFFF)) ? 0 : ta0; row[x0 + i] = rgba_u32(r); } break;
                    case 0x1B: row[x0 + i] = pal[v >> 24]; break;
                    case 0x24: row[x0 + i] = pal[(v >> 24) & 15]; break;
                    default: row[x0 + i] = pal[v >> 28]; break;
                    }
                }
            }
        } break;
        case 0x02: case 0x0A: {
            const uint8_t (*bt)[4] = psm == 0x02 ? gs_block16 : gs_block16s;
            const u8 *col = gs_col16[yy & 7];
            for (u32 x0 = 0; x0 < tw; x0 += 16) {
                u32 base = a16(tbp, tbw, x0, yy, bt, 0) - col[0], n = tw - x0 < 16 ? tw - x0 : 16;
                for (u32 i = 0; i < n; i++) row[x0 + i] = rgba_u32(c16(vram16()[base + col[i]], aem, ta0, ta1));
            }
        } break;
        default:
            for (u32 x = 0; x < tw; x++) row[x] = rgba_u32(texel_raw(t0, tbp, tbw, x, yy));
            break;
        }
    }
}

static void gs_drain(void);
static int dbg_race(u32 d0, u32 d1, const char *what, u32 bp);
static int dbg_drain_all = -1;
static u32 n_layout_drain;   /* testing: UYA_DRAINALL=1 (texture builds), 2 (transfers), 3 (both) */
static u8 page_drawn[512];            /* pages draws (maybe still queued) write */
static int range_drawn(u32 bp, u32 n)
{
    u32 p0 = bp / 32, p1 = (bp + (n ? n : 1) - 1) / 32;
    if (p1 > 511) p1 = 511;
    for (u32 p = p0; p <= p1; p++) if (page_drawn[p]) return 1;
    return 0;
}
static u32 cur_target[6];            /* the current target's pages (frame, then Z), still being drawn; [4], [5]: their layouts */
/* A page's layout (buffer width and format) as queued draws use it: workers
 * own rows, and the same memory seen with another width or format puts a
 * row in other rows, so a draw with another layout over queued draws' pages
 * must wait for them (else a worker can read what another has not written) */
static u16 page_layout[512];
static int layout_conflict(u32 bp, u32 n, u16 lay)
{
    u32 p0 = bp / 32, p1 = (bp + (n ? n : 1) - 1) / 32;
    if (p1 > 511) p1 = 511;
    for (u32 p = p0; p <= p1; p++) if (page_drawn[p] && page_layout[p] != lay) return 1;
    return 0;
}
static void set_drawn(u32 bp, u32 n, u16 lay)
{
    u32 p0 = bp / 32, p1 = (bp + (n ? n : 1) - 1) / 32;
    if (p1 > 511) p1 = 511;
    for (u32 p = p0; p <= p1; p++) { page_drawn[p] = 1; page_layout[p] = lay; }
}
static void set_drawn_target(void) { set_drawn(cur_target[0], cur_target[1], (u16)cur_target[4]); if (cur_target[3]) set_drawn(cur_target[2], cur_target[3], (u16)cur_target[5]); }
static void st_dirty_force(void);
static void retire(void *px);
static size_t retire_n;
static void retire_free(int all);
/* Decoded textures come and go all the time (about 200 a vsync in a level, as
 * the game streams textures through GS memory). Big blocks from malloc are
 * fresh mappings that fault in page by page, so freed buffers are kept for
 * reuse by size (a few dozen, at most 64 MB). */
static struct { void *p; size_t n; } tpool[64];
static int tpool_n; static size_t tpool_bytes;
static void *tex_alloc(size_t n)
{
    for (int i = tpool_n - 1; i >= 0; i--)
        if (tpool[i].n == n) { void *p = tpool[i].p; tpool_bytes -= n; tpool[i] = tpool[--tpool_n]; return p; }
    return malloc(n);
}
static void tex_free(void *p, size_t n)
{
    if (!p) return;
    if (n >= 4096 && tpool_bytes + n <= ((size_t)64 << 20)) {
        if (tpool_n == 64) { free(tpool[0].p); tpool_bytes -= tpool[0].n; tpool[0] = tpool[--tpool_n]; }
        tpool[tpool_n].p = p; tpool[tpool_n].n = n; tpool_n++; tpool_bytes += n;
        return;
    }
    free(p);
}
static void tex_pool_flush(void) { while (tpool_n) { tpool_n--; free(tpool[tpool_n].p); } tpool_bytes = 0; }
static const tcache_t *tc_get(const gs_ctx_t *c, int lvl)
{
    if (tc_memo[lvl].serial == prim_serial) return tc_memo[lvl].e;
    u64 t0 = c->tex0;
    u32 tbp, tbw, tw, th, psm = BITS(t0, 20, 6);
    tex_level(c, lvl, &tbp, &tbw, &tw, &th);
    if (lvl == 0) {   /* the REGION modes can address past the texture's size: decode that far too */
        u32 wms = BITS(c->clamp, 0, 2), wmt = BITS(c->clamp, 2, 2);
        u32 mu = BITS(c->clamp, 14, 10), mv = BITS(c->clamp, 34, 10);
        if (wms == 2 && mu + 1 > tw) tw = mu + 1;
        if (wms == 3 && (mu | BITS(c->clamp, 4, 10)) + 1 > tw) tw = (mu | BITS(c->clamp, 4, 10)) + 1;
        if (wmt == 2 && mv + 1 > th) th = mv + 1;
        if (wmt == 3 && (mv | BITS(c->clamp, 24, 10)) + 1 > th) th = (mv | BITS(c->clamp, 24, 10)) + 1;
        if (tw > 1024) tw = 1024;
        if (th > 1024) th = 1024;
    }
    u64 k1 = (u64)tbp | ((u64)tbw << 14) | ((u64)psm << 20) | ((u64)tw << 26) | ((u64)th << 40);
    u32 pal = (psm == 0x13 || psm == 0x1B || psm == 0x14 || psm == 0x24 || psm == 0x2C);
    u64 k2 = pal ? ((u64)clut_hash | ((u64)BITS(t0, 51, 4) << 32) | ((u64)BITS(t0, 56, 5) << 36)) : 0;
    k2 ^= (u64)(BITS(texa, 0, 8) | (BITS(texa, 15, 1) << 8) | (BITS(texa, 32, 8) << 9)) << 44;
    static tcache_t *last[8];
    tcache_t *e = 0;
    static int16_t hidx[8192];                          /* hash of the keys -> entry + 1 */
    u32 hh = (u32)((k1 * 0x9E3779B97F4A7C15ull ^ k2 * 0xC2B2AE3D27D4EB4Full) >> 51);
    u32 p0 = tbp / 32, p1 = (tbp + area_blocks(psm, tbw, th)) / 32;
    if (p1 > 511) p1 = 511;
    u32 sum = 0; int have_sum = 0;
    u64 ch = 0; int have_ch = 0;
    /* a texture that is exactly one of the game's uploads (PSMT8, still intact): found by
     * the upload's content, decoded from it, without GS memory (urec_t, above) */
    const urec_t *rec = (psm == 0x13 || psm == 0x00) ? urec_find(psm, tbp, tbw, tw, th) : 0;
    if (rec) n_tc_uplook++; else n_tc_memlook++;
    {   /* testing: UYA_NTEXWHY=1: why lookups go to GS memory (logged with UYA_FPSLOG) */
        static int why = -1; if (why < 0) why = getenv("UYA_NTEXWHY") != 0;
        if (why && !rec) {
            int k;
            if (psm != 0x13) k = psm == 0x00 ? 1 : psm == 0x14 ? 2 : 3;
            else if (tbp == 0) k = 4;
            else if (urec_at[tbp] == 0) k = lvl ? 5 : 6;
            else { const urec_t *r = &urec[urec_at[tbp] - 1]; k = (r->w != tw || r->h != th || r->bw != tbw) ? 7 : !r->complete ? 8 : 9; }
            ntex_why[k]++;
            {   /* (UYA_NTEXWHY_FROM=v: the first 60 lookups from vsync v, each with its class and what is at its TBP) */
                static int left = -1; static long from = -2;
                if (from == -2) { const char *e = getenv("UYA_NTEXWHY_FROM"); from = e ? atol(e) : 0; e = getenv("UYA_NTEXWHY_N"); left = e ? atoi(e) : 60; }
                if (left > 0 && (long)uya_frame_count() >= from) {
                    left--;
                    static const char *kn[10] = { "", "CT32", "T4", "other format", "TBP 0", "mip level, no upload", "level 0, no upload", "upload of another size", "incomplete", "written over" };
                    char at[96] = "";
                    if (tbp < 0x4000 && urec_at[tbp]) { const urec_t *r = &urec[urec_at[tbp] - 1]; snprintf(at, sizeof at, "; upload there psm %X bw %u %ux%u complete %u", r->psm, r->bw, r->w, r->h, r->complete); }
                    uya_log("NTEXWHY: %s: level %d TBP %04X TBW %u PSM %X %ux%u TEX0 %016llX TEX1 %016llX MIPTBP1 %016llX [%s]%s", kn[k], lvl, tbp, tbw, psm, tw, th,
                            (unsigned long long)t0, (unsigned long long)c->tex1, (unsigned long long)c->miptbp1, gs_src_int, at);
                }
            }
        }
    }
    u64 rch = rec ? ((u64)rec->hash << 24 | (u64)tw << 12 | th) : 0;
    if (rec) {
        if (last[lvl] && last[lvl]->px && last[lvl]->used && last[lvl]->k1 == k1 && last[lvl]->k2 == k2 && last[lvl]->up && last[lvl]->ch == rch) e = last[lvl];
        for (u32 k = 0; k < 16 && !e; k++) {
            int j = hidx[(hh + k) & 8191] - 1;
            if (j >= 0 && tc[j].px && tc[j].used && tc[j].up && tc[j].k1 == k1 && tc[j].k2 == k2 && tc[j].ch == rch) e = &tc[j];
        }
        if (e) n_tc_reuse++;
    }
    else if (last[lvl] && last[lvl]->px && last[lvl]->used && last[lvl]->k1 == k1 && last[lvl]->k2 == k2 && last[lvl]->epoch == dirty_epoch && !last[lvl]->up) e = last[lvl];
    else {
        /* the entries of these keys (several, one per content the memory held): the
         * one whose pages did not change, else one with the same bytes there now */
        int stale_seen = 0;
        for (u32 k = 0; k < 16 && !e; k++) {
            int j = hidx[(hh + k) & 8191] - 1;
            if (j < 0 || !tc[j].px || !tc[j].used || tc[j].k1 != k1 || tc[j].k2 != k2) continue;
            tcache_t *x = &tc[j];
            if (x->up) continue;                /* (an entry decoded from an upload: found by the upload only) */
            if (x->epoch == dirty_epoch) { e = x; break; }
            if (!have_sum) { for (u32 p = p0; p <= p1; p++) sum += page_gen[p]; have_sum = 1; }
            if (x->sum == sum) { x->epoch = dirty_epoch; e = x; break; }
            /* the bytes decide, unless a GPU target owns some of those pages (GL: GS memory is not current there) */
            if ((dbg_drain_all & 128) || (gsgl_on && gsgl_tex_owned(psm, tbp, tbw, tw, th))) { x->used = 0; x->k1 = ~0ull; stale_seen = 1; continue; }
            if (range_drawn(p0 * 32, (p1 - p0 + 1) * 32)) gs_drain();   /* queued draws may still write those pages */
            if (!have_ch) { ch = pages_content(p0, p1); have_ch = 1; }
            if (x->ch == ch) { x->sum = sum; x->epoch = dirty_epoch; e = x; n_tc_reuse++; break; }
            stale_seen = 1;
        }
        if (!e && stale_seen) n_tc_stale++;
    }
    if (dbg_drain_all & 64) e = 0;
    if (!e) {
        /* the texture's memory must hold what the queued draws put there; the
         * entry's old pixels may still be in use by queued draws, so they are
         * retired (freed once the workers are past them), not overwritten */
        if (!rec && (dbg_drain_all & 1 || range_drawn(p0 * 32, (p1 - p0 + 1) * 32))) gs_drain();
        else if (dbg_drain_all & 8) dbg_race(p0, p1, "texture", tbp);
        n_tc_build++;
        if (trxlog_on()) uya_log("tex build lvl %d TBP %04X TBW %u PSM %X %ux%u k2 %llX pages %u-%u", lvl, tbp, tbw, psm, tw, th, (unsigned long long)k2, p0, p1);
        int best = 0;                                    /* the least recently used slot */
        extern u32 gsgl_tc_pin[TC_N], gsgl_batch_id;
        if (tc_nopin < 0) tc_nopin = getenv("UYA_TC_NOPIN") != 0;   /* testing: the old behaviour */
#define TC_PINNED(i) (gsgl_on && gsgl_tc_pin[i] == gsgl_batch_id && !tc_nopin)   /* the GL batch being built uses it */
        while (TC_PINNED(best) && best < TC_N - 1) best++;
        for (int i = 0; i < TC_N; i++) { if (TC_PINNED(i)) continue; if (!tc[i].px || !tc[i].used) { best = i; break; } if (tc[i].used < tc[best].used) best = i; }
        e = &tc[best];
        {   /* put it in the hash (a free or stale slot of its probe run) */
            u32 k;
            for (k = 0; k < 16; k++) { int j = hidx[(hh + k) & 8191] - 1; if (j < 0 || j == best || !tc[j].px || !tc[j].used) break; }
            hidx[(hh + (k < 16 ? k : 0)) & 8191] = (int16_t)(best + 1);
        }
        if (e->px) { retire_n = (size_t)e->w * e->h * 4; retire(e->px); tc_bytes -= (size_t)e->w * e->h * 4; e->px = 0; }
        {   /* a memory budget for the decoded textures: the least recently used go first */
            size_t need = (size_t)tw * th * 4;
            static size_t budget;   /* testing: UYA_TC_BUDGET_MB=n keeps less (evictions often) */
            if (!budget) { const char *x = getenv("UYA_TC_BUDGET_MB"); budget = x && atoi(x) > 0 ? (size_t)atoi(x) << 20 : TC_BUDGET; }
            while (tc_bytes + need > budget) {
                int lru = -1;
                for (int i = 0; i < TC_N; i++)
                    if (tc[i].px && &tc[i] != e && !TC_PINNED(i) && tc[i].used + 8 < tc_clock && (lru < 0 || tc[i].used < tc[lru].used)) lru = i;
                if (lru < 0) break;
                retire_n = (size_t)tc[lru].w * tc[lru].h * 4; retire(tc[lru].px); tc_bytes -= (size_t)tc[lru].w * tc[lru].h * 4;
                tc[lru].px = 0; tc[lru].used = 0; tc[lru].k1 = ~0ull;
            }
            e->px = tex_alloc(need);
            if (!e->px) { gs_drain(); retire_free(1); tex_pool_flush(); e->px = malloc(need); }
            if (!e->px) { uya_log("GS: out of memory for a %ux%u texture", tw, th); exit(1); }
            tc_bytes += need;
        }
        e->k1 = k1; e->k2 = k2; e->w = tw; e->h = th; e->p0 = p0; e->p1 = p1;
        { static u32 ser_n; e->ser = ++ser_n; }
        unsigned long long t_ = rdtsc_();
        if (rec) {
            urec_decode(e->px, t0, rec, tw, th);
            if (ntex_check > 0) {   /* testing: the same as from GS memory? */
                static u32 *tmp; static size_t tmpn; static unsigned long long nchk, nbad;
                if (tmpn < (size_t)tw * th) { free(tmp); tmp = malloc((size_t)tw * th * 4); tmpn = (size_t)tw * th; }
                if (!(gsgl_on && gsgl_tex_owned(psm, tbp, tbw, tw, th))) {
                    if (range_drawn(p0 * 32, (p1 - p0 + 1) * 32)) gs_drain();
                    decode_tex(tmp, t0, tbp, tbw, tw, th);
                    nchk++;
                    if (memcmp(tmp, e->px, (size_t)tw * th * 4)) { if (nbad++ < 20) uya_log("NTEXCHECK: texture %04X %ux%u from its upload differs from GS memory", tbp, tw, th); }
                    if ((nchk & 4095) == 0) uya_log("NTEXCHECK: %llu textures from uploads checked, %llu differ", nchk, nbad);
                }
            }
        } else {
            if (gsgl_on) gsgl_tex_read(psm, tbp, tbw, tw, th);   /* GS memory a GPU target drew */
            decode_tex(e->px, t0, tbp, tbw, tw, th);
            /* testing: UYA_NTEXPOISON=1: an 8-bit texture level that is no intact upload is made solid
             * magenta, to see (frames compared with a normal run) whether any draw really samples one */
            static int poison = -1; if (poison < 0) { const char *x = getenv("UYA_NTEXPOISON"); poison = x && atoi(x); }
            if ((poison & 1) && psm == 0x13) for (u32 i = 0; i < tw * th; i++) e->px[i] = 0x80FF00FFu;
        }
        {   /* testing: UYA_TEXDUMP=tbp saves that texture (RGBA, alpha as a second image) when decoded */
            static int want = -2; static int nd;
            if (want == -2) { const char *x = getenv("UYA_TEXDUMP"); want = x ? (int)strtol(x, 0, 16) : -1; }
            if ((int)tbp == want && nd < 40 && (!getenv("UYA_TRXLOG") || trxlog_on())) {
                char path[64]; snprintf(path, sizeof path, "tex_%04X_%02d_v%u_l%d.ppm", tbp, nd++, uya_frame_count(), lvl);
                FILE *f = fopen(path, "wb");
                if (f) { fprintf(f, "P6 %u %u 255\n", tw, th * 2);
                    for (u32 i = 0; i < tw * th; i++) { fputc(e->px[i] & 255, f); fputc((e->px[i] >> 8) & 255, f); fputc((e->px[i] >> 16) & 255, f); }
                    for (u32 i = 0; i < tw * th; i++) { u32 a = e->px[i] >> 24; fputc(a > 255 ? 255 : a, f); fputc(a, f); fputc(a, f); }
                    fclose(f); }
                uya_log("texture %04X (%ux%u, PSM %X, CBP %X) saved to %s", tbp, tw, th, (u32)BITS(t0, 20, 6), (u32)BITS(t0, 37, 14), path);
            }
        }
        t_tcb += rdtsc_() - t_;
        e->up = rec != 0;
        if (rec) { e->sum = ~0u; e->epoch = 0; e->ch = rch; n_tc_ntex++; }
        else {
            sum = 0;
            for (u32 p = p0; p <= p1; p++) sum += page_gen[p];
            e->sum = sum; e->epoch = dirty_epoch;
            e->ch = (gsgl_on && gsgl_tex_owned(psm, tbp, tbw, tw, th)) ? 0 : pages_content(p0, p1);   /* (never a hash of pages a target owns) */
        }
    }
    e->used = ++tc_clock;
    last[lvl] = e;
    tc_memo[lvl].serial = prim_serial; tc_memo[lvl].e = e;
    return e;
}

/* ---- drawing ----
 * A primitive is turned into a command (pcmd_t: the pixel pipeline's state,
 * the texture's decoded levels from the cache, the vertices) that the
 * rasterizer runs. With UYA_GS_THREADS (default: the number of cores - 1, at
 * most 8) worker threads run the commands, each one drawing its own rows
 * (4-row bands), so every pixel still sees the primitives in order. Anything
 * that reads or writes GS memory outside the drawing (transfers, the texture
 * cache, the display) waits for the workers first (gs_drain). */
/* (tlevel_t, pcmd_t, pprim_t: gs_types.h) */

#define AINL static inline __attribute__((always_inline))
static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static inline float fast_log2(float x)   /* log2|x| to about 0.0002 */
{
    u32 b; memcpy(&b, &x, 4);
    b &= 0x7FFFFFFFu;
    if (!b) return -126.0f;
    float m = (float)(b & 0x7FFFFF) * (1.0f / 8388608.0f);
    return (float)((int)(b >> 23) - 127) + m * (1.4385482f + m * (-0.6780911f + m * (0.3236495f - 0.0842966f * m)));
}

static u32 texel_outside(const pcmd_t *c, int lvl, int x, int y)
{
    gs_ctx_t t; memset(&t, 0, sizeof t);
    t.tex0 = c->tex0; t.tex1 = c->tex1; t.miptbp1 = c->miptbp1; t.miptbp2 = c->miptbp2;
    u32 tbp, tbw, w, h;
    tex_level(&t, lvl, &tbp, &tbw, &w, &h);
    return rgba_u32(texel_raw(c->tex0, tbp, tbw, (u32)x, (u32)y));
}

AINL int wrap1(int x, int n, int wm, int mn, int mx)
{
    switch (wm) {
    case 0: return x & (n - 1);
    case 1: return x < 0 ? 0 : (x >= n ? n - 1 : x);
    case 2: return x < mn ? mn : (x > mx ? mx : x);
    default: return (x & mn) | mx;
    }
}
AINL u32 tfetch(const pcmd_t *c, int lvl, int x, int y)
{
    const tlevel_t *L = &c->lv[lvl];
    int tw = L->w, th = L->h;
    x = wrap1(x, L->lw, c->wms, c->minu, c->maxu);
    y = wrap1(y, L->lh, c->wmt, c->minv, c->maxv);
    if ((u32)x < (u32)tw && (u32)y < (u32)th) return L->px[y * tw + x];
    return texel_outside(c, lvl, x, y);
}

AINL u32 lerp4(u32 p00, u32 p10, u32 p01, u32 p11, u32 fu, u32 fv)
{
    /* two channels at a time: red/blue and green/alpha, 8.8 fixed point */
    u32 iu = 256 - fu, iv = 256 - fv;
    u32 rb0 = ((p00 & 0x00FF00FF) * iu + (p10 & 0x00FF00FF) * fu) >> 8 & 0x00FF00FF;
    u32 rb1 = ((p01 & 0x00FF00FF) * iu + (p11 & 0x00FF00FF) * fu) >> 8 & 0x00FF00FF;
    u32 ga0 = (((p00 >> 8) & 0x00FF00FF) * iu + ((p10 >> 8) & 0x00FF00FF) * fu) >> 8 & 0x00FF00FF;
    u32 ga1 = (((p01 >> 8) & 0x00FF00FF) * iu + ((p11 >> 8) & 0x00FF00FF) * fu) >> 8 & 0x00FF00FF;
    u32 rb = (rb0 * iv + rb1 * fv) >> 8 & 0x00FF00FF;
    u32 ga = (ga0 * iv + ga1 * fv) >> 8 & 0x00FF00FF;
    return rb | (ga << 8);
}

AINL u32 tsample_lvl(const pcmd_t *c, int lvl, float u, float v, int linear)
{
    if (!linear) return tfetch(c, lvl, (int)floorf(u), (int)floorf(v));
    u -= 0.5f; v -= 0.5f;
    float fx = floorf(u), fy = floorf(v);
    int x0 = (int)fx, y0 = (int)fy;
    /* u - floor(u) is in [0, 1): a signed conversion gives the same (and a NaN the same 0x80000000 as clang's unsigned one) */
    u32 fu = (u32)(s32)((u - fx) * 256.0f), fv = (u32)(s32)((v - fy) * 256.0f);
    const tlevel_t *L = &c->lv[lvl];
    int tw = L->w, th = L->h;
    int xa = wrap1(x0, L->lw, c->wms, c->minu, c->maxu), xb = wrap1(x0 + 1, L->lw, c->wms, c->minu, c->maxu);
    int ya = wrap1(y0, L->lh, c->wmt, c->minv, c->maxv), yb = wrap1(y0 + 1, L->lh, c->wmt, c->minv, c->maxv);
    u32 p00, p10, p01, p11;
    if ((u32)xa < (u32)tw && (u32)xb < (u32)tw && (u32)ya < (u32)th && (u32)yb < (u32)th) {
        const u32 *ra = L->px + ya * tw, *rb = L->px + yb * tw;
        p00 = ra[xa]; p10 = ra[xb]; p01 = rb[xa]; p11 = rb[xb];
    } else {
        p00 = tfetch(c, lvl, x0, y0); p10 = tfetch(c, lvl, x0 + 1, y0); p01 = tfetch(c, lvl, x0, y0 + 1); p11 = tfetch(c, lvl, x0 + 1, y0 + 1);
    }
    return lerp4(p00, p10, p01, p11, fu, fv);
}

/* filtering and mipmapping (TEX1: LCM, MXL, MMAG, MMIN, L, K); q is the pixel's Q */
AINL u32 tsample(const pcmd_t *c, float u, float v, float q)
{
    int lin = (c->mmin == 1 || c->mmin == 4 || c->mmin == 5);   /* minification filter within a level */
    if ((c->mxl == 0 || c->mmin < 2) && lin == c->mmag) return tsample_lvl(c, 0, u, v, lin);
    float lod = c->lcm ? c->k : -fast_log2(q) * (float)(1 << c->l) + c->k;
    /* The GS works the LOD out to 4 fraction bits, rounded (the precision of K):
     * a level boundary has that much slack. Games rely on it: RAC1 uploads only
     * the levels its distance test says a moby needs (the others' TBP is stale or
     * 0, the frame buffer), and its test is about as tight. */
    lod = floorf(lod * 16.0f + 0.5f) * (1.0f / 16.0f);
    if (lod <= 0.0f || c->mxl == 0 || c->mmin < 2) return tsample_lvl(c, 0, u, v, lod <= 0.0f ? c->mmag : lin);
    if (c->mmin == 3 || c->mmin == 5) {               /* between two levels */
        int l0 = (int)floorf(lod); float fr = lod - (float)l0;
        /* (x * 2^-n is x / 2^n exactly) */
        if (l0 >= (int)c->mxl) { float s = 1.0f / (float)(1 << c->mxl); return tsample_lvl(c, c->mxl, u * s, v * s, lin); }
        float s0 = 1.0f / (float)(1 << l0), s1 = 1.0f / (float)(2 << l0);
        u32 a = tsample_lvl(c, l0, u * s0, v * s0, lin);
        u32 b = tsample_lvl(c, l0 + 1, u * s1, v * s1, lin);
        u32 f = (u32)(fr * 256), o = 0;
        for (int sh = 0; sh < 32; sh += 8) o |= ((((a >> sh) & 255) * (256 - f) + ((b >> sh) & 255) * f) >> 8) << sh;
        return o;
    }
    int lvl = (int)floorf(lod + 0.5f);
    if (lvl > (int)c->mxl) lvl = (int)c->mxl;
    if (!lvl) return tsample_lvl(c, 0, u, v, lin);
    float s = 1.0f / (float)(1 << lvl);
    return tsample_lvl(c, lvl, u * s, v * s, lin);
}

/* one pixel through the pipeline: u, v are level-0 texel coordinates */
static int ptrace_on, ptrace_x, ptrace_y; static u32 ptrace_fbp; static const void *ptrace_prim;   /* testing: UYA_PIXTRACE=x,y,fbp */
static unsigned ptrace_from;   /* optional 4th field of UYA_PIXTRACE: first vsync to log */
static void ptrace_log(const pcmd_t *p, u32 z, int r, int g, int b, int a, float u, float v, float q)
{
    static int left = 400;
    if (uya_frame_count() < ptrace_from) return;
    if (left-- <= 0) return;
    if (left == 399) uya_log("pixtrace: from vsync %u", uya_frame_count());
    uya_log("pixtrace [%s]: z %08X rgba %d,%d,%d,%d uv %.2f,%.2f q %g | TME %u FST %u TFX %u TCC %u tex0 %016llX tw %d th %d ABE %u (%u%u%u%u fix %u) ATE %u/%u/%u ZTE %u/%u ZPSM %X FBMSK %08X IIP %u FGE %u",
            gs_src_int, z, r, g, b, a, u, v, q, p->tme, p->fst, p->tfx, p->tcc, (unsigned long long)p->tex0, p->lv[0].w, p->lv[0].h, p->abe, p->ba, p->bb, p->bc, p->bd, p->fix,
            p->ate, p->atst, p->aref, p->zte, p->ztst, p->zpsm, p->fbmsk, p->iip, p->fge);
    uya_log("   afail %u, fba %u, colclamp %u, prim at %p", p->afail, p->fba, p->clampc, ptrace_prim);
    if (p->tme) uya_log("   tex1 %016llX miptbp1 %016llX mmin %u mmag %u mxl %u l %u k %g lcm %u: lod %g", (unsigned long long)p->tex1, (unsigned long long)p->miptbp1,
                        p->mmin, p->mmag, p->mxl, p->l, p->k, p->lcm, p->lcm ? p->k : -fast_log2(q) * (float)(1 << p->l) + p->k);
    if (ptrace_prim) {
        const pprim_t *pr = ptrace_prim;
        uya_log("   prim type %u: (%.1f,%.1f rgba %u,%u,%u,%u st %g,%g q %g) (%.1f,%.1f rgba %u,%u,%u,%u) (%.1f,%.1f rgba %u,%u,%u,%u)", pr->type,
                pr->v[0].x, pr->v[0].y, pr->v[0].r, pr->v[0].g, pr->v[0].b, pr->v[0].a, pr->v[0].s, pr->v[0].t, pr->v[0].q,
                pr->v[1].x, pr->v[1].y, pr->v[1].r, pr->v[1].g, pr->v[1].b, pr->v[1].a, pr->v[2].x, pr->v[2].y, pr->v[2].r, pr->v[2].g, pr->v[2].b, pr->v[2].a);
    }
}
static inline void pixel(const pcmd_t *p, int x, int y, u32 z, int r, int g, int b, int a, float u, float v, float q, int f)
{
    if (__builtin_expect(ptrace_on, 0) && x == ptrace_x && y == ptrace_y && p->fbp == ptrace_fbp) ptrace_log(p, z, r, g, b, a, u, v, q);
    if (p->date) {                         /* destination alpha test (RAC1 darkens the pixels its shadows marked with it) */
        u32 m;
        if (p->fpsm == 0x00) m = vram[a32(p->fbp, p->fbw, (u32)x, (u32)y)] >> 31;
        else if (p->fpsm == 0x02 || p->fpsm == 0x0A) m = (px_read(p->fpsm, p->fbp, p->fbw, (u32)x, (u32)y) >> 15) & 1;
        else m = p->datm;                  /* PSMCT24 has no alpha to test */
        if (m != p->datm) return;
    }
    u32 zaddr = 0;                         /* depth test first: a failing pixel is dropped whatever else happens */
    if (p->zpsm == 0x31 || p->zpsm == 0x30) zaddr = a32z(p->zbp, p->fbw, (u32)x, (u32)y);
    if (p->ztst != 1) {
        if (p->ztst == 0) return;
        u32 zd = (p->zpsm == 0x31 || p->zpsm == 0x30) ? vram[zaddr] : px_read(p->zpsm, p->zbp, p->fbw, (u32)x, (u32)y);
        u32 zs = z;
        if (p->zpsm == 0x31) { zs &= 0xFFFFFF; zd &= 0xFFFFFF; }
        if (p->zpsm == 0x32 || p->zpsm == 0x3A) { zs = zs > 0xFFFF ? 0xFFFF : zs; }
        if (p->ztst == 2 ? zs < zd : zs <= zd) return;
    }
    if (p->tme) {
        u32 t = tsample(p, u, v, q);
        int tr = t & 255, tg = (t >> 8) & 255, tb = (t >> 16) & 255, ta = t >> 24;
        switch (p->tfx) {
        case 0: r = (tr * r) >> 7; g = (tg * g) >> 7; b = (tb * b) >> 7; a = p->tcc ? (ta * a) >> 7 : a; break;
        case 1: r = tr; g = tg; b = tb; a = p->tcc ? ta : a; break;
        case 2: r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a; a = p->tcc ? ta + a : a; break;
        default: r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a; a = p->tcc ? ta : a; break;
        }
        r = clamp255(r); g = clamp255(g); b = clamp255(b); a = clamp255(a);
    }
    if (p->fge) {
        r = (r * f + p->fogr * (255 - f)) >> 8; g = (g * f + p->fogg * (255 - f)) >> 8; b = (b * f + p->fogb * (255 - f)) >> 8;
    }
    int write_fb = 1, write_z = !p->zmsk, write_a = 1;
    if (p->ate) {
        int pass;
        switch (p->atst) {
        case 0: pass = 0; break;
        case 1: pass = 1; break;
        case 2: pass = a < (int)p->aref; break;
        case 3: pass = a <= (int)p->aref; break;
        case 4: pass = a == (int)p->aref; break;
        case 5: pass = a >= (int)p->aref; break;
        case 6: pass = a > (int)p->aref; break;
        default: pass = a != (int)p->aref; break;
        }
        if (!pass) {
            switch (p->afail) {
            case 0: return;
            case 1: write_z = 0; break;
            case 2: write_fb = 0; break;
            case 3: write_z = 0; write_a = 0; break;
            }
        }
    }
    if (write_fb) {
        u32 old = 0, out;
        int dr = 0, dg = 0, db = 0, da = 0;
        int need_old = p->abe || p->fbmsk || !write_a || p->fpsm == 0x01;
        u32 faddr = p->fpsm <= 1 ? a32(p->fbp, p->fbw, (u32)x, (u32)y) : 0;
        if (need_old) {
            old = p->fpsm <= 1 ? vram[faddr] : px_read(p->fpsm, p->fbp, p->fbw, (u32)x, (u32)y);
            if (p->fpsm == 0x02 || p->fpsm == 0x0A) {
                dr = (old & 31) << 3; dg = ((old >> 5) & 31) << 3; db = ((old >> 10) & 31) << 3; da = (old & 0x8000) ? 0x80 : 0;
            } else {
                dr = old & 255; dg = (old >> 8) & 255; db = (old >> 16) & 255; da = p->fpsm == 0x01 ? 0x80 : (int)(old >> 24);
            }
        }
        if (p->abe) {
            int cs[3] = {r, g, b}, cd[3] = {dr, dg, db}, o[3];
            int cc = p->bc == 0 ? a : (p->bc == 1 ? da : (int)p->fix);
            for (int k = 0; k < 3; k++) {
                int A = p->ba == 0 ? cs[k] : (p->ba == 1 ? cd[k] : 0);
                int B = p->bb == 0 ? cs[k] : (p->bb == 1 ? cd[k] : 0);
                int D = p->bd == 0 ? cs[k] : (p->bd == 1 ? cd[k] : 0);
                o[k] = (((A - B) * cc) >> 7) + D;
            }
            r = o[0]; g = o[1]; b = o[2];
        }
        if (p->clampc) { r = clamp255(r); g = clamp255(g); b = clamp255(b); }
        else { r &= 255; g &= 255; b &= 255; }
        if (p->fba) a |= 0x80;
        if (!write_a) a = da;
        if (p->fpsm == 0x02 || p->fpsm == 0x0A) {
            out = (u32)(r >> 3) | ((u32)(g >> 3) << 5) | ((u32)(b >> 3) << 10) | ((a & 0x80) ? 0x8000u : 0);
            out = (out & ~(p->fbmsk & 0xFFFF)) | (old & p->fbmsk & 0xFFFF);
        } else {
            out = (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)(a & 255) << 24);
            out = (out & ~p->fbmsk) | (old & p->fbmsk);
        }
        if (p->fpsm == 0x00) vram[faddr] = out;
        else if (p->fpsm == 0x01) vram[faddr] = (vram[faddr] & 0xFF000000u) | (out & 0xFFFFFF);
        else px_write(p->fpsm, p->fbp, p->fbw, (u32)x, (u32)y, out);
    }
    if (write_z && p->zte) {
        if (p->zpsm == 0x30) vram[zaddr] = z;
        else if (p->zpsm == 0x31) vram[zaddr] = (vram[zaddr] & 0xFF000000u) | (z & 0xFFFFFF);
        else px_write(p->zpsm, p->zbp, p->fbw, (u32)x, (u32)y, z);
    }
    if (__builtin_expect(ptrace_on, 0) && x == ptrace_x && y == ptrace_y && p->fbp == ptrace_fbp && uya_frame_count() >= ptrace_from)
        uya_log("   -> %08X (fb %d, z %d, a %d)", px_read(p->fpsm, p->fbp, p->fbw, (u32)x, (u32)y), write_fb, write_z, write_a);
}

/* The same pipeline for the targets almost every pixel goes to: 32-bit colour
 * (PSMCT32, no FBMSK) and 32/24-bit Z. The frame and Z addresses share the
 * swizzle (Z's block is the colour block ^ 0x18), and the row's part of it is
 * worked out once per row (rowaddr_t). Same results as pixel(). */
typedef struct { u32 rowoff; const u8 *brow, *crow; } rowaddr_t;
static inline int fast_ok(const pcmd_t *p) { return p->fpsm == 0 && !p->date && (p->zpsm == 0x30 || p->zpsm == 0x31) && !p->fbmsk && !ptrace_on; }
AINL void rowaddr(const pcmd_t *p, int y, rowaddr_t *ra)
{
    ra->rowoff = ((u32)y >> 5) * p->fbw * 32; ra->brow = gs_block32[((u32)y >> 3) & 3]; ra->crow = gs_col32[y & 7];
}
/* the conditions under which pixel() would use z (else it can be left at 0) */
static inline int z_used(const pcmd_t *p) { return p->ztst != 1 || (p->zte && !p->zmsk) || ptrace_on || !fast_ok(p); }
AINL u32 z_conv(double zz) { return zz <= 0 ? 0 : (zz >= 4294967295.0 ? 0xFFFFFFFFu : (u32)zz); }
/* Specialised copies: a spec S (SP_ON and the flags) makes the pipeline's main
 * switches constants, so each copy has only its own steps; S = 0 reads them
 * all from the command. */
#define SP_ON 1
#define SP_TME 2
#define SP_BIL 4      /* bilinear on level 0 only (else tsample decides) */
#define SP_ABE 8
#define SP_ZT 16      /* a depth test (ZTST 2 or 3) */
#define SP_ZW 32      /* Z can be written (ZTE, no ZMSK) */
#define SPF(S, bit, rt) (((S) & SP_ON) ? (((S) & (bit)) != 0) : (rt))
static inline int spec_key(const pcmd_t *p)
{
    int S = SP_ON;
    if (p->tme) {
        int lin = (p->mmin == 1 || p->mmin == 4 || p->mmin == 5);
        S |= SP_TME;
        if ((p->mxl == 0 || p->mmin < 2) && lin == p->mmag && lin) S |= SP_BIL;
    }
    if (p->abe) S |= SP_ABE;
    if (p->ztst != 1) S |= SP_ZT;
    if (p->zte && !p->zmsk) S |= SP_ZW;
    return S;
}
#define SPEC_LIST(M) SL_T(M, SP_ON) SL_T(M, SP_ON | SP_TME) SL_T(M, SP_ON | SP_TME | SP_BIL)
#define SL_T(M, b) SL_A(M, b) SL_A(M, b | SP_ABE)
#define SL_A(M, b) SL_Z(M, b) SL_Z(M, b | SP_ZT)
#define SL_Z(M, b) M(b) M(b | SP_ZW)
AINL void pixel_fast(u32 *__restrict vr, const pcmd_t *p, const rowaddr_t *ra, int x, u32 z, int r, int g, int b, int a, float u, float v, float q, int f, const int S)
{
    u32 off = ra->rowoff + ((u32)x >> 6) * 32, blk = ra->brow[((u32)x >> 3) & 7], col = ra->crow[x & 7];
    u32 zaddr = (((p->zbp + off + (blk ^ 0x18)) & 0x3FFF) << 6) | col;
    if (SPF(S, SP_ZT, p->ztst != 1)) {
        if (p->ztst == 0) return;
        u32 zd = vr[zaddr], zs = z;
        if (p->zpsm == 0x31) { zs &= 0xFFFFFF; zd &= 0xFFFFFF; }
        if (p->ztst == 2 ? zs < zd : zs <= zd) return;
    }
    if (SPF(S, SP_TME, p->tme)) {
        u32 t = (S & SP_BIL) ? tsample_lvl(p, 0, u, v, 1) : tsample(p, u, v, q);
        int tr = t & 255, tg = (t >> 8) & 255, tb = (t >> 16) & 255, ta = t >> 24;
        switch (p->tfx) {
        case 0: r = (tr * r) >> 7; g = (tg * g) >> 7; b = (tb * b) >> 7; a = p->tcc ? (ta * a) >> 7 : a; break;
        case 1: r = tr; g = tg; b = tb; a = p->tcc ? ta : a; break;
        case 2: r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a; a = p->tcc ? ta + a : a; break;
        default: r = ((tr * r) >> 7) + a; g = ((tg * g) >> 7) + a; b = ((tb * b) >> 7) + a; a = p->tcc ? ta : a; break;
        }
        r = clamp255(r); g = clamp255(g); b = clamp255(b); a = clamp255(a);
    }
    if (p->fge) {
        r = (r * f + p->fogr * (255 - f)) >> 8; g = (g * f + p->fogg * (255 - f)) >> 8; b = (b * f + p->fogb * (255 - f)) >> 8;
    }
    int write_fb = 1, write_z = SPF(S, SP_ZW, p->zte && !p->zmsk), write_a = 1;
    if (p->ate) {
        int pass;
        switch (p->atst) {
        case 0: pass = 0; break;
        case 1: pass = 1; break;
        case 2: pass = a < (int)p->aref; break;
        case 3: pass = a <= (int)p->aref; break;
        case 4: pass = a == (int)p->aref; break;
        case 5: pass = a >= (int)p->aref; break;
        case 6: pass = a > (int)p->aref; break;
        default: pass = a != (int)p->aref; break;
        }
        if (!pass) {
            switch (p->afail) {
            case 0: return;
            case 1: write_z = 0; break;
            case 2: write_fb = 0; break;
            case 3: write_z = 0; write_a = 0; break;
            }
        }
    }
    if (write_fb) {
        u32 faddr = (((p->fbp + off + blk) & 0x3FFF) << 6) | col;
        int da = 0;
        if (SPF(S, SP_ABE, p->abe)) {
            u32 old = vr[faddr];
            int cs[3] = {r, g, b}, cd[3] = {(int)(old & 255), (int)((old >> 8) & 255), (int)((old >> 16) & 255)}, o[3];
            da = (int)(old >> 24);
            int cc = p->bc == 0 ? a : (p->bc == 1 ? da : (int)p->fix);
            for (int k = 0; k < 3; k++) {
                int A = p->ba == 0 ? cs[k] : (p->ba == 1 ? cd[k] : 0);
                int B = p->bb == 0 ? cs[k] : (p->bb == 1 ? cd[k] : 0);
                int D = p->bd == 0 ? cs[k] : (p->bd == 1 ? cd[k] : 0);
                o[k] = (((A - B) * cc) >> 7) + D;
            }
            r = o[0]; g = o[1]; b = o[2];
        } else if (!write_a) da = (int)(vr[faddr] >> 24);
        if (p->clampc) { r = clamp255(r); g = clamp255(g); b = clamp255(b); }
        else { r &= 255; g &= 255; b &= 255; }
        if (p->fba) a |= 0x80;
        if (!write_a) a = da;
        vr[faddr] = (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)(a & 255) << 24);
    }
    if (write_z) {
        if (p->zpsm == 0x30) vr[zaddr] = z;
        else vr[zaddr] = (vr[zaddr] & 0xFF000000u) | (z & 0xFFFFFF);
    }
}

/* a triangle's attributes as planes (value = dx * x + dy * y + c) */
typedef struct { float rdx, rdy, rc, gdx, gdy, gc, bdx, bdy, bc, adx, ady, ac, fdx, fdy, fc, udx, udy, uc, vdx, vdy, vc, qdx, qdy, qc; double zA, zB, zC; int fr, fg, fb, fa; } tplanes_t;
/* a row of a triangle (x from xs to xe) and of a sprite, through pixel_fast; a
 * row starts each attribute from its plane (only those the pixels use) */
AINL void tri_row(const pcmd_t *p, int y, int xs, int xe, const tplanes_t *P, const int S)
{
    rowaddr_t ra; rowaddr(p, y, &ra);
    u32 *__restrict vr = vram;   /* GS memory is not the command's memory */
    const int need_z = (S & SP_ON) ? (S & (SP_ZT | SP_ZW)) != 0 : z_used(p), tme = SPF(S, SP_TME, p->tme), persp = tme && !p->fst, iip = p->iip;
    const float fx = (float)xs, py = (float)y;
    const float rdx = P->rdx, gdx = P->gdx, bdx = P->bdx, adx = P->adx, fdx = P->fdx, udx = P->udx, vdx = P->vdx, qdx = P->qdx;
    const double zA = P->zA;
    const int fr = P->fr, fg = P->fg, fb = P->fb, fa = P->fa;
    float r = 0, g = 0, b = 0, a = 0, f = 0, u = 0, v = 0, q = 0;
    double zz = 0;
    if (iip) { r = rdx * fx + P->rdy * py + P->rc; g = gdx * fx + P->gdy * py + P->gc; b = bdx * fx + P->bdy * py + P->bc; a = adx * fx + P->ady * py + P->ac; }
    if (p->fge) f = fdx * fx + P->fdy * py + P->fc;
    if (tme) { u = udx * fx + P->udy * py + P->uc; v = vdx * fx + P->vdy * py + P->vc; q = qdx * fx + P->qdy * py + P->qc; }
    if (need_z) zz = zA * xs + P->zB * y + P->zC;
    for (int x = xs; x <= xe; x++) {
        u32 z = need_z ? z_conv(zz) : 0;
        float tu = u, tv = v;
        if (persp && q != 0) { float iq = 1.0f / q; tu = u * iq; tv = v * iq; }
        if (iip) pixel_fast(vr, p, &ra, x, z, (int)r, (int)g, (int)b, (int)a, tu, tv, q, (int)f, S);
        else pixel_fast(vr, p, &ra, x, z, fr, fg, fb, fa, tu, tv, q, (int)f, S);
        r += rdx; g += gdx; b += bdx; a += adx; f += fdx; u += udx; v += vdx; q += qdx; zz += zA;
    }
}
AINL void sprite_row(const pcmd_t *p, int y, int ix0, int ix1, u32 z, int cr, int cg, int cb, int ca, int cf, float u0, float x0, float du, float vv, float q, const int S)
{
    rowaddr_t ra; rowaddr(p, y, &ra);
    u32 *__restrict vr = vram;   /* GS memory is not the command's memory */
    for (int x = ix0; x < ix1; x++)
        pixel_fast(vr, p, &ra, x, z, cr, cg, cb, ca, u0 + (x - x0) * du, vv, q, cf, S);
}

/* rows a worker draws */
#define ROW_MINE(y) (nth == 1 || (((u32)(y) >> 2) % (u32)nth) == (u32)tid)

static void vtx_uvq(const pcmd_t *p, const vtx_t *v, float *u, float *w, float *q)
{
    if (p->fst) { *u = v->u; *w = v->v; *q = 1.0f; }
    else { *u = v->s * p->tw; *w = v->t * p->th; *q = v->q; }
}

static void r_sprite(const pcmd_t *p, const vtx_t *V_, int tid, int nth)
{
    const vtx_t *a = &V_[0], *b = &V_[1];
    float x0 = a->x, x1 = b->x, y0 = a->y, y1 = b->y;
    float u0, v0, q0, u1, v1, q1;
    vtx_uvq(p, a, &u0, &v0, &q0);
    vtx_uvq(p, b, &u1, &v1, &q1);
    if (!p->fst) { u0 /= q1; v0 /= q1; u1 /= q1; v1 /= q1; }     /* sprites use the last Q */
    float q = p->fst ? 1.0f : q1;
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = u0; u0 = u1; u1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; t = v0; v0 = v1; v1 = t; }
    int ix0 = (int)ceilf(x0), ix1 = (int)ceilf(x1), iy0 = (int)ceilf(y0), iy1 = (int)ceilf(y1);
    if (ix0 < p->sx0) ix0 = p->sx0;
    if (iy0 < p->sy0) iy0 = p->sy0;
    if (ix1 > p->sx1 + 1) ix1 = p->sx1 + 1;
    if (iy1 > p->sy1 + 1) iy1 = p->sy1 + 1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > 2048) ix1 = 2048;
    if (iy1 > 2048) iy1 = 2048;
    float du = x1 > x0 ? (u1 - u0) / (x1 - x0) : 0, dv = y1 > y0 ? (v1 - v0) / (y1 - y0) : 0;
    if (fast_ok(p)) {
        if (p->ztst == 0) return;
        u32 z = b->z; int cr = b->r, cg = b->g, cb = b->b, ca = b->a, cf = b->f, S = spec_key(p);
        for (int y = iy0; y < iy1; y++) {
            if (!ROW_MINE(y)) continue;
            float vv = v0 + (y - y0) * dv;
            switch (S) {
#define SR(k) case k: sprite_row(p, y, ix0, ix1, z, cr, cg, cb, ca, cf, u0, x0, du, vv, q, k); break;
            SPEC_LIST(SR)
#undef SR
            default: sprite_row(p, y, ix0, ix1, z, cr, cg, cb, ca, cf, u0, x0, du, vv, q, 0); break;
            }
        }
        return;
    }
    for (int y = iy0; y < iy1; y++) {
        if (!ROW_MINE(y)) continue;
        float vv = v0 + (y - y0) * dv;
        for (int x = ix0; x < ix1; x++)
            pixel(p, x, y, b->z, b->r, b->g, b->b, b->a, u0 + (x - x0) * du, vv, q, b->f);
    }
}

static void r_tri(const pcmd_t *p, const vtx_t *V_, int tid, int nth)
{
    const vtx_t *v0 = &V_[0], *v1 = &V_[1], *v2 = &V_[2];
    float area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
    if (fabsf(area) < 1e-6f) return;
    float minx = fminf(v0->x, fminf(v1->x, v2->x)), maxx = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    float miny = fminf(v0->y, fminf(v1->y, v2->y)), maxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    int ix0 = (int)ceilf(minx), ix1 = (int)ceilf(maxx), iy0 = (int)ceilf(miny), iy1 = (int)ceilf(maxy);
    if (ix0 < p->sx0) ix0 = p->sx0;
    if (iy0 < p->sy0) iy0 = p->sy0;
    if (ix1 > p->sx1 + 1) ix1 = p->sx1 + 1;
    if (iy1 > p->sy1 + 1) iy1 = p->sy1 + 1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > 2048) ix1 = 2048;
    if (iy1 > 2048) iy1 = 2048;
    if (ix0 >= ix1 || iy0 >= iy1) return;
    /* barycentric weights as planes: w_k = A_k x + B_k y + C_k */
    float inv = 1.0f / area;
    const vtx_t *V[3] = {v0, v1, v2};
    float A[3], B[3], C[3];
    for (int k = 0; k < 3; k++) {
        const vtx_t *a = V[(k + 1) % 3], *b = V[(k + 2) % 3];
        /* w_k(px,py) = ((a.x - px)(b.y - py) - (b.x - px)(a.y - py)) / area */
        A[k] = (a->y - b->y) * inv;
        B[k] = (b->x - a->x) * inv;
        C[k] = (a->x * b->y - b->x * a->y) * inv;
    }
    /* attribute planes: value = dx * px + dy * py + c */
#define PLANE(get, dx, dy, c0) do { float a0_ = (get(v0)), a1_ = (get(v1)), a2_ = (get(v2)); \
        dx = a0_ * A[0] + a1_ * A[1] + a2_ * A[2]; dy = a0_ * B[0] + a1_ * B[1] + a2_ * B[2]; c0 = a0_ * C[0] + a1_ * C[1] + a2_ * C[2]; } while (0)
    float rdx = 0, rdy = 0, rc = 0, gdx = 0, gdy = 0, gc = 0, bdx = 0, bdy = 0, bc_ = 0, adx = 0, ady = 0, ac = 0, fdx, fdy, fc;
    if (p->iip) {
#define GR(v) (float)(v)->r
#define GG(v) (float)(v)->g
#define GB(v) (float)(v)->b
#define GA(v) (float)(v)->a
        PLANE(GR, rdx, rdy, rc); PLANE(GG, gdx, gdy, gc); PLANE(GB, bdx, bdy, bc_); PLANE(GA, adx, ady, ac);
    }
#define GF(v) (float)(v)->f
    PLANE(GF, fdx, fdy, fc);
    /* z in double: 32-bit depths need more than a float's precision */
    double zA = 0, zB = 0, zC = 0;
    if (!fast_ok(p) || z_used(p)) {
        double d[3] = {v0->z, v1->z, v2->z};
        double Ad[3], Bd[3], Cd[3], invd = 1.0 / ((double)(v1->x - v0->x) * (v2->y - v0->y) - (double)(v2->x - v0->x) * (v1->y - v0->y));
        for (int k = 0; k < 3; k++) {
            const vtx_t *a = V[(k + 1) % 3], *b = V[(k + 2) % 3];
            Ad[k] = ((double)a->y - b->y) * invd; Bd[k] = ((double)b->x - a->x) * invd; Cd[k] = ((double)a->x * b->y - (double)b->x * a->y) * invd;
        }
        for (int k = 0; k < 3; k++) { zA += d[k] * Ad[k]; zB += d[k] * Bd[k]; zC += d[k] * Cd[k]; }
    }
    float udx = 0, udy = 0, uc = 0, vdx = 0, vdy = 0, vc = 0, qdx = 0, qdy = 0, qc = 1;
    if (p->tme) {
        float uu[3], vv[3], qq[3];
        for (int k = 0; k < 3; k++) vtx_uvq(p, V[k], &uu[k], &vv[k], &qq[k]);
        float a0_, a1_, a2_;
        a0_ = uu[0]; a1_ = uu[1]; a2_ = uu[2];
        udx = a0_ * A[0] + a1_ * A[1] + a2_ * A[2]; udy = a0_ * B[0] + a1_ * B[1] + a2_ * B[2]; uc = a0_ * C[0] + a1_ * C[1] + a2_ * C[2];
        a0_ = vv[0]; a1_ = vv[1]; a2_ = vv[2];
        vdx = a0_ * A[0] + a1_ * A[1] + a2_ * A[2]; vdy = a0_ * B[0] + a1_ * B[1] + a2_ * B[2]; vc = a0_ * C[0] + a1_ * C[1] + a2_ * C[2];
        if (!p->fst) {
            a0_ = qq[0]; a1_ = qq[1]; a2_ = qq[2];
            qdx = a0_ * A[0] + a1_ * A[1] + a2_ * A[2]; qdy = a0_ * B[0] + a1_ * B[1] + a2_ * B[2]; qc = a0_ * C[0] + a1_ * C[1] + a2_ * C[2];
        }
    }
    /* Pixel centres on an edge belong to one triangle only (the GS's top-left
     * rule): to the triangle that edge is a left edge of (w grows with x), or a
     * top edge (horizontal, w grows with y). Drawing them twice put a darker or
     * brighter seam on every shared edge of blended geometry (the diagonal of
     * a quad). eps[k] is the weight a pixel must reach for edge k. */
    float eps[3];
    for (int k = 0; k < 3; k++) eps[k] = (A[k] > 0 || (A[k] == 0 && B[k] > 0)) ? -1e-5f : 1e-5f;
    const int fast = fast_ok(p), S = fast ? spec_key(p) : 0;
    if (fast && p->ztst == 0) return;          /* nothing passes */
    tplanes_t P = { rdx, rdy, rc, gdx, gdy, gc, bdx, bdy, bc_, adx, ady, ac, fdx, fdy, fc, udx, udy, uc, vdx, vdy, vc, qdx, qdy, qc, zA, zB, zC,
                    V_[2].r, V_[2].g, V_[2].b, V_[2].a };
    float iA[3];
    for (int k = 0; k < 3; k++) iA[k] = A[k] != 0 ? 1.0f / A[k] : 0;
    for (int y = iy0; y < iy1; y++) {
        if (!ROW_MINE(y)) continue;
        float py = (float)y;
        /* the row's span: every w_k >= eps[k] */
        float lo = (float)ix0, hi = (float)(ix1 - 1);
        int empty = 0;
        for (int k = 0; k < 3; k++) {
            float rest = B[k] * py + C[k];
            if (A[k] > 0) { float bnd = (eps[k] - rest) * iA[k]; if (bnd > lo) lo = bnd; }
            else if (A[k] < 0) { float bnd = (eps[k] - rest) * iA[k]; if (bnd < hi) hi = bnd; }
            else if (rest < eps[k]) { empty = 1; break; }
        }
        if (empty) continue;
        int xs = (int)ceilf(lo), xe = (int)floorf(hi);
        if (xs < ix0) xs = ix0;
        if (xe > ix1 - 1) xe = ix1 - 1;
        /* the bounds are exact to rounding: check the end pixels with the weights */
        while (xs <= xe) {
            float px = (float)xs;
            if (A[0] * px + B[0] * py + C[0] >= eps[0] && A[1] * px + B[1] * py + C[1] >= eps[1] && A[2] * px + B[2] * py + C[2] >= eps[2]) break;
            xs++;
        }
        while (xe >= xs) {
            float px = (float)xe;
            if (A[0] * px + B[0] * py + C[0] >= eps[0] && A[1] * px + B[1] * py + C[1] >= eps[1] && A[2] * px + B[2] * py + C[2] >= eps[2]) break;
            xe--;
        }
        if (xs > xe) continue;
        if (fast) {
            switch (S) {
#define TR(k) case k: tri_row(p, y, xs, xe, &P, k); break;
            SPEC_LIST(TR)
#undef TR
            default: tri_row(p, y, xs, xe, &P, 0); break;
            }
            continue;
        }
        float fx = (float)xs;
        float r = rdx * fx + rdy * py + rc, g = gdx * fx + gdy * py + gc, b = bdx * fx + bdy * py + bc_, a = adx * fx + ady * py + ac;
        float f = fdx * fx + fdy * py + fc;
        float u = udx * fx + udy * py + uc, v = vdx * fx + vdy * py + vc, q = qdx * fx + qdy * py + qc;
        double zz = zA * xs + zB * y + zC;
        int fr = V_[2].r, fg = V_[2].g, fb = V_[2].b, fa = V_[2].a;
        for (int x = xs; x <= xe; x++) {
            u32 z = zz <= 0 ? 0 : (zz >= 4294967295.0 ? 0xFFFFFFFFu : (u32)zz);
            float tu = u, tv = v;
            if (p->tme && !p->fst && q != 0) { float iq = 1.0f / q; tu = u * iq; tv = v * iq; }
            if (p->iip) pixel(p, x, y, z, (int)r, (int)g, (int)b, (int)a, tu, tv, q, (int)f);
            else pixel(p, x, y, z, fr, fg, fb, fa, tu, tv, q, (int)f);
            r += rdx; g += gdx; b += bdx; a += adx; f += fdx; u += udx; v += vdx; q += qdx; zz += zA;
        }
    }
#undef PLANE
}

static void r_line(const pcmd_t *p, const vtx_t *V_, int tid, int nth)
{
    const vtx_t *a = &V_[0], *b = &V_[1];
    float dx = b->x - a->x, dy = b->y - a->y;
    int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (n <= 0) n = 1;
    float ua, va, qa, ub, vb, qb;
    vtx_uvq(p, a, &ua, &va, &qa);
    vtx_uvq(p, b, &ub, &vb, &qb);
    if (!p->fst) { ua /= qa; va /= qa; ub /= qb; vb /= qb; }
    float q = p->fst ? 1.0f : qb;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        int x = (int)floorf(a->x + dx * t + 0.5f), y = (int)floorf(a->y + dy * t + 0.5f);
        if (x < p->sx0 || x > p->sx1 || y < p->sy0 || y > p->sy1 || x < 0 || y < 0 || x > 2047 || y > 2047 || !ROW_MINE(y)) continue;
        pixel(p, x, y, b->z, (int)(a->r + (b->r - a->r) * t), (int)(a->g + (b->g - a->g) * t), (int)(a->b + (b->b - a->b) * t),
              (int)(a->a + (b->a - a->a) * t), ua + (ub - ua) * t, va + (vb - va) * t, q, b->f);
    }
}

static void r_point(const pcmd_t *p, const vtx_t *V_, int tid, int nth)
{
    const vtx_t *v = &V_[0];
    int x = (int)floorf(v->x), y = (int)floorf(v->y);
    if (x < p->sx0 || x > p->sx1 || y < p->sy0 || y > p->sy1 || x < 0 || y < 0 || x > 2047 || y > 2047 || !ROW_MINE(y)) return;
    float u, w, q;
    vtx_uvq(p, v, &u, &w, &q);
    if (!p->fst && q != 0) { u /= q; w /= q; }
    pixel(p, x, y, v->z, v->r, v->g, v->b, v->a, u, w, q, v->f);
}

static void run_cmd(const pcmd_t *p, const pprim_t *pr, int tid, int nth)
{
    ptrace_prim = pr;
    if (nth > 1 && !(dbg_drain_all & 16)) {   /* skip a primitive none of whose rows (4-row bands, dealt round-robin) are this thread's */
        const vtx_t *v = pr->v;
        int nv = pr->type == 3 ? 3 : (pr->type == 0 ? 1 : 2);
        float y0 = v[0].y, y1 = v[0].y;
        for (int k = 1; k < nv; k++) { if (v[k].y < y0) y0 = v[k].y; if (v[k].y > y1) y1 = v[k].y; }
        int a = (int)floorf(y0) - 1, b = (int)ceilf(y1) + 1;
        if (a < p->sy0) a = p->sy0;
        if (b > p->sy1) b = p->sy1;
        if (a < 0) a = 0;
        if (b > 2047) b = 2047;
        if (a > b) return;
        int b0 = a >> 2, b1 = b >> 2;
        if (b1 - b0 + 1 < nth) {
            int mine = 0;
            for (int k = b0; k <= b1; k++) if (k % nth == tid) { mine = 1; break; }
            if (!mine) return;
        }
    }
    switch (pr->type) {
    case 0: r_point(p, pr->v, tid, nth); break;
    case 1: r_line(p, pr->v, tid, nth); break;
    case 3: r_tri(p, pr->v, tid, nth); break;
    case 6: r_sprite(p, pr->v, tid, nth); break;
    }
}

/* ---- worker threads ---- */
#define QN 8192
#define SN (QN * 2)
static pprim_t *cq;                      /* ring of primitives */
static pcmd_t *sring;                    /* ring of states the primitives use (a new one only when the state changes) */
static u32 s_head;
/* the counters on cache lines of their own: the producer writes q_head and
 * each worker its q_tail all the time, and sharing a line makes every write
 * take the line from the other cores */
typedef struct { volatile u32 v; char pad_[60]; } __attribute__((aligned(64))) line_u32;
static line_u32 q_head_;                 /* written by the producer */
static line_u32 q_tail_[16];             /* each worker's progress */
#define q_head (q_head_.v)
static int nworkers;
static volatile int gs_quit;
static int dbg_race(u32 d0, u32 d1, const char *what, u32 bp)
{
    if (!nworkers || !sring || !cq) return 0;
    u32 lo = q_head; for (int i = 0; i < nworkers; i++) { u32 t = q_tail_[i].v; if (q_head - t > q_head - lo) lo = t; }
    for (u32 q = lo; q != q_head; q++) {
        const pcmd_t *S = &sring[cq[q % QN].st % SN];
        u32 f0 = S->fbp / 32, f1 = (S->fbp + area_blocks(S->fpsm, S->fbw, S->sy1 + 1) - 1) / 32;
        if (f0 <= d1 && d0 <= f1) { uya_log("RACE: %s at %04X (pages %u-%u) while prim %u draws FBP %04X (pages %u-%u), %u queued", what, bp, d0, d1, q, S->fbp, f0, f1, q_head - lo); gs_drain(); return 1; }
    }
    return 0;
}

#ifdef _WIN32
#include <windows.h>
static void cpu_relax(int n) { if (n < 64) __asm__ volatile("pause"); else Sleep(0); }
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
static void cpu_relax(int n) { if (n < 64) __asm__ volatile("pause"); else sched_yield(); }
#include <semaphore.h>
#include <time.h>
#endif

/* parking an idle thread (so idle drawing threads leave the cores to the
 * game and the GS thread) and waking it */
typedef struct __attribute__((aligned(64))) {
    volatile int sleeping;
#ifdef _WIN32
    HANDLE ev;
#else
    sem_t sem;
#endif
} park_t;
static void park_init(park_t *k)
{
    k->sleeping = 0;
#ifdef _WIN32
    k->ev = CreateEventA(0, FALSE, FALSE, 0);
#else
    sem_init(&k->sem, 0, 0);
#endif
}
static void park_wait(park_t *k)             /* returns on a wake-up or after 2 ms */
{
#ifdef _WIN32
    WaitForSingleObject(k->ev, 2);
#else
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 2000000; if (ts.tv_nsec >= 1000000000) { ts.tv_nsec -= 1000000000; ts.tv_sec++; }
    while (sem_timedwait(&k->sem, &ts) == -1 && errno == EINTR) ;
#endif
}
static void park_wake(park_t *k)
{
    if (!__atomic_load_n(&k->sleeping, __ATOMIC_ACQUIRE)) return;
#ifdef _WIN32
    SetEvent(k->ev);
#else
    sem_post(&k->sem);
#endif
}
static park_t wpark[16];

static void worker_loop(int tid)
{
    u32 t = 0;
    int spin = 0;
    for (;;) {
        u32 h = __atomic_load_n(&q_head, __ATOMIC_ACQUIRE);
        if (t == h) {
            if (spin++ < 2000) { __asm__ volatile("pause"); continue; }
            __atomic_store_n(&wpark[tid].sleeping, 1, __ATOMIC_SEQ_CST);
            if (__atomic_load_n(&q_head, __ATOMIC_SEQ_CST) == t) park_wait(&wpark[tid]);
            __atomic_store_n(&wpark[tid].sleeping, 0, __ATOMIC_RELEASE);
            continue;
        }
        spin = 0;
        while (t != h) { const pprim_t *pr = &cq[t % QN]; run_cmd(&sring[pr->st % SN], pr, tid, nworkers); t++; if ((t & 255) == 0) __atomic_store_n(&q_tail_[tid].v, t, __ATOMIC_RELEASE); }
        __atomic_store_n(&q_tail_[tid].v, t, __ATOMIC_RELEASE);
    }
}
#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID a) { worker_loop((int)(intptr_t)a); return 0; }
#else
static void *worker_main(void *a) { worker_loop((int)(intptr_t)a); return 0; }
#endif

static void mtgs_init(int cores);
static void gs_threads_init(void)
{
    { const char *d = getenv("UYA_DRAINALL"); dbg_drain_all = d ? atoi(d) : 0; }
    { const char *t = getenv("UYA_PIXTRACE"); if (t && sscanf(t, "%d,%d,%x,%u", &ptrace_x, &ptrace_y, &ptrace_fbp, &ptrace_from) >= 3) ptrace_on = 1; }
    const char *e = getenv("UYA_GS_THREADS");
    int n;
    if (e) n = atoi(e);
    else {
#ifdef _WIN32
        SYSTEM_INFO si; GetSystemInfo(&si); n = (int)si.dwNumberOfProcessors - 1;
#else
        n = (int)sysconf(_SC_NPROCESSORS_ONLN) - 1;
#endif
        if (n > 8) n = 8;
    }
    {
        int cores;
#ifdef _WIN32
        SYSTEM_INFO si2; GetSystemInfo(&si2); cores = (int)si2.dwNumberOfProcessors;
#else
        cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
        mtgs_init(cores);
        if (mtgs && !e && n > 1) n--;     /* the GS thread takes a core */
    }
    if (n < 1) n = 0;
    if (n > 16) n = 16;
    if (n == 1) n = 0;                    /* one worker gains nothing over drawing here */
    if (gsgl_on) n = 0;                   /* the GPU draws */
    nworkers = n;
    if (!n) return;
    cq = malloc(sizeof(pprim_t) * QN);
    for (int i = 0; i < n; i++) park_init(&wpark[i]);
    for (int i = 0; i < n; i++) {
#ifdef _WIN32
        CreateThread(0, 0, worker_main, (LPVOID)(intptr_t)i, 0, 0);
#else
        pthread_t th; pthread_create(&th, 0, worker_main, (void *)(intptr_t)i); pthread_detach(th);
#endif
    }
    uya_log("GS: drawing on %d threads", n);
}

static inline int gs_busy(void)
{
    for (int i = 0; i < nworkers; i++) if (__atomic_load_n(&q_tail_[i].v, __ATOMIC_ACQUIRE) != q_head) return 1;
    return 0;
}
/* wait until the workers have drawn everything queued */
static void retire_free(int all);
static void gs_drain(void)
{
    if (!nworkers) return;
    if (gs_busy()) n_drain_wait++;
    int spin = 0;
    while (gs_busy()) cpu_relax(spin++);
    retire_free(1);
    memset(page_drawn, 0, sizeof page_drawn);
    set_drawn_target();
}
static struct { void *px; u32 head; size_t n; } retired[4096];
static u32 n_retired;
static void retire_free(int all)
{
    u32 lo = q_head;
    if (!all) for (int i = 0; i < nworkers; i++) { u32 t = __atomic_load_n(&q_tail_[i].v, __ATOMIC_ACQUIRE); if (q_head - t > q_head - lo) lo = t; }
    u32 k = 0;
    for (u32 i = 0; i < n_retired; i++) {
        if ((s32)(lo - retired[i].head) >= 0) { if (dbg_drain_all & 4) { u32 *q = retired[i].px; for (size_t j = 0; j < retired[i].n / 4; j++) q[j] = 0xFFFF00FFu; static void *pz[64]; static u32 npz; if (pz[npz & 63]) free(pz[npz & 63]); pz[npz++ & 63] = q; } else tex_free(retired[i].px, retired[i].n); }
        else retired[k++] = retired[i];
    }
    n_retired = k;
}
static void retire(void *px)
{
    st_dirty_force();
    if (!nworkers) { tex_free(px, retire_n); return; }
    if (n_retired == 4096) retire_free(0);
    if (n_retired == 4096) { gs_drain(); }
    retired[n_retired].px = px; retired[n_retired].head = q_head; retired[n_retired].n = retire_n; n_retired++;
}

static void submit(const pprim_t *p)
{
    if (!nworkers) {
        if (!uya_timing) { run_cmd(&sring[p->st % SN], p, 0, 1); return; }
        unsigned long long t_ = rdtsc_(); run_cmd(&sring[p->st % SN], p, 0, 1); t_draw += rdtsc_() - t_; return;
    }
    u32 h = q_head;
    int spin = 0;
    for (;;) {                            /* wait for room: no worker may be QN behind */
        int full = 0;
        for (int i = 0; i < nworkers; i++) if (h - __atomic_load_n(&q_tail_[i].v, __ATOMIC_ACQUIRE) >= QN - 1) full = 1;
        if (!full) break;
        cpu_relax(spin++);
    }
    cq[h % QN] = *p;
    __atomic_store_n(&q_head, h + 1, __ATOMIC_SEQ_CST);
    for (int i = 0; i < nworkers; i++) park_wake(&wpark[i]);
}

/* the command for the current primitive: pipeline state and texture levels */
static void cmd_pass(pcmd_t *p, const gs_pass_t *q);
static void cmd_target(pcmd_t *p);
static void cmd_texture(pcmd_t *p, const gs_ctx_t *c);
static int cmd_setup(pcmd_t *p)
{
    u64 pr = prmodecont ? prim : ((prim & 7) | (prmode & ~7ull));
    const gs_ctx_t *c = &ctx[BITS(pr, 9, 1)];
    memset(p, 0, sizeof *p);
    p->tme = BITS(pr, 4, 1); p->fge = BITS(pr, 5, 1); p->abe = BITS(pr, 6, 1); p->fst = BITS(pr, 8, 1); p->iip = BITS(pr, 3, 1);
    cmd_pass(p, &gpass[BITS(pr, 9, 1)]);
    p->tfx = BITS(c->tex0, 35, 2); p->tcc = BITS(c->tex0, 34, 1);
    p->tw = (float)(1u << BITS(c->tex0, 26, 4)); p->th = (float)(1u << BITS(c->tex0, 30, 4));
    cmd_target(p);
    cmd_texture(p, c);
    return 1;
}
/* a command's pass part: target, depth, tests, blend, scissor (the frame's, from gpass), fog
 * colour and colour clamp */
static void cmd_pass(pcmd_t *p, const gs_pass_t *q)
{
    p->fbp = q->fbp; p->fbw = q->fbw; p->fpsm = q->fpsm; p->fbmsk = q->fbmsk;
    p->zbp = q->zbp; p->zpsm = q->zpsm; p->zmsk = q->zmsk;
    p->ate = q->ate; p->atst = q->atst; p->aref = q->aref; p->afail = q->afail;
    p->zte = q->zte; p->ztst = q->ztst; p->date = q->date; p->datm = q->datm;
    p->ba = q->ba; p->bb = q->bb; p->bc = q->bc; p->bd = q->bd; p->fix = q->fix;
    p->fba = q->fba; p->clampc = BITS(colclamp, 0, 1);
    p->sx0 = q->sx0; p->sx1 = q->sx1; p->sy0 = q->sy0; p->sy1 = q->sy1;
    p->fogr = (u8)BITS(fogcol, 0, 8); p->fogg = (u8)BITS(fogcol, 8, 8); p->fogb = (u8)BITS(fogcol, 16, 8);
    if (!p->zte) { p->ztst = 1; }          /* depth test off: always pass */
}
/* the pages this target writes count as changed (texture cache) */
static void cmd_target(pcmd_t *p)
{
    prim_serial++;
    static u32 lk[12]; static int lvalid;
    u32 k[12] = { p->fbp, p->fbw, p->fpsm, p->fbmsk, p->zbp, p->zpsm, p->zmsk, (u32)p->sx0, (u32)p->sx1, (u32)p->sy0, (u32)p->sy1, p->zte };
    if (!lvalid || memcmp(k, lk, sizeof k)) {
        u32 h = (u32)p->sy1 + 1;
        if (lvalid) mark_blocks(lk[0], area_blocks(lk[2], lk[1], lk[10] + 1));
        mark_blocks(p->fbp, area_blocks(p->fpsm, p->fbw, h)); n_mark_draw++;
        if (p->zte) mark_blocks(p->zbp, area_blocks(p->zpsm, p->fbw, h));
        cur_target[0] = p->fbp; cur_target[1] = area_blocks(p->fpsm, p->fbw, h);
        cur_target[2] = p->zbp; cur_target[3] = (p->zte || !p->zmsk) ? area_blocks(p->zpsm, p->fbw, h) : 0;
        cur_target[4] = p->fbw | (p->fpsm << 8); cur_target[5] = p->fbw | (p->zpsm << 8);
        if (layout_conflict(cur_target[0], cur_target[1], (u16)cur_target[4]) ||
            (cur_target[3] && layout_conflict(cur_target[2], cur_target[3], (u16)cur_target[5]))) { n_layout_drain++; gs_drain(); }
        set_drawn_target();
        memcpy(lk, k, sizeof k); lvalid = 1;
    }
}
/* a command's texture part, from the texture's description c (TEX0 TEX1 CLAMP MIPTBP: the GIF's
 * registers, or a native material's, nd_mesh_draw) */
static void cmd_texture(pcmd_t *p, const gs_ctx_t *c)
{
    if (p->tme) {
        u64 t1 = c->tex1;
        p->lcm = BITS(t1, 0, 1); p->mxl = BITS(t1, 2, 3); p->mmag = BITS(t1, 5, 1); p->mmin = BITS(t1, 6, 3); p->l = BITS(t1, 19, 2);
        if (p->mxl > 6) p->mxl = 6;
        p->k = (float)((s32)(BITS(t1, 32, 12) << 20) >> 20) / 16.0f;
        p->wms = BITS(c->clamp, 0, 2); p->wmt = BITS(c->clamp, 2, 2);
        p->minu = (int)BITS(c->clamp, 4, 10); p->maxu = (int)BITS(c->clamp, 14, 10);
        p->minv = (int)BITS(c->clamp, 24, 10); p->maxv = (int)BITS(c->clamp, 34, 10);
        p->tex0 = c->tex0; p->tex1 = c->tex1; p->miptbp1 = c->miptbp1; p->miptbp2 = c->miptbp2;
        int nl = (p->mmin >= 2) ? p->mxl : 0;
        if (gsgl_on) {
            u32 tpsm_ = BITS(c->tex0, 20, 6);
            if (tpsm_ == 0x13 || tpsm_ == 0x1B || tpsm_ == 0x14 || tpsm_ == 0x24 || tpsm_ == 0x2C) clut_flush();   /* (only a paletted texture uses it) */
            p->clamp = c->clamp; p->texa = texa;
            if (gsgl_tex_setup(p)) { nl = -1; p->mxl = 0; }   /* read from a GPU target */
        }
        /* GL, a mipmapped texture: every level is decoded when the batch is drawn, and only the
         * levels its LOD range (its vertices' Q) can reach (gs_gl.c lod_levels). A level the game
         * did not upload because its distance test says it is not needed (TBP 0 or a stale TBP:
         * the frame buffer, or another frame's texture) is then never read. The batch is drawn
         * before any upload can change the memory (gsgl_cpu_write_rect). UYA_GLMIPDEFER=0: off. */
        static int mipdefer = -1;
        if (mipdefer < 0) { const char *e = getenv("UYA_GLMIPDEFER"); mipdefer = !e || atoi(e) != 0; }
        int defer_all = gsgl_on && mipdefer && p->mmin >= 2 && p->mxl > 0;
        for (int lv = 0; lv <= nl; lv++) {
            u32 b_, bw_, lw_, lh_; tex_level(c, lv, &b_, &bw_, &lw_, &lh_);
            p->lv[lv].lw = (int)lw_; p->lv[lv].lh = (int)lh_;
            if (defer_all) { p->defer |= (u8)(1 << lv); continue; }
            /* GL: a level in memory a GPU target owns is decoded only if a batch needs it */
            if (gsgl_on && gsgl_tex_gpu(BITS(c->tex0, 20, 6), b_, bw_, lw_, lh_)) { p->defer |= (u8)(1 << lv); continue; }
            const tcache_t *e = tc_get(c, lv);
            p->lv[lv].px = e->px; p->lv[lv].w = (int)e->w; p->lv[lv].h = (int)e->h;
            p->lv[lv].tci = (int)(e - tc); p->lv[lv].ser = e->ser;
            if (gsgl_on) gsgl_tex_touch(&p->lv[lv]);
        }
        if (nl >= 0 && nl < (int)p->mxl) p->mxl = (u8)nl;
    }
}


/* ---- testing: UYA_MEMSURVEY=n (with UYA_MTGS=0) ----
 * How the game uses GS memory, for a renderer without it: every 256-byte block
 * remembers what last wrote it (an upload from the EE and its format, a local
 * copy, a draw into a frame or Z buffer); every texture and palette a draw
 * reads is classed by that. Logged every n vsyncs (counts since the last log,
 * and since the start). */
enum { SV_NONE, SV_UP, SV_COPY, SV_DRAW, SV_ZDRAW };
static u8 sv_kind[0x4000], sv_psm[0x4000]; static u16 sv_by[0x4000];   /* the frame buffer (FBP) that drew a block, or the upload's DBW<<8|...  */
static int sv_on = -1;
static int survey_on(void) { if (sv_on < 0) { const char *e = getenv("UYA_MEMSURVEY"); sv_on = e ? atoi(e) : 0; } return sv_on > 0; }
static u32 sv_blocks(u32 psm, u32 bw, u32 h)
{
    u32 bits = psm_bits(psm); if (bits == 24) bits = 32;
    return ((bw ? bw : 1) * 64 * h * bits / 8 + 255) / 256;
}
static u16 sv_cur_by;
static void sv_mark(u32 bp, u32 n, u8 kind, u8 psm) { for (u32 i = 0; i < n && bp + i < 0x4000; i++) { sv_kind[bp + i] = kind; sv_psm[bp + i] = psm; sv_by[bp + i] = sv_cur_by; } }
#define SV_T 4096
static struct { u64 key; u32 cls, n, last, by, nper; } sv_tex[SV_T];
static unsigned long long sv_cls_n[8], sv_cls_u[8], sv_cls_n_all[8], sv_cls_u_all[8], sv_clut[8], sv_clut_all[8];
static u32 sv_pairs[64][64], sv_epoch;
static const char *sv_name[8] = { "never written", "uploaded, same format", "uploaded as another format", "rendered (render to texture)", "local copy", "mixed", "rendered Z", "" };
/* the class of a range of blocks read as format psm */
static int sv_class(u32 bp, u32 n, u32 psm, int note)
{
    int c = -1;
    for (u32 i = 0; i < n && bp + i < 0x4000; i++) {
        int k = sv_kind[bp + i], cc;
        if (k == SV_NONE) cc = 0;
        else if (k == SV_UP) { cc = sv_psm[bp + i] == psm ? 1 : 2; if (cc == 2 && note) sv_pairs[sv_psm[bp + i] & 63][psm & 63]++; }
        else if (k == SV_DRAW) cc = 3;
        else if (k == SV_ZDRAW) cc = 6;
        else cc = 4;
        if (c < 0) c = cc; else if (c != cc) c = 5;
    }
    return c < 0 ? 0 : c;
}
static void survey_state(const pcmd_t *p)
{
    sv_cur_by = (u16)(p->fbp / 32);
    if (p->fbw) sv_mark(p->fbp, sv_blocks(p->fpsm, p->fbw, (u32)p->sy1 + 1), SV_DRAW, (u8)p->fpsm);
    if (p->zte && !p->zmsk && p->fbw) sv_mark(p->zbp, sv_blocks(p->zpsm, p->fbw, (u32)p->sy1 + 1), SV_ZDRAW, (u8)p->zpsm);
    if (!p->tme) return;
    u64 t0 = p->tex0;
    u32 tbp = BITS(t0, 0, 14), tbw = BITS(t0, 14, 6), psm = BITS(t0, 20, 6), tw = 1u << BITS(t0, 26, 4), th = 1u << BITS(t0, 30, 4);
    u32 cbp = BITS(t0, 37, 14);
    /* the levels the draw can read: level 0 unless the LOD settings never pick it (a fixed
     * LOD past 0: LCM 1 with K >= 1), and levels 1..MXL from MIPTBP1/2 when mipmapping */
    int cls = -1, lmin = 0;
    if (p->mmin >= 2 && p->mxl > 0 && p->lcm == 1 && p->k >= 1.0f) lmin = 1;
    int lmax = (p->mmin >= 2) ? p->mxl : 0;
    for (int l = lmin; l <= lmax; l++) {
        u32 bp_, bw_;
        if (l == 0) { bp_ = tbp; bw_ = tbw; }
        else if (l <= 3) { u64 m = p->miptbp1; bp_ = BITS(m, (l - 1) * 20, 14); bw_ = BITS(m, (l - 1) * 20 + 14, 6); }
        else { u64 m = p->miptbp2; bp_ = BITS(m, (l - 4) * 20, 14); bw_ = BITS(m, (l - 4) * 20 + 14, 6); }
        u32 lw = tw >> l, lh = th >> l; if (!lw) lw = 1; if (!lh) lh = 1;
        int c1 = sv_class(bp_, sv_blocks(psm, bw_ ? bw_ : (lw + 63) / 64, lh), psm, 1);
        cls = cls < 0 ? c1 : (cls == c1 ? cls : 5);
    }
    if (cls < 0) cls = 0;
    u64 key = (u64)tbp | ((u64)psm << 14) | ((u64)tbw << 20) | ((u64)BITS(t0, 26, 8) << 26) | ((u64)cbp << 34);
    u32 h = (u32)((key * 0x9E3779B97F4A7C15ull) >> 52) & (SV_T - 1);
    for (int k = 0; k < 16; k++) {
        u32 j = (h + k) & (SV_T - 1);
        if (!sv_tex[j].n || sv_tex[j].key == key) {
            if (!sv_tex[j].n) { sv_tex[j].key = key; sv_cls_u_all[cls]++; }
            if (sv_tex[j].last != sv_epoch + 1) { sv_tex[j].last = sv_epoch + 1; sv_cls_u[cls]++; }
            sv_tex[j].n++; sv_tex[j].nper++; sv_tex[j].cls = cls; sv_tex[j].by = sv_kind[tbp] == SV_DRAW ? sv_by[tbp] : 0xFFFF; break;
        }
    }
    sv_cls_n[cls]++; sv_cls_n_all[cls]++;
}
static void survey_clut(u64 t0)
{
    u32 cbp = BITS(t0, 37, 14), cpsm = BITS(t0, 51, 4), psm = BITS(t0, 20, 6);
    u32 n = (psm == 0x13 || psm == 0x1B) ? 4 : 1;
    int cls = sv_class(cbp, n, cpsm == 0 ? 0x00 : cpsm, 0);
    sv_clut[cls]++; sv_clut_all[cls]++;
}
static void survey_report(void)
{
    static u32 vs;
    if (++vs % (u32)sv_on) return;
    uya_log("memory survey at vsync %u: texture reads by what last wrote the texture's blocks (draw states / distinct textures, last %d vsyncs | since the start)", uya_frame_count(), sv_on);
    for (int c = 0; c < 7; c++) if (sv_cls_n_all[c] || sv_clut_all[c])
        uya_log("  %-30s textures %7llu / %5llu | %8llu / %5llu   palettes %6llu | %7llu", sv_name[c], sv_cls_n[c], sv_cls_u[c], sv_cls_n_all[c], sv_cls_u_all[c], sv_clut[c], sv_clut_all[c]);
    for (int a = 0; a < 64; a++) for (int b = 0; b < 64; b++) if (sv_pairs[a][b]) uya_log("  uploaded as format %02X, read as %02X: %u draw states", a, b, sv_pairs[a][b]);
    for (int pass = 0; pass < 16; pass++) {   /* the most drawn textures that are not plain uploads */
        int b = -1;
        for (int j = 0; j < SV_T; j++) if (sv_tex[j].nper && sv_tex[j].cls != 1 && (b < 0 || sv_tex[j].nper > sv_tex[b].nper)) b = j;
        if (b < 0) break;
        u64 k = sv_tex[b].key;
        uya_log("    TBP %04X PSM %02X TBW %u size %ux%u CBP %04X: %u draw states, %s%s", (u32)(k & 0x3FFF), (u32)((k >> 14) & 63), (u32)((k >> 20) & 63),
                1u << ((k >> 26) & 15), 1u << ((k >> 30) & 15), (u32)((k >> 34) & 0x3FFF), sv_tex[b].nper, sv_name[sv_tex[b].cls],
                sv_tex[b].by != 0xFFFF ? " (drawn as frame buffer page" : "");
        if (sv_tex[b].by != 0xFFFF) uya_log("      ... FBP page %u)", sv_tex[b].by);
        sv_tex[b].nper = 0;
    }
    for (int j = 0; j < SV_T; j++) sv_tex[j].nper = 0;
    memset(sv_cls_n, 0, sizeof sv_cls_n); memset(sv_cls_u, 0, sizeof sv_cls_u); memset(sv_clut, 0, sizeof sv_clut); sv_epoch++;
}

/* ---- testing: UYA_GSCENSUS=a,b (best with UYA_MTGS=0) ----
 * What the draws ask of the GS, by source (the GIF/draw-list source name), from vsync a to b: primitives
 * (vertex kicks) and native mesh triangles, the share of them using each GS feature, the distinct
 * materials (the draw state less addresses and scissor), blend equations and targets; transfers by
 * direction. For scoping a renderer that takes native draws without the GS model. */
enum { CF_TME, CF_PAL, CF_MIP, CF_LIN, CF_REGION, CF_FST, CF_FOG, CF_ABE, CF_BLENDX, CF_ATE, CF_AFAIL, CF_DATE, CF_NOZT,
       CF_ZMSK, CF_FBA, CF_FBMSK, CF_NOCLAMP, CF_F16, CF_RT, CF_NOUP, CF_TFX, CF_N };
static const char *const cf_name[CF_N] = { "texture", "paletted", "mipmapped", "bilinear", "region clamp/repeat", "UV (FST)",
    "fog", "blend", "blend not Cs-Cd*As+Cd", "alpha test", "alpha test keeps FB/Z", "destination alpha test", "no Z test",
    "no Z write", "FBA", "frame mask", "colour clamp off", "16-bit target", "texture is a render target", "texture not an intact upload",
    "TFX not modulate" };
#define CS_N 48
#define CH_N 512
typedef struct { const char *src; u64 prims, tris, draws, f[CF_N]; u32 mat[CH_N], nmat; } census_src_t;
static census_src_t cs[CS_N]; static int ncs;
static long cs_a = -2, cs_b;
static u32 cs_blend[64], cs_nblend, cs_tgt[64], cs_ntgt;
static u64 cs_trx[3], cs_trx_bytes[3];
static int census_on(void)
{
    if (cs_a == -2) { const char *e = getenv("UYA_GSCENSUS"); cs_a = -1; if (e) sscanf(e, "%ld,%ld", &cs_a, &cs_b); }
    if (cs_a < 0) return 0;
    long f = (long)uya_frame_count();
    return f >= cs_a && f < cs_b;
}
static void cs_put(u32 *set, u32 *n, u32 cap, u32 v) { for (u32 i = 0; i < *n; i++) if (set[i] == v) return; if (*n < cap) set[(*n)++] = v; }
static void census_add(const pcmd_t *S, u32 n, int mesh)
{
    if (!census_on()) return;
    int i;
    for (i = 0; i < ncs; i++) if (cs[i].src == gs_src_int || !strcmp(cs[i].src, gs_src_int)) break;
    if (i == ncs) { if (ncs == CS_N) i = CS_N - 1; else { cs[ncs].src = gs_src_int; ncs++; } }
    census_src_t *c = &cs[i];
    if (mesh) { c->tris += n; c->draws++; } else c->prims += n;
    u32 psm = BITS(S->tex0, 20, 6);
    int up = 1;
    if (S->tme && !S->rt && (psm == 0x13 || psm == 0x00)) {
        u32 tw = 1u << BITS(S->tex0, 26, 4), th = 1u << BITS(S->tex0, 30, 4);
        up = urec_find(psm, BITS(S->tex0, 0, 14), BITS(S->tex0, 14, 6), tw > 1024 ? 1024 : tw, th > 1024 ? 1024 : th) != 0;
    }
    int on[CF_N] = { S->tme, S->tme && (psm == 0x13 || psm == 0x1B || psm == 0x14 || psm == 0x24 || psm == 0x2C), S->tme && S->mmin >= 2 && S->mxl > 0,
        S->tme && (S->mmag || (S->mmin & 1)), S->tme && (S->wms >= 2 || S->wmt >= 2), S->tme && S->fst, S->fge, S->abe,
        S->abe && !(S->ba == 0 && S->bb == 1 && S->bc == 0 && S->bd == 1), S->ate && S->atst != 1, S->ate && S->atst != 1 && S->afail != 0,
        S->date, S->ztst == 1, S->zmsk, S->fba, S->fbmsk != 0, !S->clampc, S->fpsm != 0 && S->fpsm != 1, S->tme && S->rt, S->tme && !up, S->tme && S->tfx != 0 };
    for (int k = 0; k < CF_N; k++) if (on[k]) c->f[k] += n;
    /* the material: state less the addresses (texture, frame, Z), scissor and texture size */
    u32 h = 2166136261u;
    u32 m[] = { S->tme, S->fge, S->abe, S->fst, S->iip, S->ate, S->atst, S->afail, S->zte, S->ztst, S->zmsk, S->ba, S->bb, S->bc, S->bd,
                S->tfx, S->tcc, S->fba, S->clampc, S->mxl > 0, S->mmag, S->mmin, S->wms, S->wmt, S->date, S->datm, S->aref, S->fix,
                S->fpsm, S->zpsm, S->fbmsk, S->tme ? psm : 99, S->rt != 0 };
    for (u32 k = 0; k < sizeof m / 4; k++) h = (h ^ m[k]) * 16777619u;
    cs_put(c->mat, &c->nmat, CH_N, h);
    if (S->abe) cs_put(cs_blend, &cs_nblend, 64, S->ba | S->bb << 2 | S->bc << 4 | S->bd << 6 | (u32)S->fix << 8);
    cs_put(cs_tgt, &cs_ntgt, 64, S->fbp | S->fpsm << 14 | S->fbw << 20 | (S->zte ? 1u << 31 : 0));
}
static void census_trx(u32 dir, u32 bytes) { if (dir < 3 && census_on()) { cs_trx[dir]++; cs_trx_bytes[dir] += bytes; } }
static void census_vsync(void)
{
    if (cs_a < 0 || (long)uya_frame_count() != cs_b) return;
    double k = (double)(cs_b - cs_a);
    u64 tp = 0, tt = 0; for (int i = 0; i < ncs; i++) { tp += cs[i].prims; tt += cs[i].tris; }
    uya_log("GS census from vsync %ld to %ld, per vsync: %.0f primitives (vertex kicks), %.0f mesh triangles; %u blend equations, %u targets",
            cs_a, cs_b, tp / k, tt / k, cs_nblend, cs_ntgt);
    for (int pass = 0; pass < ncs; pass++) {
        int b = -1;
        for (int i = 0; i < ncs; i++) if (cs[i].src && (b < 0 || cs[i].prims + cs[i].tris > cs[b].prims + cs[b].tris)) b = i;
        if (b < 0) break;
        census_src_t *c = &cs[b]; u64 n = c->prims + c->tris;
        char line[1024]; int o = 0;
        for (int f = 0; f < CF_N; f++) if (c->f[f]) o += snprintf(line + o, sizeof line - o, "%s%s %.0f%%", o ? ", " : "", cf_name[f], 100.0 * c->f[f] / n);
        line[o] = 0;
        uya_log("  %-26s %8.0f prims %8.0f mesh tris (%6.0f mesh draws), %3u materials: %s", c->src, c->prims / k, c->tris / k, c->draws / k, c->nmat, line);
        c->src = 0;
    }
    for (int i = 0; i < (int)cs_nblend; i++) uya_log("    blend (A %u - B %u) * C %u + D %u, FIX %u", cs_blend[i] & 3, cs_blend[i] >> 2 & 3, cs_blend[i] >> 4 & 3, cs_blend[i] >> 6 & 3, cs_blend[i] >> 8);
    for (int i = 0; i < (int)cs_ntgt; i++) uya_log("    target FBP %04X PSM %02X FBW %u%s", cs_tgt[i] & 0x3FFF, cs_tgt[i] >> 14 & 63, cs_tgt[i] >> 20 & 63, cs_tgt[i] >> 31 ? ", Z" : "");
    uya_log("  transfers per vsync: to GS memory %.1f (%.0f KB), back to the EE %.2f (%.0f KB), within GS memory %.2f (%.0f KB)",
            cs_trx[0] / k, cs_trx_bytes[0] / k / 1024, cs_trx[1] / k, cs_trx_bytes[1] / k / 1024, cs_trx[2] / k, cs_trx_bytes[2] / k / 1024);
    if (gsgl_on) gsgl_stats((int)k);
}
/* memcmp of two commands (cmd_setup zeroes the padding), a word at a time */
static inline int cmd_same(const pcmd_t *a, const pcmd_t *b)
{
    const u32 *x = (const u32 *)a, *y = (const u32 *)b;
    u32 d = 0;
    for (u32 i = 0; i < sizeof *a / 4; i++) d |= x[i] ^ y[i];
    return d == 0;
}
/* (the native texture path: a draw into memory uploads were kept for). rows: the rows the
 * primitive can reach (its lowest vertex's, at most the scissor's): a state whose scissor
 * reaches the uploads' memory need not draw there */
static void urec_hits(u32 sti, u32 rows)
{
    const pcmd_t *S = &sring[sti % SN];
    if (!S->fbw) return;
    if (rows > (u32)S->sy1 + 1) rows = (u32)S->sy1 + 1;
    u32 fn = ((S->fbw * 64 * rows * 4) + 255) / 256;
    int hits = (S->fbp < up_hi && S->fbp + fn > up_lo) || (S->zte && !S->zmsk && S->zbp < up_hi && S->zbp + fn > up_lo);
    if (hits) {   /* testing: UYA_NTEXWHY_DRAW=n: the first n draws that reach the uploads' memory */
        static int left = -1; if (left < 0) { const char *e = getenv("UYA_NTEXWHY_DRAW"); left = e ? atoi(e) : 0; }
        static u32 lst = ~0u;
        if (left > 0 && sti != lst) { left--; lst = sti; uya_log("NTEXWHY draw [%s] v%u: FBP %04X FBW %u FPSM %X ZBP %04X ZPSM %X zte %u zmsk %u rows %u (uploads' blocks %04X..%04X) TME %u", gs_src_int, uya_frame_count(), S->fbp, S->fbw, S->fpsm, S->zbp, S->zpsm, S->zte, S->zmsk, rows, up_lo, up_hi, S->tme); }
    }
    if (hits) urec_drawn(S->fbp, S->fpsm, S->fbw, S->zbp, S->zpsm, S->zte && !S->zmsk, rows);
}
static int st_dirty = 1, st_tme;
static u32 st_epoch;
static void st_dirty_force(void) { st_dirty = 1; }
static unsigned long long gs_kicks_drawn;   /* (UYA_SRCSTATS) */
unsigned long long uya_gs_kicks(void) { return gs_kicks_drawn; }
static void vertex_kick(u32 X, u32 Y, u32 Z, u8 F, int draw)
{
    gs_kicks_drawn += draw != 0;
    u64 pr = prmodecont ? prim : ((prim & 7) | (prmode & ~7ull));
    const gs_ctx_t *c = &ctx[BITS(pr, 9, 1)];
    vtx_t v;
    v.x = ((float)X - (float)BITS(c->xyoffset, 0, 16)) / 16.0f;
    v.y = ((float)Y - (float)BITS(c->xyoffset, 32, 16)) / 16.0f;
    v.z = Z;
    v.r = (u8)BITS(rgbaq, 0, 8); v.g = (u8)BITS(rgbaq, 8, 8); v.b = (u8)BITS(rgbaq, 16, 8); v.a = (u8)BITS(rgbaq, 24, 8);
    { u32 s = (u32)st, t = (u32)(st >> 32); memcpy(&v.s, &s, 4); memcpy(&v.t, &t, 4); }
    v.q = qreg;
    v.u = (float)BITS(uv, 0, 14) / 16.0f;
    v.v = (float)BITS(uv, 16, 14) / 16.0f;
    v.f = F;
    u32 type = (u32)(prim & 7);
    static const int need[8] = {1, 2, 2, 3, 3, 3, 2, 1};
    vq[vcount++] = v;
    if (vcount < need[type]) return;
    if (draw && type != 7) {
        pprim_t pc;
        static const u8 kind[8] = {0, 1, 1, 3, 3, 3, 6, 0};
        stat_prims++;
        if (!sring) sring = calloc(SN, sizeof(pcmd_t));
        /* a new state only when a register changed, or GS memory under the texture */
        if (st_dirty || (st_tme && st_epoch != dirty_epoch)) {
            pcmd_t ns;
            cmd_setup(&ns);
            if (__builtin_expect(sv_on > 0, 0)) survey_state(&ns);
            if (!s_head || !cmd_same(&ns, &sring[(s_head - 1) % SN])) { sring[s_head % SN] = ns; s_head++; }
            st_dirty = 0; st_tme = ns.tme; st_epoch = dirty_epoch;
        }
        pc.st = s_head - 1;
        census_add(&sring[pc.st % SN], 1, 0);
        {
            static const u8 nvk[8] = {1, 2, 2, 3, 3, 3, 2, 1};
            float ym = vq[0].y;
            for (int k = 1; k < nvk[type]; k++) if (vq[k].y > ym) ym = vq[k].y;
            urec_hits(pc.st, ym < 0 ? 1u : (u32)ym + 2);
        }
        {
            static int left = -1;
            if (left < 0) { const char *e = getenv("UYA_SPRLOG"); left = e ? atoi(e) : 0; }
            if (left > 0 && kind[type] == 6) {
                const pcmd_t *S = &sring[pc.st % SN];
                left--;
                uya_log("sprite (%s) (%.1f,%.1f)-(%.1f,%.1f) z %X rgba %u,%u,%u,%u uv %.1f,%.1f-%.1f,%.1f st %g,%g q %g TME %u FST %u TEX0 %016llX ABE %u ATE %u/%u ZTE %u/%u FRAME %llX", gs_src_int,
                        vq[0].x, vq[0].y, vq[1].x, vq[1].y, vq[1].z, vq[1].r, vq[1].g, vq[1].b, vq[1].a, vq[0].u, vq[0].v, vq[1].u, vq[1].v, vq[0].s, vq[0].t, vq[1].q,
                        S->tme, S->fst, (unsigned long long)ctx[BITS(pr, 9, 1)].tex0, S->abe, S->ate, S->atst, S->zte, S->ztst, (unsigned long long)ctx[BITS(pr, 9, 1)].frame);
            }
        }
        {   /* testing: UYA_PRIMLOG=v,n[,1] logs n primitives from vsync v (,1: only FST ones, the 2D overlays) */
            static int left = -1, fst_only; static unsigned from;
            if (left < 0) { const char *e = getenv("UYA_PRIMLOG"); left = 0; if (e) sscanf(e, "%u,%d,%d", &from, &left, &fst_only); }
            if (left > 0 && uya_frame_count() >= from && (!fst_only || BITS(pr, 8, 1))) {
                const pcmd_t *S = &sring[pc.st % SN];
                left--;
                uya_log("prim [%s] %u (%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f) rgba %u,%u,%u,%u uv %.1f,%.1f TME %u FST %u TEX0 %016llX ABE %u ATE %u/%u/%u ZTE %u/%u FRAME %llX ALPHA %llX TEXA %llX", gs_src_int, kind[type],
                        vq[0].x, vq[0].y, vq[1].x, vq[1].y, vq[2].x, vq[2].y, vq[0].r, vq[0].g, vq[0].b, vq[0].a, vq[0].u, vq[0].v,
                        S->tme, S->fst, (unsigned long long)ctx[BITS(pr, 9, 1)].tex0, S->abe, S->ate, S->atst, S->aref, S->zte, S->ztst, (unsigned long long)ctx[BITS(pr, 9, 1)].frame,
                        (unsigned long long)ctx[BITS(pr, 9, 1)].alpha, (unsigned long long)texa);
            }
        }
        pc.type = kind[type];
        pc.v[0] = vq[0]; pc.v[1] = vq[1]; pc.v[2] = vq[2];
        if (kind[type] == 3) {
            const vtx_t *v0 = &vq[0], *v1 = &vq[1], *v2 = &vq[2];
            float area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
            static int left = -1;
            if (left < 0) { const char *e = getenv("UYA_BIGTRI"); left = e ? atoi(e) : 0; }
            if (left > 0 && fabsf(area) > 60000.0f) {
                left--;
                uya_log("big tri (%s): (%.1f,%.1f,%08X q %g) (%.1f,%.1f,%08X q %g) (%.1f,%.1f,%08X q %g) prim %llX", gs_src_int,
                        v0->x, v0->y, v0->z, v0->q, v1->x, v1->y, v1->z, v1->q, v2->x, v2->y, v2->z, v2->q, (unsigned long long)pr);
            }
        }
#ifdef GS_PRIM_HOOK
        GS_PRIM_HOOK(&sring[pc.st % SN], &pc, &ctx[BITS(pr, 9, 1)]);
#endif
        if (gsgl_on) gsgl_prim(&sring[pc.st % SN], pc.st, &pc);
        else submit(&pc);
    }
    switch (type) {
    case 2: vq[0] = vq[1]; vcount = 1; break;                 /* line strip */
    case 4: vq[0] = vq[1]; vq[1] = vq[2]; vcount = 2; break;  /* triangle strip */
    case 5: vq[1] = vq[2]; vcount = 2; break;                 /* fan keeps vertex 0 */
    default: vcount = 0; break;
    }
}

/* ---- transfers ---- */
static u32 trx_x, trx_y, trx_active;
static u8 trx_acc[8];
static u32 trx_nacc;

/* Uploads: games re-send the same texture to the same place every frame. An
 * upload whose data (hash) and place match the last one there, with nothing
 * else written to those pages since, leaves GS memory as it was, so it does
 * not count as a change for the texture cache. */
static u32 trx_pend, trx_rbp, trx_rn, trx_hash;
static struct { u64 key; u32 hash, sum, used; } up_rec[1024];
static u32 pages_sum(u32 bp, u32 n)
{
    u32 p0 = bp / 32, p1 = (bp + (n ? n : 1) - 1) / 32, sum = 0;
    if (p1 > 511) p1 = 511;
    for (u32 p = p0; p <= p1; p++) sum = sum * 31 + page_gen[p];
    return sum;
}
static u32 si_hash[0x4000];   /* testing: the VRAM StoreImage saved, by block (UYA_SICHECK) */
static u32 vhash(u32 bp, u32 nb) { u32 h = 2166136261u; for (u32 i = bp * 64; i < (bp + nb) * 64 && i < (1u << 20); i++) h = (h ^ vram[i]) * 16777619u; return h; }
static void trx_finish(void)
{
    {   static int on = -1; if (on < 0) on = getenv("UYA_SICHECK") != 0;
        u32 dbp = BITS(bitbltbuf, 32, 14);
        if (on && si_hash[dbp] && BITS(bitbltbuf, 48, 6) == 1) { uya_log("restore %04X: VRAM %s the saved data", dbp, (vhash(dbp, 0x40) | 1) == si_hash[dbp] ? "matches" : "DIFFERS from"); si_hash[dbp] = 0; }
    }
    trx_pend = 0;
    u64 key = (bitbltbuf >> 32) ^ ((u64)(trxpos >> 32) << 20) ^ (trxreg * 0x9E3779B97F4A7C15ull);
    u32 i = (u32)(key ^ (key >> 29)) & 1023, k;
    for (k = 0; k < 8; k++) { u32 j = (i + k) & 1023; if (up_rec[j].used && up_rec[j].key == key) { i = j; break; } }
    if (k < 8 && !(dbg_drain_all & 32) && up_rec[i].hash == trx_hash && up_rec[i].sum == pages_sum(trx_rbp, trx_rn)) { if (trxlog_on()) uya_log("   upload done (same data, %08X)", trx_hash); return; }   /* same data again */
    if (trxlog_on()) uya_log("   upload done (%08X), marks blocks %04X+%X", trx_hash, trx_rbp, trx_rn);
    if (k == 8) {   /* a free slot near i, else i */
        for (k = 0; k < 8; k++) if (!up_rec[(i + k) & 1023].used) break;
        i = (i + (k < 8 ? k : 0)) & 1023;
    }
    mark_blocks(trx_rbp, trx_rn); n_mark_trx++;
    up_rec[i].key = key; up_rec[i].hash = trx_hash; up_rec[i].sum = pages_sum(trx_rbp, trx_rn); up_rec[i].used = 1;
}

static void census_trx(u32 dir, u32 bytes);
static void trx_start(u32 dir)
{
    census_trx(dir, BITS(trxreg, 0, 12) * BITS(trxreg, 32, 12) * trx_bits(dir == 1 ? BITS(bitbltbuf, 24, 6) : BITS(bitbltbuf, 56, 6)) / 8);
    {   /* wait for queued draws only if they write what this reads or writes */
        u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6), dpsm = BITS(bitbltbuf, 56, 6);
        u32 sbp = BITS(bitbltbuf, 0, 14), sbw = BITS(bitbltbuf, 16, 6), spsm = BITS(bitbltbuf, 24, 6);
        u32 h = BITS(trxreg, 32, 12);
        if (range_drawn(dbp, area_blocks(dpsm, dbw, BITS(trxpos, 48, 11) + h)) ||
            (dir == 2 && range_drawn(sbp, area_blocks(spsm, sbw, BITS(trxpos, 16, 11) + h))) || (dbg_drain_all & 2)) gs_drain();
        else if ((dbg_drain_all & 8) && nworkers && sring) {   /* testing: a transfer over pages queued draws still write */
            u32 lo = q_head; for (int i = 0; i < nworkers; i++) { u32 t = q_tail_[i].v; if (q_head - t > q_head - lo) lo = t; }
            u32 d0 = dbp / 32, d1 = (dbp + area_blocks(dpsm, dbw, BITS(trxpos, 48, 11) + h) - 1) / 32;
            for (u32 q = lo; q != q_head; q++) {
                const pcmd_t *S = &sring[cq[q % QN].st % SN];
                u32 f0 = S->fbp / 32, f1 = (S->fbp + area_blocks(S->fpsm, S->fbw, S->sy1 + 1) - 1) / 32;
                if (f0 <= d1 && d0 <= f1) { uya_log("RACE: transfer dir %u to %04X (pages %u-%u) while prim %u draws FBP %04X (pages %u-%u), %u queued", dir, dbp, d0, d1, q, S->fbp, f0, f1, q_head - lo); gs_drain(); break; }
            }
        }
    }
    if (trxlog_on()) uya_log("TRX dir %u [%s]: %04X/%u/%X (%u,%u) -> %04X/%u/%X (%u,%u) %ux%u%s", dir, gs_src_int, (u32)BITS(bitbltbuf, 0, 14), (u32)BITS(bitbltbuf, 16, 6), (u32)BITS(bitbltbuf, 24, 6),
                             (u32)BITS(trxpos, 0, 11), (u32)BITS(trxpos, 16, 11), (u32)BITS(bitbltbuf, 32, 14), (u32)BITS(bitbltbuf, 48, 6), (u32)BITS(bitbltbuf, 56, 6),
                             (u32)BITS(trxpos, 32, 11), (u32)BITS(trxpos, 48, 11), (u32)BITS(trxreg, 0, 12), (u32)BITS(trxreg, 32, 12), trx_pend ? " (last one unfinished)" : "");
    if (gsgl_on && (dir == 0 || dir == 2)) {   /* GL: what GPU targets drew there, and their copies of it */
        u32 w = BITS(trxreg, 0, 12), h = BITS(trxreg, 32, 12);
        if (dir == 2) gsgl_cpu_read_rect(BITS(bitbltbuf, 24, 6), BITS(bitbltbuf, 0, 14), BITS(bitbltbuf, 16, 6), BITS(trxpos, 0, 11), BITS(trxpos, 16, 11), w, h);
        gsgl_cpu_write_rect(BITS(bitbltbuf, 56, 6), BITS(bitbltbuf, 32, 14), BITS(bitbltbuf, 48, 6), BITS(trxpos, 32, 11), BITS(trxpos, 48, 11), w, h);
    }
    if (survey_on() && (dir == 0 || dir == 2)) {
        u32 dbp_ = BITS(bitbltbuf, 32, 14), dbw_ = BITS(bitbltbuf, 48, 6), dpsm_ = BITS(bitbltbuf, 56, 6);
        sv_cur_by = 0xFFFF;
        sv_mark(dbp_, sv_blocks(dpsm_, dbw_, BITS(trxpos, 48, 11) + BITS(trxreg, 32, 12)), dir == 0 ? SV_UP : SV_COPY, (u8)dpsm_);
    }
    if (dir == 0 || dir == 2) urec_transfer(dir);
    trx_active = 0;
    if (trx_pend) { mark_blocks(trx_rbp, trx_rn); trx_pend = 0; }   /* an unfinished upload */
    if (dir == 0 || dir == 2) {
        u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6), dpsm = BITS(bitbltbuf, 56, 6);
        u32 dy = BITS(trxpos, 48, 11), h = BITS(trxreg, 32, 12);
        trx_rbp = dbp; trx_rn = area_blocks(dpsm, dbw, dy + h);
        if (dir == 2) mark_blocks(trx_rbp, trx_rn);
        else { trx_pend = 1; trx_hash = 2166136261u; }
    }
    if (dir == 0) { trx_x = 0; trx_y = 0; trx_nacc = 0; trx_active = 1; }
    else if (dir == 2) {                    /* local to local */
        u32 sbp = BITS(bitbltbuf, 0, 14), sbw = BITS(bitbltbuf, 16, 6), spsm = BITS(bitbltbuf, 24, 6);
        u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6), dpsm = BITS(bitbltbuf, 56, 6);
        u32 sx = BITS(trxpos, 0, 11), sy = BITS(trxpos, 16, 11), dx = BITS(trxpos, 32, 11), dy = BITS(trxpos, 48, 11);
        u32 w = BITS(trxreg, 0, 12), h = BITS(trxreg, 32, 12);
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                px_write(dpsm, dbp, dbw, (dx + x) & 2047, (dy + y) & 2047, px_read(spsm, sbp, sbw, (sx + x) & 2047, (sy + y) & 2047));
    }
}

static void trx_pixel(u32 v)
{
    n_trx_px++;
    u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6), dpsm = BITS(bitbltbuf, 56, 6);
    u32 dx = BITS(trxpos, 32, 11), dy = BITS(trxpos, 48, 11);
    u32 w = BITS(trxreg, 0, 12), h = BITS(trxreg, 32, 12);
    if (!w || trx_y >= h) { trx_active = 0; return; }
    px_write(dpsm, dbp, dbw, (dx + trx_x) & 2047, (dy + trx_y) & 2047, v);
    trx_hash = (trx_hash ^ v) * 16777619u;
    if (++trx_x >= w) { trx_x = 0; if (++trx_y >= h) { trx_active = 0; if (trx_pend) trx_finish(); } }
}

static void trx_data_(const u8 *d, u32 bytes);
static void trx_data(const u8 *d, u32 bytes)
{
    unsigned long long t_ = rdtsc_();
    trx_data_(d, bytes);
    t_trx += rdtsc_() - t_;
}
static void trx_data_(const u8 *d, u32 bytes)
{
    u32 dpsm = BITS(bitbltbuf, 56, 6), bits = trx_bits(dpsm);
    u32 i = 0;
    /* whole runs of a row at a time for the common formats */
    if ((dpsm == 0x13 || dpsm == 0x00 || dpsm == 0x14) && !trx_nacc) {
        u32 dbp = BITS(bitbltbuf, 32, 14), dbw = BITS(bitbltbuf, 48, 6);
        u32 dx = BITS(trxpos, 32, 11), dy = BITS(trxpos, 48, 11);
        u32 w = BITS(trxreg, 0, 12), h = BITS(trxreg, 32, 12);
        while (trx_active && i < bytes) {
            if (!w || trx_y >= h) { trx_active = 0; break; }
            u32 avail = dpsm == 0x13 ? bytes - i : (dpsm == 0x00 ? (bytes - i) / 4 : (bytes - i) * 2);
            u32 run = w - trx_x;
            if (run > avail) run = avail;
            if (dpsm == 0x14) run &= ~1u;
            if (!run) break;
            u32 y = (dy + trx_y) & 2047, hsh = trx_hash;
            /* a swizzle column group (16, 8 or 32 pixels: the same block) at a time */
            if (dpsm == 0x13) {
                const u8 *col = gs_col8[y & 15];
                for (u32 k = 0; k < run; ) {
                    u32 X = (dx + trx_x + k) & 2047, n = 16 - (X & 15);
                    if (n > run - k) n = run - k;
                    u32 base = a8(dbp, dbw, X & ~15u, y) - col[0];
                    for (u32 j = 0; j < n; j++) { u32 v = d[i + k + j]; vram8()[base + col[(X & 15) + j]] = (u8)v; hsh = (hsh ^ v) * 16777619u; }
                    k += n;
                }
                if (urec_cur >= 0) urec_data(trx_x, trx_y, d + i, run);
                i += run;
            } else if (dpsm == 0x00) {
                const u8 *col = gs_col32[y & 7];
                for (u32 k = 0; k < run; ) {
                    u32 X = (dx + trx_x + k) & 2047, n = 8 - (X & 7);
                    if (n > run - k) n = run - k;
                    u32 base = a32(dbp, dbw, X & ~7u, y) - col[0];
                    for (u32 j = 0; j < n; j++) { u32 v; memcpy(&v, d + i + 4 * (k + j), 4); vram[base + col[(X & 7) + j]] = v; hsh = (hsh ^ v) * 16777619u; }
                    k += n;
                }
                if (urec_cur >= 0) urec_data(trx_x, trx_y, d + i, run);
                i += run * 4;
            } else {
                const u16 *col = gs_col4[y & 15];
                for (u32 k = 0; k < run; ) {
                    u32 X = (dx + trx_x + k) & 2047, n = 32 - (X & 31);
                    if (n > run - k) n = run - k;
                    u32 base = a4(dbp, dbw, X & ~31u, y) - col[0];
                    for (u32 j = 0; j < n; j++) {
                        u32 kk = k + j, v = (d[i + kk / 2] >> ((kk & 1) * 4)) & 15, nb = base + col[(X & 31) + j];
                        vram8()[nb >> 1] = (u8)((vram8()[nb >> 1] & ((nb & 1) ? 0x0F : 0xF0)) | (v << ((nb & 1) * 4)));
                        hsh = (hsh ^ v) * 16777619u;
                    }
                    k += n;
                }
                i += run / 2;
            }
            trx_hash = hsh; n_trx_px += run;
            trx_x += run;
            if (trx_x >= w) { trx_x = 0; if (++trx_y >= h) { trx_active = 0; if (trx_pend) trx_finish(); } }
        }
    }
    for (; i < bytes && trx_active; i++) {
        if (bits == 4) { trx_pixel(d[i] & 15); if (trx_active) trx_pixel(d[i] >> 4); continue; }
        if (bits == 8) { trx_pixel(d[i]); continue; }
        trx_acc[trx_nacc++] = d[i];
        u32 need = bits / 8;
        if (trx_nacc == need) {
            u32 v = trx_acc[0] | (trx_acc[1] << 8) | (need > 2 ? (u32)trx_acc[2] << 16 : 0) | (need > 3 ? (u32)trx_acc[3] << 24 : 0);
            trx_pixel(v);
            trx_nacc = 0;
        }
    }
}

static void gs_bad(const char *what, u64 v);
/* ---- register writes ---- */
void uya_gs_write(u32 addr, u64 v)
{
#ifdef GS_WRITE_HOOK
    GS_WRITE_HOOK(addr, v);
#endif
    {   /* testing: UYA_FINDREG=reg,value (hex): where the GIF data that writes it lies (with UYA_MTGS=0), once per vsync */
        static int on = -1; static unsigned reg, lastv; static u64 want;
        if (on < 0) { const char *e = getenv("UYA_FINDREG"); on = e && sscanf(e, "%x,%llx", &reg, (unsigned long long *)&want) == 2; }
        if (on && addr == reg && v == want && lastv != uya_frame_count()) {
            lastv = uya_frame_count();
            uya_log("FINDREG %02X = %016llX at vsync %u from %s, data at %08X", reg, (unsigned long long)v, lastv, gs_src_int, (u32)(uintptr_t)gif_cur_qw);
            if (!mtgs && gif_cur_qw) for (int j = -24; j < 8; j++) { const u32 *w = gif_cur_qw + j * 4; uya_log("   %08X: %08X %08X %08X %08X", (u32)(uintptr_t)w, w[0], w[1], w[2], w[3]); }
        }
    }
    if (!(addr <= 0x05 && addr) && addr != 0x0A && addr != 0x0C && addr != 0x0D && (addr < 0x50 || addr > 0x54)) st_dirty = 1;
    switch (addr) {
    case 0x00: prim = v; prim_reset(); break;
    case 0x01: rgbaq = v; { u32 q = (u32)(v >> 32); memcpy(&qreg, &q, 4); } break;
    case 0x02: st = v; break;
    case 0x03: uv = v; break;
    case 0x04: vertex_kick(BITS(v, 0, 16), BITS(v, 16, 16), BITS(v, 32, 24), (u8)BITS(v, 56, 8), 1); break;
    case 0x05: vertex_kick(BITS(v, 0, 16), BITS(v, 16, 16), (u32)(v >> 32), 0, 1); break;
    case 0x0C: vertex_kick(BITS(v, 0, 16), BITS(v, 16, 16), BITS(v, 32, 24), (u8)BITS(v, 56, 8), 0); break;
    case 0x0D: vertex_kick(BITS(v, 0, 16), BITS(v, 16, 16), (u32)(v >> 32), 0, 0); break;
    case 0x06: case 0x07:
        {
            static int left = -1;      /* testing: UYA_BADTEX=n logs TEX0 writes whose top half looks like a pointer, with where the data came from */
            if (left < 0) { const char *e = getenv("UYA_BADTEX"); left = e ? atoi(e) : 0; }
            if (left > 0 && (v >> 48) >= 0xFF00) {
                left--;
                uya_log("odd TEX0 %016llX from %s, data at %08X", (unsigned long long)v, gs_src_int, (u32)(uintptr_t)gif_cur_qw);
                for (int j = -12; j < 8; j++) { const u32 *w = gif_cur_qw + j * 4; uya_log("   %08X: %08X %08X %08X %08X", (u32)(uintptr_t)w, w[0], w[1], w[2], w[3]); }
            }
        }
        ctx[addr - 6].tex0 = v; clut_load(v); break;
    case 0x08: case 0x09: ctx[addr - 8].clamp = v; break;
    case 0x0A: fog = v; break;
    case 0x14: case 0x15: ctx[addr - 0x14].tex1 = v; break;
    case 0x16: case 0x17: {
        gs_ctx_t *c = &ctx[addr - 0x16];
        u64 m = (0x3Full << 20) | (~0ull << 37);           /* PSM and the CLUT fields come from TEX2 */
        c->tex0 = (c->tex0 & ~m) | (v & m);
        clut_load(c->tex0);
    } break;
    case 0x18: case 0x19: ctx[addr - 0x18].xyoffset = v; pass_decode(addr - 0x18); break;
    case 0x34: case 0x35: ctx[addr - 0x34].miptbp1 = v; break;
    case 0x36: case 0x37: ctx[addr - 0x36].miptbp2 = v; break;
    case 0x1A: prmodecont = v & 1; break;
    case 0x1B: prmode = v; break;
    case 0x1C: texclut = v; break;
    case 0x3B: texa = v; break;
    case 0x3D: fogcol = v; break;
    case 0x40: case 0x41: ctx[addr - 0x40].scissor = v; pass_decode(addr - 0x40); break;
    case 0x42: case 0x43: ctx[addr - 0x42].alpha = v; pass_decode(addr - 0x42); break;
    case 0x45: dthe = v; break;
    case 0x46: colclamp = v; break;
    case 0x47: case 0x48: ctx[addr - 0x47].test = v; pass_decode(addr - 0x47); break;
    case 0x49: pabe = v; break;
    case 0x4A: case 0x4B: ctx[addr - 0x4A].fba = v; pass_decode(addr - 0x4A); break;
    case 0x4C: case 0x4D: {
        static int nlog = -1;
        if (nlog < 0) { const char *e = getenv("UYA_FRAMELOG"); nlog = e ? atoi(e) : 0; }
        if (nlog > 0 && ctx[addr - 0x4C].frame != v) { nlog--; uya_log("GS FRAME_%u = FBP 0x%X FBW %u PSM 0x%X MSK %08X", addr - 0x4C + 1, BITS(v, 0, 9) * 32, BITS(v, 16, 6) * 64, BITS(v, 24, 6), (u32)(v >> 32)); }
        { u32 ps = BITS(v, 24, 6); if (!BITS(v, 16, 6) || !(ps == 0 || ps == 1 || ps == 2 || ps == 0xA)) gs_bad("FRAME", v); }
        ctx[addr - 0x4C].frame = v; pass_decode(addr - 0x4C);
    } break;
    case 0x4E: case 0x4F: ctx[addr - 0x4E].zbuf = v; pass_decode(addr - 0x4E); break;
    case 0x50: bitbltbuf = v; break;
    case 0x51: trxpos = v; break;
    case 0x52: trxreg = v; break;
    case 0x53: trx_start(BITS(v, 0, 2)); break;
    case 0x54: { u8 b[8]; memcpy(b, &v, 8); if (trx_active) trx_data(b, 8); } break;
    default: break;
    }
}

/* ---- GIF ---- */
/* Each GIF path (PATH1 XGKICK, PATH2 VIF1 DIRECT, PATH3 GIF DMA/FIFO) has its
 * own tag in progress: the GIF only switches paths between packets. */
typedef struct { u32 nloop, nreg, flg, eop, r; u64 regs; int active; } giftag_t;
static giftag_t gif_tags[3];
/* testing: who sent the GIF data (set by the callers), and the last tags */
const char *uya_gs_src = "?";
static struct { const char *src; u64 lo, hi; } tag_hist[16];
static u32 tag_hist_n;
static void gs_bad(const char *what, u64 v)
{
    static int left = 3;
    if (left <= 0) return;
    left--;
    uya_log("GS: implausible %s %016llX (from %s) at vsync %u; last GIF tags:", what, (unsigned long long)v, gs_src_int, uya_frame_count());
    for (u32 i = 0; i < 16; i++) {
        u32 k = (tag_hist_n + i) & 15;
        if (tag_hist[k].src) uya_log("   %-28s %016llX %016llX", tag_hist[k].src, (unsigned long long)tag_hist[k].lo, (unsigned long long)tag_hist[k].hi);
    }
    if (gs_src_int[4] == '1' && !mtgs) { void uya_vu_dump_kick(void); uya_vu_dump_kick(); }
}


/* ---- testing: recording the GS input ----
 * UYA_GSREC=file,from,n (or F9 in the game window) saves the GS state at vsync `from`
 * (registers and memory) and then every GIF packet, movie frame and vsync of
 * the next n vsyncs, so tools/gs_replay.c can run the same input through
 * another build of this file and compare the results (VRAM after each vsync). */
#define GS_SNAP_FIELDS(X) X(ctx) X(prim) X(prmode) X(prmodecont) X(rgbaq) X(st) X(uv) X(fog) X(texa) X(fogcol) X(colclamp) \
    X(pabe) X(dthe) X(bitbltbuf) X(trxpos) X(trxreg) X(texclut) X(qreg) X(clut) X(clut_hash) X(cbp0) X(cbp1) X(vq) X(vcount) \
    X(gif_tags) X(trx_x) X(trx_y) X(trx_active) X(trx_acc) X(trx_nacc)
static FILE *rec_f;
static long rec_vs, rec_from = -1, rec_to;
static volatile int rec_hotkey;
/* F9 (platform_win32.c): record the next UYA_GSREC_N vsyncs (default 150) to gsrec_NN.bin next to the exe */
void uya_gs_rec_hotkey(void) { rec_hotkey = 1; }
static void rec_put(const void *p, size_t n) { if (rec_f) fwrite(p, 1, n, rec_f); }
static void rec_meshes(void);
static void rec_vsync(const u64 *r)
{
    if (rec_from == -1) {
        const char *e = getenv("UYA_GSREC");
        rec_from = -2;
        if (e) { char path[256]; long n = 0; if (sscanf(e, "%255[^,],%ld,%ld", path, &rec_from, &n) == 3) { rec_to = rec_from + n; rec_f = fopen(path, "wb"); if (!rec_f) rec_from = -2; } else rec_from = -2; }
    }
    if (rec_hotkey && !rec_f) {
        static int nrec;
        char path[600]; const char *ne = getenv("UYA_GSREC_N"); long n = ne ? atol(ne) : 150;
        rec_hotkey = 0;
        if (n < 1) n = 150;
        snprintf(path, sizeof path, "%s/gsrec_%02d.bin", plat_exe_dir(), nrec++);
        rec_f = fopen(path, "wb");
        if (rec_f) { rec_from = rec_vs; rec_to = rec_vs + n; uya_log("GS input recording to %s (%ld vsyncs)", path, n); }
    }
    rec_hotkey = 0;
    if (!rec_f) { rec_vs++; return; }
    if (rec_vs == rec_from) {
        u32 m = 0x52534755u; rec_put(&m, 4);
        if (gsgl_on) gsgl_cpu_read(0, 0x4000);
#define X(f) { u32 n_ = sizeof f; rec_put(&n_, 4); rec_put(&f, sizeof f); }
        GS_SNAP_FIELDS(X)
#undef X
        rec_put(vram, 4 << 20);
        rec_meshes();   /* (the native meshes alive: as native draw lists that make them again) */
    } else if (rec_vs > rec_from) {
        u32 k = 2; rec_put(&k, 4); rec_put(r, 48);
        if (rec_vs >= rec_to) { fclose(rec_f); rec_f = 0; uya_log("GS input recorded"); }
    }
    rec_vs++;
}
static void rec_gif(const u32 *d, u32 qwc, u32 path)
{
    if (!rec_f || rec_vs <= rec_from) return;
    u32 h[3] = { 1, path, qwc }; rec_put(h, 12); rec_put(d, qwc * 16);
}
static void uya_gs_gif_(const u32 *d, u32 qwc);
static void gs_dlist_(const u32 *d, u32 qwc);
/* ---- the GS thread ----
 * With UYA_MTGS (on by default with 3 or more cores) GIF data is copied into
 * a FIFO and the GS (register writes, primitives, transfers, the texture
 * cache) runs on its own thread, like the real GS beside the EE. The EE side
 * waits for it at each vsync before the frame is shown (gs_sync). */
#define GF_WORDS (1u << 22)
static u32 *gfifo;
static line_u32 gf_head_, gf_tail_;     /* written by the game's thread and the GS thread */
#define gf_head (gf_head_.v)
#define gf_tail (gf_tail_.v)
static const char *path_name[3] = { "PATH1 (XGKICK)", "PATH2 (VIF1 DIRECT)", "PATH3" };

static park_t gspark;
static void gs_vsync_marker(const u32 *e);
static void gs_movie_marker(const u32 *e);
static void gl_mem_do(const u32 *e);
static void gs_thread_loop(void)
{
    int spin = 0;
    if (gsgl_on && !gsgl_threaded()) plat_gl_current(1);       /* the GL context lives on this thread (unless on the GL thread) */
    for (;;) {
        u32 h = __atomic_load_n(&gf_head, __ATOMIC_ACQUIRE), t = gf_tail;
        if (t == h) {
            if (spin++ < 2000) { __asm__ volatile("pause"); continue; }
            __atomic_store_n(&gspark.sleeping, 1, __ATOMIC_SEQ_CST);
            if (__atomic_load_n(&gf_head, __ATOMIC_SEQ_CST) == t) park_wait(&gspark);
            __atomic_store_n(&gspark.sleeping, 0, __ATOMIC_RELEASE);
            continue;
        }
        spin = 0;
        unsigned long long b0_ = rdtsc_();
        while (t != h) {
            const u32 *e = gfifo + (t % GF_WORDS);
            u32 qwc = e[0], path = e[1];
            if (path == 0xFFFFFFFFu) { t += GF_WORDS - (t % GF_WORDS); continue; }   /* wrap to the start */
            if (path == 0xFFFFFFFEu) { gs_vsync_marker(e + 4); t += 4 + qwc * 4; continue; }   /* a vsync: capture the frame */
            if (path == 0xFFFFFFFCu) { gs_movie_marker(e + 4); t += 4 + qwc * 4; continue; }   /* an FMV frame to show */
            if (path == 0xFFFFFFFDu) { gl_mem_do(e + 4); t += 4 + qwc * 4; continue; }         /* GL: the game's thread touches GS memory */
            if (path == 3) {                                                            /* a native draw list */
                gs_src_int = "native";
                if (__builtin_expect(rec_f != 0, 0)) rec_gif(e + 4, qwc, 3);
                gs_dlist_(e + 4, qwc);
                t += 4 + qwc * 4; continue;
            }
            gs_src_int = path_name[path];
            if (__builtin_expect(rec_f != 0, 0)) rec_gif(e + 4, qwc, path);
            uya_gs_gif_(e + 4, qwc);
            t += 4 + qwc * 4;
        }
        gs_thread_busy += rdtsc_() - b0_;
        __atomic_store_n(&gf_tail, t, __ATOMIC_RELEASE);
    }
}
#ifdef _WIN32
static DWORD WINAPI gs_thread_main(LPVOID a) { (void)a; gs_thread_loop(); return 0; }
#else
static void *gs_thread_main(void *a) { (void)a; gs_thread_loop(); return 0; }
#endif
static void mtgs_init(int cores)
{
    const char *e = getenv("UYA_MTGS");
    mtgs = e ? atoi(e) : cores >= 3;
    if (!mtgs) return;
    gfifo = malloc(GF_WORDS * 4);
    park_init(&gspark);
#ifdef _WIN32
    CreateThread(0, 0, gs_thread_main, 0, 0, 0);
#else
    pthread_t th; pthread_create(&th, 0, gs_thread_main, 0); pthread_detach(th);
#endif
    uya_log("GS: on its own thread");
}
/* wait until the GS thread has done everything sent to it */
static void gs_sync(void)
{
    if (!mtgs) return;
    int spin = 0;
    while (__atomic_load_n(&gf_tail, __ATOMIC_ACQUIRE) != gf_head) cpu_relax(spin++);
}

static void gs_fifo_put(const void *data, u32 qwc, u32 path)
{
    const u32 *d = data;
    while (qwc) {
        u32 n = qwc > 0x10000 ? 0x10000 : qwc, need = 4 + n * 4, h = gf_head, spin = 0;
        if ((h % GF_WORDS) + need > GF_WORDS) {    /* no room before the end: a wrap marker */
            while (h + (GF_WORDS - h % GF_WORDS) - __atomic_load_n(&gf_tail, __ATOMIC_ACQUIRE) > GF_WORDS - 4) cpu_relax(spin++);
            gfifo[h % GF_WORDS] = 0; gfifo[h % GF_WORDS + 1] = 0xFFFFFFFFu;
            h += GF_WORDS - h % GF_WORDS;
            __atomic_store_n(&gf_head, h, __ATOMIC_RELEASE);
        }
        while (h + need - __atomic_load_n(&gf_tail, __ATOMIC_ACQUIRE) > GF_WORDS - 4) cpu_relax(spin++);
        u32 *w = gfifo + h % GF_WORDS;
        w[0] = n; w[1] = path; w[2] = w[3] = 0;
        memcpy(w + 4, d, n * 16);
        __atomic_store_n(&gf_head, h + need, __ATOMIC_SEQ_CST);
        park_wake(&gspark);
        d += n * 4; qwc -= n;
    }
}

/* testing: UYA_SRCSTATS=n logs, every n vsyncs, where the GS work comes from: per source (PATH1 by
 * the VU1 microprogram and its start address, PATH2, PATH3) the GS front end's time, the GIF
 * qwords, the vertex kicks that drew, and the VU time of each microprogram ("VU" rows).
 * UYA_DROPSRC="a,b" drops GIF data from sources whose name contains a or b (to see what they draw).
 * Both with UYA_MTGS=0 (the GS work then runs on the caller's thread). */
static struct { char key[48]; unsigned long long t, qw, k, n; } srcst[128];
static int nsrc, srcstats = -1;
static unsigned long long gs_time_in;
unsigned long long uya_gs_time_in(void) { return gs_time_in; }
static const char *dropsrc;
void uya_srcstat_add(const char *src, unsigned long long t, unsigned long long qw, unsigned long long k)
{
    char key[48]; int n = 0;
    while (src[n] && n < 47 && strncmp(src + n, " kick", 5)) { key[n] = src[n]; n++; }
    key[n] = 0;
    int i;
    for (i = 0; i < nsrc; i++) if (!strcmp(srcst[i].key, key)) break;
    if (i == nsrc) { if (nsrc == 128) return; strcpy(srcst[nsrc++].key, key); }
    srcst[i].t += t; srcst[i].qw += qw; srcst[i].k += k; srcst[i].n++;
}
int uya_srcstats_on(void) { if (srcstats < 0) { const char *e = getenv("UYA_SRCSTATS"); srcstats = e ? atoi(e) : 0; dropsrc = getenv("UYA_DROPSRC"); } return srcstats > 0; }
static int src_dropped(const char *src)
{
    if (!dropsrc || !*dropsrc) return 0;
    const char *p = dropsrc;
    while (*p) {
        char w[48]; int n = 0;
        while (*p && *p != ',' && n < 47) w[n++] = *p++;
        w[n] = 0; if (*p == ',') p++;
        if (n && strstr(src, w)) return 1;
    }
    return 0;
}
static void srcstats_log(void)
{
    static unsigned long long t0;
    unsigned long long now = rdtsc_(), tot = 0;
    for (int i = 0; i < nsrc; i++) tot += srcst[i].t;
    uya_log("sources over %d vsyncs (rdtsc %llu M total in the rows, %llu M wall):", srcstats, tot >> 20, t0 ? (now - t0) >> 20 : 0);
    for (int pass = 0; pass < nsrc; pass++) {
        int b = -1;
        for (int i = 0; i < nsrc; i++) if (srcst[i].n && (b < 0 || srcst[i].t > srcst[b].t)) b = i;
        if (b < 0) break;
        uya_log("  %-34s %6.2f%%  %8llu calls %9llu qw %9llu drawn kicks", srcst[b].key, tot ? 100.0 * srcst[b].t / tot : 0.0,
                srcst[b].n / srcstats, srcst[b].qw / srcstats, srcst[b].k / srcstats);
        srcst[b].n = 0;
    }
    for (int i = 0; i < nsrc; i++) srcst[i].t = srcst[i].qw = srcst[i].k = srcst[i].n = 0;
    t0 = now;
}
void uya_gs_gif(const u32 *d, u32 qwc)
{
    if (uya_srcstats_on() && !mtgs) {
        if (src_dropped(uya_gs_src)) return;
        unsigned long long t0 = rdtsc_(), k0 = gs_kicks_drawn;
        gs_src_int = uya_gs_src; uya_gs_gif_(d, qwc);
        unsigned long long dt = rdtsc_() - t0; gs_time_in += dt;
        uya_srcstat_add(uya_gs_src, dt, qwc, gs_kicks_drawn - k0);
        return;
    }
    unsigned long long t = timing() ? rdtsc_() : 0;
    if (__builtin_expect(rec_f != 0, 0) && !mtgs) rec_gif(d, qwc, (uya_gs_src[0] == 'P' && uya_gs_src[4] >= '1' && uya_gs_src[4] <= '3') ? (u32)(uya_gs_src[4] - '1') : 2);
    if (!mtgs) { gs_src_int = uya_gs_src; uya_gs_gif_(d, qwc); }
    else gs_fifo_put(d, qwc, (uya_gs_src[0] == 'P' && uya_gs_src[4] >= '1' && uya_gs_src[4] <= '3') ? (u32)(uya_gs_src[4] - '1') : 2);
    if (uya_timing) uya_t_gs += rdtsc_() - t;
}
/* ---- native draw lists ----
 * The native renderers (host/src/native) hand their draws over as a list of
 * commands rather than as GIF packets (host/src/native/native_dlist.c builds
 * it), one qword each:
 *   {1, addr, lo, hi}    a register write
 *   {2, n, 0, 0}         a strip: n vertices follow, two qwords each:
 *                        {x | y << 16, z, r | g << 8 | b << 16 | a << 24, f | adc << 8 | xyz2 << 9}
 *                        {s, t, q, 0} (the bits of the floats)
 *   {3, 0, 0, 0}         a new primitive run (Q back to 1, as at a GIF tag)
 * Each vertex sets ST and Q, RGBA (the Q of RGBAQ 0) and kicks, as the GIF's
 * PACKED ST, RGBAQ and XYZF2 (or XYZ2) qwords do: the same primitives. */
/* ---- native meshes (the GS side): kept for the GPU renderer (gsgl_mesh_*), and here too, to
 * draw them in software (the same transform on the CPU, then the same primitives) ---- */
#define GSMESH_N 32768
static struct { int gl; float *v6; u8 *rgba; u32 *idx; u32 nv, ni; float bb[8]; } gsmesh[GSMESH_N];   /* bb: x y z w ranges of its vertices */
static void gsmesh_bbox(u32 id)
{
    float *b = gsmesh[id].bb; const float *v = gsmesh[id].v6;
    for (int k = 0; k < 4; k++) { b[2 * k] = 1e30f; b[2 * k + 1] = -1e30f; }
    for (u32 i = 0; i < gsmesh[id].nv; i++)
        for (int k = 0; k < 4; k++) { float x = v[i * 6 + k]; if (x < b[2 * k]) b[2 * k] = x; if (x > b[2 * k + 1]) b[2 * k + 1] = x; }
}
/* the rows a draw of mesh id with transform xf can reach on the screen (the box's 16 corners
 * projected: the image of a box lies within them while w keeps one sign); ~0u if unknown */
static u32 gsmesh_rows(u32 id, const float *xf)
{
    const float *b = gsmesh[id].bb;
    if (b[0] > b[1]) return ~0u;
    float ymax = -1e30f; int pos = 0, neg = 0;
    for (int c = 0; c < 16; c++) {
        float x = b[c & 1], y = b[2 + ((c >> 1) & 1)], z = b[4 + ((c >> 2) & 1)], a = b[6 + ((c >> 3) & 1)];
        float p1 = xf[12 + 1] * a + xf[1] * x + xf[5] * y + xf[9] * z, p3 = xf[12 + 3] * a + xf[3] * x + xf[7] * y + xf[11] * z;
        if (p3 > 0) pos = 1; else if (p3 < 0) neg = 1; else return ~0u;
        float sy = p1 * (xf[16] / p3) + xf[21] - xf[25];
        if (sy > ymax) ymax = sy;
    }
    if (pos && neg) return ~0u;
    return ymax < 0 ? 1u : ymax > 4096 ? ~0u : (u32)ymax + 2;
}
static void gs_mesh_free(u32 id)
{
    if (id >= GSMESH_N) return;
    if (gsgl_on && gsmesh[id].gl >= 0 && gsmesh[id].v6) gsgl_mesh_free(gsmesh[id].gl);
    free(gsmesh[id].v6); free(gsmesh[id].rgba); free(gsmesh[id].idx);
    memset(&gsmesh[id], 0, sizeof gsmesh[id]); gsmesh[id].gl = -1;
}
static void gs_mesh_create(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni)
{
    if (id >= GSMESH_N) return;
    gs_mesh_free(id);
    gsmesh[id].v6 = malloc((size_t)nv * 24); memcpy(gsmesh[id].v6, v6, (size_t)nv * 24);
    gsmesh[id].rgba = malloc((size_t)nv * 4); memcpy(gsmesh[id].rgba, rgba, (size_t)nv * 4);
    gsmesh[id].idx = malloc((size_t)ni * 4); memcpy(gsmesh[id].idx, idx, (size_t)ni * 4);
    gsmesh[id].nv = nv; gsmesh[id].ni = ni;
    gsmesh_bbox(id);
    gsmesh[id].gl = gsgl_on ? gsgl_mesh_create(v6, rgba, (int)nv, idx, (int)ni) : -1;
}
/* new colours (and, v6 not 0, new vertices) for a mesh */
static void gs_mesh_update(u32 id, const float *v6, const u8 *rgba, u32 nv)
{
    if (id >= GSMESH_N || !gsmesh[id].v6 || nv != gsmesh[id].nv) return;
    memcpy(gsmesh[id].rgba, rgba, (size_t)nv * 4);
    if (v6) { memcpy(gsmesh[id].v6, v6, (size_t)nv * 24); gsmesh_bbox(id); }
    if (gsgl_on && gsmesh[id].gl >= 0) gsgl_mesh_update(gsmesh[id].gl, v6, rgba);
}
/* new vertices, colours and triangle indices (ni: no more than the mesh was made with) */
static void gs_mesh_update_idx(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni)
{
    if (id >= GSMESH_N || !gsmesh[id].v6 || nv != gsmesh[id].nv || ni > gsmesh[id].ni) return;
    gs_mesh_update(id, v6, rgba, nv);
    memcpy(gsmesh[id].idx, idx, (size_t)ni * 4);
    if (gsgl_on && gsmesh[id].gl >= 0 && ni) gsgl_mesh_index(gsmesh[id].gl, idx, (int)ni);
}
static void rec_meshes(void)
{
    for (u32 id = 0; id < GSMESH_N; id++) {
        if (!gsmesh[id].v6) continue;
        u32 nvx = gsmesh[id].nv, ni = gsmesh[id].ni, q6 = (nvx * 6 + 3) / 4, qc = (nvx + 3) / 4, qi = (ni + 3) / 4, n = 1 + q6 + qc + qi;
        u32 *b = calloc((size_t)n * 4, 4);
        if (!b) return;
        b[0] = 4; b[1] = id; b[2] = nvx; b[3] = ni;
        memcpy(b + 4, gsmesh[id].v6, (size_t)nvx * 24);
        memcpy(b + 4 + q6 * 4, gsmesh[id].rgba, (size_t)nvx * 4);
        memcpy(b + 4 + (q6 + qc) * 4, gsmesh[id].idx, (size_t)ni * 4);
        u32 h[3] = { 1, 3, n }; rec_put(h, 12); rec_put(b, (size_t)n * 16);
        free(b);
    }
}
/* the state for the next primitives (as vertex_kick makes it) */
static u32 gs_cur_state(void)
{
    if (!sring) sring = calloc(SN, sizeof(pcmd_t));
    if (st_dirty || (st_tme && st_epoch != dirty_epoch)) {
        pcmd_t ns;
        cmd_setup(&ns);
        if (__builtin_expect(sv_on > 0, 0)) survey_state(&ns);
        if (!s_head || !cmd_same(&ns, &sring[(s_head - 1) % SN])) { sring[s_head % SN] = ns; s_head++; }
        st_dirty = 0; st_tme = ns.tme; st_epoch = dirty_epoch;
    }
    return s_head - 1;
}
/* one vertex of a mesh as the GS would have received it (the transform of nvs_src in gs_gl.c) */
static void gs_mesh_vertex(const float *xf, const float *p6, const u8 *c, vtx_t *o, float *w_out)
{
    const float *m = xf;
    float P[4];
    for (int k = 0; k < 4; k++) { float a = m[12 + k] * p6[3]; a = a + m[k] * p6[0]; a = a + m[4 + k] * p6[1]; P[k] = a + m[8 + k] * p6[2]; }
    float w = P[3], q = xf[16] / w;
    float sx = P[0] * q + xf[20], sy = P[1] * q + xf[21], sz = P[2] * q + xf[22];
    o->x = truncf(sx * 16.0f) / 16.0f - xf[24]; o->y = truncf(sy * 16.0f) / 16.0f - xf[25];
    o->z = sz <= 0.0f ? 0u : (sz >= 4294967040.0f ? 0xFFFFFF00u : (u32)sz);
    o->r = c[0]; o->g = c[1]; o->b = c[2]; o->a = c[3];
    o->s = p6[4] * q; o->t = p6[5] * q; o->q = q; o->u = o->v = 0;
    float f = w + xf[19]; if (f < xf[17]) f = xf[17]; if (f > xf[18]) f = xf[18];
    o->f = (u8)(f < 0 ? 0 : f > 255 ? 255 : (u32)f);
    *w_out = w / xf[16];                    /* (1/Q: the distance the near limit is in) */
}
static void gs_mesh_draw_st(u32 id, u32 first, u32 count, const float *xf, u32 sti);
static void gs_mesh_draw(u32 id, u32 first, u32 count, const float *xf)
{
    if (id >= GSMESH_N || !gsmesh[id].v6 || first + count > gsmesh[id].ni) return;
    gs_mesh_draw_st(id, first, count, xf, gs_cur_state());
}
static void gs_mesh_draw_st(u32 id, u32 first, u32 count, const float *xf, u32 sti)
{
    if (id >= GSMESH_N || !gsmesh[id].v6 || first + count > gsmesh[id].ni) return;
    const pcmd_t *S = &sring[sti % SN];
    float xfl[32];   /* the caller's transform, with the pass's offset and the texture's size */
    memcpy(xfl, xf, sizeof xfl);
    {
        u64 pr = prmodecont ? prim : ((prim & 7) | (prmode & ~7ull));
        const gs_pass_t *q = &gpass[BITS(pr, 9, 1)];
        xfl[24] = q->xoff; xfl[25] = q->yoff;
        xfl[26] = S->tw; xfl[27] = S->th;
    }
    xf = xfl;
    urec_hits(sti, gsmesh_rows(id, xf));
    census_add(S, count / 3, 1);
    if (gsgl_on) {
        if (gsmesh[id].gl >= 0) gsgl_mesh_draw(S, sti, gsmesh[id].gl, (int)first, (int)count, xf);
        return;
    }
    const u32 *ix = gsmesh[id].idx + first;
    for (u32 t = 0; t + 2 < count; t += 3) {
        pprim_t pc; float w[3];
        for (int k = 0; k < 3; k++) {
            u32 j = ix[t + k];
            if (j >= gsmesh[id].nv) return;
            gs_mesh_vertex(xf, gsmesh[id].v6 + j * 6, gsmesh[id].rgba + j * 4, &pc.v[k], &w[k]);
        }
        if (w[0] < xf[23] || w[1] < xf[23] || w[2] < xf[23]) continue;   /* (behind the near limit: the GPU clips it) */
        pc.st = sti; pc.type = 3; stat_prims++; gs_kicks_drawn++;
        submit(&pc);
    }
}


/* ---- material draws (host/src/native/native_draw.h) ----
 * A native renderer's mesh draw comes with its material in host terms. Its parts (texture and
 * palette, filtering, wrap, mip levels, the primitive) become the frame's texture state: the
 * palette is loaded as its load mode says (the GS's CLUT buffer: an unchanged base is not read
 * again), and the state the game's own GIF data reads after it is what the PS2's GS would hold
 * after the class's material blocks. The draw's pipeline state is then built from the material
 * and the frame's pass (gpass): the primitive's flags, target, depth, tests, blend and scissor,
 * and the texture's levels from the uploads' store. No GS register is written or decoded for it.
 * Check mode (UYA_NDRAW=1, op 16): the renderer also sends the register writes the material came
 * from; they are applied first as GIF data would be and the state the GIF path builds from them
 * is compared with the material's (counted, the first differences logged). */
#include "../native/native_draw.h"
#define ND_QW ((u32)((sizeof(nd_material) + 15) / 16))
static unsigned long long nd_n, nd_chk, nd_bad;
static u32 ndc_addr[16]; static u64 ndc_val[16]; static int ndc_n = -1;
/* a material's parts as the texture state keeps them */
static u64 nd_tex0(const nd_material *m)
{
    return (u64)m->tbp | (u64)m->tbw << 14 | (u64)m->fmt << 20 | (u64)m->wlog << 26 | (u64)m->hlog << 30 | (u64)m->tex_alpha << 34 |
           (u64)m->combine << 35 | (u64)m->pal << 37 | (u64)m->pal_fmt << 51 | (u64)m->pal_layout << 55 | (u64)m->pal_offset << 56 | (u64)m->pal_load << 61;
}
static u64 nd_tex1(const nd_material *m)
{
    return (u64)m->lod_fixed | (u64)m->levels << 2 | (u64)m->mag_linear << 5 | (u64)m->min_mode << 6 | (u64)m->mip_follow << 9 |
           (u64)m->lod_shift << 19 | (u64)(u16)m->lod_bias << 32;
}
static u64 nd_clamp(const nd_material *m)
{
    return (u64)m->wrap_u | (u64)m->wrap_v << 2 | (u64)m->u_min << 4 | (u64)m->u_max << 14 | (u64)m->v_min << 24 | (u64)m->v_max << 34;
}
static u64 nd_mip(const nd_material *m, int h)
{
    u64 v = 0;
    for (int k = 0; k < 3; k++) v |= ((u64)m->mip_tbp[h * 3 + k] | (u64)m->mip_tbw[h * 3 + k] << 14) << (k * 20);
    return v;
}
static u64 nd_prim(const nd_material *m)
{
    return (u64)m->kind | (u64)m->gouraud << 3 | (u64)m->textured << 4 | (u64)m->fog << 5 | (u64)m->blend << 6 | (u64)m->aa << 7 |
           (u64)m->uv << 8 | (u64)m->ctx << 9;
}
static void nd_apply(const nd_material *m)
{
    gs_ctx_t *c = &ctx[m->pass & 1];
    if (m->has & ND_TEX) { c->tex0 = nd_tex0(m); clut_load(c->tex0); }
    if (m->has & ND_SAMPLE) c->tex1 = nd_tex1(m);
    if (m->has & ND_WRAP) c->clamp = nd_clamp(m);
    if (m->has & ND_MIP13) c->miptbp1 = nd_mip(m, 0);
    if (m->has & ND_MIP46) c->miptbp2 = nd_mip(m, 1);
    if (m->has & ND_PRIM) { prim = nd_prim(m); prim_reset(); }
    st_dirty = 1;   /* (the GIF path builds its next state again) */
}
/* the draw's pipeline state: the primitive's flags (PRMODE's when the game set PRMODECONT off),
 * the pass the primitive names, the texture state of that pass */
static void cmd_material(pcmd_t *p)
{
    u32 tme, fge, abe, fst, iip, cx;
    if (prmodecont) { tme = BITS(prim, 4, 1); fge = BITS(prim, 5, 1); abe = BITS(prim, 6, 1); fst = BITS(prim, 8, 1); iip = BITS(prim, 3, 1); cx = BITS(prim, 9, 1); }
    else { tme = BITS(prmode, 4, 1); fge = BITS(prmode, 5, 1); abe = BITS(prmode, 6, 1); fst = BITS(prmode, 8, 1); iip = BITS(prmode, 3, 1); cx = BITS(prmode, 9, 1); }
    const gs_ctx_t *t = &ctx[cx];
    memset(p, 0, sizeof *p);
    p->tme = (u8)tme; p->fge = (u8)fge; p->abe = (u8)abe; p->fst = (u8)fst; p->iip = (u8)iip;
    cmd_pass(p, &gpass[cx]);
    p->tfx = BITS(t->tex0, 35, 2); p->tcc = BITS(t->tex0, 34, 1);
    p->tw = (float)(1u << BITS(t->tex0, 26, 4)); p->th = (float)(1u << BITS(t->tex0, 30, 4));
    cmd_target(p);
    cmd_texture(p, t);
}
/* a renderer's pass settings (its own packets' blend, tests, target...): the frame's pass as the
 * GS would hold it after them */
#define NP_QW ((u32)((sizeof(nd_pass) + 15) / 16))
static void nd_pass_apply(const nd_pass *q)
{
    int i = q->pass & 1; gs_ctx_t *c = &ctx[i];
    if (q->has & NP_BLEND) c->alpha = (u64)q->ba | (u64)q->bb << 2 | (u64)q->bc << 4 | (u64)q->bd << 6 | (u64)q->fix << 32;
    if (q->has & NP_TEST) c->test = (u64)q->ate | (u64)q->atst << 1 | (u64)q->aref << 4 | (u64)q->afail << 12 | (u64)q->date << 14 | (u64)q->datm << 15 | (u64)q->zte << 16 | (u64)q->ztst << 17;
    if (q->has & NP_TARGET) c->frame = (u64)q->fbp | (u64)q->fbw << 16 | (u64)q->fpsm << 24 | (u64)q->fbmsk << 32;
    if (q->has & NP_DEPTH) c->zbuf = (u64)q->zbp | (u64)q->zpsm << 24 | (u64)q->zmsk << 32;
    if (q->has & NP_SCISSOR) c->scissor = (u64)q->sx0 | (u64)q->sx1 << 16 | (u64)q->sy0 << 32 | (u64)q->sy1 << 48;
    if (q->has & NP_OFFSET) c->xyoffset = (u64)q->xoff | (u64)q->yoff << 32;
    if (q->has & NP_FBA) c->fba = q->fba;
    if (q->has & NP_GFOG) fogcol = (u64)q->fog_r | (u64)q->fog_g << 8 | (u64)q->fog_b << 16;
    if (q->has & NP_GTEXA) texa = (u64)q->ta0 | (u64)q->aem << 15 | (u64)q->ta1 << 32;
    if (q->has & NP_GCLAMP) colclamp = q->colclamp;
    if (q->has & NP_GPRCONT) prmodecont = q->prcont;
    if (q->has & NP_GPRMODE) prmode = q->prmode;
    if (q->has & NP_GDITHER) dthe = q->dither;
    if (q->has & NP_GPABE) pabe = q->pabe;
    pass_decode(i);
    st_dirty = 1;
}
static void nd_diff(const pcmd_t *a, const pcmd_t *b)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (u32 i = 0; i < sizeof *a; i++)
        if (x[i] != y[i]) { uya_log("UYA_NDRAW check: the material's state differs from the registers' at byte %u (%02X, registers %02X) [%s]", i, x[i], y[i], gs_src_int); return; }
}
static void gs_nd_mesh_draw(u32 id, u32 first, u32 count, const float *xf, const nd_material *m)
{
    if (!sring) sring = calloc(SN, sizeof(pcmd_t));
    pcmd_t rs; int chk = 0;
    if (ndc_n >= 0) {   /* check mode: the register writes first, as the GIF path takes them */
        for (int i = 0; i < ndc_n; i++) uya_gs_write(ndc_addr[i], ndc_val[i]);
        ndc_n = -1;
        if (count) { cmd_setup(&rs); chk = 1; }
    }
    nd_apply(m);
    if (!count) return;
    nd_n++;
    pcmd_t ns;
    cmd_material(&ns);
    if (chk) { nd_chk++; if (!cmd_same(&ns, &rs)) { if (nd_bad++ < 10) nd_diff(&ns, &rs); } if (nd_chk % 200000 == 0) uya_log("UYA_NDRAW check: %llu material draws compared, %llu differ", nd_chk, nd_bad); }
    if (__builtin_expect(sv_on > 0, 0)) survey_state(&ns);
    if (!s_head || !cmd_same(&ns, &sring[(s_head - 1) % SN])) { sring[s_head % SN] = ns; s_head++; }
    u32 sti = s_head - 1;
    gs_mesh_draw_st(id, first, count, xf, sti);
}

/* ---- host primitives (op 19): a native 2D draw's primitives, made by host code from its own
 * vertices (no GS vertex queue): the material (with its primitive kind) applied, the vertices put
 * together as the kind says (triangles, strip, fan, sprites, lines, points), each primitive drawn
 * with the state built from the material and the pass. After them, the GS's vertex registers and
 * queue are left as the same primitives sent as GIF data would leave them (the colour and Q, ST,
 * UV, the last vertices), for GIF data that still follows from the game. Vertices: {x | y << 16
 * (screen, 1/16 pixel, the pass's offset included), z, fog, rgba} {s, t, q (float bits), u | v << 16
 * (1/16 texel)}. */
static u32 host_state(void)
{
    if (!sring) sring = calloc(SN, sizeof(pcmd_t));
    if (st_dirty || (st_tme && st_epoch != dirty_epoch)) {
        pcmd_t ns;
        cmd_material(&ns);
        if (__builtin_expect(sv_on > 0, 0)) survey_state(&ns);
        if (!s_head || !cmd_same(&ns, &sring[(s_head - 1) % SN])) { sring[s_head % SN] = ns; s_head++; }
        st_dirty = 0; st_tme = ns.tme; st_epoch = dirty_epoch;
    }
    return s_head - 1;
}
static void host_prim(u8 kind, const vtx_t *a, const vtx_t *b, const vtx_t *c)
{
    pprim_t pc;
    stat_prims++; gs_kicks_drawn++;
    pc.st = host_state();
    census_add(&sring[pc.st % SN], 1, 0);
    static const u8 nvk[8] = { 1, 2, 2, 3, 3, 3, 2, 1 };
    pc.type = kind; pc.v[0] = *a; pc.v[1] = b ? *b : *a; pc.v[2] = c ? *c : pc.v[1];
    {
        int n = kind == 0 ? 1 : kind == 3 ? 3 : 2;
        float ym = pc.v[0].y;
        for (int k = 1; k < n; k++) if (pc.v[k].y > ym) ym = pc.v[k].y;
        urec_hits(pc.st, ym < 0 ? 1u : (u32)ym + 2);
    }
    (void)nvk;
    if (gsgl_on) gsgl_prim(&sring[pc.st % SN], pc.st, &pc);
    else submit(&pc);
}
static void gs_nd_prims(const nd_material *m, const u32 *w, u32 nv, u32 flags)
{
    /* flags 1: the vertices have no ST (UV primitives): ST is what the GS holds; 2: no colour either:
     * RGBA is what the GS holds, Q 1.0 (as the GIF tag leaves it); 4: no UV: UV is what the GS holds */
    nd_apply(m);
    qreg = 1.0f;                                     /* (as a GIF tag leaves Q) */
    u32 cx = prmodecont ? BITS(prim, 9, 1) : BITS(prmode, 9, 1);
    const gs_pass_t *ps = &gpass[cx];
    static vtx_t *V; static u32 vcap;
    if (nv > vcap) { free(V); vcap = nv + 256; V = malloc(vcap * sizeof *V); if (!V) { vcap = 0; return; } }
    if (vcount) {   /* (the GS's queue holds vertices from before, no PRIM since: they go as the GIF would send them) */
        static unsigned long long nq; if (nq++ < 3) uya_log("host primitives: %d vertices queued before (kind %u): sent through the vertex queue", vcount, (u32)(prim & 7));
        for (u32 i = 0; i < nv; i++) {
            const u32 *a = w + i * 8;
            rgbaq = (u64)a[3] | (u64)a[6] << 32; memcpy(&qreg, &a[6], 4);
            if (!(flags & 1)) st = (u64)a[4] | (u64)a[5] << 32;
            if (!(flags & 4)) uv = (u64)(a[7] & 0x3FFF) | (u64)((a[7] >> 16) & 0x3FFF) << 16;
            vertex_kick(a[0] & 0xFFFF, a[0] >> 16, a[1], (u8)a[2], 1);
        }
        return;
    }
    for (u32 i = 0; i < nv; i++) {
        const u32 *a = w + i * 8;
        vtx_t *v = &V[i];
        v->x = (float)(a[0] & 0xFFFF) / 16.0f - ps->xoff; v->y = (float)(a[0] >> 16) / 16.0f - ps->yoff;
        v->z = a[1]; v->f = (u8)a[2];
        u32 cl = (flags & 2) ? (u32)rgbaq : a[3];
        v->r = (u8)cl; v->g = (u8)(cl >> 8); v->b = (u8)(cl >> 16); v->a = (u8)(cl >> 24);
        if (flags & 1) { u32 s_ = (u32)st, t_ = (u32)(st >> 32); memcpy(&v->s, &s_, 4); memcpy(&v->t, &t_, 4); }
        else { memcpy(&v->s, &a[4], 4); memcpy(&v->t, &a[5], 4); }
        if (flags & 2) v->q = 1.0f; else memcpy(&v->q, &a[6], 4);
        u32 uvw = (flags & 4) ? (u32)(uv & 0x3FFF) | (u32)((uv >> 16) & 0x3FFF) << 16 : a[7];
        v->u = (float)(uvw & 0x3FFF) / 16.0f; v->v = (float)((uvw >> 16) & 0x3FFF) / 16.0f;
    }
    u32 type = (u32)(prim & 7);
    static const u8 kind[8] = { 0, 1, 1, 3, 3, 3, 6, 0 };
    switch (type) {
    case 0: for (u32 i = 0; i < nv; i++) host_prim(0, &V[i], 0, 0); break;
    case 1: for (u32 i = 0; i + 1 < nv; i += 2) host_prim(1, &V[i], &V[i + 1], 0); break;
    case 2: for (u32 i = 0; i + 1 < nv; i++) host_prim(1, &V[i], &V[i + 1], 0); break;
    case 3: for (u32 i = 0; i + 2 < nv; i += 3) host_prim(3, &V[i], &V[i + 1], &V[i + 2]); break;
    case 4: for (u32 i = 0; i + 2 < nv; i++) host_prim(3, &V[i], &V[i + 1], &V[i + 2]); break;
    case 5: for (u32 i = 1; i + 1 < nv; i++) host_prim(3, &V[0], &V[i], &V[i + 1]); break;
    case 6: for (u32 i = 0; i + 1 < nv; i += 2) host_prim(6, &V[i], &V[i + 1], 0); break;
    default: break;
    }
    (void)kind;
    /* the GS's vertex registers and queue as the GIF would leave them */
    if (nv) {
        const u32 *a = w + (nv - 1) * 8;
        if (flags & 2) qreg = 1.0f;
        else { rgbaq = (u64)a[3] | (u64)a[6] << 32; memcpy(&qreg, &a[6], 4); }
        if (!(flags & 1)) st = (u64)a[4] | (u64)a[5] << 32;
        if (!(flags & 4)) uv = (u64)(a[7] & 0x3FFF) | (u64)((a[7] >> 16) & 0x3FFF) << 16;
        static const u32 need[8] = { 1, 2, 2, 3, 3, 3, 2, 1 };
        u32 left;
        switch (type) {
        case 2: left = 1; break;
        case 4: left = nv >= 2 ? 2 : nv; break;
        case 5: left = nv >= 2 ? 2 : nv; break;
        default: left = nv % need[type]; break;
        }
        vcount = (int)left;
        if (type == 5 && nv >= 2) { vq[0] = V[0]; vq[1] = V[nv - 1]; }
        else for (u32 k = 0; k < left; k++) vq[k] = V[nv - left + k];
    }
}

static void gs_dlist_(const u32 *d, u32 qwc)
{
    if (!vram) return;
    u32 q = 0;
    while (q < qwc) {
        const u32 *c = d + q * 4;
        switch (c[0]) {
        case 1: uya_gs_write(c[1], ((u64)c[3] << 32) | c[2]); q++; break;
        case 3: qreg = 1.0f; q++; break;
        case 2: {
            u32 n = c[1];
            const u32 *v = c + 4;
            for (u32 i = 0; i < n; i++, v += 8) {
                st = ((u64)v[5] << 32) | v[4];
                memcpy(&qreg, &v[6], 4);
                rgbaq = v[2];
                if (v[3] & 0x200) vertex_kick(v[0] & 0xFFFF, v[0] >> 16, v[1], 0, !((v[3] >> 8) & 1));
                else vertex_kick(v[0] & 0xFFFF, v[0] >> 16, v[1], (u8)v[3], !((v[3] >> 8) & 1));
            }
            q += 1 + 2 * n;
        } break;
        case 4: {   /* a native mesh: {4, id, nv, ni} + nv * (x y z w s t) + nv rgba + ni indices (each part to a qword) */
            u32 id = c[1], nvx = c[2], ni = c[3];
            const u32 *v = c + 4;
            u32 q6 = (nvx * 6 + 3) / 4, qc = (nvx + 3) / 4, qi = (ni + 3) / 4;
            gs_mesh_create(id, (const float *)v, (const u8 *)(v + q6 * 4), nvx, v + (q6 + qc) * 4, ni);
            q += 1 + q6 + qc + qi;
        } break;
        case 5: {   /* new colours for mesh id: {5, id, nv, 0} + nv rgba */
            gs_mesh_update(c[1], 0, (const u8 *)(c + 4), c[2]);
            q += 1 + (c[2] + 3) / 4;
        } break;
        case 8: {   /* new vertices and colours for mesh id: {8, id, nv, 0} + nv * (x y z w s t) + nv rgba */
            u32 nvx = c[2], q6 = (nvx * 6 + 3) / 4;
            gs_mesh_update(c[1], (const float *)(c + 4), (const u8 *)(c + 4 + q6 * 4), nvx);
            q += 1 + q6 + (nvx + 3) / 4;
        } break;
        case 9: {   /* new vertices, colours and indices for mesh id: {9, id, nv, ni} + nv * (x y z w s t) + nv rgba + ni indices */
            u32 nvx = c[2], ni = c[3], q6 = (nvx * 6 + 3) / 4, qc = (nvx + 3) / 4;
            const u32 *v = c + 4;
            gs_mesh_update_idx(c[1], (const float *)v, (const u8 *)(v + q6 * 4), nvx, v + (q6 + qc) * 4, ni);
            q += 1 + q6 + qc + (ni + 3) / 4;
        } break;
        /* PACKED GIF registers one by one, as uya_gs_gif_ takes them (native_direct in native_dlist.c) */
        case 10: rgbaq = c[1]; q++; break;                                          /* {10, rgba} */
        case 11: st = ((u64)c[2] << 32) | c[1]; memcpy(&qreg, &c[3], 4); q++; break;  /* {11, s, t, q} */
        case 12: uv = c[1]; q++; break;                                             /* {12, u | v << 16} */
        case 13: vertex_kick(c[1] & 0xFFFF, c[1] >> 16, c[2], (u8)c[3], (c[3] >> 8) & 1); q++; break;   /* {13, x | y << 16, z, f | draw << 8} */
        case 14: fog = (u64)(c[1] & 255) << 56; q++; break;                         /* {14, f} */
        case 6: gs_mesh_free(c[1]); q++; break;
        case 15: {  /* a material draw: {15, id, first, count} + 8 qwords of transform + the material (native_draw.h) */
            nd_material m; memcpy(&m, c + 36, sizeof m);
            gs_nd_mesh_draw(c[1], c[2], c[3], (const float *)(c + 4), &m);
            q += 9 + ND_QW;
        } break;
        case 19: {  /* host primitives: {19, flags, nv} + the material + nv * 2 qwords of vertex */
            nd_material m; memcpy(&m, c + 4, sizeof m);
            gs_nd_prims(&m, c + 4 + ND_QW * 4, c[2], c[1]);
            q += 1 + ND_QW + 2 * c[2];
        } break;
        case 20: rgbaq = (u64)c[1] | (u64)c[2] << 32; memcpy(&qreg, &c[2], 4); q++; break;   /* the vertex colour a 2D draw leaves: {20, rgba, q} */
        case 17: {  /* a material on its own (a renderer's packet): {17} + the material */
            nd_material m; memcpy(&m, c + 4, sizeof m);
            nd_apply(&m);
            q += 1 + ND_QW;
        } break;
        case 18: {  /* a renderer's pass settings: {18} + the settings */
            nd_pass pq; memcpy(&pq, c + 4, sizeof pq);
            nd_pass_apply(&pq);
            q += 1 + NP_QW;
        } break;
        case 16: {  /* check mode: the register writes the next material came from, {16, n} + n * {addr, lo, hi} */
            u32 n = c[1] < 16 ? c[1] : 16;
            for (u32 i = 0; i < n; i++) { const u32 *r = c + 4 + i * 4; ndc_addr[i] = r[0]; ndc_val[i] = ((u64)r[2] << 32) | r[1]; }
            ndc_n = (int)n;
            q += 1 + c[1];
        } break;
        case 7: {   /* draw indices [first, first + count) of mesh id: {7, id, first, count} + 8 qwords of transform */
            gs_mesh_draw(c[1], c[2], c[3], (const float *)(c + 4));
            q += 9;
        } break;
        default: q = qwc; break;
        }
    }
}
int uya_gs_renderer_gl(void) { return gsgl_on; }   /* (the GPU renderer draws: native meshes are clipped there) */
/* a native draw list from the game's thread: run here, or queued for the GS thread */
void uya_gs_dlist(const u32 *d, u32 qwc)
{
    if (uya_srcstats_on() && !mtgs) {
        if (src_dropped(uya_gs_src)) return;
        unsigned long long t0 = rdtsc_(), k0 = gs_kicks_drawn;
        gs_src_int = uya_gs_src; gs_dlist_(d, qwc);
        unsigned long long dt = rdtsc_() - t0; gs_time_in += dt;
        uya_srcstat_add(uya_gs_src, dt, qwc, gs_kicks_drawn - k0);
        return;
    }
    unsigned long long t = timing() ? rdtsc_() : 0;
    if (__builtin_expect(rec_f != 0, 0) && !mtgs) rec_gif(d, qwc, 3);
    if (!mtgs) { gs_src_int = uya_gs_src; gs_dlist_(d, qwc); }
    else gs_fifo_put(d, qwc, 3);
    if (uya_timing) uya_t_gs += rdtsc_() - t;
}

static void uya_gs_gif_(const u32 *d, u32 qwc)
{
    u32 q = 0;
    if (!vram) return;
    int path = (gs_src_int[0] == 'P' && gs_src_int[4] >= '1' && gs_src_int[4] <= '3') ? gs_src_int[4] - '1' : 2;
    giftag_t *tp = &gif_tags[path];
#define tag (*tp)
    if (path == 0) tag.active = 0;          /* XGKICK starts a packet at its address */
    while (q < qwc) {
        const u32 *t = d + q * 4; gif_cur_qw = t;
        if (!tag.active) {
            u64 lo = ((u64)t[1] << 32) | t[0], hi = ((u64)t[3] << 32) | t[2];
            tag.nloop = (u32)(lo & 0x7FFF); tag.eop = BITS(lo, 15, 1); tag.flg = BITS(lo, 58, 2);
            tag.nreg = BITS(lo, 60, 4); if (!tag.nreg) tag.nreg = 16;
            tag.regs = hi; tag.r = 0;
            tag_hist[tag_hist_n & 15].src = gs_src_int; tag_hist[tag_hist_n & 15].lo = lo; tag_hist[tag_hist_n & 15].hi = hi; tag_hist_n++;
            {
                static int nlog = -1;
                static unsigned nfrom;
                if (nlog < 0) { const char *e = getenv("UYA_GSLOG"); nlog = e ? atoi(e) : 0; e = getenv("UYA_GSLOG_FROM"); nfrom = e ? (unsigned)atoi(e) : 0; }   /* testing: UYA_GSLOG=n tags from vsync UYA_GSLOG_FROM */
                if (nlog > 0 && uya_frame_count() >= nfrom) { nlog--; uya_log("GIF tag [%s, %u qw]: nloop %u eop %u pre %u prim 0x%X flg %u nreg %u regs %016llX", gs_src_int, qwc - q, tag.nloop, tag.eop, BITS(lo, 46, 1), BITS(lo, 47, 11), tag.flg, tag.nreg, (unsigned long long)hi); }
            }
            if (path == 1) {   /* testing: UYA_P2LOG=1 logs PATH2 tags that look like data, with the PATH2 tags before */
                static int p2on = -1, p2n = 30; static u32 hn; static struct { u32 v, q, n; u64 lo, hi; } h[12];
                if (p2on < 0) p2on = getenv("UYA_P2LOG") != 0;
                if (p2on) {
                    if (((lo >> 16) & 0x3FFFFFFFull) && p2n > 0) {
                        p2n--;
                        uya_log("P2 bad tag v%u q %u/%u: %016llX %016llX; earlier PATH2 tags:", uya_frame_count(), q, qwc, (unsigned long long)lo, (unsigned long long)hi);
                        for (u32 k = 0; k < 12; k++) { u32 x = (hn + k) % 12; if (h[x].n) uya_log("   v%u q %u/%u: %016llX %016llX", h[x].v, h[x].q, h[x].n, (unsigned long long)h[x].lo, (unsigned long long)h[x].hi); }
                        if (p2n >= 28) { void uya_vif1_dump_hist(void); uya_vif1_dump_hist(); }
                    }
                    h[hn % 12].v = uya_frame_count(); h[hn % 12].q = q; h[hn % 12].n = qwc; h[hn % 12].lo = lo; h[hn % 12].hi = hi; hn++;
                }
            }
            if (BITS(lo, 46, 1)) uya_gs_write(0x00, BITS(lo, 47, 11));
            qreg = 1.0f;
            q++;
            tag.active = tag.nloop > 0;
            continue;
        }
        if (tag.flg == 0) {                      /* PACKED */
            u32 rg = (u32)(tag.regs >> (tag.r * 4)) & 15;
            u64 lo = ((u64)t[1] << 32) | t[0], hi = ((u64)t[3] << 32) | t[2];
            switch (rg) {
            case 0x0: uya_gs_write(0x00, lo); break;
            case 0x1: rgbaq = (t[0] & 255) | ((t[1] & 255) << 8) | ((t[2] & 255) << 16) | ((u64)(t[3] & 255) << 24) | ((u64)0 << 32); break;
            case 0x2: st = lo; memcpy(&qreg, &t[2], 4); break;
            case 0x3: uv = (t[0] & 0x3FFF) | ((u64)(t[1] & 0x3FFF) << 16); break;
            case 0x4: vertex_kick(t[0] & 0xFFFF, t[1] & 0xFFFF, (t[2] >> 4) & 0xFFFFFF, (u8)(t[3] >> 4), !((t[3] >> 15) & 1)); break;
            case 0x5: vertex_kick(t[0] & 0xFFFF, t[1] & 0xFFFF, t[2], 0, !((t[3] >> 15) & 1)); break;
            case 0xA: fog = (u64)((t[3] >> 4) & 255) << 56; break;
            case 0xC: vertex_kick(t[0] & 0xFFFF, t[1] & 0xFFFF, (t[2] >> 4) & 0xFFFFFF, (u8)(t[3] >> 4), 0); break;
            case 0xD: vertex_kick(t[0] & 0xFFFF, t[1] & 0xFFFF, t[2], 0, 0); break;
            case 0xE: uya_gs_write((u32)(hi & 0xFF), lo); break;
            case 0xF: break;
            default: uya_gs_write(rg, lo); break;    /* TEX0_x, CLAMP_x: same as A+D at that address */
            }
            q++;
            if (++tag.r >= tag.nreg) { tag.r = 0; if (--tag.nloop == 0) tag.active = 0; }
        } else if (tag.flg == 1) {               /* REGLIST: two 64-bit registers per qword */
            for (int h = 0; h < 2 && tag.active; h++) {
                u64 val = ((u64)t[h * 2 + 1] << 32) | t[h * 2];
                u32 rg = (u32)(tag.regs >> (tag.r * 4)) & 15;
                if (rg != 0xE && rg != 0xF) uya_gs_write(rg == 0x1 ? 0x01 : rg, val);
                if (++tag.r >= tag.nreg) { tag.r = 0; if (--tag.nloop == 0) tag.active = 0; }
            }
            q++;
        } else {                                  /* IMAGE: all of the packet's data here at once */
            u32 n = qwc - q < tag.nloop ? qwc - q : tag.nloop;
            if (trx_active) trx_data((const u8 *)t, n * 16);
            q += n;
            if ((tag.nloop -= n) == 0) tag.active = 0;
        }
    }
#undef tag
}

/* ---- init and display ---- */
/* UYA_RENDERER=gl|soft (or renderer= in rac1_wrapper.cfg): the GPU draws
 * (default) or the software rasterizer; GL falls
 * back to software if it cannot start */
const char *uya_renderer_cfg;
void uya_gs_init(void)
{
    if (vram) return;
    gs_swizzle_init();
    timing();
    vram = calloc(1, 4 << 20);
    {
        const char *r = getenv("UYA_RENDERER");
        if (!r) r = uya_renderer_cfg;
        if (!r) r = "gl";   /* (with no window, as headless on Linux, GL does not start: software) */
        if (!strcmp(r, "gl")) {
            if (plat_gl_init() && gsgl_init(vram)) { plat_gl_active = 1; uya_log("GS: drawing with OpenGL"); }
            else uya_log("GS: OpenGL not available, drawing in software");
        }
    }
    if (gsgl_on && !gsgl_threaded()) plat_gl_current(0);     /* free for the GS thread (MTGS) to take */
    gs_threads_init();
    if (gsgl_on && !mtgs && !gsgl_threaded()) plat_gl_current(1);
}

/* GL with MTGS: the game's thread asks the GS thread to sync GS memory with
 * the GPU targets before it reads (StoreImage) or writes (movie frames) it */
static void gl_mem_do(const u32 *e)
{
    if (e[0]) gsgl_cpu_write_rect(e[1], e[2], e[3], e[4], e[5], e[6], e[7]);
    else gsgl_cpu_read_rect(e[1], e[2], e[3], e[4], e[5], e[6], e[7]);
}
static void gs_sync(void);
static void gl_mem_req(int write, u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32 w, u32 h)
{
    if (!gsgl_on) return;
    u32 e[8] = { (u32)write, psm, bp, bw, x, y, w, h };
    if (!mtgs) { gl_mem_do(e); return; }
    gs_fifo_put(e, 2, 0xFFFFFFFDu);
    gs_sync();
}

static u32 frame_px[1024 * 1024];

/* what the display circuits show (PMODE, SMODE2, DISPFB, DISPLAY); 0 if nothing */
static int gs_disp(u64 pmode, u64 smode2, u64 dispfb_1, u64 display_1, u64 dispfb_2, u64 display_2, gs_disp_t *d)
{
    static int logged;
    int circ = (pmode & 1) ? 1 : ((pmode & 2) ? 2 : 0);
    if (!circ) return 0;
    u64 dispfb = circ == 1 ? dispfb_1 : dispfb_2;
    u64 display = circ == 1 ? display_1 : display_2;
    u32 fbp = BITS(dispfb, 0, 9) * 32, fbw = BITS(dispfb, 9, 6), psm = BITS(dispfb, 15, 5);
    u32 dbx = BITS(dispfb, 32, 11), dby = BITS(dispfb, 43, 11);
    u32 magh = BITS(display, 23, 4) + 1, magv = BITS(display, 27, 2) + 1;
    u32 dw = BITS(display, 32, 12) + 1, dh = BITS(display, 44, 11) + 1;
    u32 w = dw / magh, h = dh / magv;
    if ((smode2 & 3) == 3) h /= 2;              /* interlaced, frame mode: each field shows half the lines */
    if (w > 1024) w = 1024;
    if (h > 1024) h = 1024;
    static u64 last_disp, last_fb;
    if (display != last_disp || dispfb != last_fb) { logged = 0; last_disp = display; last_fb = dispfb; }
    if (!logged) {
        uya_log("GS display: circuit %d, FBP 0x%X FBW %u PSM 0x%X, %ux%u (DW %u DH %u MAGH %u MAGV %u, SMODE2 %llu)",
                circ, fbp, fbw * 64, psm, w, h, dw, dh, magh, magv, (unsigned long long)smode2);
        logged = 1;
    }
    if (!w || !h || !fbw) return 0;
    /* Adjust Screen moves the picture by changing DISPLAY's DX/DY. The first
     * position seen in each video mode is the centre; later changes shift the
     * picture in the window by the same amount (DX counts in MAGH units). */
    static u64 base_key; static s32 base_dx, base_dy;
    s32 cdx = (s32)BITS(display, 0, 12), cdy = (s32)BITS(display, 12, 11);
    u64 key = (display >> 23) | (smode2 << 60);
    if (key != base_key) { base_key = key; base_dx = cdx; base_dy = cdy; }
    s32 shx = (cdx - base_dx) / (s32)magh, shy = cdy - base_dy;
    if ((smode2 & 1) && !((smode2 >> 1) & 1)) shy *= 2;   /* interlaced field mode: DY counts field lines */
    d->fbp = fbp; d->fbw = fbw; d->psm = psm; d->dbx = dbx; d->dby = dby; d->w = w; d->h = h; d->shx = shx; d->shy = shy;
    return 1;
}
static void fps_log(void);
/* the frame the display circuits show, from GS memory into out (BGRX); 0 if nothing is shown */
static int gs_capture(u64 pmode, u64 smode2, u64 dispfb_1, u64 display_1, u64 dispfb_2, u64 display_2, u32 *out, u32 *ow, u32 *oh)
{
    gs_disp_t d;
    if (!gs_disp(pmode, smode2, dispfb_1, display_1, dispfb_2, display_2, &d)) { fps_log(); return 0; }
    u32 fbp = d.fbp, fbw = d.fbw, psm = d.psm, dbx = d.dbx, dby = d.dby, w = d.w, h = d.h;
    s32 shx = d.shx, shy = d.shy;
    const int p32 = !(psm == 0x02 || psm == 0x0A);
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            s32 fx = (s32)x - shx, fy = (s32)y - shy;
            if (fx < 0 || fy < 0 || fx >= (s32)w || fy >= (s32)h) { out[y * w + x] = 0; continue; }
            u32 v = p32 ? vram[a32(fbp, fbw, (dbx + (u32)fx) & 2047, (dby + (u32)fy) & 2047)]   /* px_read for PSMCT32/24 */
                        : px_read(psm, fbp, fbw, (dbx + (u32)fx) & 2047, (dby + (u32)fy) & 2047), c;
            if (psm == 0x02 || psm == 0x0A) c = ((v & 31) << 19) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 3);
            else c = ((v & 255) << 16) | (v & 0xFF00) | ((v >> 16) & 255);   /* RGBA in memory to BGRX for the window */
            out[y * w + x] = c;
        }
    fps_log();
    *ow = w; *oh = h;
    return 1;
}
/* UYA_FPSLOG: speed and GS statistics every 100 vsyncs; UYA_VRAMDUMP=n: GS memory to vram.bin at vsync n */
static void fps_log(void)
{
    {
        static long vn; static long want = -2;
        if (want == -2) { const char *e = getenv("UYA_VRAMDUMP"); want = e ? atol(e) : -1; }
        if (vn % 100 == 0) {   /* VU results the VU would have kept finite (vu_fc, vu.h): a few lines, for bug reports */
            extern u32 vu_nonfinite; static u32 nf0, nlines;
            u32 nf = vu_nonfinite;
            if (nf != nf0 && nlines < 40) { nlines++; uya_log("VU: %u results overflowed to inf/NaN by vsync %ld (made +-MAX, as on the PS2)", nf - nf0, vn); }
            nf0 = nf;
        }
        if (getenv("UYA_FPSLOG") && vn % 100 == 0) {
            static struct timespec t0; struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
            extern unsigned long long uya_t_vu; static unsigned long long c0, g0, v0; unsigned long long c1 = rdtsc_();
            if (vn) uya_log("vsync %ld: %.1f vsyncs/s; GS %.0f%%, VU %.0f%% (VU includes its GS)", vn, 100.0 / ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9),
                            100.0 * (uya_t_gs - g0) / (c1 - c0), 100.0 * (uya_t_vu - v0) / (c1 - c0));
            static unsigned long long p0, x0; static unsigned long long tp0;
            extern unsigned long long uya_tex_px;
            extern unsigned long long nsends; static unsigned long long s0;
            uya_log("   %llu DMA sends (frames) in 100 vsyncs; per vsync: %llu waits for the GS threads (%u for layouts), %llu textures decoded, %llu pixels uploaded", nsends - s0, n_drain_wait / 100, n_layout_drain / 100, n_tc_build / 100, n_trx_px / 100); s0 = nsends;
            uya_log("   textures: %llu lookups by the game's uploads, %llu by GS memory; %llu decoded from uploads; palettes %llu from uploads, %llu from GS memory (per vsync)", n_tc_uplook / 100, n_tc_memlook / 100, n_tc_ntex / 100, n_pal_up / 100, n_pal_mem / 100);
            n_pal_up = n_pal_mem = 0;
            if (getenv("UYA_NTEXWHY")) { uya_log("   GS memory lookups: CT32 %llu, T4 %llu, other format %llu, T8 at TBP 0 %llu, T8 no upload there (mip level %llu, level 0 %llu), T8 upload of another size %llu, incomplete %llu, written over %llu",
                ntex_why[1] / 100, ntex_why[2] / 100, ntex_why[3] / 100, ntex_why[4] / 100, ntex_why[5] / 100, ntex_why[6] / 100, ntex_why[7] / 100, ntex_why[8] / 100, ntex_why[9] / 100); memset(ntex_why, 0, sizeof ntex_why); }
            n_tc_uplook = n_tc_memlook = n_tc_ntex = 0;
            uya_log("   texture rebuilds for stale data %llu, reused by content %llu; marks: uploads %llu, targets %llu", n_tc_stale / 100, n_tc_reuse / 100, n_mark_trx / 100, n_mark_draw / 100);
            n_tc_stale = n_tc_reuse = n_mark_trx = n_mark_draw = 0;
            uya_log("   time: drawing %.0f%%, texture decode %.0f%%, uploads %.0f%%", 100.0 * t_draw / (c1 - c0), 100.0 * t_tcb / (c1 - c0), 100.0 * t_trx / (c1 - c0));
            if (mtgs || gsgl_on) {   /* where a frame's time goes, per thread (ms per vsync) */
                double ms = (double)((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 100.0, busy = 0, wait = 0;
                if (gsgl_on) gsgl_thread_times(&busy, &wait);
                uya_log("   threads (ms per vsync, %.1f between vsyncs): GS thread busy %.1f (%.1f of it waiting for the GL thread), GL thread busy %.1f%s",
                        ms, mtgs ? ms * (double)gs_thread_busy / (double)(c1 - c0) : 0.0, wait / 100.0, busy / 100.0, gsgl_threaded() ? "" : " (no GL thread)");
                gs_thread_busy = 0;
            }
            n_drain_wait = n_tc_build = n_trx_px = 0; n_layout_drain = 0; t_draw = t_tcb = t_trx = 0;
            uya_log("   per vsync: %llu prims, %llu pixels written, %llu textured pixels", (stat_prims - p0) / 100, (stat_pixels - x0) / 100, (uya_tex_px - tp0) / 100);
            p0 = stat_prims; x0 = stat_pixels; tp0 = uya_tex_px;
            c0 = c1; g0 = uya_t_gs; v0 = uya_t_vu;
            t0 = t1;
        }
        if (++vn == want) { if (gsgl_on) gsgl_cpu_read(0, 0x4000); FILE *f = fopen("vram.bin", "wb"); if (f) { fwrite(vram, 4, 1 << 20, f); fclose(f); } uya_log("VRAM saved at vsync %ld", vn); }
    }
}
/* GL: the display drawn into the window */
static void gl_present(const u64 *r)
{
    gs_disp_t d;
    int ok = gs_disp(r[0], r[1], r[2], r[3], r[4], r[5], &d);
    fps_log();
    gsgl_present(ok ? &d : 0);
}


/* With the GS on its own thread the frame is captured there when the GS gets
 * to the vsync (a marker in its FIFO), and the game goes on at once: it shows
 * the last captured frame, so the GS works on frame N while the game prepares
 * frame N+1 (at most two vsyncs ahead). */
static u32 cap_buf[3][1024 * 1024], cap_w[3], cap_h[3], cap_ok[3];
static volatile u32 vs_sent, vs_done;
static void gs_vsync_marker(const u32 *e)
{
    u64 r[6]; memcpy(r, e, sizeof r);
    if (rec_f || rec_hotkey) gs_drain();
    rec_vsync(r);
    if (gsgl_on) { gl_present(r); __atomic_store_n(&vs_done, vs_done + 1, __ATOMIC_RELEASE); return; }
    gs_drain();
    u32 k = vs_done % 3;
    cap_ok[k] = gs_capture(r[0], r[1], r[2], r[3], r[4], r[5], cap_buf[k], &cap_w[k], &cap_h[k]);
    __atomic_store_n(&vs_done, vs_done + 1, __ATOMIC_RELEASE);
}

/* FMV frames in GL mode: the GL context belongs to the GS (thread), so a
 * decoded frame goes there to be drawn into the window. Two buffers; the
 * player waits until the older one has been shown. */
static u32 mv_buf[2][1024 * 1024];
static volatile u32 mv_sent, mv_done;
static void gs_movie_marker(const u32 *e)
{
    u32 k = e[0] & 1;
    gsgl_present_bgra(mv_buf[k], (int)e[1], (int)e[2]);
    __atomic_store_n(&mv_done, mv_done + 1, __ATOMIC_RELEASE);
}
/* 1 if the GL renderer showed the frame (else the caller presents it itself) */
int uya_gs_movie_frame(const u32 *px, int w, int h)
{
    if (!gsgl_on || !vram || w <= 0 || h <= 0 || w > 1024 || h > 1024) return 0;
    if (!mtgs) { gsgl_present_bgra(px, w, h); return 1; }
    int spin = 0;
    while (mv_sent - __atomic_load_n(&mv_done, __ATOMIC_ACQUIRE) >= 2) cpu_relax(spin++);
    u32 k = mv_sent & 1;
    memcpy(mv_buf[k], px, (size_t)w * h * 4);
    u32 m[12] = { k, (u32)w, (u32)h };
    mv_sent++;
    gs_fifo_put(m, 3, 0xFFFFFFFCu);   /* its own marker (0xFFFFFFFD is the GL memory sync) */
    return 1;
}

void uya_gs_vsync(void)
{
    if (!vram) return;
    if (survey_on()) survey_report();
    census_vsync();
    if (uya_srcstats_on() && uya_frame_count() % srcstats == 0) srcstats_log();
    u64 r[6] = { *(volatile u64 *)0x12000000, *(volatile u64 *)0x12000020,
                 *(volatile u64 *)0x12000070, *(volatile u64 *)0x12000080, *(volatile u64 *)0x12000090, *(volatile u64 *)0x120000A0 };
    if (!mtgs) {
        u32 w, h;
        gs_drain();
        rec_vsync(r);
        if (gsgl_on) { gl_present(r); if (!plat_present(0, 0, 0)) { uya_log("window closed"); exit(0); } return; }
        if (gs_capture(r[0], r[1], r[2], r[3], r[4], r[5], frame_px, &w, &h) && !plat_present(frame_px, (int)w, (int)h)) { uya_log("window closed"); exit(0); }
        return;
    }
    gs_fifo_put(r, 3, 0xFFFFFFFEu);      /* 48 bytes: 3 qwords */
    vs_sent++;
    {   /* no more than two vsyncs ahead of the GS */
        int spin = 0;
        while (vs_sent - __atomic_load_n(&vs_done, __ATOMIC_ACQUIRE) > 2) cpu_relax(spin++);
    }
    static u32 shown;
    u32 d = __atomic_load_n(&vs_done, __ATOMIC_ACQUIRE);
    if (gsgl_on) { if (!plat_present(0, 0, 0)) { uya_log("window closed"); exit(0); } return; }   /* the GS thread showed it */
    if (d != shown) {
        u32 k = (d - 1) % 3;
        shown = d;
        if (cap_ok[k] && !plat_present(cap_buf[k], (int)cap_w[k], (int)cap_h[k])) { uya_log("window closed"); exit(0); }
    } else if (!plat_present(0, 0, 0)) { uya_log("window closed"); exit(0); }   /* nothing new: keep the window alive */
}

/* Writes a w x h image of RGBA8 pixels into GS memory as PSMCT32 at block
 * address tbp, buffer width tbw (in 64-pixel units). The movie player uses it
 * to put a decoded frame where the PS2's IPU output would be. */
void uya_gs_write_ct32(u32 tbp, u32 tbw, int w, int h, const u32 *rgba)
{
    if (!vram) return;
    gs_sync();
    gs_drain();
    if (rec_f && rec_vs > rec_from) { u32 hd[5] = { 3, tbp, tbw, (u32)w, (u32)h }; rec_put(hd, 20); rec_put(rgba, (size_t)w * h * 4); }
    gl_mem_req(1, 0, tbp, tbw, 0, 0, (u32)w, (u32)h);
    mark_blocks(tbp, area_blocks(0, tbw, (u32)h));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) vram[a32(tbp, tbw, (u32)x, (u32)y)] = rgba[y * w + x];
}

/* GS -> memory (sceGsExecStoreImage): the w x h area at (x, y) of the buffer
 * at block bp (width bw, format psm), packed row by row */
void uya_gs_store_image(u32 bp, u32 bw, u32 psm, u32 x, u32 y, u32 w, u32 h, void *dst)
{
    if (!vram) return;
    gs_sync();
    gs_drain();
    gl_mem_req(0, psm, bp, bw, x, y, w, h);
    if (bw == 1 && psm == 0 && w == 64 && h == 64 && bp < 0x4000) si_hash[bp] = vhash(bp, 0x40) | 1;
    u32 bits = trx_bits(psm), n = 0;
    u8 *o = dst;
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++, n++) {
            u32 v = px_read(psm, bp, bw, (x + i) & 2047, (y + j) & 2047);
            switch (bits) {
            case 4: if (n & 1) o[n >> 1] = (u8)((o[n >> 1] & 0x0F) | (v << 4)); else o[n >> 1] = (u8)((o[n >> 1] & 0xF0) | (v & 15)); break;
            case 8: o[n] = (u8)v; break;
            case 16: memcpy(o + n * 2, &v, 2); break;
            case 24: memcpy(o + n * 3, &v, 3); break;
            default: memcpy(o + n * 4, &v, 4); break;
            }
        }
}

void uya_gs_stats(void)
{
    uya_log("GS: %llu primitives drawn, %llu pixels written", stat_prims, stat_pixels);
}

/* GL: a level of a state's texture decoded now (for gs_gl.c's deferred levels) */
int gs_tex_late(const pcmd_t *p, int lvl, tlevel_t *out)
{
    gs_ctx_t c; memset(&c, 0, sizeof c);
    c.tex0 = p->tex0; c.tex1 = p->tex1; c.clamp = p->clamp; c.miptbp1 = p->miptbp1; c.miptbp2 = p->miptbp2;
    u64 sv = texa; texa = p->texa;
    prim_serial++;                        /* not the memo of the state being set up */
    const tcache_t *e = tc_get(&c, lvl);
    prim_serial++;
    texa = sv;
    u32 b_, bw_, lw_, lh_; tex_level(&c, lvl, &b_, &bw_, &lw_, &lh_);
    out->px = e->px; out->w = (int)e->w; out->h = (int)e->h; out->lw = (int)lw_; out->lh = (int)lh_;
    out->tci = (int)(e - tc); out->ser = e->ser;
    return 1;
}
