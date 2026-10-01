#include "bin.h"

// AUTOINJECT
uchar BIN_GetByte(uchar** fstream) {
    uchar value = **fstream;
    *fstream = *fstream + 1;
    return value;
}

// AUTOINJECT
ushort BIN_GetWord(ushort** fstream) {
    ushort value = **fstream;
    *fstream = *fstream + 1;
    return value;
}

// AUTOINJECT
uint BIN_GetDWord(uint** fstream) {
    uint value = **fstream;
    *fstream = *fstream + 1;
    return value;
}

