/* native_dlist.c: the native renderers' draws as a native draw list.
 *
 * The native code (native_tie.c, native_shrub.c, native_tfrag.c, native_moby.c)
 * builds its draws in VU1 data memory exactly as the PS2's microprograms did
 * (later runs read what it leaves there), then hands them on here. Each draw
 * becomes a native draw list for the renderer (host/src/gs/gs.c, gs_dlist_):
 * register writes, and strips of plain vertices (position, colour, texture
 * coordinates, whether the vertex closes a triangle), with the same meaning as
 * the GIF data, so the same primitives are drawn without decoding PS2 GIF
 * packets. Data of another shape goes register by register (native_gif,
 * below). UYA_DLIST=0 sends everything as GIF packets (to compare). */
#include "native_dlist.h"
#include "native_draw.h"
#include <stdlib.h>
#include <stdio.h>
#include "uya_runtime.h"
#include <string.h>

void uya_gif_packet(const u32 *data, u32 qwc);
void uya_gs_dlist(const u32 *d, u32 qwc);
void native_gif(int path, const u32 *d, u32 qwc);

/* the list for a PATH1 packet (as XGKICK sends it: from a tag, to the EOP);
 * returns its qwords, or 0 if the packet has data of another shape */
static u32 convert(const u32 *pk, u32 qwc, u32 *out, u32 cap)
{
    u32 q = 0, o = 0;
#define PUT4(a, b, c, d) do { if (o + 1 > cap) return 0; out[o * 4] = (a); out[o * 4 + 1] = (b); out[o * 4 + 2] = (c); out[o * 4 + 3] = (d); o++; } while (0)
    while (q < qwc) {
        const u32 *t = pk + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0], hi = ((u64)t[3] << 32) | t[2];
        u32 nloop = (u32)(lo & 0x7FFF), eop = (u32)(lo >> 15) & 1, pre = (u32)(lo >> 46) & 1, primv = (u32)(lo >> 47) & 0x7FF;
        u32 flg = (u32)(lo >> 58) & 3, nreg = (u32)(lo >> 60) & 15;
        if (!nreg) nreg = 16;
        q++;
        if (pre) PUT4(1, 0x00, primv, 0);
        PUT4(3, 0, 0, 0);
        if (nloop) {
            if (flg != 0) return 0;
            if (q + nloop * nreg > qwc) return 0;
            u32 r[16];
            for (u32 i = 0; i < nreg; i++) r[i] = (u32)(hi >> (i * 4)) & 15;
            int all_ad = 1;
            for (u32 i = 0; i < nreg; i++) if (r[i] != 0xE) all_ad = 0;
            if (all_ad) {
                for (u32 i = 0; i < nloop * nreg; i++, q++) {
                    const u32 *v = pk + q * 4;
                    PUT4(1, v[2] & 0xFF, v[0], v[1]);
                }
            } else if (nreg == 3 && (r[2] == 0x4 || r[2] == 0x5) &&
                       ((r[0] == 0x2 && r[1] == 0x1) || (r[0] == 0x1 && r[1] == 0x2))) {
                /* ST and RGBAQ (either order), then XYZF2 or XYZ2: a strip of vertices */
                if (o + 1 + 2 * nloop > cap) return 0;
                u32 *h = out + o * 4;
                h[0] = 2; h[1] = nloop; h[2] = h[3] = 0;
                u32 *w = h + 4;
                u32 ist = r[0] == 0x2 ? 0 : 1, irg = 1 - ist;
                for (u32 l = 0; l < nloop; l++, q += 3, w += 8) {
                    const u32 *s_ = pk + (q + ist) * 4, *c = pk + (q + irg) * 4, *x = pk + (q + 2) * 4;
                    w[0] = (x[0] & 0xFFFF) | ((x[1] & 0xFFFF) << 16);
                    w[2] = (c[0] & 255) | ((c[1] & 255) << 8) | ((c[2] & 255) << 16) | ((c[3] & 255) << 24);
                    if (r[2] == 0x4) { w[1] = (x[2] >> 4) & 0xFFFFFF; w[3] = ((x[3] >> 4) & 0xFF) | (((x[3] >> 15) & 1) << 8); }
                    else { w[1] = x[2]; w[3] = (((x[3] >> 15) & 1) << 8) | 0x200; }
                    w[4] = s_[0]; w[5] = s_[1]; w[6] = s_[2]; w[7] = 0;
                }
                o += 1 + 2 * nloop;
            } else return 0;
        }
        if (eop) break;
    }
    return o;
#undef PUT4
}

void native_kick(const u32 *pk, u32 qwc)
{
    static int on = -1;
    static u32 *buf; static u32 bufq;
    if (on < 0) { const char *e = getenv("UYA_DLIST"); on = !e || atoi(e) != 0; }
    if (!on || !qwc) { uya_gif_packet(pk, qwc); return; }
    u32 cap = qwc * 2 + 16;
    if (cap > bufq) { free(buf); buf = malloc((size_t)cap * 16); bufq = cap; }
    u32 n = buf ? convert(pk, qwc, buf, cap) : 0;
    if (n) {   /* its register writes as materials and pass settings where they are (native_draw.c) */
        static u32 *pb; static u32 pbq;
        u32 pc = n * 5 + 16;
        if (pc > pbq) { free(pb); pb = malloc((size_t)pc * 16); pbq = pc; }
        u32 m = pb ? nd_pack(buf, n, pb, pc) : 0;
        if (m) uya_gs_dlist(pb, m);
        else uya_gs_dlist(buf, n);
    }
    else native_gif(0, pk, qwc);   /* (another shape: register by register) */
}

/* ---- the game's own GIF data (VIF1 DIRECT, PATH2) ----
 * Game code on the EE writes GIF packets for the HUD, text, fades, the sky,
 * shadow volumes and GS state, and sends them with VIF1 DIRECT. Each tag
 * whose data is all in the packet becomes ops that set the GS registers and
 * kick the vertices exactly as the GIF front end (gs.c uya_gs_gif_) would:
 * PACKED registers one by one (ops 1, 10 to 14), REGLIST as register writes
 * (op 1), the tag's PRIM (op 1) and Q reset (op 3). IMAGE transfers (texture
 * uploads, piece 5) and a tag whose data runs on into the next packet go on
 * as GIF data, in order; p_left counts that tag's qwords still to come, as
 * the path's tag state does. The GS library's PATH3 packets (gsdk.c, the GIF
 * DMA channel) go the same way, with their own tag state. UYA_DLIST=0 sends
 * it all as GIF data. */
static u32 p_left[3], p_img[3];
/* ops with their register runs as materials and pass settings where they are (native_draw.c
 * nd_pack); as they are otherwise */
static void send_packed(const u32 *ops, u32 n)
{
    static u32 *pb; static u32 pbq;
    u32 pc = n * 5 + 16;
    if (pc > pbq) { free(pb); pb = malloc((size_t)pc * 16); pbq = pc; }
    u32 m = pb ? nd_pack(ops, n, pb, pc) : 0;
    if (m) uya_gs_dlist(pb, m);
    else uya_gs_dlist(ops, n);
}
static unsigned long long dir_n[3];   /* (qwords as ops, as GIF data: IMAGE, split tags) */
void native_direct_stats(void)
{
    if (dir_n[0] | dir_n[1] | dir_n[2])
        uya_log("native GIF (PATH2, PATH3, other PATH1 shapes): %llu qwords as draw-list ops; as GIF data %llu IMAGE, %llu of split tags", dir_n[0], dir_n[1], dir_n[2]);
}
void native_gif(int path, const u32 *d, u32 qwc)
{
    static int on = -1;
    static u32 *buf; static u32 bufq;
    if (on < 0) { const char *e = getenv("UYA_DLIST"); on = !e || atoi(e) != 0; }
    if (!on || !qwc) { uya_gif_packet(d, qwc); return; }
    u32 cap = qwc * 2 + 16;
    if (cap > bufq) { free(buf); buf = malloc((size_t)cap * 16); bufq = cap; }
    u32 q = 0, o = 0;
#define PUT4(a, b, c_, e) do { u32 *w_ = buf + o * 4; w_[0] = (a); w_[1] = (b); w_[2] = (c_); w_[3] = (e); o++; } while (0)
#define FLUSH() do { if (o) { send_packed(buf, o); o = 0; } } while (0)
    if (path == 0) p_left[0] = 0;   /* (XGKICK starts a packet at its tag) */
    if (p_left[path]) {
        u32 n = p_left[path] < qwc ? p_left[path] : qwc;
        uya_gif_packet(d, n); dir_n[p_img[path] ? 1 : 2] += n;
        p_left[path] -= n; q = n;
    }
    while (q < qwc) {
        const u32 *t = d + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0], hi = ((u64)t[3] << 32) | t[2];
        u32 nloop = (u32)(lo & 0x7FFF), pre = (u32)(lo >> 46) & 1, primv = (u32)(lo >> 47) & 0x7FF;
        u32 flg = (u32)(lo >> 58) & 3, nreg = (u32)(lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        if (flg >= 2 || q + 1 + len > qwc) {
            u32 avail = qwc - q - 1 < len ? qwc - q - 1 : len;
            FLUSH();
            uya_gif_packet(t, 1 + avail); dir_n[flg >= 2 ? 1 : 2] += 1 + avail;
            if (flg == 2) {   /* testing: UYA_IMGLOG=v0,n logs n IMAGE tags from vsync v0: the path, the chain's last tag, the transfer */
                static int left = -1; static unsigned v0;
                if (left < 0) { const char *e = getenv("UYA_IMGLOG"); left = 0; if (e) sscanf(e, "%u,%d", &v0, &left); }
                if (left > 0 && uya_frame_count() >= v0) {
                    left--; extern u32 uya_dma_tag_ring[16][5], uya_dma_tag_n; const u32 *r = uya_dma_tag_ring[(uya_dma_tag_n - 1) & 15];
                    extern const char *uya_gs_src;
                    uya_log("IMAGE v%u path %d [%s] %u qwords at %08X; the chain's last tag at %08X: %08X %08X %08X %08X", uya_frame_count(), path, uya_gs_src, nloop, (u32)(uintptr_t)t, r[0], r[1], r[2], r[3], r[4]);
                }
            }
            p_left[path] = len - avail; p_img[path] = flg >= 2; q += 1 + avail;
            continue;
        }
        q++;
        if (pre) PUT4(1, 0x00, primv, 0);
        PUT4(3, 0, 0, 0);
        if (flg == 0) {
            for (u32 i = 0; i < len; i++, q++) {
                const u32 *v = d + q * 4;
                u32 rg = (u32)(hi >> ((i % nreg) * 4)) & 15;
                switch (rg) {
                case 0x0: PUT4(1, 0x00, v[0], v[1]); break;
                case 0x1: PUT4(10, (v[0] & 255) | ((v[1] & 255) << 8) | ((v[2] & 255) << 16) | ((v[3] & 255) << 24), 0, 0); break;
                case 0x2: PUT4(11, v[0], v[1], v[2]); break;
                case 0x3: PUT4(12, (v[0] & 0x3FFF) | ((v[1] & 0x3FFF) << 16), 0, 0); break;
                case 0x4: PUT4(13, (v[0] & 0xFFFF) | (v[1] << 16), (v[2] >> 4) & 0xFFFFFF, ((v[3] >> 4) & 255) | ((u32)!((v[3] >> 15) & 1) << 8)); break;
                case 0x5: PUT4(13, (v[0] & 0xFFFF) | (v[1] << 16), v[2], (u32)!((v[3] >> 15) & 1) << 8); break;
                case 0xA: PUT4(14, (v[3] >> 4) & 255, 0, 0); break;
                case 0xC: PUT4(13, (v[0] & 0xFFFF) | (v[1] << 16), (v[2] >> 4) & 0xFFFFFF, (v[3] >> 4) & 255); break;
                case 0xD: PUT4(13, (v[0] & 0xFFFF) | (v[1] << 16), v[2], 0); break;
                case 0xE: PUT4(1, v[2] & 0xFF, v[0], v[1]); break;
                case 0xF: break;
                default: PUT4(1, rg, v[0], v[1]); break;
                }
            }
        } else {
            for (u32 i = 0; i < nloop * nreg; i++) {
                const u32 *v = d + (q + i / 2) * 4 + (i & 1) * 2;
                u32 rg = (u32)(hi >> ((i % nreg) * 4)) & 15;
                if (rg != 0xE && rg != 0xF) PUT4(1, rg, v[0], v[1]);
            }
            q += len;
        }
        dir_n[0] += 1 + len;
    }
    FLUSH();
#undef PUT4
#undef FLUSH
}

/* ---- native meshes ---- */
static u32 *mbuf; static size_t mcap;
static u32 *mroom(size_t qw)
{
    if (qw * 4 > mcap) { free(mbuf); mcap = qw * 4 + 1024; mbuf = malloc(mcap * 4); }
    memset(mbuf, 0, qw * 16);
    return mbuf;
}
void native_mesh_create(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni)
{
    u32 q6 = (nv * 6 + 3) / 4, qc = (nv + 3) / 4, qi = (ni + 3) / 4, n = 1 + q6 + qc + qi;
    u32 *b = mroom(n);
    b[0] = 4; b[1] = id; b[2] = nv; b[3] = ni;
    memcpy(b + 4, v6, (size_t)nv * 24);
    memcpy(b + 4 + q6 * 4, rgba, (size_t)nv * 4);
    memcpy(b + 4 + (q6 + qc) * 4, idx, (size_t)ni * 4);
    uya_gs_dlist(b, n);
}
void native_mesh_colors(u32 id, const u8 *rgba, u32 nv)
{
    u32 n = 1 + (nv + 3) / 4;
    u32 *b = mroom(n);
    b[0] = 5; b[1] = id; b[2] = nv;
    memcpy(b + 4, rgba, (size_t)nv * 4);
    uya_gs_dlist(b, n);
}
void native_mesh_update(u32 id, const float *v6, const u8 *rgba, u32 nv)
{
    u32 q6 = (nv * 6 + 3) / 4, n = 1 + q6 + (nv + 3) / 4;
    u32 *b = mroom(n);
    b[0] = 8; b[1] = id; b[2] = nv;
    memcpy(b + 4, v6, (size_t)nv * 24);
    memcpy(b + 4 + q6 * 4, rgba, (size_t)nv * 4);
    uya_gs_dlist(b, n);
}
void native_mesh_update_idx(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni)
{
    u32 q6 = (nv * 6 + 3) / 4, qc = (nv + 3) / 4, qi = (ni + 3) / 4, n = 1 + q6 + qc + qi;
    u32 *b = mroom(n);
    b[0] = 9; b[1] = id; b[2] = nv; b[3] = ni;
    memcpy(b + 4, v6, (size_t)nv * 24);
    memcpy(b + 4 + q6 * 4, rgba, (size_t)nv * 4);
    memcpy(b + 4 + (q6 + qc) * 4, idx, (size_t)ni * 4);
    uya_gs_dlist(b, n);
}
void native_mesh_free(u32 id)
{
    u32 b[4] = { 6, id, 0, 0 };
    uya_gs_dlist(b, 1);
}
void native_regs(const u32 *addr, const u64 *val, int n)
{
    u32 *b = mroom((size_t)n);
    for (int i = 0; i < n; i++) { b[i * 4] = 1; b[i * 4 + 1] = addr[i]; b[i * 4 + 2] = (u32)val[i]; b[i * 4 + 3] = (u32)(val[i] >> 32); }
    uya_gs_dlist(b, (u32)n);
}
void native_mesh_draw(u32 id, u32 first, u32 count, const float xf[32])
{
    u32 b[36] = { 7, id, first, count };
    memcpy(b + 4, xf, 32 * 4);
    uya_gs_dlist(b, 9);
}
