"""diffimg.py A.ppm B.ppm OUT.png: B with the pixels that differ from A in magenta, and their bounding box"""
import sys, zlib, struct

def ppm(path):
    d = open(path, 'rb').read()
    parts, i = [], 0
    while len(parts) < 4:
        while d[i:i + 1].isspace(): i += 1
        if d[i:i + 1] == b'#':
            while d[i:i + 1] != b'\n': i += 1
            continue
        j = i
        while not d[j:j + 1].isspace(): j += 1
        parts.append(d[i:j]); i = j
    w, h = int(parts[1]), int(parts[2])
    return w, h, bytearray(d[i + 1:i + 1 + w * h * 3])

w, h, a = ppm(sys.argv[1])
_, _, b = ppm(sys.argv[2])
x0, y0, x1, y1, n = w, h, -1, -1, 0
out = bytearray(b)
for y in range(h):
    for x in range(w):
        k = (y * w + x) * 3
        if a[k:k + 3] != b[k:k + 3]:
            n += 1; out[k:k + 3] = b'\xff\x00\xff'
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
print("differing", n, "box", x0, y0, x1, y1)
raw = b''.join(b'\x00' + bytes(out[y * w * 3:(y + 1) * w * 3]) for y in range(h))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(sys.argv[3], 'wb').write(png)
