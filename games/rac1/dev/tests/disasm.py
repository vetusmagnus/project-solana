"""disasm.py ADDRESS COUNT [LEVEL]: disassembles R5900 (EE) code.

Without LEVEL: from the executable (decomp/baserom/SCES_509.16, or set RAC1_ELF).
With LEVEL (0..18): from that level's program, as build.py extracted it to
build/levels/level_NN/ (manifest.json gives each record's address).

ADDRESS is hex (e.g. 2352C8). Needs rabbitizer (pip install rabbitizer).
Level code sits at different addresses per level: find a routine in another level by its
bytes, not its address (see find_bytes.py).
"""
import json
import os
import struct
import sys

import rabbitizer

R = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def exe_segments():
    path = os.environ.get('RAC1_ELF', os.path.join(R, 'decomp', 'baserom', 'SCES_509.16'))
    d = open(path, 'rb').read()
    phoff = struct.unpack_from('<I', d, 0x1C)[0]
    n = struct.unpack_from('<H', d, 0x2C)[0]
    segs = []
    for i in range(n):
        p = struct.unpack_from('<8I', d, phoff + 32 * i)
        segs.append((p[2], d[p[1]:p[1] + p[4]]))
    return segs


def level_segments(lv):
    d = os.path.join(R, 'build', 'levels', 'level_%02d' % lv)
    m = json.load(open(os.path.join(d, 'manifest.json')))
    return [(r['address'], open(os.path.join(d, r['name'] + '.bin'), 'rb').read())
            for r in m['records'] if r.get('type') != 8]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    a, n = int(sys.argv[1], 16), int(sys.argv[2])
    segs = level_segments(int(sys.argv[3])) if len(sys.argv) > 3 else exe_segments()
    for i in range(n):
        addr = a + 4 * i
        for base, data in segs:
            if base <= addr < base + len(data) - 3:
                w = struct.unpack_from('<I', data, addr - base)[0]
                ins = rabbitizer.Instruction(w, vram=addr, category=rabbitizer.InstrCategory.R5900)
                print('%08X: %08X  %s' % (addr, w, ins.disassemble()))
                break
        else:
            break


if __name__ == "__main__":
    main()
