#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct AnimObj {
    char pad[0x30];
    celglist_tag *maybeSleeveGlist;
    // ...
} AnimObj;

typedef struct AnimState {
    char _pad_1[0x50];
    char animFlags; // bit one when weapon is zoomed in
    char _pad_2;
    char currentWeaponId; // 0x52
    char switchingToWeaponId; // 0x53
    char prevHeldWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj animObj; // 0x58 - ???? - size not known
} AnimState;

// A per-skin "datum" record, left in the loaded skin file and pointed at by sAnimSkin_tag.datums (0x24 bytes each)
typedef struct AnimDatum {
    int id;
    int bone;
    _VECTOR t;
    quaternion_tag q;
} AnimDatum;

// Built by AnimProcessSkinData from a 0x05xxxxxx skin file; the hashtable entry for the file points at this
// afterwards. The pointers point back into the file data, which stays loaded.
// Names from the Xbox Ghidra database (PS2 names in brackets; the PS2 struct is 0x40 bytes and laid out differently).
typedef struct sAnimSkin_tag {
    HASHCODE hashcode;          // 0x00 never written by AnimProcessSkinData
    celglist_tag **riggedBody;  // 0x04 numRigged entries (riggedBodyList)
    celglist_tag **discreteObj; // 0x08 numDiscrete entries (discreteObjList)
    uchar *discreteBone;        // 0x0c numDiscrete bytes (glistToBoneLinkage)
    _VECTOR skinScale;          // 0x10
    AnimDatum *datums;          // 0x1c numDatums entries (someBufferWithQuatsAndVec3s36bytes)
    uchar *bones;               // 0x20 one byte per skeleton bone (boneHierarchy)
    _MATRIX *invBind;           // 0x24 one per skeleton bone, only when numRigged != 0 (boneMatrixBuffer)
    uchar numRigged;            // 0x28 (numRiggedBodies)
    uchar numDiscrete;          // 0x29 (numDiscreteObjs)
    uchar numMorphs;            // 0x2a (maybeNumMorphs)
    uchar numDatums;            // 0x2b (numSomeDataType36bytes)
    uchar skeletonNum;          // 0x2c index into SkeletonTable
    uchar _pad[3];              // 0x2d the allocation is 0x30 bytes
} sAnimSkin_tag;

// these constants seem to be the same on both PS2 and Xbox
typedef enum {
    ANIM_SCRIPT_STATE_MAYBE_INITIALISED = 0x159f5ab2,
    ANIM_SCRIPT_STATE_MAYBE_DELETED = 0x983c4837,
} AnimScriptStateMagicNumbers;
static_assert(sizeof(AnimScriptStateMagicNumbers) == 4, "Compiler doing something weird with AnimScriptStateMagicNumbers");

typedef struct sAnimScript_tag {
    char unknown[0x3c];
    AnimScriptStateMagicNumbers magicStateIndicator;
    char unknown2[0xb4-4-0x3c];
} sAnimScript_tag;

static_assert(sizeof(sAnimScript_tag) == 0xb4, "Bad size for sAnimScript_tag");

#pragma pack(pop)

static_assert(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");
static_assert(sizeof(AnimDatum) == 0x24, "Bad size for AnimDatum");
static_assert(sizeof(_MATRIX) == 0x3c, "AnimProcessSkinData allocates invBind as numBones * 0x3c");
static_assert(offsetof(sAnimSkin_tag, riggedBody) == 0x04, "Bad offset of riggedBody");
static_assert(offsetof(sAnimSkin_tag, discreteObj) == 0x08, "Bad offset of discreteObj");
static_assert(offsetof(sAnimSkin_tag, discreteBone) == 0x0c, "Bad offset of discreteBone");
static_assert(offsetof(sAnimSkin_tag, skinScale) == 0x10, "Bad offset of skinScale");
static_assert(offsetof(sAnimSkin_tag, datums) == 0x1c, "Bad offset of datums");
static_assert(offsetof(sAnimSkin_tag, bones) == 0x20, "Bad offset of bones");
static_assert(offsetof(sAnimSkin_tag, invBind) == 0x24, "Bad offset of invBind");
static_assert(offsetof(sAnimSkin_tag, numRigged) == 0x28, "Bad offset of numRigged");
static_assert(offsetof(sAnimSkin_tag, numDiscrete) == 0x29, "Bad offset of numDiscrete");
static_assert(offsetof(sAnimSkin_tag, numMorphs) == 0x2a, "Bad offset of numMorphs");
static_assert(offsetof(sAnimSkin_tag, numDatums) == 0x2b, "Bad offset of numDatums");
static_assert(offsetof(sAnimSkin_tag, skeletonNum) == 0x2c, "Bad offset of skeletonNum");
static_assert(sizeof(sAnimSkin_tag) == 0x30, "Bad size for sAnimSkin_tag");


void AnimObjectDelete(obj_tag* obj);
void AnimLoadFile(HASHCODE hashcode, char param_2);
void AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);
bool AnimObjectIsClose(obj_tag* obj, uint unk);
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer);
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3);
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);
void AnimObjectAimAt(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);
void AnimObjectSetSleeveType(obj_tag *param_1, int sleeveNum);
sAnimScript_tag * AnimScriptNew(obj_tag *gameObj, sAnimScript_tag *existingScriptList);
void AnimSkeletonProcess(char *data);
HASHCODE AnimSleeveGetEntity(int idx);
// Register-argument adaptor (hashcode in EAX) patched over the original; never call it from C.
void AnimProcessSkinData(HASHCODE hashcode);
// The C body of AnimProcessSkinData - C callers (a future AnimLoadFile) call this one.
void _AnimProcessSkinData(HASHCODE hashcode);
void AnimPostLoadInit(void);
void AnimLoadFile(HASHCODE hashcode,char param_2);


#endif // ANIM_H