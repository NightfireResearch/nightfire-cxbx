#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct sAnimScript_tag sAnimScript_tag;

typedef struct AnimSkin {
    //?
    char unknown[0x10];
    _VECTOR skinScale;
    void* animDatums;
    void* boneHierarchy;
    void* matrixList;
    char unknown2;
    uchar someNumBones;
    uchar maybeNumMorphs;
    uchar numDatums;
    uchar skeletonNum;
} AnimSkin;

static_assert(sizeof(AnimSkin) == 0x2d, "Size of AnimSkin is not as expected");

typedef struct AnimObj {
    char pad[0x1c];
    AnimSkin *skinPtr; // 0x1c-0x20 - pointer to AnimSkin
    char pad2[4];
    sAnimScript_tag *firstScript; // 0x24-0x28 - pointer to first sAnimScript_tag in linked list
    char pad22[4];
    uchar skeletonNum; // 0x2c
    char pad3[0x3];
    celglist_tag *maybeSleeveGlist; // 0x30-0x34 - pointer to celglist for sleeve?
    // ...
} AnimObj;

typedef struct AnimState {
    char _pad_1[0x50];
    char field_0x50; // bit one when weapon is zoomed in
    char _pad_2;
    char currentWeaponId; // 0x52
    char otherWeaponId; // 0x53
    char thirdWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj animObj; // 0x58 - ???? - size not known
} AnimState;

// these constants seem to be the same on both PS2 and Xbox
typedef enum {
    ANIM_SCRIPT_STATE_MAYBE_INITIALISED = 0x159f5ab2,
    ANIM_SCRIPT_STATE_MAYBE_DELETED = 0x983c4837,
} AnimScriptStateMagicNumbers;
static_assert(sizeof(AnimScriptStateMagicNumbers) == 4, "Compiler doing something weird with AnimScriptStateMagicNumbers");

typedef struct sAnimSeq_tag {
    char unknown[0x68];
    void* rawData; // 0x68-0x6c - pointer to raw sequence data
    void* seqUnpakOpt; // 0x6c-0x70 - pointer to some extra thing
    char unknown2[0x90-0x68-8];
} sAnimSeq_tag;

static_assert(sizeof(sAnimSeq_tag) == 0x90, "Size of sAnimSeq_tag is not as expected");


typedef struct sAnimScript_tag {
    char unknown[0x3c];
    AnimScriptStateMagicNumbers magicStateIndicator; // 0x3C-0x40
    sAnimScript_tag *prevScript; // Doubly linked list - NOT compatible with LLNODE_tag - it's in the wrong place to be compatible
    sAnimScript_tag *nextScript;
    char junk[4];
    sAnimSeq_tag *boneAnimSeq; // 0x4C-0x50
    sAnimSeq_tag *morphAnimSeq; // 0x50-0x54
    char unknown1[4];
    void* animScriptData; // 0x58-0x5c - pointer to raw animation script data
    uint unknown3[2];
    void* onDeletionCallback;
    void* onStopCallback;
    void* onEventCallback;
    HASHCODE hashcode; // 0x70-0x74
    uint field74_0x74;
    uint unknown4;
    uint field76_0x7c;
    uint timestamp;
    uint unknown5[2];
    float maybeAnimationSpeed2;
    float maybeAnimationSpeed3;
    float maybeAnimationSpeed; // 0x94-0x98
    char unknown2[0xa4-0x98];
    float someThing1; // 0xa4-0xa8
    float someThing2; // 0xa8-0xac
    short field97_0xac; // 0xac-0xae
    char field98_0xae; // 0xae-0xaf
    byte field99_0xaf; // 0xaf-0xb0
    uchar maybeSpeedControlRelated;
    uchar unknown1234;
    uchar someThing3; // 0xb2-0xb3
    char pad_fill[0xc0 - 0xb3];
} sAnimScript_tag;

static_assert(sizeof(sAnimScript_tag) == 0xc0, "Bad size for sAnimScript_tag");

#pragma pack(pop)

static_assert(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");


void AnimObjectDelete(obj_tag* obj);
void AnimLoadFile(HASHCODE hashcode, char param_2);
uchar AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);
bool AnimObjectIsClose(obj_tag* obj, uint unk);
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer);
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3);
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);
void AnimObjectAimAt(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5);
void AnimObjectSetSleeveType(obj_tag *param_1, int sleeveNum);
sAnimScript_tag * AnimScriptNew(obj_tag *gameObj, sAnimScript_tag *existingScriptList);
void AnimSkeletonProcess(char *data);
void AnimPostLoadInit(void);
sAnimSeq_tag * AnimSeqNew(size_t size, MallocFlags allocType);
sAnimScript_tag * AnimScriptAdd(obj_tag *gameObj, HASHCODE animScriptHashcode);


#define ScriptCam U32_AT(0x001f6678)
#define AnimScriptTimeStamp U32_AT(0x001d7570)
#define ReverseFlag BOOL8_AT(0x001d7828)

#endif // ANIM_H