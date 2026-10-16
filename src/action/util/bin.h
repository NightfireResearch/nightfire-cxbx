#ifndef BIN_H
#define BIN_H

#include "../actionhelpers.h"

// Readers for packed little-endian data: each returns the value at *cursor and moves the cursor past it.
uchar BIN_GetByte(uchar **cursor);
ushort BIN_GetWord(uchar **cursor);
uint BIN_GetDWord(uchar **cursor);
float BIN_GetFloat(uchar **cursor);

#endif // BIN_H
