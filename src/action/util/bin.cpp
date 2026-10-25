#include "bin.h"

#include <string.h>

// AUTOINJECT
uchar BIN_GetByte(uchar **cursor) {
    uchar value = **cursor;
    *cursor += sizeof(value);
    return value;
}

// AUTOINJECT
ushort BIN_GetWord(uchar **cursor) {
    ushort value = *(ushort *)*cursor;
    *cursor += sizeof(value);
    return value;
}

// AUTOINJECT
uint BIN_GetDWord(uchar **cursor) {
    uint value = *(uint *)*cursor;
    *cursor += sizeof(value);
    return value;
}

// The original assembles the float a byte at a time, so the data need not be aligned.
// AUTOINJECT
float BIN_GetFloat(uchar **cursor) {
    float value;
    memcpy(&value, *cursor, sizeof(value));
    *cursor += sizeof(value);
    return value;
}

// Bit fields, packed least significant bit first from bit *cursor of buf (the save blocks, LoadSave.cpp).

// Writes the low 'width' bits of value, setting or clearing each, and moves the cursor past them.
// AUTOINJECT
void BIN_PushBits(uchar *buf, uint *cursor, uint value, uchar width) {
    uint start = *cursor;
    uchar *byte = buf + start / 8;
    uint bit = start % 8;
    for (ushort i = 0; i < width; i++) {
        *byte = (*byte & ~(1 << bit)) | ((value >> i) & 1) << bit;
        if (++bit > 7) {
            byte++;
            bit = 0;
        }
    }
    *cursor = start + width;
}

// Reads 'width' bits into *out and moves the cursor past them. Each bit is gathered as a signed 32-bit word, so a
// set bit 31 also fills the high half.
// AUTOINJECT
void BIN_PullBits(uchar *buf, uint *cursor, int64_t *out, uchar width) {
    uint start = *cursor;
    const uchar *byte = buf + start / 8;
    uint bit = start % 8;
    int64_t value = 0;
    for (ushort i = 0; i < width; i++) {
        value |= (int32_t)((uint)((*byte >> bit) & 1) << i);
        if (++bit > 7) {
            byte++;
            bit = 0;
        }
    }
    *cursor = start + width;
    *out = value;
}

// AUTOINJECT
void BIN_PullBits_U32(uchar *buf, uint *cursor, uint *out, uchar width) {
    int64_t value;
    BIN_PullBits(buf, cursor, &value, width);
    *out = (uint)value;
}

// AUTOINJECT
void BIN_PullBits_U8(uchar *buf, uint *cursor, uchar *out, uchar width) {
    int64_t value;
    BIN_PullBits(buf, cursor, &value, width);
    *out = (uchar)value;
}

// AUTOINJECT
void BIN_PullBits_U16(uchar *buf, uint *cursor, ushort *out, uchar width) {
    int64_t value;
    BIN_PullBits(buf, cursor, &value, width);
    *out = (ushort)value;
}
