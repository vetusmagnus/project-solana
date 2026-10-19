/* harness-only (tfrag, image 55907): like harness.c, but also
 *  - compares the VU memory the native code leaves with the program's (all 16 KB but the output
 *    buffers' ST fourth words), and the carried registers (vf3, vf4) afterwards;
 *  - replays each run from other starts the capture does not use (0x20/0x60 on plain runs,
 *    0x90 on 0x80 runs, 0x10/0xB0/0xC0 on every run), program against native.
 * build: zig cc ... tfrag_harness.c trace.c prog_55907.c vup_55907_vu1.c -o tfrag_check (it includes native_tfrag.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "vu.h"
#include "harness.h"
#include "native_tfrag.c"
static int adc_count;

RcVu0 rc_vu1;
u32 vu_nonfinite;
static u32 cur_top, cur_itop;
u32 vu_vif_top(int vu) { (void)vu; return cur_top; }
u32 vu_vif_itop(int vu) { (void)vu; return cur_itop; }
void vu_bad(int vu, u32 pc, const char *what) { fprintf(stderr, "vu_bad %d %04X %s\n", vu, pc, what); }
static u32 kick_pending = 0xFFFFFFFFu;
packet_t got[MAXPK]; int ngot;
static void kick_send(u32 qaddr)
{
    const u32 *mem = (const u32 *)(uintptr_t)0x1100C000u;
    u32 q = qaddr & 0x3FF, n = 0;
    packet_t *p = &got[ngot < MAXPK ? ngot++ : MAXPK - 1];
    while (n < 0x400) {
        const u32 *t = mem + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        for (u32 k = 0; k < len && n < 0x400; k++) { memcpy(p->d + n * 4, mem + ((q + k) & 0x3FF) * 4, 16); n++; }
        q = (q + len) & 0x3FF;
        if (eop) break;
    }
    p->n = n;
}
void vu_xgkick(u32 qaddr) { if (kick_pending != 0xFFFFFFFFu) kick_send(kick_pending); kick_pending = qaddr & 0x3FF; }
static void kick_flush(void) { if (kick_pending != 0xFFFFFFFFu) { u32 q = kick_pending; kick_pending = 0xFFFFFFFFu; kick_send(q); } }
void prog_run(RcVu0 *v, u32 pc);
extern int trace_on;
int trace_on;

static int read_state(FILE *f, RcVu0 *v, u8 *mem, u32 *tops) { return fread(v, sizeof *v, 1, f) == 1 && fread(mem, 1, 0x4000, f) == 0x4000 && fread(tops, 4, 2, f) == 2; }
static u8 *VM = (u8 *)0x1100C000u;
static u8 pmem[0x4000];
static u32 nout[0x400 * MAXPK * 4];
static NativeTfragPacket npkt[MAXPK];

static void st_slots(const u32 *d, u32 n, u8 *isst)
{
    memset(isst, 0, n);
    for (u32 q = 0; q < n;) {
        const u32 *t = d + q * 4;
        u64 lo = ((u64)t[1] << 32) | t[0], regs = ((u64)t[3] << 32) | t[2];
        u32 nloop = lo & 0x7FFF, eop = (lo >> 15) & 1, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        u32 len = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        if (flg == 0) for (u32 k = 0; k < len && q + 1 + k < n; k++) { u32 rg = (regs >> 4 * (k % nreg)) & 15; if (rg == 2) isst[q + 1 + k] = 1; if (rg == 4) isst[q + 1 + k] = 2; }
        q += 1 + len;
        if (eop) break;
    }
}

static int stw_diffs, stw_pc[256];
/* program vs native from one state; 1 if identical (packets, memory, carried registers) */
static int one(u32 pc, const RcVu0 *before, const u8 *mb, u32 top, u32 itop, int rec, int verbose)
{
    static RcVu0 v;
    memcpy(VM, mb, 0x4000); v = *before; cur_top = top; cur_itop = itop; ngot = 0; kick_pending = 0xFFFFFFFFu;
    prog_run(&v, pc); kick_flush();
    memcpy(pmem, VM, 0x4000);
    /* native from the program's registers */
    memcpy(st.buf, before->vf[3], 16); memcpy(st.cscale, before->vf[4], 16);
    memcpy(VM, mb, 0x4000);
    int n = native_tfrag_run(pc, VM, top, nout, sizeof nout / 16, npkt, MAXPK);
    int ok = 1;
    if (n != ngot) { if (verbose) fprintf(stderr, "rec %d pc %02X: kicks %d vs %d\n", rec, pc, n, ngot); return 0; }
    static u8 isst[0x400];
    for (int i = 0; i < n; i++) {
        if (npkt[i].n != got[i].n) { ok = 0; if (verbose) fprintf(stderr, "rec %d pc %02X kick %d: %u vs %u\n", rec, pc, i, npkt[i].n, got[i].n); continue; }
        st_slots(got[i].d, got[i].n, isst);
        for (u32 q = 0; q < got[i].n; q++) if (isst[q] == 2 && (got[i].d[q * 4 + 3] & 0x8000)) adc_count++;
        for (u32 q = 0; q < got[i].n; q++) for (int w = 0; w < 4; w++) {
            u32 a = nout[(npkt[i].off + q) * 4 + w], b = got[i].d[q * 4 + w];
            if (a == b) continue;
            if (w == 3 && isst[q] == 1) { stw_diffs++; stw_pc[pc & 0xFF]++; continue; }
            if (ok && verbose) fprintf(stderr, "rec %d pc %02X kick %d qw %u w %d: %08X vs %08X\n", rec, pc, i, q, w, a, b);
            ok = 0;
        }
    }
    /* memory: everything, but output-buffer qwords whose only difference is word 3 */
    int memdiff = 0;
    for (u32 q = 0; q < 0x400; q++) for (int w = 0; w < 4; w++) {
        u32 a = ((u32 *)VM)[q * 4 + w], b = ((u32 *)pmem)[q * 4 + w];
        if (a == b) continue;
        if (q >= 672 && w == 3) continue;
        if (!memdiff && verbose) fprintf(stderr, "rec %d pc %02X mem qw %u w %d: %08X vs %08X\n", rec, pc, q, w, a, b);
        memdiff++;
    }
    if (memdiff) ok = 0;
    if (memcmp(st.buf, v.vf[3], 16) || memcmp(st.cscale, v.vf[4], 16)) { ok = 0; if (verbose) fprintf(stderr, "rec %d pc %02X: carried registers differ\n", rec, pc); }
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    int verbose = argc > 2 ? atoi(argv[2]) : 0;
    __builtin_ia32_ldmxcsr((__builtin_ia32_stmxcsr() & ~0x6000u) | 0x6000u);
    if (mmap((void *)0x1100C000u, 0x4000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != (void *)0x1100C000u) { perror("mmap"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static RcVu0 before, after; static u8 mb[0x4000], ma[0x4000]; static u32 tops[2], t2[2]; static packet_t kk[MAXPK];
    u32 pc; char magic[4]; int nrec = 0;
    int tried[256] = { 0 }, okc[256] = { 0 };
    static int synth_tried[256], synth_ok[256];
    while (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "VCAP", 4)) {
        int nk;
        if (fread(&pc, 4, 1, f) != 1 || !read_state(f, &before, mb, tops) || fread(&nk, 4, 1, f) != 1) break;
        for (int i = 0; i < nk; i++) { packet_t *p = &kk[i < MAXPK ? i : MAXPK - 1]; if (fread(&p->n, 4, 1, f) != 1 || fread(p->d, 16, p->n, f) != p->n) return 1; }
        if (!read_state(f, &after, ma, t2)) break;
        int rec = nrec++;
        u32 starts[8]; int ns = 0;
        starts[ns++] = pc;
        if (pc == 0x30 || pc == 0xA0) { starts[ns++] = 0x20; starts[ns++] = 0x60; }
        if (pc == 0x80) starts[ns++] = 0x90;
        if (pc != 0x00) { starts[ns++] = 0x10; starts[ns++] = 0xB0; starts[ns++] = 0xC0; }
        for (int s = 0; s < ns; s++) { tried[starts[s]]++; okc[starts[s]] += one(starts[s], &before, mb, tops[0], tops[1], rec, verbose); }
        /* synthetic: a smaller clip box (662 scaled up: more vertices outside, ADC set), an
         * inverted fog clamp (656 y > z), on the clipped starts */
        static u8 mm[0x4000];
        u32 cs = pc == 0x80 ? 0x90 : (pc == 0x30 || pc == 0xA0) ? 0x30 : 0;
        if (cs) for (int t = 0; t < 4; t++) {
            memcpy(mm, mb, 0x4000);
            float *c = (float *)(mm + 662 * 16), *g = (float *)(mm + 656 * 16);
            static const float sc[4] = { 1.5f, 3.0f, 8.0f, 40.0f };
            for (int k = 0; k < 3; k++) c[k] *= sc[t];
            if (t == 1) { float y = g[1]; g[1] = g[2]; g[2] = y; }
            int id = 0x100 + t * 0x10 + (cs >> 4);
            synth_tried[id & 0xFF]++; synth_ok[id & 0xFF] += one(cs, &before, mm, tops[0], tops[1], rec, verbose);
        }
    }
    for (int p = 0; p < 256; p++) if (tried[p]) printf("start %02X: %d runs, identical %d\n", p, tried[p], okc[p]);
    for (int p = 0; p < 256; p++) if (synth_tried[p]) printf("synthetic clip test %d, start %X0: %d runs, identical %d\n", p >> 4, p & 15, synth_tried[p], synth_ok[p]);
    printf("(ADC set on %d vertices)\n", adc_count);
    printf("(ST 4th-word differences ignored: %d:", stw_diffs);
    for (int p = 0; p < 256; p++) if (stw_pc[p]) printf(" start %02X %d", p, stw_pc[p]);
    printf(")\n");
    return 0;
}
