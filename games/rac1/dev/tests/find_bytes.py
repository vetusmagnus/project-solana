"""find_bytes.py ADDRESS COUNT LEVEL [LEVEL ...]: finds a routine of the executable in level programs.

Takes COUNT instructions from ADDRESS (hex) in the executable and looks for the same code in
each level's program. Fields that change with where code and data sit (jump targets, the upper
halves of lui and of gp/absolute loads and stores) are masked, so a routine relinked at another
address still matches. Prints where it is in each level.

ADDRESS may name a level instead of the executable: L0:2569F8 (MobyProc, which lives in
level code, as level 0 has it).

Example: python3 find_bytes.py 2352C8 64 0 3 13   (TfragProc in levels 0, 3 and 13)
"""
import struct
import sys

from disasm import exe_segments, level_segments  # noqa: E402  (same folder)

MEM = {0x1A, 0x1B, 0x1E, 0x1F, *range(0x20, 0x30), 0x31, 0x36, 0x37, 0x39, 0x3E, 0x3F}


def mask(w):
    op, rs = w >> 26, (w >> 21) & 31
    if op in (2, 3):
        return w & 0xFC000000
    if op == 0x0F or (rs == 28 and op >= 8):
        return w & 0xFFFF0000
    if (op in MEM or op in (0x09, 0x0D, 0x19)) and rs != 29:
        return w & 0xFFFF0000
    return w


def words(segs, a, n):
    for base, data in segs:
        if base <= a and a + 4 * n <= base + len(data):
            return [struct.unpack_from('<I', data, a - base + 4 * i)[0] for i in range(n)]
    sys.exit('address %08X not found' % a)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, n = sys.argv[1], int(sys.argv[2])
    if src.upper().startswith('L') and ':' in src:
        lv, a = src[1:].split(':')
        segs, a = level_segments(int(lv)), int(a, 16)
    else:
        segs, a = exe_segments(), int(src, 16)
    want = [mask(w) for w in words(segs, a, n)]
    for lv in map(int, sys.argv[3:]):
        hits = []
        for base, data in level_segments(lv):
            ws = [mask(w) for w in struct.unpack('<%dI' % (len(data) // 4), data[:len(data) // 4 * 4])]
            for i in range(len(ws) - n + 1):
                if ws[i] == want[0] and ws[i:i + n] == want:
                    hits.append('%08X' % (base + 4 * i))
        print('level %d: %s' % (lv, ', '.join(hits) or 'not found'))


if __name__ == "__main__":
    main()
