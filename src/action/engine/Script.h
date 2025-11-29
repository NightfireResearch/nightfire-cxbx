#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    char unknown[0x28];
} SSTREAM;

typedef struct SCRIPTINFO {
    _MATRIX maybeMatrix;
    _MATRIX otherMatrix;
    char unknown[0x94-2*sizeof(_MATRIX)];
    int maybeSomeSleepFrames; // 0x94
    float maybeProgressFrames;
    char unknownpad[0xa0-0x94-8];
    SSTREAM streamDataBuffer[64]; // start at 0xa0, guessed at the size. Nicely fills almost all the space
    char unknown1[0xaa8-0xaa0];
    ushort maybeNumFrames; // at 0xaa8
    char unknown2; // 0xaaa
    char streamNum; // at 0xaab
} SCRIPTINFO;


static_assert(offsetof(SCRIPTINFO, maybeSomeSleepFrames) == 0x94, "Offset of maybeSomeSleepFrames is bad");
static_assert(offsetof(SCRIPTINFO, maybeProgressFrames) == 0x98, "Offset of maybeProgressFrames is bad");
static_assert(offsetof(SCRIPTINFO, streamDataBuffer[0]) == 0xa0, "Offset of streamDataBuffer is bad");
static_assert(offsetof(SCRIPTINFO, maybeNumFrames) == 0xaa8, "Offset of maybeNumFrames is bad");
static_assert(offsetof(SCRIPTINFO, streamNum) == 0xaab, "Offset of streamNum is bad");

#pragma pack(pop)

void Script_Free(SCRIPTINFO *param_1);
void Script_Run(SCRIPTINFO *);
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
SCRIPTINFO* Script_Load(HASHCODE param_1,_VECTOR *transform,_VECTOR *rotation,uint *fileBuf,void*, void* callback, void* maybeCallbackContext);
void Script_Update(SCRIPTINFO *param_1);
bool Script_Play(SCRIPTINFO *param_1, char param_2);
bool Script_IsPlaying(SCRIPTINFO* param_1);