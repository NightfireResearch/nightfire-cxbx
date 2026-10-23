#ifndef INFLATE_H_
#define INFLATE_H_

#include "EDL.h"

void Inflate_huffman(EDLDecompressorState *state);
void Inflate_bitwise(EDLDecompressorState *state);
void Inflate_directcopy(EDLDecompressorState *state);


#endif // INFLATE_H_