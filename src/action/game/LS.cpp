// Loading / Saving System

#include "LS.h"

// AUTOGEN
void BIN_PullBits(uchar *data, uint *bitOffset, uint* dataSignOut, uchar numBits);

// Helpers (these are all the same name in C++ land, differentiated only by type in the signature)

// AUTOINJECT
void BIN_PullBits_U32(uchar *data, uint *bitOffset, uint *dataOut, uchar numBits) {
    uint dataAndSign[2] = {0};
    BIN_PullBits(data, bitOffset, dataAndSign, numBits);
    *dataOut = dataAndSign[0];
}

// AUTOINJECT
void BIN_PullBits_U16(uchar *data, uint *bitOffset, ushort *dataOut, uchar numBits) {
    uint dataAndSign[2] = {0};
    BIN_PullBits(data, bitOffset, dataAndSign, numBits);
    *dataOut = dataAndSign[0];
}

// AUTOINJECT
void BIN_PullBits_U8(uchar *data, uint *bitOffset, uchar *dataOut, uchar numBits) {
    uint dataAndSign[2] = {0};
    BIN_PullBits(data, bitOffset, dataAndSign, numBits);
    *dataOut = dataAndSign[0];
}

// AUTOINJECT
void LS_LoadCheats(uchar *data) {
    uint bitOffset = 64;
    BIN_PullBits_U32(data, &bitOffset, &CheatInfo.Immortal, 1);
    BIN_PullBits_U32(data, &bitOffset, &CheatInfo.AllWeapons, 1);
    BIN_PullBits_U32(data, &bitOffset, &CheatInfo.UnlimitedAmmo, 1);
}