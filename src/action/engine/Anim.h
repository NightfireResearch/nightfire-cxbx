#ifndef ANIM_H
#define ANIM_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"
#include "Collide.h"    // HITDATA_tag
#include "../../driving/platform/Ptr32.h"   // the anim files' pointer words

#pragma pack(push, 1)

typedef struct sAnimScript_tag sAnimScript_tag;
typedef struct sAnimSeq_tag sAnimSeq_tag;
typedef struct sAnimSkin_tag sAnimSkin_tag;
typedef struct AnimObj AnimObj;

// A skeleton, as loaded from a level bundle and registered by AnimSkeletonProcess. It holds no hierarchy: that is in
// each skin.
typedef struct SkeletonInfo {
    short skeletonIdx;      // 0x00 - index into SkeletonTable
    uchar numBones;         // 0x02
    uchar numTransBones;    // 0x03 - the number of bits set in transMask
    uint transMask[3];      // 0x04 - bit b set: bone b has animated translation tracks
    _VECTOR boneOffset[1];  // 0x10 - [numBones] each bone's translation when it has none, unscaled
} SkeletonInfo;

// An attachment point on a skin: weapons, gadgets, effects. weapon_data's weaponDatum holds such ids.
typedef struct AnimDatum {
    int id;                 // 0x00
    int bone;               // 0x04 - -1 for none
    _VECTOR t;              // 0x08 - in the bone's space
    quaternion_tag q;       // 0x14
} AnimDatum;

// Built by AnimProcessSkinData from a 0x05xxxxxx skin file; the hashtable entry for the file points at this
// afterwards. The pointers point back into the file data, which stays loaded.
// Names from the Xbox Ghidra database (PS2 names in brackets; the PS2 struct is 0x40 bytes and laid out differently).
typedef struct sAnimSkin_tag {
    HASHCODE hashcode;          // 0x00 written by AnimObjectNew, not at load
    Ptr32<celglist_tag> *riggedBody;    // 0x04 numRigged entries in the file, its hashcodes made glists (riggedBodyList)
    Ptr32<celglist_tag> *discreteObj;   // 0x08 numDiscrete entries, the same (discreteObjList)
    uchar *discreteBone;        // 0x0c numDiscrete bytes (glistToBoneLinkage)
    _VECTOR skinScale;          // 0x10
    AnimDatum *datums;          // 0x1c numDatums entries
    uchar *bones;               // 0x20 one byte per skeleton bone: ANIM_BONE_USED, and the parent (boneHierarchy)
    _MATRIX *invBind;           // 0x24 one per skeleton bone, only when numRigged != 0 (boneMatrixBuffer)
    uchar numRigged;            // 0x28 (numRiggedBodies)
    uchar numDiscrete;          // 0x29 (numDiscreteObjs)
    uchar numMorphs;            // 0x2a (maybeNumMorphs)
    uchar numDatums;            // 0x2b
    uchar skeletonNum;          // 0x2c index into SkeletonTable
    uchar _pad[3];              // 0x2d the allocation is 0x30 bytes
} sAnimSkin_tag;

// sAnimSkin_tag::bones
#define ANIM_BONE_USED          0x80
#define ANIM_BONE_PARENT_MASK   0x7f
#define ANIM_BONE_ROOT          0x7f

// The bones head tracking and aiming turn
#define ANIM_HEADTRACK_BONE 5
#define ANIM_AIM_BONE 2

// What a script, an object or a set is told
typedef void (*AnimScriptEventFunc)(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, ushort event);
typedef void (*AnimScriptStopFunc)(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script);
typedef void (*AnimRootMotionFunc)(obj_tag *obj, AnimObj *animObj, _VECTOR *delta);
typedef void (*AnimPostPoseFunc)(obj_tag *obj, AnimObj *animObj, int numBones, _VECTOR *trans, quaternion_tag *quats);
typedef void (*AnimSeqFrameFunc)(obj_tag *obj, sAnimSkin_tag *skin, sAnimSeq_tag *seq, sAnimScript_tag *script);

// The animated part of an object (sAnimObject_tag), embedded in AnimState at +0x58.
// The pose is _VECTOR trans[numBones] then quaternion_tag quats[numBones], each parent-relative, the translations
// already multiplied by the skin's scale.
typedef struct AnimObj {
    const void *headTrackTarget;    // 0x00 - an obj_tag with ANIMOBJ_HEAD_TARGET_OBJ, else a position
    const void *aimTarget;          // 0x04 - an obj_tag with ANIMOBJ_AIM_AT_BONE, else a position
    float boundSphere[4];           // 0x08 - from the body sequence (AnimSeqSet)
    AnimRootMotionFunc rootMotionCallback;  // 0x18 - told how far the root moved (AnimFrameResolve)
    sAnimSkin_tag *skin;            // 0x1c
    sAnimSet_tag *animSets;         // 0x20 - up to four (AnimSetAppend)
    sAnimScript_tag *firstScript;   // 0x24
    AnimPostPoseFunc postPoseCallback;      // 0x28 - shown the pose at the end of AnimFrameResolve
    celglist_tag **datumGlist;      // 0x2c - [numDatums] what is drawn at each datum
    celglist_tag *sleeveGlist;      // 0x30 - drawn for rigged body 0 when set (AnimObjectSetSleeveType)
    float *pose;                    // 0x34 - poseBufSize bytes, as above
    float *morphWeights;            // 0x38 - [numMorphs]
    uchar *datumTimer;              // 0x3c - [numDatums] frames each datum is drawn for: 0 none, 0xff always
    _MATRIX *boneMtxs;              // 0x40 - [numBones] in the world, built by psiBuildMatrixPalette
    _MATRIX *palette;               // 0x44 - [numBones] the skinning matrices
    obj_tag **discreteObj;          // 0x48 - [skin->numDiscrete] the rigid parts, each following one bone
    uint lastUpdateFrame;           // 0x4c - GameState.NumFrames when last updated or drawn
    uint flags;                     // 0x50 - ANIMOBJ_*
    float rootHeight;               // 0x54
    float heightOffset;             // 0x58 - added to the root's height (AnimObjectNew)
    float distanceThisTick;         // 0x5c - AnimSetUpdate's forward speed times the set's scale
    float rootTravel;               // 0x60 - how far the object moved this frame (Player_Update; Ghidra's name)
    float distancePhase;            // 0x64 - where the walk is along its sequence's distance table
    float headTrackSpeed;           // 0x68 - NDrone2_HeadTrackObj
    float aimRate;                  // 0x6c
    float headTrackRange2;          // 0x70 - the head tracking range, squared
    float aimRange2;                // 0x74 - the aiming range, squared
    float headYaw;                  // 0x78
    float aimYaw;                   // 0x7c
    float headPitch;                // 0x80
    float aimPitch;                 // 0x84
    ushort poseBufSize;             // 0x88 - numBones * 0x1c
    ushort facialTimer;             // 0x8a - frames to the next blink
    uchar headTrackTargetBone;      // 0x8c - with ANIMOBJ_HEAD_TARGET_OBJ, the bone of the target looked at
    uchar aimTargetBone;            // 0x8d
    uchar headTrackDelay;           // 0x8e - frames before the head starts to turn (AnimObjectSetHeadTracking)
    uchar lookMorphX;               // 0x8f - the eyes, 0..255 (AnimObjectHeadTrack)
    uchar lookMorphY;               // 0x90
    uchar facialState;              // 0x91 - FACIAL_*, and FACIAL_QUICK_BLINK
    char _unknown_92[2];
    uchar lod[4];                   // 0x94 - per viewport, how far away (AnimObjectIsClose)
} AnimObj;

// AnimObj::flags
#define ANIMOBJ_POSE_VALID      0x1     // a pose has been resolved: nothing is drawn or asked of the bones before
#define ANIMOBJ_FULL_CHARACTER  0x2     // a skeleton of more than 70 bones: faces, datum weapons, the height offset
#define ANIMOBJ_BONES_VALID     0x4     // boneMtxs are this frame's
#define ANIMOBJ_PALETTE_VALID   0x8     // palette is this frame's
#define ANIMOBJ_HEAD_TRACK      0x10    // head tracking asked for (AnimObjectSetHeadTracking)
#define ANIMOBJ_HEAD_TRACKING   0x30    // AnimObjectIsHeadTracking
#define ANIMOBJ_HEAD_TURNED     0x20    // the head is turned, or turning back
#define ANIMOBJ_HEAD_TARGET_OBJ 0x40    // headTrackTarget is an object, looked at by headTrackTargetBone
#define ANIMOBJ_AIMING          0x80
#define ANIMOBJ_AIMING_2        0x100   // with ANIMOBJ_AIMING, AnimObjectIsAiming: aimed, or turning back
#define ANIMOBJ_AIM_AT_BONE     0x200   // aimTargetBone is used
#define ANIMOBJ_ALT_SKELETON    0x400   // a full character whose skeletonNum is not 1
#define ANIMOBJ_AIM_IN_RANGE    0x1000
#define ANIMOBJ_AIM_YAW_LIMIT   0x2000
#define ANIMOBJ_AIM_PITCH_LIMIT 0x4000
#define ANIMOBJ_INV_SKIN_SCALE  0x8000  // the root's translation is divided by the skin's scale
#define ANIMOBJ_NO_FACE         0x10000 // no facial animation (AnimFacialUpdate)
#define ANIMOBJ_MUTE            0x40000 // script sounds and events are not played

// AnimObj::facialState
#define FACIAL_START        0
#define FACIAL_IDLE         1
#define FACIAL_TALKING      2
#define FACIAL_QUICK_BLINK  0x80    // the next blink follows quickly

typedef struct AnimState {
    HITDATA_tag floor; // 0x00 - what Drone_FeetOnPoint found the feet on
    char animFlags; // 0x50 - bit one when weapon is zoomed in; ANIM_CONTACT_* for drones; ANIM_FOOTSTEP
    char inWater;     // 0x51 - bit 0: the player is in water (Player_InWater)
    char currentWeaponId; // 0x52
    char switchingToWeaponId; // 0x53
    char prevHeldWeaponId; // 0x54
    char _pad_3[3]; // 55-57
    AnimObj animObj; // 0x58
} AnimState;

// AnimState::animFlags, for drones (Drone_FeetOnPoint, Drone_CollisionHandler)
#define ANIM_CONTACT_FLOOR  0x08    // the feet stand on something
#define ANIM_CONTACT_HIT    0x10    // the capsule hit something
#define ANIM_CONTACT_ABOVE  0x20    // the capsule hit something above its top
#define ANIM_CONTACT_MOVER  0x80    // moves with what it stands on (PlayerOrDrone_FeetOnPoint_DoCollision)
// AnimState::animFlags, for the player: the weapon's scope is up
#define ANIM_SCOPED         0x01
// AnimState::animFlags: a footstep this frame (AnimObjectUpdate)
#define ANIM_FOOTSTEP       0x04

// these constants seem to be the same on both PS2 and Xbox
typedef enum {
    ANIM_SCRIPT_STATE_MAYBE_INITIALISED = 0x159f5ab2,
    ANIM_SCRIPT_STATE_MAYBE_DELETED = 0x983c4837,
} AnimScriptStateMagicNumbers;
static_assert(sizeof(AnimScriptStateMagicNumbers) == 4, "Compiler doing something weird with AnimScriptStateMagicNumbers");

// A sequence file (hashcode type 0x04) as loaded; the tracks follow at +0x40. AnimProcessSeqData replaces the
// first three words. A track is a u32 head (its byte size | its first key word << 16), then key words
// (frames << 6 | coefficient mask) taken from u32s low half first, each followed by its float coefficients.
typedef struct sAnimRawSeq_tag {
    Ptr32<uint> trackData;  // 0x00 - in the file: the file's hashcode
    uint trackDataSize;     // 0x04
    Ptr32<uint> cursor;     // 0x08
    uint flags;             // 0x0c - the low four bits are copied to sAnimSeq_tag::flags
    float boundSphere[4];   // 0x10
    float rootBox[6];       // 0x20
    short numFrames;        // 0x38
    uchar numElems;         // 0x3a - the low byte of the track count, the morph count of a morph sequence
    uchar _unknown_3b;
    uchar skeletonNum;      // 0x3c
    uchar _unknown_3d;
    uchar raw3e;            // 0x3e
    uchar _unknown_3f;
} sAnimRawSeq_tag;

// sAnimRawSeq_tag::flags and sAnimSeq_tag::flags
#define ANIMSEQ_DISTANCE_AXES   0x7         // the axes its distance table measures
#define ANIMSEQ_MORPH           0x8         // a facial sequence: one track per morph
#define ANIMSEQ_FIRST_TRANS     0x100000    // firstTrans is set
#define ANIMSEQ_UNPAK_CACHE     0x200000    // the key search resumes from unpakCache
#define ANIMSEQ_ZERO_ROOT_X     0x2000000   // the root's motion in x is dropped
#define ANIMSEQ_ZERO_ROOT_Y     0x4000000
#define ANIMSEQ_ZERO_ROOT_Z     0x8000000
#define ANIMSEQ_STOPPED         0x10000000
#define ANIMSEQ_STARTED         0x20000000  // the next tick takes no root motion
#define ANIMSEQ_WRAPPED         0x40000000  // its script looped: no root motion this tick
#define ANIMSEQ_LOOP            0x80000000

// One track's place in the key search, kept for the next frame when ANIMSEQ_UNPAK_CACHE is on
typedef struct AnimUnpakEntry {
    uint keyOffset;     // the key word's offset from the track data, | 1 if the high half of pending is still unread
    ushort pendingHigh; // the high half of the last u32 read
    ushort accBefore;   // the frames of the keys before this one
} AnimUnpakEntry;

// A walk sequence's distance table (AnimDistanceTableGet): how far the root has gone at the end of each frame
typedef struct AnimDistanceTable {
    float length;       // 0x00 - over the whole sequence
    int count;          // 0x04 - frames
    float unknown08;
    float edge[1];      // 0x0c - [count]
} AnimDistanceTable;

// A sequence being sampled for a script (AnimSeqNew).
typedef struct sAnimSeq_tag {
    _VECTOR rootPrev;           // 0x00 - the root's translation at the last tick
    _VECTOR rootDelta;          // 0x0c - how far the root moved this tick
    _VECTOR firstTrans;         // 0x18 - taken one past the last translation (AnimFrameCopy)
    _VECTOR transRelFirst;      // 0x24
    _VECTOR objPosAtSet;        // 0x30
    float rootBox[6];           // 0x3c
    float boundSphere[4];       // 0x54
    AnimDistanceTable *distanceTable;   // 0x64
    float *pose;                // 0x68 - as AnimObj::pose, or float[numElems] for a morph sequence
    AnimUnpakEntry *unpakCache; // 0x6c - [numElems]
    AnimSeqFrameFunc frameCallback;     // 0x70 - samples the frame in place of AnimFrameSet
    sAnimRawSeq_tag *rawSeq;    // 0x74
    HASHCODE hashcode;          // 0x78
    uint flags;                 // 0x7c - ANIMSEQ_*
    float frame;                // 0x80 - 1-based
    short startTime;            // 0x84 - the script frame it starts at
    short firstFrame;           // 0x86
    short lastFrame;            // 0x88
    short numFrames;            // 0x8a
    uchar raw3e;                // 0x8c
    uchar skeletonNum;          // 0x8d
    uchar numElems;             // 0x8e
    uchar _unknown_8f;
} sAnimSeq_tag;

// sAnimScript_tag::flags
#define ANIMSCRIPT_OVERLAY 0x4000           // blended over the rest by its fade (the strafes)
#define ANIMSCRIPT_SHARE_ROOT 0x8000        // keeps the root rotation of the script it blends with
#define ANIMSCRIPT_SCRIPT_DRIVEN 0x10000    // Script_AnimStart and Script_Interp set the frame themselves
#define ANIMSCRIPT_ANIM_ROTATION 0x20000    // the root's turn turns the object (AnimScriptSetAnimRotation)
#define ANIMSCRIPT_MUTE_MORPH 0x40000       // its first sequence is a morph one; the next morph is not set
#define ANIMSCRIPT_NO_MORPH 0x80000         // morph sequences are not set
#define ANIMSCRIPT_FADE_OUT 0x100000        // deleted by AnimFrameResolve once faded out
#define ANIMSCRIPT_FADED 0x200000           // faded out
#define ANIMSCRIPT_FADING 0x400000          // fading out (AnimScriptAppendBlend)
#define ANIMSCRIPT_HAS_ROOT_TRANS 0x800000  // AnimGetFirstTrans only answers for such a script
#define ANIMSCRIPT_ZERO_ROOT_X 0x1000000    // its sequences' root motion in x is dropped
#define ANIMSCRIPT_ZERO_ROOT_Y 0x2000000
#define ANIMSCRIPT_ZERO_ROOT_Z 0x4000000
#define ANIMSCRIPT_GAME_FLAG 0x8000000      // set by the drones and AnimSets; the anim code keeps it
#define ANIMSCRIPT_STOPPED 0x10000000
#define ANIMSCRIPT_STARTED 0x20000000       // its first tick is to come
#define ANIMSCRIPT_WRAPPED 0x40000000
#define ANIMSCRIPT_LOOP 0x80000000

// sAnimScript_tag::speedMode
#define ANIMSPEED_TIME      0
#define ANIMSPEED_DISTANCE  1   // the speed comes from the distance walked (AnimDistanceTableDistanceToFrame2)
#define ANIMSPEED_SYNC      2   // kept in step with syncMaster
#define ANIMSPEED_SEEK      3   // heads for targetPos

// sAnimScript_tag::playMode
#define ANIMPLAY_ONCE       0
#define ANIMPLAY_LOOP       1
#define ANIMPLAY_PINGPONG   2
#define ANIMPLAY_DELETE     3   // once, then deleted

// sAnimScript_tag::groupTag of the facial scripts
#define ANIMTAG_FACE_MOOD   0xe0000000
#define ANIMTAG_FACE_BLINK  0xe0000001
#define ANIMTAG_FACE_SPEECH 0xe0000002

// One playing animation script (hashcode type 0x06).
typedef struct sAnimScript_tag {
    _MATRIX rotMtx;                 // 0x00 - the object's rotation, taken for ANIMSCRIPT_ANIM_ROTATION
    AnimScriptStateMagicNumbers magicStateIndicator; // 0x3c
    sAnimScript_tag *prevScript;    // 0x40
    sAnimScript_tag *nextScript;    // 0x44
    sAnimScript_tag *syncMaster;    // 0x48 - followed with ANIMSPEED_SYNC, and blended into
    sAnimSeq_tag *boneAnimSeq;      // 0x4c
    sAnimSeq_tag *morphAnimSeq;     // 0x50 - only if the skin has morphs
    uchar *boneMask;                // 0x54 - bones taken whole: a byte each, every other byte, 0xff ends it
    uchar *scriptData;              // 0x58 - the 0x06 file
    uchar *doneFlag;                // 0x5c - set to 1 when it stops
    sAnimScript_tag **ownerSlot;    // 0x60 - its owner's pointer to it, cleared when it is deleted
    AnimScriptStopFunc onDelete;    // 0x64
    AnimScriptStopFunc onStop;      // 0x68 - called once, when it stops
    AnimScriptEventFunc onEvent;    // 0x6c
    HASHCODE hashcode;              // 0x70
    uint flags;                     // 0x74
    uint firedMask;                 // 0x78 - one bit per command: it fired last tick
    uint groupTag;                  // 0x7c - its set's tag, or ANIMTAG_FACE_*
    uint timestamp;                 // 0x80 - creation order
    float syncBlend;                // 0x84
    float targetPos;                // 0x88 - ANIMSPEED_SEEK
    float curFrame;                 // 0x8c - 1..numFrames
    float prevFrame;                // 0x90
    float speed;                    // 0x94 - frames a tick; the sign is the direction
    float weight;                   // 0x98 - fadeTime / fadeLen
    float rotBaseYaw;               // 0x9c - 8.0: taken afresh on the next update
    float rotLastYaw;               // 0xa0
    float fadeTime;                 // 0xa4 - the blend's counter
    float fadeLen;                  // 0xa8 - frames the blend takes
    short numFrames;                // 0xac
    uchar numCmds;                  // 0xae
    uchar rate;                     // 0xaf
    uchar speedMode;                // 0xb0 - ANIMSPEED_*
    uchar playMode;                 // 0xb1 - ANIMPLAY_*
    char fadeDir;                   // 0xb2 - 1 in, -1 out, 0 held
    uchar _pad_b3;
} sAnimScript_tag;

// A locomotion set: an idle and walk scripts kept in step and blended by speed, and a strafe overlay. The list is
// idle, walks slowest first, 0, then the strafe scripts.
typedef struct sAnimSet_tag {
    sAnimSet_tag *prev;             // 0x00
    sAnimSet_tag *next;             // 0x04
    uint walkTimestamp;             // 0x08
    uint blendTimestamp;            // 0x0c
    sAnimScript_tag *blendScript;   // 0x10
    sAnimScript_tag *strafeScript;  // 0x14
    HASHCODE *speedList;            // 0x18
    HASHCODE *strafeList;           // 0x1c
    uint tag;                       // 0x20 - its scripts' groupTag
    float a;                        // 0x24 - the distance phase it starts from
    float b;                        // 0x28 - the speed's scale to distance
    ushort idleTimer;               // 0x2c
    char speedIndex;                // 0x2e
    uchar strafeDir;                // 0x2f
    uchar switchCooldown;           // 0x30
    uchar speedCount;               // 0x31
    uchar strafeCount;              // 0x32
    uchar oneShotState;             // 0x33
} sAnimSet_tag;

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

#pragma pack(pop)

static_assert(sizeof(AnimCollBone) == 0x14, "Bad size for AnimCollBone");
static_assert(sizeof(COLLJOINT_tag) == 0x4c, "Bad size for COLLJOINT_tag");
LAYOUT_32(offsetof(AnimState, animObj) == 0x58, "Offset of animObj wrong");
LAYOUT_32(offsetof(AnimState, animFlags) == 0x50, "Offset of animFlags wrong");
LAYOUT_32(sizeof(AnimState) == 0xf0, "Bad size for AnimState");
LAYOUT_32(sizeof(AnimObj) == 0x98, "Bad size for AnimObj");
LAYOUT_32(offsetof(AnimObj, aimTarget) == 0x04, "Offset of aimTarget wrong");
LAYOUT_32(offsetof(AnimObj, rootMotionCallback) == 0x18, "Offset of rootMotionCallback wrong");
LAYOUT_32(offsetof(AnimObj, skin) == 0x1c, "Offset of skin wrong");
LAYOUT_32(offsetof(AnimObj, firstScript) == 0x24, "Offset of firstScript wrong");
LAYOUT_32(offsetof(AnimObj, sleeveGlist) == 0x30, "Offset of sleeveGlist wrong");
LAYOUT_32(offsetof(AnimObj, pose) == 0x34, "Offset of pose wrong");
LAYOUT_32(offsetof(AnimObj, boneMtxs) == 0x40, "Offset of boneMtxs wrong");
LAYOUT_32(offsetof(AnimObj, discreteObj) == 0x48, "Offset of discreteObj wrong");
LAYOUT_32(offsetof(AnimObj, flags) == 0x50, "Offset of flags wrong");
LAYOUT_32(offsetof(AnimObj, rootHeight) == 0x54, "Offset of rootHeight wrong");
LAYOUT_32(offsetof(AnimObj, distancePhase) == 0x64, "Offset of distancePhase wrong");
LAYOUT_32(offsetof(AnimObj, aimRate) == 0x6c, "Offset of aimRate wrong");
LAYOUT_32(offsetof(AnimObj, headTrackRange2) == 0x70, "Offset of headTrackRange2 wrong");
LAYOUT_32(offsetof(AnimObj, aimRange2) == 0x74, "Offset of aimRange2 wrong");
LAYOUT_32(offsetof(AnimObj, aimPitch) == 0x84, "Offset of aimPitch wrong");
LAYOUT_32(offsetof(AnimObj, poseBufSize) == 0x88, "Offset of poseBufSize wrong");
LAYOUT_32(offsetof(AnimObj, headTrackTargetBone) == 0x8c, "Offset of headTrackTargetBone wrong");
LAYOUT_32(offsetof(AnimObj, aimTargetBone) == 0x8d, "Offset of aimTargetBone wrong");
LAYOUT_32(offsetof(AnimObj, headTrackDelay) == 0x8e, "Offset of headTrackDelay wrong");
LAYOUT_32(offsetof(AnimObj, facialState) == 0x91, "Offset of facialState wrong");
LAYOUT_32(offsetof(AnimObj, lod) == 0x94, "Offset of lod wrong");
static_assert(sizeof(AnimDatum) == 0x24, "Bad size for AnimDatum");
static_assert(sizeof(_MATRIX) == 0x3c, "AnimProcessSkinData allocates invBind as numBones * 0x3c");
static_assert(offsetof(SkeletonInfo, boneOffset) == 0x10, "Offset of boneOffset wrong");
static_assert(offsetof(sAnimSkin_tag, riggedBody) == 0x04, "Bad offset of riggedBody");
LAYOUT_32(offsetof(sAnimSkin_tag, skinScale) == 0x10, "Bad offset of skinScale");
LAYOUT_32(offsetof(sAnimSkin_tag, datums) == 0x1c, "Bad offset of datums");
LAYOUT_32(offsetof(sAnimSkin_tag, bones) == 0x20, "Bad offset of bones");
LAYOUT_32(offsetof(sAnimSkin_tag, invBind) == 0x24, "Bad offset of invBind");
LAYOUT_32(offsetof(sAnimSkin_tag, numRigged) == 0x28, "Bad offset of numRigged");
LAYOUT_32(offsetof(sAnimSkin_tag, numMorphs) == 0x2a, "Bad offset of numMorphs");
LAYOUT_32(offsetof(sAnimSkin_tag, skeletonNum) == 0x2c, "Bad offset of skeletonNum");
LAYOUT_32(sizeof(sAnimSkin_tag) == 0x30, "Bad size for sAnimSkin_tag");
static_assert(sizeof(sAnimRawSeq_tag) == 0x40, "Bad size for sAnimRawSeq_tag");
static_assert(offsetof(sAnimRawSeq_tag, numFrames) == 0x38, "Offset of numFrames wrong");
static_assert(sizeof(AnimUnpakEntry) == 8, "Bad size for AnimUnpakEntry");
LAYOUT_32(sizeof(sAnimSeq_tag) == 0x90, "Bad size for sAnimSeq_tag");
static_assert(offsetof(sAnimSeq_tag, rootBox) == 0x3c, "Offset of rootBox wrong");
LAYOUT_32(offsetof(sAnimSeq_tag, pose) == 0x68, "Offset of pose wrong");
LAYOUT_32(offsetof(sAnimSeq_tag, rawSeq) == 0x74, "Offset of rawSeq wrong");
LAYOUT_32(offsetof(sAnimSeq_tag, frame) == 0x80, "Offset of frame wrong");
LAYOUT_32(offsetof(sAnimSeq_tag, numFrames) == 0x8a, "Offset of numFrames wrong");
LAYOUT_32(sizeof(sAnimScript_tag) == 0xb4, "Bad size for sAnimScript_tag");
static_assert(offsetof(sAnimScript_tag, magicStateIndicator) == 0x3c, "Offset of magicStateIndicator wrong");
LAYOUT_32(offsetof(sAnimScript_tag, boneAnimSeq) == 0x4c, "Offset of boneAnimSeq wrong");
LAYOUT_32(offsetof(sAnimScript_tag, ownerSlot) == 0x60, "Offset of ownerSlot wrong");
LAYOUT_32(offsetof(sAnimScript_tag, onStop) == 0x68, "Offset of onStop wrong");
LAYOUT_32(offsetof(sAnimScript_tag, onEvent) == 0x6c, "Offset of onEvent wrong");
LAYOUT_32(offsetof(sAnimScript_tag, flags) == 0x74, "Offset of flags wrong");
LAYOUT_32(offsetof(sAnimScript_tag, curFrame) == 0x8c, "Offset of curFrame wrong");
LAYOUT_32(offsetof(sAnimScript_tag, speed) == 0x94, "Offset of speed wrong");
LAYOUT_32(offsetof(sAnimScript_tag, rotBaseYaw) == 0x9c, "Offset of rotBaseYaw wrong");
LAYOUT_32(offsetof(sAnimScript_tag, fadeLen) == 0xa8, "Offset of fadeLen wrong");
LAYOUT_32(offsetof(sAnimScript_tag, numFrames) == 0xac, "Offset of numFrames wrong");
LAYOUT_32(offsetof(sAnimScript_tag, fadeDir) == 0xb2, "Offset of fadeDir wrong");
LAYOUT_32(sizeof(sAnimSet_tag) == 0x34, "Bad size for sAnimSet_tag");
LAYOUT_32(offsetof(sAnimSet_tag, tag) == 0x20, "Offset of tag wrong");
LAYOUT_32(offsetof(sAnimSet_tag, oneShotState) == 0x33, "Offset of oneShotState wrong");

// SkeletonInfo *SkeletonTable[128]
extern SkeletonInfo *SkeletonTable[128];

// The animation code's working list (AnimListBuild)
extern int32_t GlbAnimListCount;
extern sAnimScript_tag *GlbAnimList[32];
// The foot the last footstep command named (AnimProcessScriptCmds)
extern uint8_t FootstepFoot;

// AnimListBuild's criteria, tested on each of the object's scripts against value
#define ANIMLIST_ALL            0x1
#define ANIMLIST_NOT            0x2     // the script is not value
#define ANIMLIST_FIRST          0x4
#define ANIMLIST_LAST           0x8
#define ANIMLIST_STOPPED        0x10
#define ANIMLIST_HASHCODE       0x20
#define ANIMLIST_IS_OR_SYNCED   0x40    // the script is value, or follows it
#define ANIMLIST_SYNCED_TO      0x80    // the script follows value
#define ANIMLIST_SYNCED         0x100   // the script follows another
#define ANIMLIST_TAG            0x200   // groupTag is value
#define ANIMLIST_TAG_BITS       0x400   // groupTag has a bit of value
#define ANIMLIST_MUTE_MORPH     0x800   // ANIMSCRIPT_MUTE_MORPH
#define ANIMLIST_MORPH          0x1000  // its morph sequence is a morph one
#define ANIMLIST_MASKED         0x2000  // it has a bone mask
#define ANIMLIST_OVERLAY        0x4000  // ANIMSCRIPT_OVERLAY
// AnimListBuild's mode
#define ANIMLIST_RESTART        0x1     // the list starts empty
#define ANIMLIST_REMOVE         0x2     // the scripts that pass leave the list

typedef void (*AnimListFunc)(obj_tag *obj, sAnimScript_tag *script);

void AnimPreLoadInit(char keepSkeletons);
void AnimLoadFile(HASHCODE hashcode, char noProcess);
void AnimProcessSeqData(HASHCODE hashcode);
void AnimPostLoadInit(void);
void AnimSkeletonProcess(char *data);
// Register-argument adaptor (hashcode in EAX) patched over the original; never call it from C.
void AnimProcessSkinData(HASHCODE hashcode);
// The C body of AnimProcessSkinData
void _AnimProcessSkinData(HASHCODE hashcode);
HASHCODE _AnimSleeveGetEntity(uint idx);

sAnimScript_tag* AnimListBuild(obj_tag *obj, uint criteria, uint value, AnimListFunc callback, uint mode);
void AnimListDelete(obj_tag *obj);
sAnimScript_tag* AnimScriptAdd(obj_tag *obj, HASHCODE hashcode);
sAnimScript_tag* AnimScriptAddSpeed(obj_tag *obj, HASHCODE hashcode, float speed);
sAnimScript_tag* AnimScriptAddReverse(obj_tag *obj, HASHCODE hashcode);
sAnimScript_tag* AnimScriptAppend(obj_tag *obj, HASHCODE hashcode);
sAnimScript_tag* AnimScriptAppendBlend(obj_tag *obj, HASHCODE hashcode);
void AnimScriptSetPlayDirection(obj_tag *obj, int direction);
void AnimScriptDelete(obj_tag *obj, sAnimScript_tag **slot);
int AnimScriptIsStopped(obj_tag *obj);
void AnimScriptCallOnEvent(sAnimScript_tag *script, AnimScriptEventFunc onEvent);
void AnimScriptCallOnStop(sAnimScript_tag *script, AnimScriptStopFunc onStop);
void AnimScriptSetAnimRotation(sAnimScript_tag *script, char on);
sAnimScript_tag* AnimFacialAppend(obj_tag *obj, HASHCODE hashcode);

int AnimDatumGetIndex(obj_tag *obj, int id);
void AnimDatumSetEntity(obj_tag *obj, int id, uint entity, uchar frames);
int AnimDatumGetWeaponInfo(obj_tag *obj, HASHCODE *gfx, HASHCODE *flash, ushort *sound);

int AnimObjectNew(obj_tag *obj, HASHCODE skin);
void AnimObjectDelete(obj_tag *obj);
int AnimObjectUpdate(obj_tag *obj);
int AnimObjectUpdateThunk(obj_tag *obj);
void Discrete_Delete(obj_tag *obj);
sAnimRawSeq_tag* AnimScriptPeekSeq(HASHCODE script);
void AnimDebugCreate(void);
float _AnimObjectInitialPose(obj_tag *obj);
bool AnimObjectIsClose(obj_tag *obj, uint limit);
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer);
void AnimObjectSetSleeveType(obj_tag *obj, int sleeveNum);
obj_tag* AnimGetNDiscreteObj(obj_tag *obj, uint n);
COLLJOINT_tag* AnimGetCollData(obj_tag *obj, ushort *count);
bool AnimObjectIsAiming(obj_tag *obj);
bool AnimObjectIsHeadTracking(obj_tag *obj);
void AnimObjectSetAiming(obj_tag *obj, const void *target, float range, float rate, char atBone, uchar bone);
void AnimObjectSetHeadTracking(obj_tag *obj, const void *target, float range, float rate, char randomDelay,
                               char targetIsObj, uchar bone);

void AnimProcessScriptCmds(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, int from, int to);
void AnimSet_WalkUpdater(obj_tag *obj, sAnimSkin_tag *skin, sAnimSeq_tag *seq, sAnimScript_tag *script);
void AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frame);
void AnimFrameResolve(obj_tag *obj, AnimObj *animObj);
void psiBuildMatrixPalette(obj_tag *obj, AnimObj *animObj, char buildPalette);
void AnimObjectHeadTrack(obj_tag *obj, AnimObj *animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);
void AnimObjectAimAt(obj_tag *obj, AnimObj *animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);
void AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut);
void AnimGetBoneWorldTrans2(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut);
bool AnimGetFirstTrans(sAnimScript_tag *script, _VECTOR *out);
void AnimGetFrameTransRaw(HASHCODE hashcode, int frame, _VECTOR *out);

void AnimSetDeleteAll(obj_tag *obj);
void AnimSetDelete(obj_tag *obj, sAnimSet_tag **slot);
sAnimSet_tag* AnimSetAppend(obj_tag *obj, HASHCODE *list, float a, float b);
void AnimSetUpdate(obj_tag *obj, sAnimSet_tag *set, float speed, float forward, float maxForward, float side,
                   float maxSide);

// Register-argument adaptors patched over the originals (the registers in Anim.cpp); never call them from C.
HASHCODE AnimSleeveGetEntity(void);
void loadAnimFileCapturingSize(void);
void AnimProcessScriptData(void);
void AnimSeqNew(void);
void AnimSeqGetInfo(void);
void AnimSeqSet(void);
void AnimScriptNew(void);
void AnimScriptInit(void);
void AnimScriptEnd(void);
void AnimDistanceTableDistanceToFrame2(void);
AnimDistanceTable* _AnimDistanceTableCreate(uint axes, HASHCODE hashcode, int noWrap);
AnimDistanceTable* _AnimDistanceTableGet(HASHCODE hashcode);
AnimDistanceTable* AnimDistanceTableCreate(void);
AnimDistanceTable* AnimDistanceTableGet(void);
void AnimScriptTick(void);
void AnimFacialUpdate(void);
void AnimFacialBlend(void);
void AnimFrameBlend(void);
void AnimFrameSet(void);
void AnimSeqTick(void);
HASHCODE maybeFixupSkinHashcode(void);
void AnimObjectInitialPose(void);
void AnimTagAllocAndGetUnique(void);
void AnimSetInit(void);

extern const HASHCODE FacialAnims[5], FacialAnimsAltSkeleton[5];
extern const AnimCollBone HumanCollisionBones[17], AstronautCollisionBones[17];

#endif // ANIM_H
