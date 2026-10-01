# Converts the 24-bit BMP dumps the D3D9 backend writes (see D3D9_DUMP_EVERY in d3d9Backend.cpp) to PNG, with
# no dependencies beyond the standard library. Usage: python tools/bmp2png.py <file.bmp> ...
import struct, sys, zlib

def read_bmp24(path):
    d = open(path, 'rb').read()
    offset = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24, "24-bit BMP only"
    top_down = h < 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        row = d[offset + y * stride: offset + y * stride + w * 3]
        rgb = bytearray(w * 3)
        rgb[0::3] = row[2::3]; rgb[1::3] = row[1::3]; rgb[2::3] = row[0::3]
        rows.append(bytes(rgb))
    if not top_down:
        rows.reverse()
    return w, h, rows

def write_png(path, w, h, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(tag, body):
        c = tag + body
        return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)

for path in sys.argv[1:]:
    w, h, rows = read_bmp24(path)
    out = path.rsplit('.', 1)[0] + '.png'
    write_png(out, w, h, rows)
    print(out, w, h)
