"""cmp_frames.py DIR_A DIR_B: compares the .ppm frames two runs dumped, pixel by pixel.

Prints each frame that differs with its count of differing pixels, then a total line:
"frames N differing pixels M". Frames missing from DIR_B are reported, not skipped.
"""
import os
import sys


def pixels(path):
    data = open(path, 'rb').read()
    return data[data.index(b'255\n') + 4:]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a, b = sys.argv[1], sys.argv[2]
    names = sorted(f for f in os.listdir(a) if f.endswith('.ppm'))
    total = missing = 0
    for f in names:
        pb = os.path.join(b, f)
        if not os.path.exists(pb):
            print(f, 'missing in', b)
            missing += 1
            continue
        x, y = pixels(os.path.join(a, f)), pixels(pb)
        if len(x) != len(y):
            print(f, 'different size')
            missing += 1
            continue
        n = sum(1 for i in range(0, len(x), 3) if x[i:i + 3] != y[i:i + 3])
        total += n
        if n:
            print(f, n)
    print('frames', len(names), 'differing pixels', total, ('(%d missing)' % missing) if missing else '')


if __name__ == "__main__":
    main()
