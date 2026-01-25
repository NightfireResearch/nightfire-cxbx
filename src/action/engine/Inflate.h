#ifndef INFLATE_H_
#define INFLATE_H_

#include "EDL.h"

void Inflate_huffman(maybeEDLDecompressorState *state);
void Inflate_bitwise(maybeEDLDecompressorState *state);
void Inflate_directcopy(maybeEDLDecompressorState *state);


#endif // INFLATE_H_