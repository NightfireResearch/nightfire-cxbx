# Extracts and identifies the DirectSound effects image the game hands to IDirectSound_DownloadEffectsImage -
# the program for the Xbox's audio DSP that provides, among other things, the I3DL2 reverb the game's
# SetI3DL2Source calls feed. See docs/audio-inventory.md for why it matters: the native audio backend has to
# stand something in for it, and the first question is whether it is Eurocom's own work or stock XDK output,
# because that decides whether there is anything here to reverse-engineer at all.
#
# Usage: python tools/dsp_image_dump.py <default.xbe> [<other.xbe> ...] [-o out.bin]
#
# xboxInitSound passes effectsImage at VA 0x00194840, 0x6168 bytes (see dsndSeam.cpp). Pass Driving.xbe as a
# second argument to compare the two engines' images - that comparison is what shows the DSP code to be a
# shared, toolchain-produced asset rather than something written for this game.
#
# The image does not contain its own length, so the size is taken from the call site for the primary XBE and
# derived from the header for any others.
import struct, sys, os, hashlib

ACTION_IMAGE_VA, ACTION_IMAGE_SIZE = 0x00194840, 0x6168
TAIL_BYTES = 0x170   # constant-length trailer after segment 2 in both images seen

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'XBEH', '%s is not an XBE' % path
    base = struct.unpack_from('<I', d, 0x104)[0]
    nsec = struct.unpack_from('<I', d, 0x11c)[0]
    secptr = struct.unpack_from('<I', d, 0x120)[0] - base
    secs = [struct.unpack_from('<IIIII', d, secptr + i * 0x38)[1:] for i in range(nsec)]
    return d, secs

def file_offset(secs, va):
    for vaddr, vsize, raw, rawsize in secs:
        if vaddr <= va < vaddr + vsize:
            return raw + (va - vaddr)
    return None

def va_at(secs, off):
    for vaddr, vsize, raw, rawsize in secs:
        if raw <= off < raw + rawsize:
            return vaddr + (off - raw)
    return None

# Layout, confirmed against both XBEs (the totals come out exact, and for the action image the total equals
# the 0x6168 the game itself passes):
#
#   0x000  0x800 bytes of zeroes
#   0x800  header: { 0, seg1 length in dwords, seg2 file offset, seg2 length in dwords, 3, 0 }
#   0x818  segment 1 - DSP code
#   ....   segment 2 - at the offset the header gives, which is always 0x818 + seg1 length
#   ....   0x170-byte trailer
#
# Note this corrects CXBX-Reloaded's note on the same structure (reversed from Otogi), which reads the dword
# at 0x808 as a second code-segment size. It is an offset: in both images it equals 0x818 + seg1 length
# exactly, and reading it as a size makes the totals disagree with the real image length.
def parse(data, start, label):
    hdr = struct.unpack_from('<6I', data, start + 0x800)
    seg1_off, seg1_len = 0x818, hdr[1] * 4
    seg2_off, seg2_len = hdr[2], hdr[3] * 4
    total = seg2_off + seg2_len + TAIL_BYTES
    consistent = (seg2_off == seg1_off + seg1_len)
    print('%s' % label)
    print('  header at 0x800   %s' % ' '.join('%08x' % v for v in hdr))
    print('  segment 1 (code)  0x%05x .. 0x%05x  %6d bytes' % (seg1_off, seg1_off + seg1_len, seg1_len))
    print('  segment 2         0x%05x .. 0x%05x  %6d bytes   %s'
          % (seg2_off, seg2_off + seg2_len, seg2_len,
             'starts where segment 1 ends' if consistent else 'INCONSISTENT with segment 1'))
    print('  trailer           0x%05x .. 0x%05x  %6d bytes' % (total - TAIL_BYTES, total, TAIL_BYTES))
    print('  total             %d bytes (0x%x)' % (total, total))
    return seg1_off, seg1_len, seg2_off, seg2_len, total

def main():
    argv = sys.argv[1:]
    out = None
    if '-o' in argv:
        i = argv.index('-o')
        out = argv[i + 1]
        del argv[i:i + 2]

    primary = argv[0]
    d, secs = load(primary)
    start = file_offset(secs, ACTION_IMAGE_VA)
    assert start is not None, 'VA 0x%08x is not in any section of %s' % (ACTION_IMAGE_VA, primary)
    a = d[start:start + ACTION_IMAGE_SIZE]

    print('%s - effects image at VA 0x%08x' % (os.path.basename(primary), ACTION_IMAGE_VA))
    layout = parse(d, start, '  layout')
    print('  the game passes   %d bytes (0x%x)   %s'
          % (ACTION_IMAGE_SIZE, ACTION_IMAGE_SIZE,
             'agrees with the header' if layout[4] == ACTION_IMAGE_SIZE else 'DISAGREES with the header'))
    print('  md5               %s' % hashlib.md5(a).hexdigest())
    print('  sha1              %s' % hashlib.sha1(a).hexdigest())
    if out:
        open(out, 'wb').write(a)
        print('  written to        %s' % out)

    for other in argv[1:]:
        od, osecs = load(other)
        # Locate by searching for the head of the primary's code segment: whatever else differs, both images
        # begin with the same DSP code.
        pos = od.find(a[0x818:0x818 + 1024])
        print()
        if pos < 0:
            print('%s - no image sharing this code prefix' % os.path.basename(other))
            continue
        ostart = pos - 0x818
        print('%s - effects image at VA 0x%08x' % (os.path.basename(other), va_at(osecs, ostart)))
        olayout = parse(od, ostart, '  layout')
        b = od[ostart:ostart + olayout[4]]
        print('  md5               %s' % hashlib.md5(b).hexdigest())

        shared = 0
        limit = min(layout[1], olayout[1])
        while shared < limit and a[0x818 + shared] == b[0x818 + shared]:
            shared += 1
        print('\n  versus %s:' % os.path.basename(primary))
        print('    segment 1 identical for %d of %d bytes%s'
              % (shared, limit, ' (all but the last %d of the shorter one)' % (limit - shared)
                 if shared < limit else ''))
        print('    segment 2 differs from its first byte'
              if b[olayout[2]:olayout[2] + 16] != a[olayout[2]:olayout[2] + 16]
              else '    segment 2 starts identically')
        print('    -> the DSP code is shared between two separately built engines; %s carries %d bytes more'
              % (os.path.basename(primary), layout[1] - olayout[1]))

main()
