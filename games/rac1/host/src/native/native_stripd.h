/* the strip effect drawn without the VIF (native_stripd.c) */
#ifndef NATIVE_STRIPD_H
#define NATIVE_STRIPD_H
#include "uya_host.h"
/* a strip producer's range of the chain (from ch0 to end; chain_word: the chain pointer's address) as
 * records and one marker, when every item in it has the game's template shape */
void native_stripd_convert(u32 chain_word, u32 ch0, u32 end);
/* the same with a level builder's semantic strips: begin before the producer's run (its result goes to end),
 * sem for each strip it wrote: its chain range, n, the GIF tag and the VU 940 qword as unpacked, where its ST,
 * colour and position data are (grid: a grid cell's order, positions first), its MSCALF's address */
u32 native_stripd_begin(void);
void native_stripd_sem(u32 b0, u32 b1, u32 n, const u32 gif[4], const u32 v940[4], u32 st, u32 col, u32 xyz, int grid, u32 pc);
void native_stripd_end(u32 s0, u32 chain_word, u32 ch0, u32 end);
/* the marker: a CNT of 0 qwords whose VIF words are NOP NATIVE_STRIP_MARK and the run's index; 1 drawn */
#define NATIVE_STRIP_MARK 0x00005453u
int native_strip_tag(const u32 *q);
#endif
