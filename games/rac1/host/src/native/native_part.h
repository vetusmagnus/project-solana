/* the particles drawn directly (native_partproc.c) */
#ifndef NATIVE_PART_H
#define NATIVE_PART_H
#include "uya_host.h"
/* PartProc's part of the chain as a record: its first qword becomes a NEXT past the part (qwc 0), its
 * VIF words NOP NATIVE_PART_MARK and the record's index. native_part_tag: 1 drawn (go to the NEXT's
 * address), 2 the qword put back (read it again: the part goes through the VIF), 0 not known */
#define NATIVE_PART_MARK 0x00005450u
int native_part_tag(const u32 *q, u32 t);
#endif
