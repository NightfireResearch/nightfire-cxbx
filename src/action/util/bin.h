#ifndef BIN_H
#define BIN_H

#include "../actionhelpers.h"

uchar BIN_GetByte(uchar** fstream);
ushort BIN_GetWord(ushort** fstream);
uint BIN_GetDWord(uint** fstream);

#endif // BIN_H