#ifndef OBJECT_H_
#define OBJECT_H_

#include "../../actionhelpers.h"

#include "../../math/math.h"

#include "../../gfx/Animation.h"


typedef enum {
    OBJECTTYPE_DELETED=1,
    OBJECTTYPE_DRONE=2,
    OBJECTTYPE_PLAYER=3,
    OBJECTTYPE_BULLET=5,
    OBJECTTYPE_ROTOR_HELI_MAYBE=7,
    OBJECTTYPE_PARTICLES=12,
    OBJECTTYPE_CASING=13,
    OBJECTTYPE_GAS=14,
    OBJECTTYPE_EXPLODE=15,
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
    OBJECTTYPE_CREATURE=35,
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

inline const char* Object_GetName(ObjectType type) {
    switch (type) {
        case OBJECTTYPE_DELETED: return "Deleted";
        case OBJECTTYPE_DRONE: return "Drone";
        case OBJECTTYPE_PLAYER: return "Player";
        case OBJECTTYPE_BULLET: return "Bullet";
        case OBJECTTYPE_ROTOR_HELI_MAYBE: return "Rotor Heli Maybe";
        case OBJECTTYPE_PARTICLES: return "Particles";
        case OBJECTTYPE_CASING: return "Casing";
        case OBJECTTYPE_GAS: return "Gas";
        case OBJECTTYPE_EXPLODE: return "Explode";
        case OBJECTTYPE_EFFECT: return "Effect";
        case OBJECTTYPE_DEAD_DRONE: return "Dead Drone";
        case OBJECTTYPE_DEAD_PLAYER: return "Dead Player";
        case OBJECTTYPE_ANIMDEBUGHANDLER: return "Anim Debug Handler";
        case OBJECTTYPE_RIGIDBODY: return "RigidBody";
        case OBJECTTYPE_SCRIPTPLAYER: return "Script Player";
        case OBJECTTYPE_DOOR: return "Door";
        case OBJECTTYPE_TRIGGER: return "Trigger";
        case OBJECTTYPE_BREAK: return "Break";
        case OBJECTTYPE_DESTROY: return "Destroy";
        case OBJECTTYPE_SPOTLIGHT: return "Spotlight";
        case OBJECTTYPE_CREATURE: return "Creature";
        case OBJECTTYPE_CLOUD: return "Cloud";
        case OBJECTTYPE_SIMPLESCRIPT: return "Simple Script";
        case OBJECTTYPE_FLICKER: return "Flicker";
        case OBJECTTYPE_HURT: return "Hurt";
        case OBJECTTYPE_CAR: return "Car";
        case OBJECTTYPE_OCCLUDE: return "Occlude";
        case OBJECTTYPE_CREEPWALL: return "Creep Wall";
        case OBJECTTYPE_THIRDCAM_OR_WIRE: return "Third Cam or Wire";
        case OBJECTTYPE_LADDER: return "Ladder";
        case OBJECTTYPE_LEAF: return "Leaf";
        case OBJECTTYPE_MP_PICKUP: return "MP Pickup";
        case OBJECTTYPE_DRONE_SPAWNER: return "Drone Spawner";
        case OBJECTTYPE_LEAFGEN: return "Leaf Generator";
        case OBJECTTYPE_EMITTER: return "Emitter";
        case OBJECTTYPE_LIGHTNING: return "Lightning";
        case OBJECTTYPE_RIPPLES: return "Ripples";
        case OBJECTTYPE_MPOBJECT: return "MP Object";
        case OBJECTTYPE_GUNTURRET: return "Gun Turret";
        case OBJECTTYPE_SENSOR: return "Sensor";
        case OBJECTTYPE_MONITOR: return "Monitor";
        case OBJECTTYPE_SWITCH: return "Switch";
        case OBJECTTYPE_LOCK: return "Lock";
        case OBJECTTYPE_COPTER: return "Copter";
        case OBJECTTYPE_FUSEBOX: return "Fuse Box";
        case OBJECTTYPE_ANIMOBJECT: return "Anim Object";
        case OBJECTTYPE_DRONE_AIVOLUME: return "Drone AI Volume";
        case OBJECTTYPE_TREE: return "Tree";
        case OBJECTTYPE_SOUNDTRIGGER: return "Sound Trigger";
        case OBJECTTYPE_SWOOSH: return "Swoosh";
        case OBJECTTYPE_THIRDICON: return "Third Icon";
        case OBJECTTYPE_HINT: return "Hint";
        case OBJECTTYPE_MUSIC_TRIGGER: return "Music Trigger";
        case OBJECTTYPE_CORONA: return "Corona";
        case OBJECTTYPE_BODYGLOW: return "Body Glow";
        case OBJECTTYPE_GUNTURRET2: return "Gun Turret 2";
        case OBJECTTYPE_CAMSUBJECT: return "Cam Subject";
        case OBJECTTYPE_QWORM: return "Q Worm";
        case OBJECTTYPE_SUB: return "Sub";
        case OBJECTTYPE_MINE: return "Mine";
        case OBJECTTYPE_ONESIDED: return "One-Sided";
        case OBJECTTYPE_MINISUB: return "Mini Sub";
        case OBJECTTYPE_SHOOTER: return "Shooter";
        case OBJECTTYPE_SPACELASER: return "Space Laser";
        case OBJECTTYPE_GRAPPLE: return "Grapple";
        case OBJECTTYPE_DYNAMICOBJECT: return "Dynamic Object";
        case OBJECTTYPE_INVERTER_TRIGGER: return "Inverter Trigger";
        case OBJECTTYPE_APOCALYPSE: return "Apocalypse";
        default: return "Unknown";
    }
}


// A base object consists of the minimum functionality required to be updated, rendered, collided with, destroyed, etc.
// All specific object types will consist of this, plus additional data specific to that object type (pointed to by extraObjectData)
#pragma pack(push, 1)

// Forward declaration of obj_tag so that it can be used within the struct
struct obj_tag;

typedef struct obj_tag {
    // TODO: Complete this
    LLNODE_tag* llPrev; // Required at the start of the struct for insertion into eg ForcedList
    LLNODE_tag* llNext;
    obj_tag* prevInCel; // 0x8 -- doubly-linked list
    obj_tag *nextInCel; // 0xC
    char pad_0000[4];
    obj_tag *nextObject; // 0x14 - doubly-linked list
    obj_tag* prevObject; // 0x18
    obj_tag* maybeParent; // 0x1c
    cel_tag* inCel; // 0x20 - cel_tag
    _VECTOR position; // 0x24
    _VECTOR lastPosition; // 0x28
    _VECTOR rotation; // 0x2C
    _VECTOR lastRotation; // 0x30
    char _pad_2[12];
    _VECTOR centrePoint; // 0x60
    float radius; // 0x6C
    _MATRIX transformMatrix; // 0x70
    HITDATA_tag* hitList; // 0xAC - HITLIST_tag
    void* maybeCollision; // 0xB0
    celglist_tag* objGraphics; // 0xB4
    AnimState* animState; // 0xB8
    void* extraObjectData; // 0xBC
    void* scriptPlayer;
    float scale;
    int creationTimeFrames;
    int specialFlags; // 0xCC, unclear what the meaning is but sometimes relevant for rendering or object state or straddle tests?
    unsigned short curState; // 0xD0
    unsigned short subState; // MovementType (Player), PlayerNum (Car)
    ushort unknown_0xd4;
    ushort renderType;
    char unknown_0xd8;
    char _pad_6;
    char flags; // (1 == Marked for deletion)
    char objectType; // Actually ObjectType but can't tell the compiler to make it just one byte
    char light_related1;
    char light_related2;
    char light_related3;
    char tweakR;
    char tweakG;
    char tweakB;
    char maybeBrightness; // Maybe glow intensity?
    char _pad_8;
} obj_tag;
#pragma pack(pop)

typedef enum{
    FLAG_UNKNOWN_40 = 0x40,
    FLAG_IN_FORCEDLIST = 0x30000000,
} ObjectSpecialFlags;

//char (*__kaboom)[offsetof(obj_tag,objectType)] = 1;
static_assert(offsetof(obj_tag, position) == 0x24, "Offset of position not correct");
static_assert(offsetof(obj_tag, transformMatrix) == 0x70, "Offset of transformMatrix not correct");
static_assert(offsetof(obj_tag, extraObjectData) == 0xbc, "Offset of extraObjectData not correct");
static_assert(offsetof(obj_tag, specialFlags) == 0xcc, "Offset of specialFlags not correct");
static_assert(offsetof(obj_tag, flags) == 0xda, "Offset of flags not correct");
static_assert(offsetof(obj_tag, objectType) == 0xdb, "Offset of objectType not correct");
static_assert(offsetof(obj_tag, tweakB) == 0xe1, "Offset of tweakB not correct");

static_assert(sizeof(obj_tag) == 0xe4, "Size of obj_tag wrong");

#endif // OBJECT_H_