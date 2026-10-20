#ifndef SSYS_H_
#define SSYS_H_

#include "../../actionhelpers.h"

// The security system's countdowns: four of them (items 1 to 4; item 0 is never used), each started by its switch
// channel and announced as it runs down. Player_RamSave keeps a copy, which SSys_RamLoad puts back.
#define SSYS_ITEMS 5
#define SSYS_NO_CHANNEL 0xff

#pragma pack(push, 1)
typedef struct {
    float timer;            // 0x00 - seconds left; -1 while not counting
    uchar channel;          // 0x04 - SSYS_NO_CHANNEL for none
    uchar state;            // 0x05 - which message comes next, 6 down to 1; 0 while not counting
    uchar unknown06[2];
} SSYSITEM;
#pragma pack(pop)

static_assert(sizeof(SSYSITEM) == 8, "Bad size for SSYSITEM");

void SSys_Init(void);
void _SSys_Msg(ushort item, ushort msg);
void SSys_Msg(void);
void SSys_Monitor(void);
void SSys_RamLoad(SSYSITEM *saved);

// Each item's seven messages, by state: the text and the sound. Only the sound is played.
typedef struct {
    Action_TranslatedText label;
    uint sound;
} SSYSMSG;
static_assert(sizeof(SSYSMSG) == 8, "SSYSMSG is 8 bytes");

extern SSYSMSG SSysMsgs1[7], SSysMsgs2[7], SSysMsgs3[7], SSysMsgs4[7];

#endif // SSYS_H_
