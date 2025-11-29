#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    char unknown[0x28];
} SSTREAM;

typedef struct SCRIPTINFO {
    _MATRIX maybeMatrix;
    _MATRIX otherMatrix;
    char unknown[0x94-2*sizeof(_MATRIX)];
    int maybeSomeSleepFrames;
    char unknownpad[0xa0-0x94-4];
    SSTREAM streamDataBuffer[64]; // start at 0xa0, guessed at the size. Nicely fills almost all the space
    char unknown1[0xaab-0xaa0];
    char streamNum; // at 0xaab
} SCRIPTINFO;


static_assert(offsetof(SCRIPTINFO, maybeSomeSleepFrames) == 0x94, "Offset of maybeSomeSleepFrames is bad");
static_assert(offsetof(SCRIPTINFO, streamDataBuffer[0]) == 0xa0, "Offset of streamDataBuffer is bad");
static_assert(offsetof(SCRIPTINFO, streamNum) == 0xaab, "Offset of streamNum is bad");

#pragma pack(pop)

void Script_Free(SCRIPTINFO *param_1);
void Script_Run(SCRIPTINFO *);
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
SCRIPTINFO* Script_Load(HASHCODE param_1,_VECTOR *transform,_VECTOR *rotation,uint *fileBuf,void*, void*, void*);
void Script_Update(SCRIPTINFO *param_1);
bool Script_Play(SCRIPTINFO *param_1, char param_2);
bool Script_IsPlaying(SCRIPTINFO* param_1);