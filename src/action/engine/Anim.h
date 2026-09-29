#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct sAnimScript_tag sAnimScript_tag;
typedef struct sAnimSet_tag sAnimSet_tag;

// A skeleton, as loaded from a SKELnnnn entry of a level bundle and registered by AnimSkeletonProcess.
// INVENTED NAME: the record has no symbol of its own. It holds no hierarchy - that is in each skin.
typedef struct SkeletonInfo {
    short skeletonIdx;      // 0x00 - index into SkeletonTable
    uchar numBones;         // 0x02
    uchar numTransBones;    // 0x03 - the number of bits set in transMask (no reader found)
    uint transMask[3];      // 0x04 - bit b set: bone b has an animated translation channel (AnimFrameCopy)
    _VECTOR boneOffset[1];  // 0x10 - [numBones] default local translation of each bone, unscaled
} SkeletonInfo;

// An attachment point on a skin: weapons, gadgets, effects. Ids are matched by AnimDatumGetIndex and
// AnimGetBoneWorldTrans; weapon_data's whichBone holds such ids.
typedef struct AnimDatum {
    int id;                 // 0x00
    int bone;               // 0x04 - -1 = none, else a bone index
    _VECTOR t;              // 0x08 - bone-local position
    quaternion_tag q;       // 0x14 - bone-local rotation (x, y, z, w)
} AnimDatum;

// A skin (hashcode type 0x05), built by AnimProcessSkinData from the file. The file's hashtable slot is
// re-pointed at this struct.
typedef struct sAnimSkin_tag {
    HASHCODE hashcode;              // 0x00 - written by AnimObjectNew, not at load
    celglist_tag **riggedBody;      // 0x04 - [numRigged] skinned meshes
    celglist_tag **discreteObj;     // 0x08 - [numDiscrete] rigid parts, each following one bone
    uchar *discreteBone;            // 0x0c - [numDiscrete] the bone each rigid part hangs on
    _VECTOR skinScale;              // 0x10 - applied to translations while decoding
    AnimDatum *datums;              // 0x1c - [numDatums]
    uchar *bones;                   // 0x20 - [numBones] bit 7: bone used by this skin; bits 0-6: parent (0x7f = root)
    _MATRIX *invBind;               // 0x24 - [numBones] inverse bind poses (identity rotation, -bind position)
    uchar numRigged;                // 0x28
    uchar numDiscrete;              // 0x29
    uchar numMorphs;                // 0x2a
    uchar numDatums;                // 0x2b
    uchar skeletonNum;              // 0x2c
    uchar _pad_2d[3];
} sAnimSkin_tag;

#define ANIM_BONE_USED 0x80
#define ANIM_BONE_PARENT_MASK 0x7f
#define ANIM_BONE_ROOT 0x7f

// sAnimObject_tag flags (+0x50)
#define ANIMOBJ_POSE_VALID      0x0001 // a pose has been resolved; nothing is drawn or queried without it
#define ANIMOBJ_FULL_CHARACTER  0x0002 // skeleton of more than 70 bones: faces, datum weapons, height offset
#define ANIMOBJ_BONES_VALID     0x0004 // boneMtxs are current this frame
#define ANIMOBJ_PALETTE_VALID   0x0008 // palette is current this frame
#define ANIMOBJ_HEADTRACK_REQ   0x0010
#define ANIMOBJ_HEADTRACK_ON    0x0020
#define ANIMOBJ_HEADTRACK_OBJ   0x0040 // headTrackTarget is an obj_tag, not a point
#define ANIMOBJ_AIM_REQ         0x0080
#define ANIMOBJ_AIM_ON          0x0100
#define ANIMOBJ_AIM_OBJ         0x0200 // aimTarget is an obj_tag, not a point
#define ANIMOBJ_ALT_HEIGHT      0x0400 // full character whose skeletonNum != 1
#define ANIMOBJ_AIM_IN_RANGE    0x1000
#define ANIMOBJ_AIM_YAW_CLAMP   0x2000
#define ANIMOBJ_AIM_PITCH_CLAMP 0x4000
#define ANIMOBJ_INV_SKIN_SCALE  0x8000

// The bone head tracking turns, and the bone aiming turns (the spine; the whole upper body follows).
#define ANIM_HEADTRACK_BONE 5
#define ANIM_AIM_BONE 2

// The animated part of an object (sAnimObject_tag), embedded in AnimState at +0x58.
// The pose buffer is structure-of-arrays: _VECTOR trans[numBones] then quaternion_tag quat[numBones], local
// (parent-relative), translations already multiplied by the skin scale.
typedef struct AnimObj {
    void *headTrackTarget;          // 0x00 - obj_tag* if ANIMOBJ_HEADTRACK_OBJ, else a _VECTOR* world point
    void *aimTarget;                // 0x04 - obj_tag* if ANIMOBJ_AIM_OBJ, else a _VECTOR*
    float boundSphere[4];           // 0x08 - centre and radius, from the base sequence (AnimSeqSet)
    void *rootMotionCallback;       // 0x18 - (obj, animObj, _VECTOR *delta), from AnimFrameResolve
    sAnimSkin_tag *skin;            // 0x1c
    sAnimSet_tag *animSets;         // 0x20 - locomotion sets (AnimSetAppend), up to 4
    sAnimScript_tag *firstScript;   // 0x24
    void *postPoseCallback;         // 0x28 - (obj, animObj, numBones, trans*, quats*), end of AnimFrameResolve
    celglist_tag **datumGlist;      // 0x2c - [numDatums] entity drawn at each datum
    celglist_tag *maybeSleeveGlist; // 0x30 - replaces rigged body 0 when set
    float *pose;                    // 0x34 - numBones*28 bytes, see above
    float *morphWeights;            // 0x38 - [numMorphs]
    uchar *datumTimer;              // 0x3c - [numDatums] 0 hidden, 0xff always, else frames left
    _MATRIX *boneMtxs;              // 0x40 - [numBones] world matrices
    _MATRIX *palette;               // 0x44 - [numBones] skinning matrices: bind space -> world
    obj_tag **discreteObj;          // 0x48 - [numDiscrete]
    uint lastUpdateFrame;           // 0x4c
    uint flags;                     // 0x50 - ANIMOBJ_*; also 0x40000 = mute script sounds and events
    float rootHeight;               // 0x54
    float heightOffset;             // 0x58
    float distanceThisTick;         // 0x5c - AnimSetUpdate: speed * set.b
    float rootTravel;               // 0x60 - this frame's root travel (footsteps)
    float distancePhase;            // 0x64 - AnimSet distance phase
    float headTrackRate;            // 0x68
    float aimRate;                  // 0x6c
    float headTrackRange2;          // 0x70 - squared
    float aimRange2;                // 0x74 - squared
    float headYaw;                  // 0x78
    float aimYaw;                   // 0x7c
    float headPitch;                // 0x80
    float aimPitch;                 // 0x84
    ushort poseBufSize;             // 0x88 - numBones*28
    ushort facialTimer;             // 0x8a
    uchar headTrackTargetBone;      // 0x8c
    uchar aimTargetBone;            // 0x8d
    uchar headTrackHoldTimer;       // 0x8e
    uchar lookMorphX;               // 0x8f
    uchar lookMorphY;               // 0x90
    uchar facialState;              // 0x91
    char _unknown_92[2];
    uchar lod[4];                   // 0x94 - per viewport, distance / zoom / 4, clamped to 255
} AnimObj;

typedef AnimObj sAnimObject_tag;

typedef struct AnimState {
    char _pad_1[0x50];
    char animFlags; // bit one when weapon is zoomed in; bit 4 = footstep this frame
    char _pad_2;
    char currentWeaponId; // 0x52
    char switchingToWeaponId; // 0x53
    char prevHeldWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj animObj; // 0x58
} AnimState;

// these constants seem to be the same on both PS2 and Xbox
typedef enum {
    ANIM_SCRIPT_STATE_MAYBE_INITIALISED = 0x159f5ab2,
    ANIM_SCRIPT_STATE_MAYBE_DELETED = 0x983c4837,
} AnimScriptStateMagicNumbers;
static_assert(sizeof(AnimScriptStateMagicNumbers) == 4, "Compiler doing something weird with AnimScriptStateMagicNumbers");

// A sequence file (hashcode type 0x04) as loaded; the tracks follow at +0x40. AnimProcessSeqData replaces the
// first three words. A track is a u32 head (byte size | first key word << 16), then key words (frames << 6 |
// coefficient mask) each followed by its float polynomial coefficients, in absolute time t = frame - 1. Bone b
// has six tracks (tx ty tz qx qy qz) if its skeleton transMask bit is set, else three; w is derived.
typedef struct sAnimRawSeq_tag {
    uint *trackData;        // 0x00 - on disk: the file's hashcode
    uint trackDataSize;     // 0x04
    uint *cursor;           // 0x08
    uint flags;             // 0x0c - bits 0-2 distance-table axes, bit 3 morph (facial) sequence
    float boundSphere[4];   // 0x10
    float rootBox[6];       // 0x20 - bone 0's translation range
    short numFrames;        // 0x38
    short numTracks;        // 0x3a - the low byte is also the element count of a morph sequence
    uchar skeletonNum;      // 0x3c
    uchar _pad_3d;
    uchar raw3e;            // 0x3e - equals skeletonNum in every file; no reader
    uchar _pad_3f;
} sAnimRawSeq_tag;

// A sequence being sampled for a script (AnimSeqNew).
typedef struct sAnimSeq_tag {
    _VECTOR rootPrev;       // 0x00 - raw bone 0 translation at the last tick
    _VECTOR rootDelta;      // 0x0c - root motion this tick
    _VECTOR firstTrans;     // 0x18 - the original records quat[0].xyz here, not the translation (a bug)
    _VECTOR transRelFirst;  // 0x24
    _VECTOR objPosAtSet;    // 0x30
    float rootBox[6];       // 0x3c
    float boundSphere[4];   // 0x54
    void *distanceTable;    // 0x64
    float *pose;            // 0x68 - _VECTOR[nb] + quaternion_tag[nb], or float[numElems] for a morph sequence
    uint *unpakCache;       // 0x6c
    void *frameCallback;    // 0x70 - (obj, skin, seq), replaces AnimFrameSet
    sAnimRawSeq_tag *rawSeq;// 0x74
    HASHCODE hashcode;      // 0x78
    uint flags;             // 0x7c
    float frame;            // 0x80 - 1-based
    short startTime;        // 0x84 - in script frames
    short firstFrame;       // 0x86
    short lastFrame;        // 0x88
    short numFrames;        // 0x8a
    uchar raw3e;            // 0x8c
    uchar skeletonNum;      // 0x8d
    uchar numElems;         // 0x8e
    uchar _pad_8f;
} sAnimSeq_tag;

// One playing animation script (hashcode type 0x06). The file is: u32 hashcode, s16 numFrames (60 Hz), u16
// numCmds | rate << 9 | loop << 15, then commands (op | len << 8, operands): 0 = play sequence 0x04000000|id over
// [start, end], 1 = sound at a frame, 4 = event at a frame.
typedef struct sAnimScript_tag {
    _MATRIX rotMtx;                             // 0x00 - object rotation captured for anim-rotation mode
    AnimScriptStateMagicNumbers magicStateIndicator; // 0x3c
    sAnimScript_tag *prevScript;                // 0x40
    sAnimScript_tag *nextScript;                // 0x44
    sAnimScript_tag *syncMaster;                // 0x48 - followed in speed mode 2 / blended into
    sAnimSeq_tag *boneAnimSeq;                  // 0x4c
    sAnimSeq_tag *morphAnimSeq;                 // 0x50 - only if the skin has morphs
    void *boneMask;                             // 0x54 - non-zero: a partial (overlay) blend
    uchar *scriptData;                          // 0x58 - the 0x06 file
    uchar *doneFlag;                            // 0x5c
    sAnimScript_tag **ownerSlot;                // 0x60 - cleared on delete
    void *onDelete;                             // 0x64 - (obj, animObj, script)
    void *onStop;                               // 0x68 - (obj, animObj, script)
    void *onEvent;                              // 0x6c - (obj, animObj, script, ushort param)
    HASHCODE hashcode;                          // 0x70
    uint flags;                                 // 0x74
    uint firedMask;                             // 0x78 - one bit per command
    uint groupTag;                              // 0x7c - AnimSet tag; 0xe0000000-2 = facial mood / blink / speech
    uint timestamp;                             // 0x80 - creation order
    float syncBlend;                            // 0x84
    float targetPos;                            // 0x88 - speed mode 3
    float curFrame;                             // 0x8c - 1..numFrames, 60 Hz
    float prevFrame;                            // 0x90
    float speed;                                // 0x94 - script frames per 60 Hz frame; sign = direction
    float weight;                               // 0x98 - fadeTime / fadeLen
    float rotBaseYaw;                           // 0x9c
    float rotLastYaw;                           // 0xa0
    float fadeTime;                             // 0xa4
    float fadeLen;                              // 0xa8
    short numFrames;                            // 0xac
    uchar numCmds;                              // 0xae
    uchar rate;                                 // 0xaf - 60, unused
    uchar speedMode;                            // 0xb0 - 0 normal, 1 distance, 2 sync to master, 3 seek
    uchar playMode;                             // 0xb1 - 0 once, 1 loop, 2 ping-pong, 3 once then delete
    char fadeDir;                               // 0xb2 - +1 in, -1 out, 0 hold
    uchar _pad_b3;
} sAnimScript_tag;

static_assert(sizeof(sAnimScript_tag) == 0xb4, "Bad size for sAnimScript_tag");
static_assert(offsetof(sAnimScript_tag, flags) == 0x74, "Offset of flags wrong");
static_assert(offsetof(sAnimScript_tag, numFrames) == 0xac, "Offset of numFrames wrong");
static_assert(sizeof(sAnimSeq_tag) == 0x90, "Bad size for sAnimSeq_tag");
static_assert(offsetof(sAnimSeq_tag, pose) == 0x68, "Offset of pose wrong");
static_assert(offsetof(sAnimSeq_tag, skeletonNum) == 0x8d, "Offset of skeletonNum wrong");
static_assert(sizeof(sAnimRawSeq_tag) == 0x40, "Bad size for sAnimRawSeq_tag");

// A locomotion controller: walk and run scripts kept in step and blended by speed, plus a strafe overlay.
// INVENTED tag name.
typedef struct sAnimSet_tag {
    sAnimSet_tag *prev;             // 0x00
    sAnimSet_tag *next;             // 0x04
    uint walkTimestamp;             // 0x08
    uint blendTimestamp;            // 0x0c
    sAnimScript_tag *blendScript;   // 0x10
    sAnimScript_tag *strafeScript;  // 0x14
    HASHCODE *speedList;            // 0x18 - idle, walk.., run.., 0
    HASHCODE *strafeList;           // 0x1c
    uint tag;                       // 0x20 - copied to its scripts' groupTag
    float a;                        // 0x24 - distance phase reset value
    float b;                        // 0x28 - speed -> distance scale
    ushort idleTimer;               // 0x2c
    char speedIndex;                // 0x2e
    uchar strafeDir;                // 0x2f
    uchar switchCooldown;           // 0x30
    uchar speedCount;               // 0x31
    uchar strafeCount;              // 0x32
    uchar oneShotState;             // 0x33
} sAnimSet_tag;

static_assert(sizeof(sAnimSet_tag) == 0x34, "Bad size for sAnimSet_tag");

#pragma pack(pop)

static_assert(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");
static_assert(sizeof(AnimState) == 0xf0, "Bad size for AnimState");
static_assert(sizeof(AnimObj) == 0x98, "Bad size for AnimObj");
static_assert(offsetof(AnimObj, pose) == 0x34, "Offset of pose wrong");
static_assert(offsetof(AnimObj, flags) == 0x50, "Offset of flags wrong");
static_assert(offsetof(AnimObj, headTrackRate) == 0x68, "Offset of headTrackRate wrong");
static_assert(offsetof(AnimObj, poseBufSize) == 0x88, "Offset of poseBufSize wrong");
static_assert(sizeof(sAnimSkin_tag) == 0x30, "Bad size for sAnimSkin_tag");
static_assert(offsetof(sAnimSkin_tag, bones) == 0x20, "Offset of bones wrong");
static_assert(sizeof(AnimDatum) == 0x24, "Bad size for AnimDatum");
static_assert(offsetof(SkeletonInfo, boneOffset) == 0x10, "Offset of boneOffset wrong");

// SkeletonInfo *SkeletonTable[128]
#define SkeletonTable (*(SkeletonInfo*(*)[128])0x001d7578)

// The skin scale the decoder multiplies translations by, and its inverse (bone 0 is put back into raw units).
#define Glb_SkinScale (*(_VECTOR *)0x001d7794)
#define Glb_InvSkinScale (*(_VECTOR *)0x001d6a28)

// sAnimSeq_tag flags (+0x7c), those the decoder uses
#define ANIMSEQ_FLAG_MORPH 0x8
#define ANIMSEQ_FLAG_FIRST_TRANS 0x100000
#define ANIMSEQ_FLAG_UNPAK_CACHE 0x200000

// A track's decoder state, kept per track by the unpack cache (sAnimSeq_tag.unpakCache, numElems entries) so the
// next frame resumes the key search where the last one ended. Only valid while frames never go backwards; the
// scripts enable it only for long, non-looping, step-mode playback. Entries are counted over the tracks actually
// decoded, so they depend on which bones the skin passed to AnimFrameCopy uses.
typedef struct {
    uint keyOffset;     // (key word pointer - track data) | whether the high half of 'pending' is still unread
    ushort pendingHigh; // the high half of the last u32 read
    ushort accBefore;   // frames covered by the keys before this one
} AnimUnpakEntry;


void AnimObjectDelete(obj_tag* obj);
void AnimLoadFile(HASHCODE hashcode, char param_2);
void AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut);
bool AnimObjectIsClose(obj_tag* obj, uint unk);
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer);
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3);
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);
void AnimObjectAimAt(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);
void AnimObjectSetSleeveType(obj_tag *param_1, int sleeveNum);
sAnimScript_tag * AnimScriptNew(obj_tag *gameObj, sAnimScript_tag *existingScriptList);
void AnimSkeletonProcess(char *data);
void AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frame);
void AnimPostLoadInit(void);
void AnimLoadFile(HASHCODE hashcode,char param_2);


#endif // ANIM_H