#ifndef OBJECT_H_
#define OBJECT_H_

#include "../../math/math.h"

#include <stddef.h>

typedef enum {
    OBJECTTYPE_DELETED=1,
    OBJECTTYPE_DRONE=2,
    OBJECTTYPE_PLAYER=3,
    OBJECTTYPE_BULLET=5,
    OBJECTTYPE_ROTOR_HELI_MAYBE=7,
    OBJECTTYPE_PARTICLES=12,
    OBJECTTYPE_CASING=13,
    OBJECTTYPE_GAS=14,
    OBJECTTYPE_MINE1=15,
    OBJECTTYPE_EFFECT=16,
    OBJECTTYPE_DEAD_DRONE=17,
    OBJECTTYPE_DEAD_PLAYER=18,
    OBJECTTYPE_ANIMDEBUGHANDLER=21,
    OBJECTTYPE_RIGIDBODY=26,
    OBJECTTYPE_SCRIPTPLAYER=27,
    OBJECTTYPE_DOOR=28,
    OBJECTTYPE_TRIGGER=29,
    OBJECTTYPE_BREAK=32,
    OBJECTTYPE_DESTROY=33,
    OBJECTTYPE_SPOTLIGHT=34,
    OBJECTTYPE_CLOUD=36,
    OBJECTTYPE_SIMPLESCRIPT=37,
    OBJECTTYPE_FLICKER=38,
    OBJECTTYPE_HURT=39,
    OBJECTTYPE_CAR=40,
    OBJECTTYPE_OCCLUDE=42,
    OBJECTTYPE_CREEPWALL=43,
    OBJECTTYPE_THIRDCAM_OR_WIRE=44,
    OBJECTTYPE_LADDER=45,
    OBJECTTYPE_LEAF=46,
    OBJECTTYPE_MP_PICKUP=47,
    OBJECTTYPE_DRONE_SPAWNER=48,
    OBJECTTYPE_LEAFGEN=49,
    OBJECTTYPE_EMITTER=50,
    OBJECTTYPE_LIGHTNING=51,
    OBJECTTYPE_RIPPLES=52,
    OBJECTTYPE_MPOBJECT=53,
    OBJECTTYPE_GUNTURRET=54,
    OBJECTTYPE_SENSOR=55,
    OBJECTTYPE_MONITOR=56,
    OBJECTTYPE_SWITCH=57,
    OBJECTTYPE_LOCK=58,
    OBJECTTYPE_COPTER=59,
    OBJECTTYPE_FUSEBOX=60,
    OBJECTTYPE_ANIMOBJECT=61,
    OBJECTTYPE_DRONE_AIVOLUME=62,
    OBJECTTYPE_TREE=63,
    OBJECTTYPE_SOUNDTRIGGER=64,
    OBJECTTYPE_SWOOSH=65,
    OBJECTTYPE_THIRDICON=66,
    OBJECTTYPE_HINT=67,
    OBJECTTYPE_MUSIC_TRIGGER=68,
    OBJECTTYPE_CORONA=69,
    OBJECTTYPE_BODYGLOW=70,
    OBJECTTYPE_GUNTURRET2=71,
    OBJECTTYPE_CAMSUBJECT=72,
    OBJECTTYPE_QWORM=73,
    OBJECTTYPE_SUB=74,
    OBJECTTYPE_MINE=75,
    OBJECTTYPE_ONESIDED=76,
    OBJECTTYPE_MINISUB=77,
    OBJECTTYPE_SHOOTER=78,
    OBJECTTYPE_SPACELASER=80,
    OBJECTTYPE_GRAPPLE=81,
    OBJECTTYPE_DYNAMICOBJECT=82,
    OBJECTTYPE_INVERTER_TRIGGER=83,
    OBJECTTYPE_APOCALYPSE=84
} ObjectType;

// A base object consists of the minimum functionality required to be updated, rendered, collided with, destroyed, etc.
// All specific object types will consist of this, plus additional data specific to that object type (pointed to by extraObjectData)
#pragma pack(push, 1)

// Forward declaration of obj_tag so that it can be used within the struct
struct obj_tag;

typedef struct obj_tag {
    // TODO: Complete this
    char _pad_1[0x14];
    obj_tag *nextObject;
    char _pad_11111[0xc];
    _VECTOR position;
    char _pad_2[0x40];
    _MATRIX transformMatrix; // 0x70 - 0xAC
    char _pad_9999[4];
    void* maybeCollision; // 0xB0
    char _pad_3[0x8];
    void* extraObjectData; // 0xBC
    char _pad_4[0xc];
    int someFlags_0xcc; // 0xCC, unclear what the meaning is but sometimes relevant for rendering or object state or straddle tests?
    unsigned short curState; // 0xD0
    unsigned short playerNum; // 0xD2
    char _pad_5[0x4];
    char _pad_6[0x2];
    char flags; // (1 == Marked for deletion)
    char objectType; // Actually ObjectType but can't tell the compiler to make it just one byte
    char _pad_7[0x3]; // Related to lighting
    char tweakR;
    char tweakG;
    char tweakB;
    char _pad_8[2];
} obj_tag;
#pragma pack(pop)

//char (*__kaboom)[offsetof(obj_tag,objectType)] = 1;
static_assert(offsetof(obj_tag, position) == 0x24, "Offset of position not correct");
static_assert(offsetof(obj_tag, transformMatrix) == 0x70, "Offset of transformMatrix not correct");
static_assert(offsetof(obj_tag, maybeCollision) == 0xb0, "Offset of maybeCollision not correct");
static_assert(offsetof(obj_tag, extraObjectData) == 0xbc, "Offset of extraObjectData not correct");
static_assert(offsetof(obj_tag, someFlags_0xcc) == 0xcc, "Offset of someFlags_0xcc not correct");
static_assert(offsetof(obj_tag, objectType) == 0xdb, "Offset of objectType not correct");
static_assert(offsetof(obj_tag, flags) == 0xda, "Offset of flags not correct");
static_assert(offsetof(obj_tag, tweakB) == 0xe1, "Offset of tweakB not correct");

static_assert(sizeof(obj_tag) == 0xe4, "Size of obj_tag wrong");

#endif // OBJECT_H_