/* the native renderers' draws as host draws: a mesh drawn with a material (native_draw.c) */
#ifndef NATIVE_DRAW_H
#define NATIVE_DRAW_H
#include "uya_host.h"

/* A material: what a draw's pixels are made of, in the renderer's own terms. The texture is named
 * by where the game's upload put it (its base in 256-byte blocks, its row width in 64 pixels, its
 * format), as the uploads' store keeps it (piece 5), with its palette the same way. A renderer's
 * material can give only some parts (`has`): the others stay as its last draw left them, as the
 * game's own material data does (a class's blocks give the texture; the primitive comes once).
 * Pass 0 or 1: which of the frame's two draw passes (target, depth, blend, alpha test, scissor:
 * set up for the frame by the game) the draw goes to. */
enum { ND_TEX = 1, ND_SAMPLE = 2, ND_WRAP = 4, ND_MIP13 = 8, ND_MIP46 = 16, ND_PRIM = 32 };
typedef struct {
    u8 has, pass;
    /* ND_TEX: the texture and its palette */
    u16 tbp; u8 tbw, fmt, wlog, hlog;
    u8 tex_alpha;                       /* the texture's alpha is used (else the colour's) */
    u8 combine;                         /* 0 modulate, 1 decal, 2 highlight, 3 highlight 2 */
    u16 pal; u8 pal_fmt, pal_layout, pal_offset, pal_load;   /* palette base, format, layout, offset (16 entries), when it loads */
    /* ND_SAMPLE: filtering and levels of detail */
    u8 mag_linear, min_mode, levels, lod_fixed, lod_shift, mip_follow;   /* min_mode: 0 nearest .. 5 linear-mip-linear; mip_follow: levels 1-3 right after level 0 */
    s16 lod_bias;                       /* in 1/16: 12 bits (the game writes them sign-extended to 16) */
    /* ND_WRAP: repeat, clamp, region clamp, region repeat; the region */
    u8 wrap_u, wrap_v; u16 u_min, u_max, v_min, v_max;
    /* ND_MIP13 / ND_MIP46: where levels 1-3 / 4-6 are */
    u16 mip_tbp[6]; u8 mip_tbw[6];
    /* ND_PRIM: the primitive */
    u8 kind, gouraud, textured, fog, blend, aa, uv, ctx;
} nd_material;

/* the material a renderer's game data gives (the GS register writes of a class's material blocks:
 * TEX0, TEX1, TEX2, CLAMP, MIPTBP1, MIPTBP2 and PRIM, all of one pass); 0 if it has others */
int nd_material_from_regs(nd_material *m, const u32 *addr, const u64 *val, int n);
/* draw triangles [first, first + count) of native mesh id (native_dlist.h) with material m; xf as
 * native_mesh_draw */
void nd_mesh_draw(u32 id, u32 first, u32 count, const float xf[32], const nd_material *m);
/* a renderer's register block and mesh draw: as a material draw when the block is one
 * (UYA_NDRAW: 0 always as register writes; 1 check: both, compared; 2 the default) */
void nd_regs_mesh_draw(const u32 *addr, const u64 *val, int n, u32 id, u32 first, u32 count, const float xf[32]);
/* a mesh draw with the material the renderer's last draws left (no part of its own) */
void nd_mesh_draw_last(u32 id, u32 first, u32 count, const float xf[32]);

/* A pass's settings, as a renderer's own packets give them (the effect programs set their blend,
 * tests and target around their draws): the parts in `has`, for pass 0 or 1; the frame's fog
 * colour, texture alpha expansion and colour clamp (for both passes) with the NP_G* parts. */
enum { NP_BLEND = 1, NP_TEST = 2, NP_TARGET = 4, NP_DEPTH = 8, NP_SCISSOR = 16, NP_OFFSET = 32, NP_FBA = 64,
       NP_GFOG = 128, NP_GTEXA = 256, NP_GCLAMP = 512, NP_GFLUSH = 1024,
       NP_GPRCONT = 2048, NP_GPRMODE = 4096, NP_GDITHER = 8192, NP_GPABE = 16384 };
typedef struct {
    u16 has; u8 pass;
    u8 ba, bb, bc, bd, fix;                         /* blend: (A - B) * C / 128 + D; A B D: 0 source, 1 target, 2 zero; C: 0 source alpha, 1 target alpha, 2 fix */
    u8 ate, atst, aref, afail, date, datm, zte, ztst;   /* alpha test (and what fails it keeps), target alpha test, depth test */
    u16 fbp; u8 fbw, fpsm; u32 fbmsk;               /* target: base (in 8 KB pages), width (64 px), format, bits kept */
    u16 zbp; u8 zpsm, zmsk;                         /* depth: base (pages), format, not written */
    u16 sx0, sx1, sy0, sy1;                         /* scissor */
    u16 xoff, yoff;                                 /* the screen's offset (1/16 pixel) */
    u8 fba;                                         /* alpha's top bit set on write */
    u8 fog_r, fog_g, fog_b, ta0, aem, ta1, colclamp;
    u8 prcont;                                      /* the primitive's attributes from the primitive (1) or from prmode (0) */
    u16 prmode;                                     /* those attributes (PRIM's bits 3-10) */
    u8 dither, pabe;                                /* dithering; alpha blending only where the source alpha's top bit is set */
} nd_pass;
/* the settings a block of GS register writes gives (ALPHA, TEST, FRAME, ZBUF, SCISSOR, XYOFFSET,
 * FBA of one pass; FOGCOL, TEXA, COLCLAMP); 0 if it has others */
int nd_pass_from_regs(nd_pass *q, const u32 *addr, const u64 *val, int n);
/* a renderer's packet as host draws: its blocks of register writes become materials and pass
 * settings where they are (ops 17, 18), its vertices stay as they are (native_dlist.c) */
u32 nd_pack(const u32 *ops, u32 nq, u32 *out, u32 cap);
#endif
