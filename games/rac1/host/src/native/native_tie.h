/* Ties drawn natively (host/src/native/native_tie.c) */
#ifndef NATIVE_TIE_H
#define NATIVE_TIE_H
#include "uya_host.h"
void native_tie_setup(const u8 *vumem);
/* one instance: the GS packet into out (at most 0x400 qwords); returns its length in qwords
 * (0: no class set up) */
u32 native_tie_draw(const u8 *vumem, u32 top, u32 *out);
u32 native_tie_size(void);
/* the alpha test switch the program sends before an instance, when its mode changes (0 or 2 qwords) */
u32 native_tie_state(u8 *mem, u32 top, u32 *out);   /* the current class's packet length in qwords */
/* an instance drawn as its class's native mesh (1), or not (0: send the packet) */
int native_tie_mesh(const u8 *vumem, u32 top);
/* the tie clip program (224979), drawn by the GPU renderer as meshes cut at the frustum: a class
 * packet (before the program's own start 0x30), an instance at VU memory inst (1: drawn) */
void native_tie_clip_class(const u8 *vumem);
int native_tie_clip_mesh(const u8 *vumem, u32 inst);
/* TieProc done natively (native_tieproc.c): a packet's draws go straight to this file, not into
 * the chain. direct_frame: the chain's constants for the frame (VU 966..974, the colours' ROW) and
 * a signature of the level's tie data (a new one drops the packets read so far). direct_class: the
 * packet (its table entry; base: its class's data, +0x2C) as a slot to draw, or -1 (it goes into
 * the chain). A run of such draws is a segment: seg_begin gives the id the routine puts in the
 * chain's marker (a CNT tag of 0 qwords, VIF NOPs: MARK, id), seg_packet and seg_inst add to it.
 * The chain walker (host/src/sdk/dma.c) gives native_tie_tag each tag: 1 if it was a marker
 * (its segment drawn). */
#define NATIVE_TIE_MARK 0x00004954u
void native_tie_direct_frame(u32 sig, const u32 k966[36], const u32 row[4]);
int native_tie_direct_class(u32 p, u32 base);
u32 native_tie_seg_begin(void);
void native_tie_seg_packet(int slot, u32 a3, u32 base);
void native_tie_seg_inst(u32 blk, u32 mtx);
/* the clip list's part, with the GPU renderer: clip_frame (its constants, VU 966..975) is 0 when
 * it cannot go here (it goes into the chain); clip_class as direct_class; clip_inst 1 if an
 * instance with these rows (its block) draws as its mesh; seg_clip_packet as seg_packet */
int native_tie_direct_clip_frame(const u32 k966[40]);
int native_tie_direct_clip_class(u32 p, u32 base);
int native_tie_direct_clip_inst(const u32 *rows);
void native_tie_seg_clip_packet(int slot, u32 a3, u32 base);
int native_tie_tag(const u32 *tag);
#endif
