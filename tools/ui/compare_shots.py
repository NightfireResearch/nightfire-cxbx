"""Compares the screenshots of two menu replay runs (tools/ui/run_menu.sh), shot by shot - typically one with a
reimplementation and one with ORIGINAL= running the game's own code.

  python tools/ui/compare_shots.py build/menurun/credits_new build/menurun/credits_orig

For each PNG in both runs' menu_shots/: identical, or how many pixels differ and by how much at most, and where
(the bounding box). --diff writes <name>_diff.png into the first run's folder, the differing pixels in white.
"""
import glob
import os
import struct
import sys
import zlib


def read_png(path):
    """(width, height, rows of RGB bytes) for the 8-bit RGB/RGBA PNGs bmp2png.py writes"""
    data = open(path, 'rb').read()
    pos, idat, w = 8, b'', None
    while pos < len(data):
        n, kind = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', body)
            assert depth == 8 and ctype in (2, 6), 'only 8-bit RGB/RGBA'
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3))
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(k, b):
        return struct.pack('>I', len(b)) + k + b + struct.pack('>I', zlib.crc32(k + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    a_dir, b_dir = sys.argv[1], sys.argv[2]
    write_diff = '--diff' in sys.argv
    same = 0
    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(a_dir, 'menu_shots', '*.png'))
                   if not p.endswith('_diff.png'))
    for name in names:
        other = os.path.join(b_dir, 'menu_shots', name)
        if not os.path.exists(other):
            print('%-40s only in %s' % (name, a_dir))
            continue
        w, h, ra = read_png(os.path.join(a_dir, 'menu_shots', name))
        w2, h2, rb = read_png(other)
        if (w, h) != (w2, h2):
            print('%-40s sizes differ: %dx%d / %dx%d' % (name, w, h, w2, h2))
            continue
        count, worst, box, out = 0, 0, None, []
        for y in range(h):
            line = bytearray(w * 3)
            for x in range(w):
                d = max(abs(ra[y][3 * x + k] - rb[y][3 * x + k]) for k in range(3))
                if d:
                    count += 1
                    worst = max(worst, d)
                    box = (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)) if box else (x, y, x, y)
                    line[3 * x:3 * x + 3] = b'\xff\xff\xff'
            out.append(bytes(line))
        if count == 0:
            same += 1
            print('%-40s identical' % name)
        else:
            print('%-40s %d pixels differ (%.2f%%), by up to %d, in %s' % (name, count, 100.0 * count / (w * h), worst, box))
            if write_diff:
                write_png(os.path.join(a_dir, 'menu_shots', name.replace('.png', '_diff.png')), w, h, out)
    print('%d of %d identical' % (same, len(names)))


if __name__ == '__main__':
    main()
