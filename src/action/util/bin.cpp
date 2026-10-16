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
