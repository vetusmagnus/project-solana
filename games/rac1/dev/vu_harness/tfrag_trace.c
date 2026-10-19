/* harness-only: PC coverage / tracing of the tfrag program (image 55907) */
#include <stdio.h>
#include <stdlib.h>
#include "vu.h"
#include "tfrag_trace.h"
extern int trace_on;
static unsigned cov[0x800];
static void dump(void) { FILE *f = fopen(getenv("TFRAG_COV") ? getenv("TFRAG_COV") : "/dev/null", "w"); if (!f) return; for (int i = 0; i < 0x800; i++) if (cov[i]) fprintf(f, "%04X %u\n", i * 8, cov[i]); fclose(f); }
void tfrag_trace_pc(unsigned pc, RcVu0 *v)
{
    static int init; if (!init) { init = 1; atexit(dump); }
    cov[(pc >> 3) & 0x7FF]++;
    if (trace_on) {
        printf("%04X", pc);
        for (int i = 1; i < 16; i++) printf(" %04X", v->vi[i] & 0xFFFF);
        printf("\n");
    }
}
