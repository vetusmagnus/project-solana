/* the game's 2D producers as host primitives (native_2d.c) */
#ifndef NATIVE_2D_H
#define NATIVE_2D_H
#include "uya_host.h"
/* a 2D producer's packet as a record: its first qword a NEXT past the packet, VIF words NOP
 * NATIVE_2D_MARK and the record's index. native_2d_tag: 1 drawn (go to the NEXT's address), 2 the
 * qword put back (read it again: the packet goes as GIF data), 0 not known */
#define NATIVE_2D_MARK 0x00003244u
int native_2d_tag(const u32 *q, u32 t);
#endif
