#ifndef BIN_H
#define BIN_H

#include "../actionhelpers.h"

ushort BIN_GetWord(ushort** fstream);
uint BIN_GetDWord(uint** fstream);

#endif // BIN_H