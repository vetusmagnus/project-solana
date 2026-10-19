/* Terrain (tfrags) drawn from the game's own data as native meshes (native_terrain.c) */
#ifndef NATIVE_TERRAIN_H
#define NATIVE_TERRAIN_H
#include "uya_host.h"

/* a VIF1 chain is about to be walked (host/src/sdk/dma.c): the level's tfrag table is
 * checked, and its meshes built when it is new */
void native_terrain_chain(void);
/* the chain walker's next tag (a REF or CNT without IRQ, tte: its upper words go to the VIF):
 * 1 if this file took it (a tfrag's draw, kept until its MSCAL decides it) */
int native_terrain_tag(const u32 *tag, int tte);
/* any other tag: the tags kept so far go on to the VIF as they were */
void native_terrain_flush(void);
/* TfragProc done natively (native_tfragproc.c): its draws go straight to this file, not into the
 * chain. can_draw: 1 if tfrag idx of the table at tbl has its meshes for mode (2: the clip pass,
 * only with the GPU renderer). A run of such draws is a segment: seg_begin gives the id the
 * routine puts in the chain's marker (a CNT tag of 0 qwords, VIF NOPs: MARK, id), seg_add adds a
 * draw to it. The chain walker draws the segment where it meets the marker. */
#define NATIVE_TERRAIN_MARK 0x00004D4Bu
int native_terrain_can_draw(u32 tbl, u32 idx, u32 mode);
/* 1 if every tfrag of the table at tbl can be drawn directly (all its levels of detail have meshes) */
int native_terrain_ready(u32 tbl);
/* the frame's constants a direct draw uses (in VU memory for the chain's draws): DrawTfrag's
 * transform (VU 5..8) and the template's 15 qwords (VU 656..670); an id for seg_begin (~0u: VU memory) */
typedef struct { float m[4][4]; u32 c[15][4]; } NativeTerrainConsts;
u32 native_terrain_consts(const NativeTerrainConsts *k);
/* reset: the tfrag program's setup (the template's MSCAL 0) happens before the segment */
u32 native_terrain_seg_begin(u32 kid, int reset);
void native_terrain_seg_add(u32 idx, u32 mode);
/* DrawTfrag's two transform packets (CNT 4; STCYCL 4/4; UNPACK V4-32 4 at VU 5, then at 0x14D) as a
 * record when TfragProc draws directly (native_tfragproc.c): the first packet's qword becomes a NEXT
 * past both (VIF words NOP NATIVE_TXFORM_MARK and the record's index). native_tfrag_xform_tag: 1 the
 * two unpacks done from the chain's own words (go to the NEXT's address), 2 the qword put back (read
 * it again: the packets go through the VIF), 0 not known */
#define NATIVE_TXFORM_MARK 0x00005846u
int native_tfrag_xform_tag(const u32 *q, u32 t);
/* where TfragProc reads the tfrag table's pointer and count, and its templates t4..t7 (16 bytes apart):
 * the terrain's meshes are built from there (the executable's copy has its own, before a level loads) */
void native_terrain_where(u32 tbl_word, u32 cnt_word, u32 t4);
#endif
