#ifndef HARNESS_H
#define HARNESS_H
#include "vu.h"
#define MAXPK 64
typedef struct { u32 n; u32 d[0x400 * 4]; } packet_t;   /* a kicked GIF packet: n qwords */
/* the native replacement under test (native_*.c) */
void native_reset(void);
/* run the replacement for a microprogram start at pc with the VU state and data memory as the
 * program would find them; return -1 if it does not handle this start, else what it drew is
 * compared by native_compare() against the packets the program kicked */
int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem);
int native_compare(const packet_t *k, int nk, int verbose);
#endif
