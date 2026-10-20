#include "Inflate.h"

#include "../actionhelpers.h"

#include <stddef.h>
#include <string.h>

// ---- Inflate_huffman: blocks of stored bytes, or of literals and matches coded by two canonical Huffman trees
// (literal/length and distance) sent as code lengths. A block may send no tree and use the last one.

// The code tables and the lists they are built from, the game's (they last from one EDL block to the next). An entry
// of a table is a leaf, symbol << 7 | code length, or (length 0) a subtable's place: offset << 7 | index bits << 4.
#pragma pack(push, 1)
typedef struct HuffmanTables {
    uint lengthCounts[33];          // 0x0000 codes of each length; [0] cleared after a tree's lengths are read
    uint pad84;
    ushort literals[1024 + 512];    // 0x0088 a 10-bit root and its subtables
    uchar subtableLengths[1024];    // 0x0c88 the longest code under each root entry
    ushort distances[256 + 512];    // 0x1088 an 8-bit root and its subtables
    // 0x1688 the lengths by symbol, then in code order; the codes, bit-reversed; the symbols in code order. The
    // lengths of a tree of more than 288 symbols run on into the codes, as in the original.
    uint lists[288 + 288 + 512];
} HuffmanTables;
#pragma pack(pop)
static_assert(offsetof(HuffmanTables, lists) == 0x1688, "HuffmanTables layout");
#define HuffTables (*(HuffmanTables *)0x0029b2d0)
#define HuffLengths (HuffTables.lists)
#define HuffCodes (HuffTables.lists + 288)
#define HuffSymbols (HuffTables.lists + 576)

// The length and distance codes' bases and extra bits, the game's tables: indexed as the original does, so a symbol
// past the 29 lengths or the 30 distances reads what follows them
#define LengthBase ((const uchar *)0x0018cd5c - 257)    // [257 + n]: 0, 1, 2 ... 224, 255
#define LengthExtra ((const uchar *)0x0018cd7c - 257)   // [257 + n]: eight 0s, then four each of 1 to 5, and 0
#define DistanceBase ((const ushort *)0x0018cd9c)       // 0, 1, 2, 3, 4, 6 ... 24576
#define DistanceExtra ((const signed char *)0x0018cdd8) // 0, 0, 0, 0, 1, 1 ... 13, 13

#define HUFFMAN_END_OF_BLOCK 0x100
#define HUFFMAN_MIN_MATCH 3
#define HUFFMAN_MAX_LENGTH 15
#define HUFFMAN_ERROR_SUBTABLE (-8)     // a code too long for its root's subtable
#define HUFFMAN_ERROR_TABLE_FULL (-9)   // more subtables than the table has room for

// x86 shifts: the count's low five bits
static inline uint32_t Shl(uint32_t x, int n) { return x << (n & 31); }
static inline uint32_t Shr(uint32_t x, int n) { return x >> (n & 31); }

// The stream, read least significant bit first: the word in hand, the next, and the bits left in hand
typedef struct HuffReader {
    uint32_t bits;
    uint32_t next;
    const uint32_t *ptr;
    int count;
} HuffReader;

static uint32_t Huff_Read(HuffReader *r, int n) {
    uint32_t mask = Shl(1, n) - 1;
    r->count -= n;
    if (r->count >= 0) {
        uint32_t v = r->bits & mask;
        r->bits = Shr(r->bits, n);
        return v;
    }
    uint32_t v = (Shl(r->next, r->count + n) + r->bits) & mask;
    r->bits = Shr(r->next, -r->count);
    r->next = *r->ptr++;
    r->count += 32;
    return v;
}

// A symbol: fifteen bits looked at, the root entry's (or its subtable's) length taken
static uint Huff_Decode(HuffReader *r, const ushort *table, int rootBits) {
    uint32_t peek = r->count < HUFFMAN_MAX_LENGTH ? Shl(r->next, r->count) + r->bits : r->bits;
    uint entry = table[peek & (Shl(1, rootBits) - 1)];
    if ((entry & 0xf) == 0) {
        uint index = Shr(peek, rootBits) & (Shl(1, (entry >> 4) & 7) - 1);
        entry = table[Shl(1, rootBits) + index + (entry >> 7)];
    }
    int length = entry & 0xf;
    r->count -= length;
    if (r->count < 0) {
        r->bits = Shr(r->next, -r->count);
        r->next = *r->ptr++;
        r->count += 32;
    } else {
        r->bits = Shr(r->bits, length);
    }
    return entry >> 7;
}

// A tree of count symbols' code lengths (each a bit: a new 4-bit length follows, else the last one again) into
// table. Returns 0, or an error.
static int Huff_BuildTree(HuffReader *r, uint count, ushort *table, int rootBits, uint *lastLength) {
    HuffmanTables &t = HuffTables;
    memset(&t.lengthCounts[1], 0, 32 * sizeof(uint));
    uint length = *lastLength;
    for (uint i = 0; i < count; i++) {
        if (Huff_Read(r, 1) == 1)
            length = Huff_Read(r, 4);
        HuffLengths[i] = length;
        t.lengthCounts[length]++;
    }
    *lastLength = length;
    t.lengthCounts[0] = 0;

    // The symbols in code order (by length, then symbol), and their lengths in that order
    uint n = 0;
    for (uint l = 1; l <= HUFFMAN_MAX_LENGTH; l++)
        for (uint s = 0; s < count; s++)
            if (HuffLengths[s] == l)
                HuffSymbols[n++] = s;
    uint k = 0;
    for (uint l = 1; l <= HUFFMAN_MAX_LENGTH; l++)
        for (uint c = 0; c < t.lengthCounts[l]; c++)
            HuffLengths[k++] = l;
    HuffLengths[k] = 0;

    // The canonical codes, bit-reversed for the least-significant-first stream
    k = 0;
    uint code = 0;
    for (uint l = HuffLengths[0]; HuffLengths[k] != 0; l++, code <<= 1) {
        for (; HuffLengths[k] == l; k++) {
            uint v = Shl(1, l) | code++;
            uint reversed = 0;
            do {
                reversed = (v & 1) + reversed * 2;
                v >>= 1;
            } while (v != 1);
            HuffCodes[k] = reversed;
        }
    }

    // The root: the codes shorter than it repeated through it; the longest code under each of its other entries
    uint size = Shl(1, rootBits);
    for (uint j = 0; j < size; j++)
        t.subtableLengths[j] = 0;
    uint l = 1;
    k = 0;
    for (; l < (uint)rootBits; l++) {
        for (uint c = 1; c <= t.lengthCounts[l]; c++, k++) {
            for (uint j = HuffCodes[k]; j < size; j += Shl(1, l))
                table[j] = (ushort)((ushort)HuffSymbols[k] << 7) + l;
        }
    }
    uint first = k;
    for (; l <= HUFFMAN_MAX_LENGTH; l++) {
        for (uint c = 0; c < t.lengthCounts[l]; c++, k++) {
            uchar *longest = &t.subtableLengths[HuffCodes[k] & (size - 1)];
            if (HuffLengths[k] > *longest)
                *longest = (uchar)HuffLengths[k];
        }
    }

    // A subtable for each of those entries, as many bits as its longest code has past the root
    uint offset = 0;
    for (uint j = 0; j < size; j++) {
        uchar longest = t.subtableLengths[j];
        if (longest == 0)
            continue;
        uint extra = longest - rootBits;
        if (extra > 7)
            return HUFFMAN_ERROR_SUBTABLE;
        table[j] = (ushort)((longest + offset * 8 - rootBits) << 4);
        offset += Shl(1, extra);
    }
    if (offset > 0x1ff)
        return HUFFMAN_ERROR_TABLE_FULL;

    // The codes as long as the root or longer, into their subtables
    k = first;
    if ((uint)rootBits <= HUFFMAN_MAX_LENGTH) {
        for (l = rootBits; l <= HUFFMAN_MAX_LENGTH; l++) {
            for (uint c = 1; c <= t.lengthCounts[l]; c++, k++) {
                uint rootEntry = table[(size - 1) & HuffCodes[k]];
                ushort *subtable = table + (rootEntry >> 7) + size;
                uint limit = Shl(1, (rootEntry >> 4) & 7);
                uint j = Shr(HuffCodes[k], rootBits);
                do {
                    subtable[j] = (ushort)((ushort)HuffSymbols[k] << 7) + (ushort)HuffLengths[k];
                    j += Shl(1, HuffLengths[k] - rootBits);
                } while (j < limit);
            }
        }
    }
    return 0;
}

// AUTOINJECT
void Inflate_huffman(maybeEDLDecompressorState *state) {
    uchar *out = (uchar *)state->dst;
    // After the EDL header
    const uint32_t *words = (const uint32_t *)(state->src + sizeof(EDLHeader));
    HuffReader r;
    r.bits = words[0];
    r.next = words[1];
    r.ptr = words + 2;
    r.count = 32;
    uint lastLength = 0;
    do {
        if (Huff_Read(&r, 1) == 0) {
            // Stored bytes
            for (uint n = Huff_Read(&r, 15); n != 0; n--)
                *out++ = (uchar)Huff_Read(&r, 8);
            continue;
        }
        // The literal/length tree, then the distance tree, each kept when no lengths are sent
        for (int tree = 1; tree >= 0; tree--) {
            ushort *table = tree ? HuffTables.literals : HuffTables.distances;
            int rootBits = tree ? 10 : 8;
            uint count = Huff_Read(&r, 9);
            if (count == 0)
                continue;
            int error = Huff_BuildTree(&r, count, table, rootBits, &lastLength);
            if (error != 0) {
                state->errNum = error;
                return;
            }
        }
        for (;;) {
            uint symbol = Huff_Decode(&r, HuffTables.literals, 10);
            if (symbol < HUFFMAN_END_OF_BLOCK) {
                *out++ = (uchar)symbol;
                continue;
            }
            if (symbol == HUFFMAN_END_OF_BLOCK)
                break;
            uint extra = LengthExtra[symbol];
            uint lengthBits = extra != 0 ? Huff_Read(&r, extra) : 0;
            uint length = LengthBase[symbol] + lengthBits + HUFFMAN_MIN_MATCH;
            uint d = Huff_Decode(&r, HuffTables.distances, 8);
            int distanceExtra = DistanceExtra[d];
            uint distanceBits = distanceExtra != 0 ? Huff_Read(&r, distanceExtra) : 0;
            uint distance = DistanceBase[d] + distanceBits;
            const uchar *from = out - distance - 1;
            for (; length != 0; length--)
                *out++ = *from++;
        }
    } while (Huff_Read(&r, 1) == 0);
}

typedef struct {
    const uint32_t *ptr;
    uint32_t        bitbuf;
    int             bits;   // number of valid bits in bitbuf
} BitReader;

static inline void br_init(BitReader *br, const uint32_t *src)
{
    br->ptr   = src;
    br->bitbuf = *br->ptr++;
    br->bits   = 32;
}

static inline uint32_t br_get(BitReader *br, int n)
{
    uint32_t v = 0;
    int shift = 0;

    while (n > 0) {
        if (br->bits == 0) {
            br->bitbuf = *br->ptr++;
            br->bits = 32;
        }

        int take = n < br->bits ? n : br->bits;
        v |= (br->bitbuf & ((1u << take) - 1)) << shift;

        br->bitbuf >>= take;
        br->bits   -= take;
        shift      += take;
        n          -= take;
    }

    return v;
}

// AUTOINJECT
void Inflate_bitwise(maybeEDLDecompressorState *st) {
    BitReader br;
    uint8_t *out = (uint8_t*)(st->dst);

    br_init(&br, (uint32_t*)(st->src) + 5); // FIXME: What are those 5 words - a struct we know? EDL header is too small?

    while(true) {
        // Run of literals
        while (br_get(&br, 1) == 0) {
            *out++ = (uint8_t)br_get(&br, 8);
        }

        // Decode match length
        int len;
        if (br_get(&br, 1) == 0) {
            len = 2 + br_get(&br, 1);
        } else {
            int x = br_get(&br, 2);
            if (x == 3) {
                int n = br_get(&br, 4);
                for (int i = 0; i < n; i++)
                    *out++ = (uint8_t)br_get(&br, 8);
                continue;
            }
            len = x + 3;
        }

        // Decode distance
        int dist_lo = br_get(&br, 8);
        int dist_hi = 0;

        if (br_get(&br, 1)) {
            if (br_get(&br, 1)) {
                dist_hi = 2 + br_get(&br, 2);
            } else {
                dist_hi = br_get(&br, 1);
            }
        }

        int dist = (dist_hi << 8) | dist_lo;
        if (dist == 0)
            return; // end-of-stream marker

        dist++;

        // Copy match
        uint8_t *src = out - dist;
        for (int i = 0; i < len; i++)
            *out++ = *src++;
    }
}


// AUTOINJECT
void Inflate_directcopy(maybeEDLDecompressorState *state) {

    // The source and destination may overlap - this must be done with memmove for safety
    memmove(state->dst, state->src + sizeof(EDLHeader), state->compressedSize);
}