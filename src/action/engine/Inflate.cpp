#include "inflate.h"

#include <string.h>

// AUTOGEN
void Inflate_huffman(maybeEDLDecompressorState *state);

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