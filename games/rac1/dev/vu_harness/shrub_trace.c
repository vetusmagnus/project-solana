#include <stdio.h>
#include "vu.h"
int trace_on; static u32 last = 0xFFFFFFFF;
void trace_pc(u32 pc, RcVu0 *v)
{
    if (!trace_on) return;
    if (pc == 0x0318 || pc == 0x05B0 || pc == 0x06D8 || pc == 0x0740 || pc == 0x07A0 || pc == 0x0990 || pc == 0x07C8 || pc == 0x0978 || pc == 0x0B48 || pc == 0x0970 || pc == 0x0B40 || (last != 0xFFFFFFFF && pc != last + 8 && 0))
        printf("  @%04X vi1 %04X vi2 %04X vi4 %04X vi5 %04X vi6 %04X vi7 %04X vi8 %04X vi13 %04X vi14 %04X vi15 %04X\n", pc,
               v->vi[1]&0xFFFF, v->vi[2]&0xFFFF, v->vi[4]&0xFFFF, v->vi[5]&0xFFFF, v->vi[6]&0xFFFF, v->vi[7]&0xFFFF, v->vi[8]&0xFFFF, v->vi[13]&0xFFFF, v->vi[14]&0xFFFF, v->vi[15]&0xFFFF);
    last = pc;
}
