#ifndef BIN_H
#define BIN_H

#include "../actionhelpers.h"

// Readers for packed little-endian data: each returns the value at *cursor and moves the cursor past it.
uchar BIN_GetByte(uchar **cursor);
ushort BIN_GetWord(uchar **cursor);
uint BIN_GetDWord(uchar **cursor);
float BIN_GetFloat(uchar **cursor);

// Bit fields, least significant bit first: *cursor is a bit offset into buf, moved past the field.
void BIN_PushBits(uchar *buf, uint *cursor, uint value, uchar width);
void BIN_PullBits(uchar *buf, uint *cursor, int64_t *out, uchar width);
void BIN_PullBits_U32(uchar *buf, uint *cursor, uint *out, uchar width);
void BIN_PullBits_U8(uchar *buf, uint *cursor, uchar *out, uchar width);
void BIN_PullBits_U16(uchar *buf, uint *cursor, ushort *out, uchar width);

#endif // BIN_H
