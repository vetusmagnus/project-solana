/* Shrubs drawn natively (host/src/native/native_shrub.c) */
#ifndef NATIVE_SHRUB_H
#define NATIVE_SHRUB_H
#include "uya_host.h"
/* one run of the shrub program at pc: 1 if handled; the GS packets it sends are passed to
 * emit(), in order */
int native_shrub_run(u8 *vumem, u32 pc, u32 top, void (*emit)(const u32 *pk, u32 qwc));
/* the shrub clip program (912339), drawn by the GPU renderer as meshes cut at the near plane: a
 * class packet at base (before the program's own start), a group of instances at grp (1: drawn) */
void native_shrub_clip_class(const u8 *vumem, u32 base);
int native_shrub_clip_group(const u8 *vumem, u32 grp, const float c22[4], const float c23[4], const float c29[4], const float c30[4]);
/* ShrubProc done natively (native_shrubproc.c): its main and fading parts' draws come straight
 * here, not through the chain (see native_shrub.c). direct_frame: the chain's constants (VU 0..2)
 * and a signature of the level's shrub data; direct_class: a packet (its table entry) as a slot, or
 * -1 (the frame's draws go into the chain); seg_begin: the id for the chain's marker (a CNT of 0
 * qwords: NOP codes, MARK, id); seg_start, seg_packet, seg_group, seg_inst: the records, in the
 * chain's order (clip: the clip part's, for the shrub clip program). native_shrub_tag: the chain walker's tag (1: a marker, its segment drawn).
 * dec_push (testing, UYA_SHRUBDEC=1): the packet the chain is about to give the program. */
#define NATIVE_SHRUB_MARK 0x00005253u
#define NATIVE_SHRUB_DECMARK 0x00004453u   /* (testing: the next packet's slot, for dec_push) */
void native_shrub_direct_frame(u32 sig, const u32 consts[12]);
int native_shrub_direct_class(u32 p);
u32 native_shrub_seg_begin(void);
void native_shrub_seg_start(void);
void native_shrub_seg_packet(int slot, u32 top, u32 pc, int clip);
void native_shrub_seg_group(u32 at, u32 n, int clip);
void native_shrub_seg_cstart(u32 consts);
void native_shrub_seg_gif(const u32 (*q)[4], u32 n);   /* GS packets built by ShrubProc (billboards): sent as PATH2 */   /* the clip part's constants (6 qwords), the clip program's start */
void native_shrub_seg_inst(u32 at, u32 blk, u32 col, int fading);
int native_shrub_tag(const u32 *tag);
void native_shrub_dec_push(int slot);
#endif
