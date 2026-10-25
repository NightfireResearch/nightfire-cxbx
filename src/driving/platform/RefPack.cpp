#include "RefPack.h"

#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's packer, the format of the packed files inside the .viv archives (FILE_loadpackz, platform/FileSys.cpp, and
// the sound bank loader unpack them). A packed file starts with a type byte and 0xFB; types 0x10 and 0x90 (with
// bit 0 set or not) are RefPack, an LZ77 variant: 3 or 4 byte big-endian sizes, then commands that copy literal
// bytes from the input and runs from earlier in the output. The original dispatches the other types to a decoder
// hook (0x00247600) that nothing installs, so they never unpack; unpacksizez knows the sizes of a few more types
// than the unpacker can decode. Each function is the original at the same address, ported from it.
// ---------------------------------------------------------------------------------------------------------------

// The size the data unpacks to, from its header; 0 if it is not packed (or in a format nothing can unpack).
// AUTOINJECT
unsigned unpacksizez(const uint8_t *packed) {
    if (packed[1] != 0xfb)
        return 0;
    switch (packed[0] & 0xfe) {
    case 0x10: case 0x18: case 0x1a: case 0x30: case 0x32: case 0x34: case 0x46:   // 24-bit size
        return (unsigned)packed[2] << 16 | (unsigned)packed[3] << 8 | packed[4];
    case 0x90: case 0x98: case 0x9a: case 0xb0: case 0xb2: case 0xb4: case 0xc6:   // 32-bit size
        return (unsigned)packed[2] << 24 | (unsigned)packed[3] << 16 | (unsigned)packed[4] << 8 | packed[5];
    }
    return 0;   // 0x1e and 0x9e asked the hook, which is never set
}

// Unpacks into 'out' (unpacksizez bytes); the unpacked size, or 0.
// FUNC_AT(0x0014bff0)
int UNPACK_unpack(const uint8_t *packed, uint8_t *out) {
    if (packed[1] != 0xfb)
        return 0;
    int type = packed[0] & 0xfe;
    if (type == 0x10 || type == 0x90)
        return REFPACK_decode(out, packed, NULL);
    return 0;   // 0x1e and 0x9e went to the hook, which is never set
}

// RefPack. Copies go byte by byte, as runs may overlap what they produce. Answers the unpacked size from the header
// (whatever was produced), and with outConsumed how many input bytes were read.
// FUNC_AT(0x0014c220)
int REFPACK_decode(uint8_t *out, const uint8_t *packed, int *outConsumed) {
    const uint8_t *s = packed;
    int size = 0;
    if (packed != NULL) {
        int type = s[0] << 8 | s[1];
        s += 2;
        if (type & 0x8000) {
            if (type & 0x100)
                s += 4;   // the compressed size, unread
            size = s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3];
            s += 4;
        } else {
            if (type & 0x100)
                s += 3;
            size = s[0] << 16 | s[1] << 8 | s[2];
            s += 3;
        }
        for (;;) {
            int c = *s++;
            int literals, offset, length;
            if (!(c & 0x80)) {                       // 0ooLLLll oooooooo
                int a = *s++;
                literals = c & 3;
                offset = ((c & 0x60) << 3) + a + 1;
                length = ((c >> 2) & 7) + 3;
            } else if (!(c & 0x40)) {                // 10LLLLLL lloooooo oooooooo
                int a = s[0], b = s[1];
                s += 2;
                literals = a >> 6;
                offset = ((a & 0x3f) << 8) + b + 1;
                length = (c & 0x3f) + 4;
            } else if (!(c & 0x20)) {                // 110oLLll oooooooo oooooooo LLLLLLLL
                int a = s[0], b = s[1], d = s[2];
                s += 3;
                literals = c & 3;
                offset = ((c & 0x10) << 12) + (a << 8) + b + 1;
                length = d + ((c & 0x0c) << 6) + 5;
            } else {                                 // 111lllll: literals; above 0x70 the end
                literals = ((c & 0x1f) << 2) + 4;
                if (literals > 0x70) {
                    for (int n = c & 3; n > 0; n--)
                        *out++ = *s++;
                    break;
                }
                for (; literals > 0; literals--)
                    *out++ = *s++;
                continue;
            }
            for (; literals > 0; literals--)
                *out++ = *s++;
            const uint8_t *from = out - offset;
            for (; length > 0; length--)
                *out++ = *from++;
        }
    }
    if (outConsumed != NULL)
        *outConsumed = (int)(s - packed);
    return size;
}
