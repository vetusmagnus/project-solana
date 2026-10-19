#include <stdio.h>
#include "vu.h"
int trace_on;
void trace_pc(u32 pc, RcVu0 *v)
{
    if (!trace_on) return;
    if (pc == 0x0478 || pc == 0x10C8 || pc == 0x1E70 || pc == 0x06C8 || pc == 0x0C68 || pc == 0x0C78)
        printf("  @%04X vi1..14 %04X %04X %04X %04X %04X %04X %04X %04X %04X\n", pc,
               v->vi[1]&0xFFFF, v->vi[2]&0xFFFF, v->vi[3]&0xFFFF, v->vi[4]&0xFFFF, v->vi[5]&0xFFFF, v->vi[6]&0xFFFF, v->vi[7]&0xFFFF, v->vi[8]&0xFFFF, v->vi[9]&0xFFFF), printf("      vi10..14 %04X %04X %04X %04X %04X\n", v->vi[10]&0xFFFF, v->vi[11]&0xFFFF, v->vi[12]&0xFFFF, v->vi[13]&0xFFFF, v->vi[14]&0xFFFF);
}
