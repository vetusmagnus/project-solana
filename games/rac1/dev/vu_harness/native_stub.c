#include "harness.h"
void native_reset(void) {}
int native_run(u32 pc, const RcVu0 *v, u32 top, const u8 *mem) { (void)pc; (void)v; (void)top; (void)mem; return -1; }
int native_compare(const packet_t *k, int nk, int verbose) { (void)k; (void)nk; (void)verbose; return 0; }
