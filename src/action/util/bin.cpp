#include "bin.h"

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

