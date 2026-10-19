/* harness-only: PC trace of one record for the moby program (TRACE_REC selects it), or
 * with MOBY_COV=1 the program addresses run over the whole capture */
#include <stdio.h>
#include <stdlib.h>
#include "vu.h"
extern int trace_on;
static unsigned cov[0x800];
static int cov_mode = -1;
static void cov_dump(void) { for (int i = 0; i < 0x800; i++) if (cov[i]) printf("C %04X %u\n", i * 8, cov[i]); }
void moby_trace_pc(u32 pc, RcVu0 *v)
{
    if (cov_mode < 0) { cov_mode = getenv("MOBY_COV") != NULL; if (cov_mode) atexit(cov_dump); }
    if (cov_mode) cov[(pc >> 3) & 0x7FF]++;
    if (!trace_on) return;
    printf("P %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X\n", pc,
        v->vi[1]&0xFFFF, v->vi[2]&0xFFFF, v->vi[3]&0xFFFF, v->vi[4]&0xFFFF, v->vi[5]&0xFFFF, v->vi[6]&0xFFFF,
        v->vi[7]&0xFFFF, v->vi[8]&0xFFFF, v->vi[9]&0xFFFF, v->vi[10]&0xFFFF, v->vi[11]&0xFFFF, v->vi[12]&0xFFFF);
}
