#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"
#include "Collide.h"    // HITDATA_tag

#pragma pack(push, 1)

typedef struct sAnimScript_tag sAnimScript_tag;

// The animated part of an object (sAnimObject_tag), embedded in AnimState at +0x58. Only the fields our code uses
// are named.
typedef struct AnimObj {
    const void *headTrackTarget;    // 0x00 - an obj_tag with ANIMOBJ_HEAD_TARGET_OBJ, else a position
    const void *aimTarget;          // 0x04 - a position (AnimObjectSetAiming)
    char _unknown_08[0x18 - 0x08];
    void *rootMotionCallback;       // 0x18 - Ghidra's name; NDrone2_DefaultInit sets the drones' (0x00033320)
    struct sAnimSkin_tag *skin;     // 0x1c
    char _unknown_20[0x10];
    celglist_tag *maybeSleeveGlist; // 0x30
    char _unknown_34[0x40 - 0x34];
    _MATRIX *boneMtxs;              // 0x40 - one per bone, built by psiBuildMatrixPalette
    char _unknown_44[0x48 - 0x44];
    obj_tag **discreteObj;          // 0x48 - [skin->numDiscrete] the rigid parts, each following one bone
    char _unknown_4c[4];
    uint flags;                     // 0x50 - ANIMOBJ_*; 0x8000 set by Script_AnimStart; 0x40000 set by Script_FFwd
    float rootHeight;               // 0x54
    char _unknown_58[0x68 - 0x58];
    float headTrackSpeed;           // 0x68 - NDrone2_HeadTrackObj
    float aimRate;                  // 0x6c
    float headTrackRange2;          // 0x70 - the head tracking range, squared
    float aimRange2;                // 0x74 - the aiming range, squared
    char _unknown_78[0x8c - 0x78];
    uchar headTrackTargetBone;      // 0x8c - with ANIMOBJ_HEAD_TARGET_OBJ, the bone of the target looked at
    uchar aimTargetBone;            // 0x8d
    uchar headTrackDelay;           // 0x8e - frames before the head starts to turn (AnimObjectSetHeadTracking)
    char _unknown_8f[0x98 - 0x8f];
} AnimObj;

// AnimObj::flags
#define ANIMOBJ_FLAG_1          0x1     // with ANIMOBJ_FLAG_2, AnimGetCollData gives the joints' boxes
#define ANIMOBJ_FLAG_2          0x2
#define ANIMOBJ_HEAD_TRACK      0x10    // head tracking asked for (AnimObjectSetHeadTracking)
#define ANIMOBJ_HEAD_TRACKING   0x30    // AnimObjectIsHeadTracking
#define ANIMOBJ_HEAD_TARGET_OBJ 0x40    // headTrackTarget is an object, looked at by headTrackTargetBone
#define ANIMOBJ_AIMING          0x80
#define ANIMOBJ_AIMING_2        0x100   // with ANIMOBJ_AIMING, AnimObjectIsAiming
#define ANIMOBJ_AIM_AT_BONE     0x200   // aimTargetBone is used

typedef struct AnimState {
    HITDATA_tag floor; // 0x00 - what Drone_FeetOnPoint found the feet on
    char animFlags; // 0x50 - bit one when weapon is zoomed in; ANIM_CONTACT_* for drones
    char _pad_2;
    char currentWeaponId; // 0x52
    char switchingToWeaponId; // 0x53
    char prevHeldWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj animObj; // 0x58 - ???? - size not known
} AnimState;

// AnimState::animFlags, for drones (Drone_FeetOnPoint, Drone_CollisionHandler)
#define ANIM_CONTACT_FLOOR  0x08    // the feet stand on something
#define ANIM_CONTACT_HIT    0x10    // the capsule hit something
#define ANIM_CONTACT_ABOVE  0x20    // the capsule hit something above its top
#define ANIM_CONTACT_MOVER  0x80    // moves with what it stands on (PlayerOrDrone_FeetOnPoint_DoCollision)

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

// A sequence file (hashcode type 0x04) as loaded.
typedef struct sAnimRawSeq_tag {
    char _unknown_00[0xc];
    uint flags;             // 0x0c - the low four bits are copied to sAnimSeq_tag::flags
    float boundSphere[4];   // 0x10
    float rootBox[6];       // 0x20
    short numFrames;        // 0x38
    uchar numElems;         // 0x3a
    uchar _unknown_3b;
    uchar skeletonNum;      // 0x3c
    uchar _unknown_3d;
    uchar raw3e;            // 0x3e
    uchar _unknown_3f;
} sAnimRawSeq_tag;

// A sequence being sampled for a script.
typedef struct sAnimSeq_tag {
    char _unknown_00[0x3c];
    float rootBox[6];           // 0x3c
    float boundSphere[4];       // 0x54
    char _unknown_64[0x74 - 0x64];
    sAnimRawSeq_tag *rawSeq;    // 0x74
    HASHCODE hashcode;          // 0x78
    uint flags;                 // 0x7c
    char _unknown_80[4];
    short startTime;            // 0x84
    short firstFrame;           // 0x86
    short lastFrame;            // 0x88
    short numFrames;            // 0x8a
    uchar raw3e;                // 0x8c
    uchar skeletonNum;          // 0x8d
    uchar numElems;             // 0x8e
    uchar _unknown_8f;
} sAnimSeq_tag;

// sAnimScript_tag::flags
#define ANIMSCRIPT_SHARE_ROOT 0x8000        // keeps the root rotation of the script it blends with
#define ANIMSCRIPT_SCRIPT_DRIVEN 0x10000    // Script_AnimStart and Script_Interp set the frame themselves
#define ANIMSCRIPT_ANIM_ROTATION 0x20000    // the root's turn turns the object (AnimScriptSetAnimRotation)
#define ANIMSCRIPT_HAS_ROOT_TRANS 0x800000  // AnimGetFirstTrans only answers for such a script
#define ANIMSCRIPT_GAME_FLAG 0x8000000      // set by the drones and AnimSets; the anim code keeps it
#define ANIMSCRIPT_LOOP 0x80000000

// The callbacks a script is given: its event commands (AnimScriptCallOnEvent) and its end (AnimScriptCallOnStop)
typedef void (*AnimScriptEventFunc)(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, ushort event);
typedef void (*AnimScriptStopFunc)(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script);

typedef struct sAnimScript_tag {
    char unknown[0x3c];
    AnimScriptStateMagicNumbers magicStateIndicator; // 0x3c
    char _unknown_40[0x4c - 0x40];
    sAnimSeq_tag *boneAnimSeq;  // 0x4c
    char _unknown_50[0x60 - 0x50];
    sAnimScript_tag **ownerSlot;    // 0x60 - its owner's pointer to it, cleared when it is deleted
    void *onDelete;                 // 0x64
    AnimScriptStopFunc onStop;      // 0x68 - called once, when it stops
    AnimScriptEventFunc onEvent;    // 0x6c
    HASHCODE hashcode;              // 0x70
    uint flags;                 // 0x74
    char _unknown_78[0x8c - 0x78];
    float curFrame;             // 0x8c - 1..numFrames
    char _unknown_90[4];
    float speed;                // 0x94
    char _unknown_98[0x9c - 0x98];
    float rotBaseYaw;           // 0x9c - 8.0: taken afresh on the next update
    char _unknown_a0[0xa4 - 0xa0];
    float fadeTime;             // 0xa4 - the blend's counter
    float fadeLen;              // 0xa8 - frames the blend takes
    short numFrames;            // 0xac
    char _unknown_ae[0xb4 - 0xae];
} sAnimScript_tag;

static_assert(sizeof(sAnimScript_tag) == 0xb4, "Bad size for sAnimScript_tag");

// A row of the joint box tables AnimGetCollData reads: a box centred between bone and other (the bone's parent where
// other is negative), oriented as other, with half these sizes (times the skin's scale). A negative bone ends the table.
typedef struct AnimCollBone {
    short bone;
    short other;
    float unknown04;    // not read by AnimGetCollData
    _VECTOR half;
} AnimCollBone;

// One joint box from AnimGetCollData, for Collide_Jointy
typedef struct COLLJOINT_tag {
    _MATRIX m;          // the other bone's matrix, translated to the box's centre
    _VECTOR half;       // 0x3c
    short joint;        // 0x48 - the bone
    char _pad4a[2];
} COLLJOINT_tag;

static_assert(sizeof(AnimCollBone) == 0x14, "Bad size for AnimCollBone");
static_assert(sizeof(COLLJOINT_tag) == 0x4c, "Bad size for COLLJOINT_tag");

#pragma pack(pop)

static_assert(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");
static_assert(offsetof(AnimState, animFlags) == 0x50, "Offset of animFlags wrong");
static_assert(offsetof(AnimObj, aimTarget) == 0x04, "Offset of aimTarget wrong");
static_assert(offsetof(AnimObj, boneMtxs) == 0x40, "Offset of boneMtxs wrong");
static_assert(offsetof(AnimObj, aimRate) == 0x6c, "Offset of aimRate wrong");
static_assert(offsetof(AnimObj, aimRange2) == 0x74, "Offset of aimRange2 wrong");
static_assert(offsetof(AnimObj, aimTargetBone) == 0x8d, "Offset of aimTargetBone wrong");
static_assert(sizeof(AnimObj) == 0x98, "Bad size for AnimObj");
static_assert(offsetof(AnimObj, skin) == 0x1c, "Offset of skin wrong");
static_assert(offsetof(AnimObj, maybeSleeveGlist) == 0x30, "Offset of maybeSleeveGlist wrong");
static_assert(offsetof(AnimObj, discreteObj) == 0x48, "Offset of discreteObj wrong");
static_assert(offsetof(AnimObj, flags) == 0x50, "Offset of flags wrong");
static_assert(offsetof(AnimObj, rootHeight) == 0x54, "Offset of rootHeight wrong");
static_assert(sizeof(sAnimRawSeq_tag) == 0x40, "Bad size for sAnimRawSeq_tag");
static_assert(offsetof(sAnimRawSeq_tag, numFrames) == 0x38, "Offset of numFrames wrong");
static_assert(sizeof(sAnimSeq_tag) == 0x90, "Bad size for sAnimSeq_tag");
static_assert(offsetof(sAnimSeq_tag, rootBox) == 0x3c, "Offset of rootBox wrong");
static_assert(offsetof(sAnimSeq_tag, rawSeq) == 0x74, "Offset of rawSeq wrong");
static_assert(offsetof(sAnimSeq_tag, numFrames) == 0x8a, "Offset of numFrames wrong");
static_assert(offsetof(sAnimScript_tag, boneAnimSeq) == 0x4c, "Offset of boneAnimSeq wrong");
static_assert(offsetof(sAnimScript_tag, flags) == 0x74, "Offset of flags wrong");
static_assert(offsetof(sAnimScript_tag, curFrame) == 0x8c, "Offset of curFrame wrong");
static_assert(offsetof(sAnimScript_tag, speed) == 0x94, "Offset of speed wrong");
static_assert(offsetof(sAnimScript_tag, numFrames) == 0xac, "Offset of numFrames wrong");
static_assert(offsetof(sAnimScript_tag, ownerSlot) == 0x60, "Offset of ownerSlot wrong");
static_assert(offsetof(sAnimScript_tag, onStop) == 0x68, "Offset of onStop wrong");
static_assert(offsetof(sAnimScript_tag, onEvent) == 0x6c, "Offset of onEvent wrong");
static_assert(offsetof(sAnimScript_tag, rotBaseYaw) == 0x9c, "Offset of rotBaseYaw wrong");
static_assert(offsetof(sAnimScript_tag, fadeLen) == 0xa8, "Offset of fadeLen wrong");
static_assert(offsetof(AnimObj, headTrackRange2) == 0x70, "Offset of headTrackRange2 wrong");
static_assert(offsetof(AnimObj, headTrackTargetBone) == 0x8c, "Offset of headTrackTargetBone wrong");
static_assert(offsetof(AnimObj, headTrackDelay) == 0x8e, "Offset of headTrackDelay wrong");
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
bool AnimGetFirstTrans(sAnimScript_tag *script, _VECTOR *out);
void AnimGetFrameTransRaw(HASHCODE hashcode, int frame, _VECTOR *out);
obj_tag* AnimGetNDiscreteObj(obj_tag *obj, uint n);
COLLJOINT_tag* AnimGetCollData(obj_tag *obj, ushort *count);
bool AnimObjectIsAiming(obj_tag *obj);
bool AnimObjectIsHeadTracking(obj_tag *obj);
void AnimObjectSetAiming(obj_tag *obj, const void *target, float range, float rate, char atBone, uchar bone);
void AnimObjectSetHeadTracking(obj_tag *obj, const void *target, float range, float rate, char randomDelay,
                               char targetIsObj, uchar bone);
void AnimScriptCallOnEvent(sAnimScript_tag *script, AnimScriptEventFunc onEvent);
void AnimScriptCallOnStop(sAnimScript_tag *script, AnimScriptStopFunc onStop);
void AnimScriptSetAnimRotation(sAnimScript_tag *script, char on);
void AnimLoadFile(HASHCODE hashcode,char param_2);


#endif // ANIM_H