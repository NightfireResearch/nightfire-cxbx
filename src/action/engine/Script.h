#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    char unknown[0x28];
} SSTREAM;

typedef struct SCRIPTINFO {
    _MATRIX maybeMatrix;
    char unknown[0xa0-sizeof(_MATRIX)];
    SSTREAM streamDataBuffer[64]; // start at 0xa0, guessed at the size. Nicely fills almost all the space
    char unknown1[0xaab-0xaa0];
    char streamNum; // at 0xaab
} SCRIPTINFO;


static_assert(offsetof(SCRIPTINFO, streamDataBuffer[0]) == 0xa0, "Offset of streamDataBuffer is bad");
static_assert(offsetof(SCRIPTINFO, streamNum) == 0xaab, "Offset of streamNum is bad");

#pragma pack(pop)


void Script_Run(SCRIPTINFO *);
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);

