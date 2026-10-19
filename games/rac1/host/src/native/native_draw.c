/* native_draw.c: the native renderers' draws as host draws (native_draw.h).
 *
 * A renderer draws a mesh with a material: the texture named by where the game's upload put it
 * (and its palette), its filtering, wrap and mip levels, and the primitive's flags. The renderers'
 * materials come from the game's own material data, the blocks of GS register writes a class
 * carries for each of its textures (TEX0, TEX1, CLAMP, MIPTBP1, MIPTBP2) and the PRIM its program
 * sends: nd_material_from_regs reads them into a material here, in the renderer, and the draw
 * goes to the renderer (host/src/gs/gs.c, op 15) as a material, not as register writes. A block
 * with any other register, or a value with bits a material has no field for, stays as register
 * writes (nothing is lost). UYA_NDRAW: 0 register writes as before; 1 check (the register writes
 * go too, the renderer compares the state each way); 2 material draws (the default). */
#include "native_draw.h"
#include "native_dlist.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void uya_gs_dlist(const u32 *d, u32 qwc);
void uya_log(const char *fmt, ...);

#define ND_QW ((u32)((sizeof(nd_material) + 15) / 16))
static int mode = -1;
static unsigned long long n_mat, n_regs;
static int nd_mode(void)
{
    if (mode < 0) { const char *e = getenv("UYA_NDRAW"); mode = e ? atoi(e) : 2; uya_log("native draws: %s (UYA_NDRAW)", mode == 0 ? "register writes" : mode == 1 ? "materials, checked against register writes" : "materials"); }
    return mode;
}

int nd_material_from_regs(nd_material *m, const u32 *addr, const u64 *val, int n)
{
    memset(m, 0, sizeof *m);
    int pass = -1;
    for (int i = 0; i < n; i++) {
        u32 a = addr[i]; u64 v = val[i];
        int p = -1;
        switch (a) {
        case 0x00:   /* PRIM: kind, Gouraud, textured, fog, blend, antialiased, UV, pass */
            if (v >> 11) return 0;
            m->kind = (u8)(v & 7); m->gouraud = (u8)((v >> 3) & 1); m->textured = (u8)((v >> 4) & 1); m->fog = (u8)((v >> 5) & 1);
            m->blend = (u8)((v >> 6) & 1); m->aa = (u8)((v >> 7) & 1); m->uv = (u8)((v >> 8) & 1); m->ctx = (u8)((v >> 9) & 1);
            if ((v >> 10) & 1) return 0;   /* (FIX: a material has no field for it) */
            m->has |= ND_PRIM;
            continue;
        case 0x06: case 0x07:   /* TEX0: the texture, its palette */
            p = (int)(a - 0x06);
            m->tbp = (u16)(v & 0x3FFF); m->tbw = (u8)((v >> 14) & 63); m->fmt = (u8)((v >> 20) & 63); m->wlog = (u8)((v >> 26) & 15); m->hlog = (u8)((v >> 30) & 15);
            m->tex_alpha = (u8)((v >> 34) & 1); m->combine = (u8)((v >> 35) & 3);
            m->pal = (u16)((v >> 37) & 0x3FFF); m->pal_fmt = (u8)((v >> 51) & 15); m->pal_layout = (u8)((v >> 55) & 1); m->pal_offset = (u8)((v >> 56) & 31); m->pal_load = (u8)((v >> 61) & 7);
            m->has |= ND_TEX;
            break;
        case 0x14: case 0x15:   /* TEX1: filtering, levels of detail */
            p = (int)(a - 0x14);
            if (v & ~(0xFFFF00000000ull | 0x1803FDull)) return 0;
            m->lod_fixed = (u8)(v & 1); m->levels = (u8)((v >> 2) & 7); m->mag_linear = (u8)((v >> 5) & 1); m->min_mode = (u8)((v >> 6) & 7);
            m->mip_follow = (u8)((v >> 9) & 1); m->lod_shift = (u8)((v >> 19) & 3);
            m->lod_bias = (s16)(u16)(v >> 32);   /* (as the game writes it: 12 bits, sign-extended to 16) */
            m->has |= ND_SAMPLE;
            break;
        case 0x08: case 0x09:   /* CLAMP: wrap */
            p = (int)(a - 0x08);
            if (v >> 44) return 0;
            m->wrap_u = (u8)(v & 3); m->wrap_v = (u8)((v >> 2) & 3);
            m->u_min = (u16)((v >> 4) & 1023); m->u_max = (u16)((v >> 14) & 1023); m->v_min = (u16)((v >> 24) & 1023); m->v_max = (u16)((v >> 34) & 1023);
            m->has |= ND_WRAP;
            break;
        case 0x34: case 0x35: case 0x36: case 0x37: {   /* MIPTBP1 / MIPTBP2: where levels 1-3 / 4-6 are */
            p = (int)((a - 0x34) & 1);
            int h = a >= 0x36;
            if (v >> 60) return 0;
            for (int k = 0; k < 3; k++) { m->mip_tbp[h * 3 + k] = (u16)((v >> (k * 20)) & 0x3FFF); m->mip_tbw[h * 3 + k] = (u8)((v >> (k * 20 + 14)) & 63); }
            m->has |= h ? ND_MIP46 : ND_MIP13;
        } break;
        default: return 0;
        }
        if (pass >= 0 && p != pass) return 0;   /* (one pass a material) */
        pass = p;
        /* a register given twice: its second value would win; a material takes it once */
        for (int j = 0; j < i; j++) if (addr[j] == a) return 0;
    }
    m->pass = (u8)(pass < 0 ? 0 : pass);
    return 1;
}

void nd_mesh_draw(u32 id, u32 first, u32 count, const float xf[32], const nd_material *m)
{
    u32 b[(9 + 8) * 4];
    memset(b, 0, sizeof b);
    b[0] = 15; b[1] = id; b[2] = first; b[3] = count;
    memcpy(b + 4, xf, 32 * 4);
    memcpy(b + 36, m, sizeof *m);
    uya_gs_dlist(b, 9 + ND_QW);
}

void nd_mesh_draw_last(u32 id, u32 first, u32 count, const float xf[32])
{
    nd_regs_mesh_draw(0, 0, 0, id, first, count, xf);
}
void nd_regs_mesh_draw(const u32 *addr, const u64 *val, int n, u32 id, u32 first, u32 count, const float xf[32])
{
    nd_material m;
    int md = nd_mode();
    int ok = md && nd_material_from_regs(&m, addr, val, n);
    if (md && !ok) {   /* (why a block stays as register writes: the first few) */
        static int left = 12;
        if (left > 0) { left--; char t[400]; int o = 0; for (int i = 0; i < n && o < 360; i++) o += snprintf(t + o, sizeof t - o, " %02X:%016llX", addr[i], (unsigned long long)val[i]); uya_log("native draws: a block as register writes:%s", t); }
    }
    if (!ok) {
        n_regs++;
        if (n) native_regs(addr, val, n);
        if (count) native_mesh_draw(id, first, count, xf);
    } else {
        n_mat++;
        if (md == 1) {   /* check: the register writes too (the renderer applies them first and compares) */
            u32 b[17 * 4]; int k = n < 16 ? n : 16;
            memset(b, 0, sizeof b);
            b[0] = 16; b[1] = (u32)k;
            for (int i = 0; i < k; i++) { b[4 + i * 4] = addr[i]; b[5 + i * 4] = (u32)val[i]; b[6 + i * 4] = (u32)(val[i] >> 32); }
            uya_gs_dlist(b, 1 + (u32)k);
        }
        nd_mesh_draw(id, first, count, xf, &m);
    }
    if (((n_mat + n_regs) & 0xFFFFF) == 0) uya_log("native draws: %llu as materials, %llu as register writes", n_mat, n_regs);
}

int nd_pass_from_regs(nd_pass *q, const u32 *addr, const u64 *val, int n)
{
    memset(q, 0, sizeof *q);
    int pass = -1;
    for (int i = 0; i < n; i++) {
        u32 a = addr[i]; u64 v = val[i];
        int p = -1;
        for (int j = 0; j < i; j++) if (addr[j] == a) return 0;   /* (a register given twice) */
        switch (a) {
        case 0x42: case 0x43:   /* ALPHA */
            if (v & ~0xFF000000FFull) return 0;
            p = (int)(a - 0x42);
            q->ba = (u8)(v & 3); q->bb = (u8)((v >> 2) & 3); q->bc = (u8)((v >> 4) & 3); q->bd = (u8)((v >> 6) & 3); q->fix = (u8)(v >> 32);
            q->has |= NP_BLEND; break;
        case 0x47: case 0x48:   /* TEST */
            if (v & ~0x7FFFFull) return 0;
            p = (int)(a - 0x47);
            q->ate = (u8)(v & 1); q->atst = (u8)((v >> 1) & 7); q->aref = (u8)(v >> 4); q->afail = (u8)((v >> 12) & 3);
            q->date = (u8)((v >> 14) & 1); q->datm = (u8)((v >> 15) & 1); q->zte = (u8)((v >> 16) & 1); q->ztst = (u8)((v >> 17) & 3);
            q->has |= NP_TEST; break;
        case 0x4C: case 0x4D:   /* FRAME */
            if (v & ~0xFFFFFFFF3F3F01FFull) return 0;
            p = (int)(a - 0x4C);
            q->fbp = (u16)(v & 511); q->fbw = (u8)((v >> 16) & 63); q->fpsm = (u8)((v >> 24) & 63); q->fbmsk = (u32)(v >> 32);
            q->has |= NP_TARGET; break;
        case 0x4E: case 0x4F:   /* ZBUF */
            if (v & ~0x10F0001FFull) return 0;
            p = (int)(a - 0x4E);
            q->zbp = (u16)(v & 511); q->zpsm = (u8)((v >> 24) & 15); q->zmsk = (u8)((v >> 32) & 1);
            q->has |= NP_DEPTH; break;
        case 0x40: case 0x41:   /* SCISSOR */
            if (v & ~0x07FF07FF07FF07FFull) return 0;
            p = (int)(a - 0x40);
            q->sx0 = (u16)(v & 2047); q->sx1 = (u16)((v >> 16) & 2047); q->sy0 = (u16)((v >> 32) & 2047); q->sy1 = (u16)((v >> 48) & 2047);
            q->has |= NP_SCISSOR; break;
        case 0x18: case 0x19:   /* XYOFFSET */
            if (v & ~0x0000FFFF0000FFFFull) return 0;
            p = (int)(a - 0x18);
            q->xoff = (u16)v; q->yoff = (u16)(v >> 32);
            q->has |= NP_OFFSET; break;
        case 0x4A: case 0x4B:   /* FBA */
            if (v & ~1ull) return 0;
            p = (int)(a - 0x4A);
            q->fba = (u8)(v & 1);
            q->has |= NP_FBA; break;
        case 0x3D:              /* FOGCOL */
            if (v & ~0xFFFFFFull) return 0;
            q->fog_r = (u8)v; q->fog_g = (u8)(v >> 8); q->fog_b = (u8)(v >> 16);
            q->has |= NP_GFOG; continue;
        case 0x3B:              /* TEXA */
            if (v & ~0xFF000080FFull) return 0;
            q->ta0 = (u8)v; q->aem = (u8)((v >> 15) & 1); q->ta1 = (u8)(v >> 32);
            q->has |= NP_GTEXA; continue;
        case 0x1A:              /* PRMODECONT */
            if (v & ~1ull) return 0;
            q->prcont = (u8)(v & 1); q->has |= NP_GPRCONT; continue;
        case 0x1B:              /* PRMODE */
            if (v & ~0x7F8ull) return 0;
            q->prmode = (u16)v; q->has |= NP_GPRMODE; continue;
        case 0x45:              /* DTHE */
            if (v & ~1ull) return 0;
            q->dither = (u8)(v & 1); q->has |= NP_GDITHER; continue;
        case 0x49:              /* PABE */
            if (v & ~1ull) return 0;
            q->pabe = (u8)(v & 1); q->has |= NP_GPABE; continue;
        case 0x3F:              /* TEXFLUSH: no field */
            q->has |= NP_GFLUSH; continue;
        case 0x46:              /* COLCLAMP */
            if (v & ~1ull) return 0;
            q->colclamp = (u8)(v & 1);
            q->has |= NP_GCLAMP; continue;
        default: return 0;
        }
        if (pass >= 0 && p != pass) return 0;
        pass = p;
    }
    q->pass = (u8)(pass < 0 ? 0 : pass);
    return 1;
}

/* a run of register writes as a material and pass settings: 1 if every write is one of theirs */
static int is_mat(u32 a) { return a == 0x00 || a == 0x06 || a == 0x07 || a == 0x14 || a == 0x15 || a == 0x08 || a == 0x09 || (a >= 0x34 && a <= 0x37); }
#define NP_QW ((u32)((sizeof(nd_pass) + 15) / 16))
static unsigned long long pk_runs, pk_conv;
static int pack_run(const u32 *addr, const u64 *val, int n, u32 *out, u32 *o, u32 cap)
{
    u32 ma[16], pa[2][16]; u64 mv[16], pv[2][16]; int nm = 0, np[2] = { 0, 0 }, ng = 0;
    u32 ga[8]; u64 gv[8];
    if (n > 16) return 0;
    for (int i = 0; i < n; i++) {
        u32 a = addr[i];
        if (is_mat(a)) { ma[nm] = a; mv[nm++] = val[i]; continue; }
        if (a == 0x3D || a == 0x3B || a == 0x46 || a == 0x3F || a == 0x1A || a == 0x1B || a == 0x45 || a == 0x49) { if (ng >= 8) return 0; ga[ng] = a; gv[ng++] = val[i]; continue; }
        int p;
        switch (a) {
        case 0x42: case 0x47: case 0x4C: case 0x4E: case 0x40: case 0x18: case 0x4A: p = 0; break;
        case 0x43: case 0x48: case 0x4D: case 0x4F: case 0x41: case 0x19: case 0x4B: p = 1; break;
        default: return 0;
        }
        pa[p][np[p]] = a; pv[p][np[p]++] = val[i];
    }
    nd_material m; nd_pass q[2], g;
    if (nm && !nd_material_from_regs(&m, ma, mv, nm)) return 0;
    for (int p = 0; p < 2; p++) if (np[p] && !nd_pass_from_regs(&q[p], pa[p], pv[p], np[p])) return 0;
    if (ng && !nd_pass_from_regs(&g, ga, gv, ng)) return 0;
    u32 need = (nm ? 1 + ND_QW : 0) + (np[0] ? 1 + NP_QW : 0) + (np[1] ? 1 + NP_QW : 0) + (ng ? 1 + NP_QW : 0);
    if (*o + need > cap) return 0;
    /* (the order of writes to different registers does not matter within a run: no vertex between) */
    if (ng) { u32 *w = out + *o * 4; memset(w, 0, (1 + NP_QW) * 16); w[0] = 18; memcpy(w + 4, &g, sizeof g); *o += 1 + NP_QW; }
    for (int p = 0; p < 2; p++) if (np[p]) { u32 *w = out + *o * 4; memset(w, 0, (1 + NP_QW) * 16); w[0] = 18; memcpy(w + 4, &q[p], sizeof q[p]); *o += 1 + NP_QW; }
    if (nm) { u32 *w = out + *o * 4; memset(w, 0, (1 + ND_QW) * 16); w[0] = 17; memcpy(w + 4, &m, sizeof m); *o += 1 + ND_QW; }
    return 1;
}
/* one block of register writes as materials and pass settings (ops 17, 18) at out + *o qwords: 1 if
 * every write is one of theirs */
int nd_pack_regs(const u32 *addr, const u64 *val, int n, u32 *out, u32 *o, u32 cap)
{
    return nd_mode() && pack_run(addr, val, n, out, o, cap);
}
u32 nd_pack(const u32 *ops, u32 nq, u32 *out, u32 cap)
{
    if (!nd_mode()) return 0;
    u32 q = 0, o = 0;
    while (q < nq) {
        const u32 *c = ops + q * 4;
        if (c[0] != 1) {   /* a vertex strip (2) or Q reset (3): as it is */
            u32 len = c[0] == 2 ? 1 + 2 * c[1] : 1;
            if (o + len > cap) return 0;
            memcpy(out + o * 4, c, len * 16); o += len; q += len;
            continue;
        }
        u32 addr[16]; u64 val[16]; int n = 0; u32 q0 = q;
        while (q < nq && ops[q * 4] == 1 && n < 16) { addr[n] = ops[q * 4 + 1]; val[n++] = ((u64)ops[q * 4 + 3] << 32) | ops[q * 4 + 2]; q++; }
        pk_runs++;
        if (pack_run(addr, val, n, out, &o, cap)) { pk_conv++; continue; }
        {   /* testing: the first few runs that stay as register writes */
            static int left = 8;
            if (left > 0) { left--; char t[400]; int k = 0; for (int i = 0; i < n && k < 360; i++) k += snprintf(t + k, sizeof t - k, " %02X:%016llX", addr[i], (unsigned long long)val[i]); uya_log("native packets: a run as register writes:%s", t); }
        }
        if (o + (q - q0) > cap) return 0;
        memcpy(out + o * 4, ops + q0 * 4, (q - q0) * 16); o += q - q0;
    }
    static unsigned long long next = 200000;
    if (pk_runs >= next) { next += 200000; uya_log("native packets: %llu register runs, %llu as materials and pass settings", pk_runs, pk_conv); }
    return o;
}
