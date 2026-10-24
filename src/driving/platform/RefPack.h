#ifndef DRIVING_PLATFORM_REFPACK_H_
#define DRIVING_PLATFORM_REFPACK_H_

// EA's packer ("RefPack", the "xx FB" files inside the .viv archives), as the driving engine has it. See RefPack.cpp.

#include <stdint.h>

unsigned unpacksizez(const uint8_t *packed);
int UNPACK_unpack(const uint8_t *packed, uint8_t *out);
int REFPACK_decode(uint8_t *out, const uint8_t *packed, int *outConsumed);

#endif // DRIVING_PLATFORM_REFPACK_H_
