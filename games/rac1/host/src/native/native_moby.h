/* Mobys drawn natively (host/src/native/native_moby.c) */
#ifndef NATIVE_MOBY_H
#define NATIVE_MOBY_H
#include "uya_host.h"
#define NATIVE_MOBY_MAXPK 64
typedef struct {
    u32 npk;                          /* GIF packets kicked, in order */
    u32 len[NATIVE_MOBY_MAXPK];       /* each one's length in qwords */
    u32 off[NATIVE_MOBY_MAXPK];       /* where it starts in d[] (qwords) */
    u32 nq;                           /* qwords used in d[] */
    u32 d[NATIVE_MOBY_MAXPK * 0x100 * 4];
} NativeMobyKicks;
/* one start of the moby microprogram (VU1 image 13859) at pc (0x0000 setup, 0x0050 / 0x0070
 * draw one packet, with / without clipping), with VU1 data memory vumem (16 KB, read and
 * written as the program does) and the VIF TOP; the GIF packets it kicks go to *k.
 * Returns 0, or -1 for a start it does not know (nothing done). */
int native_moby_run(u32 pc, u8 *vumem, u32 top, NativeMobyKicks *k);
void native_moby_reset(void);
/* the VU registers the program carries from one run to the next, for a test that starts from
 * a captured VU state: vf27 (the output buffers), vf12.w (a word the GS never reads) */
void native_moby_seed(const u32 vf27[4], u32 vf12w);

/* ---- mobys straight from the native MobyProc's records (native_mobyseg.c) ----
 * MobyProc (native_mobyproc.c) still writes its whole chain; beside it, at the sites where it writes
 * each item, it records the draws (the records below, in the chain's order). At its end each run
 * (the stretch of the chain only the moby program takes) gets a marker over its first qword: a NEXT
 * of 0 qwords to the run's end, its VIF words NOPs: NATIVE_MOBY_MARK and the segment's serial (24
 * bits). Check mode instead puts NATIVE_MOBY_CHKMARK << 16 | serial (16 bits) in the address word of
 * the run's first qword, a CNT (whose address nothing reads), and the chain draws as before. */
#define NATIVE_MOBY_MARK 0x00004D42u
#define NATIVE_MOBY_CHKMARK 0x4D44u
/* UYA_NMOBYDIRECT: 0 off (the default), 1 check, 2 direct; 0 when UYA_NATIVE=0 */
int native_mobyseg_mode(void);
/* MobyProc's side. call_begin: a call's records start; begin: a run starting at chain address start
 * (its serial); close: the open run ends at chain address end (where the CNT+FLUSHA after it goes).
 * The records, in the chain's order:
 *   mscal  - an MSCAL (pc: 0x0000, 0x0050, 0x0070);
 *   setup  - the chain start's REF to the program's setup block (ref, 4 qwords) and its two
 *            STCYCL 4/4 + UNPACK V4-32 x6 (absolute 0x123 and 0x287) from chain addresses c0, c1;
 *   mtx    - STCYCL 4/4 + UNPACK V4-32 x4 (absolute vuaddr: 0x129 / 0x28D) from chain address rows;
 *   pkt    - a packet's hole (chain address hole) with its counts word (byte 1 position qwords,
 *            byte 2 colour qwords, byte 3 vertices) and its class packet (REF: cls, qwc); extra: the
 *            extra pass's (ST, positions, colours). 0: not a shape the drawer knows (the call fails);
 *   gif    - a CNT + DIRECT n (GS state): its n qwords at chain address at, sent as PATH2.
 * call_end(ok, mode): ok 0 (or anything that failed): the call's records dropped, its chain draws as it
 * is; else each run with records gets its marker (mode 2: its first qword becomes the marker, the
 * original kept, to be put back if the run must go through the chain after all; mode 1: the check
 * marker). Returns the markers written. */
void native_mobyseg_call_begin(void);
int native_mobyseg_call_end(int ok, int mode);
u32 native_mobyseg_begin(u32 start);
void native_mobyseg_close(u32 end);
void native_mobyseg_mscal(u32 pc);
void native_mobyseg_setup(u32 ref, u32 c0, u32 c1);
void native_mobyseg_mtx(u32 rows, u32 vuaddr);
int native_mobyseg_pkt(int extra, u32 hole, u32 counts, u32 cls, u32 qwc);
void native_mobyseg_gif(u32 at, u32 n);
/* the program's setup block as the drawer knows it (MobyProc checks it; the drawer sets the state
 * it leaves): STCYCL 4/4, STMASK 0x20202020, STCOL 0x1000 x4, BASE 0, OFFSET 0x164, STMOD 0, STROW 0 */
extern const u32 native_moby_setup_words[16];
/* the chain walker's side (host/src/sdk/dma.c): a marker q at chain address t (tte: CHCR TTE).
 * 1: its run drawn (the walk goes on at the marker's address); 2: the run's first qword put back
 * (the walk reads it again: the run goes through the chain); 0: not a marker known here (the NEXT
 * skips the run) */
int native_moby_tag(const u32 *q, u32 t, int tte);
/* check mode: a run's first CNT with a check marker (before its VIF words reach the VIF), and every
 * start of the moby program from the chain (host/src/gfx/vu.c, before it runs) */
void native_mobyseg_chk_begin(u32 serial, u32 t);
void native_mobyseg_chk(u32 pc, const u8 *vumem, u32 top);
/* host/src/gfx/vu.c: the moby program (native_moby_run, its GS packets as "PATH1 native moby") at pc
 * on vumem with TOP top: 1, or 0 (no native code for pc); the image VU1 micro memory has for pc is
 * name */
int uya_moby_vu1_run(u8 *vumem, u32 pc, u32 top);
int uya_vu1_image_is(u32 pc, const char *name);
/* host/src/gfx/vif.c: VIF1's state, read at a run's start and set at its end to what the run's
 * left-out codes would have left (nothing else: no VIF code is decoded through this). need: the
 * data words a command still waits for (read only; set leaves the pending command alone).
 * uya_vif1_direct_n (testing, check mode): VIF1 DIRECT/DIRECTHL commands so far */
typedef struct { u32 cl, wl, mode, mask, row[4], col[4], base, ofst, tops, top, itops, itop, dbf, mark, need; } UyaVifState;
void uya_vif1_get_state(UyaVifState *s);
void uya_vif1_set_state(const UyaVifState *s);
/* VIF1 operations for records (gfx/vif.c): a register set by its code word (STCYCL, OFFSET, BASE, ITOP,
 * STMOD), STROW, an UNPACK of data by its code word, a program's start (MSCAL) */
void uya_vif1_op_reg(u32 code);
void uya_vif1_op_strow(const u32 row[4]);
void uya_vif1_op_stcol(const u32 col[4]);
void uya_vif1_op_stmask(u32 mask);
void uya_vif1_op_unpack(u32 code, const u32 *d, u32 nwords);
void uya_vif1_op_mscal(u32 pc);
int uya_vif1_op_mpg_packet(const u32 *w, u32 qwc);   /* a microprogram packet (FLUSH*, NOP, MPG codes); 0 if other codes */
extern unsigned long long uya_vif1_direct_n;
#endif
