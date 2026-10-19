/* which qwords of a GIF packet are PACKED ST (their fourth word is not read by the GS) */
static void gif_st_mask(const u32 *d, u32 n, unsigned char *st)
{
    memset(st, 0, n);
    u32 q = 0;
    while (q < n) {
        u64 lo = ((u64)d[q * 4 + 1] << 32) | d[q * 4], hi = ((u64)d[q * 4 + 3] << 32) | d[q * 4 + 2];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        q++;
        if (flg == 0) { for (u32 l = 0; l < nloop; l++) for (u32 r = 0; r < nreg && q < n; r++, q++) if (((hi >> (4 * r)) & 15) == 2) st[q] = 1; }
        else if (flg == 1) q += (nloop * nreg + 1) / 2;
        else q += nloop;
        if (eop) break;
    }
}
