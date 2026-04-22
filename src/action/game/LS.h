#ifndef LS_H_
#define LS_H_

#include "../actionhelpers.h"

void LS_LoadCheats(uchar *data);


void BIN_PullBits_U32(uchar *data, uint *bitOffset, uint *dataOut, uchar numBits);
void BIN_PullBits_U16(uchar *data, uint *bitOffset, ushort *dataOut, uchar numBits);
void BIN_PullBits_U8(uchar *data, uint *bitOffset, uchar *dataOut, uchar numBits);

#endif // LS_H_