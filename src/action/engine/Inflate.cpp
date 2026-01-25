#include "inflate.h"

#include <string.h>

// AUTOGEN
void Inflate_huffman(maybeEDLDecompressorState *state);
// AUTOGEN
void Inflate_bitwise(maybeEDLDecompressorState *state);

// AUTOINJECT
void Inflate_directcopy(maybeEDLDecompressorState *state) {

    // The source and destination may overlap - this must be done with memmove for safety
    memmove(state->dst, state->srcData + sizeof(EDLHeader), state->compressedSize);
}