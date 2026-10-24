#ifndef OBJECT_H_
#define OBJECT_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#include "../../math/math.h"

typedef enum {
    OBJECTTYPE_0=0,             // a helicopter's body is one (Bullet_heatseekable)
    // The original uses value 1 both for graphics-only child objects and for
    // objects after their delete path has completed.
    OBJECTTYPE_GFX=1,
    OBJECTTYPE_DELETED=OBJECTTYPE_GFX,
    OBJECTTYPE_DRONE=2,
    OBJECTTYPE_PLAYER=3,
    OBJECTTYPE_4=4,             // listed with the drones (MP_GetRadarObjects)
    OBJECTTYPE_BULLET=5,
    OBJECTTYPE_6=6,             // deleted like bullets and effects out of every room (control_handle_cel_change)
    OBJECTTYPE_ROTOR=7,
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
    OBJECTTYPE_PICKUP=47,
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
    OBJECTTYPE_SPACEMISSILE=79,
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
        case OBJECTTYPE_ROTOR: return "Rotor";
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
        case OBJECTTYPE_PICKUP: return "Pickup";
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

// obj_tag::collideFlags
enum ObjCollideFlags {
    OBJCOLL_ACTOR = 0x1,        // a player or a drone: its tests pass through bullets (Collide_PickObj)
    OBJCOLL_BULLET = 0x2,
    OBJCOLL_SPHERE = 0x4,       // a sphere test meets it as a sphere
    OBJCOLL_DOOR_PORTALS = 0x8, // a door: build_LinkDoors2Portals links it to the portals it covers
};

typedef struct obj_tag {
    // TODO: Complete this
    LLNODE_tag* llPrev; // Required at the start of the struct for insertion into eg ForcedList
    LLNODE_tag* llNext;
    obj_tag* nextInCel; // 0x8 - the cel's objects, from cel_tag::firstObject
    obj_tag *prevInCel; // 0xC - (the cel itself for the first)
    struct ATTACH_tag *attachments; // 0x10 - the marks on it (Effect_Bullet, Effect_Player)
    obj_tag *nextObject; // 0x14 - doubly-linked list
    obj_tag* prevObject; // 0x18
    obj_tag* parent; // 0x1c
    cel_tag* inCel; // 0x20 - cel_tag
    _VECTOR position; // 0x24
    _VECTOR lastPosition; // 0x30 - comment previously (incorrectly) said 0x28; confirmed via Ghidra's own struct
    _VECTOR rotation; // 0x3C
    _VECTOR lastRotation; // 0x48
    _VECTOR lastCentrePoint; // 0x54 - centrePoint before Control_BuildWorldSph last moved it
    _VECTOR centrePoint; // 0x60
    float radius; // 0x6C
    _MATRIX transformMatrix; // 0x70
    HITDATA_tag* hitList; // 0xAC - HITLIST_tag
    HITTEST_tag* hitTest; // 0xB0 - the test Collide_Update runs for the object
    celglist_tag* objGraphics; // 0xB4
    AnimState* animState; // 0xB8
    void* extraObjectData; // 0xBC
    void* scriptPlayer; // 0xC0 - cleared by Script_KillStream when an entity/anim stream controlling this
                         // object ends
    float scale;
    int creationTimeFrames;
    int effectFlags;       // 0xCC - EFFECT_*
    unsigned short curState; // 0xD0
    unsigned short subState; // MovementType (Player), PlayerNum (Car)
    ushort displayMask;
    ushort transformFlags; // ObjectTransformFlags: how the update derives the matrix and bounds (0xd6)
    char drawnCountdown;   // 0xd8 - View_AddObjects sets 2, the object update counts it down: non-zero = drawn in
                           //        the last two ticks
    char collideFlags;     // 0xd9 - ObjCollideFlags
    char flags;            // 0xda - ObjectFlags
    char objectType; // Actually ObjectType but can't tell the compiler to make it just one byte
    uchar ambientR;        // 0xdc - the ambient light, eased towards its cel's (Lights_CalcAmbientLight)
    uchar ambientG;
    uchar ambientB;
    uchar tweakR;
    uchar tweakG;
    uchar tweakB;
    uchar tweakA;          // 0xe2 - the alpha of psiSetTweakARGB (View_SetupRenderModes)
    char _pad_8;
} obj_tag;
#pragma pack(pop)

// obj_tag::flags
enum ObjectFlags {
    OBJFLAG_DELETE = 0x1,           // deleted (or reset) by the object list's next update
    OBJFLAG_NEW = 0x2,              // set by control_init_object; the update works out its world sphere and ambient
                                    // light once, then clears it
    OBJFLAG_RIGGED = 0x4,           // an animated object whose skin has rigged bodies, and its discrete parts
    OBJFLAG_FULL_CHARACTER = 0x8,   // an animated object with ANIMOBJ_FULL_CHARACTER
};

// obj_tag::effectFlags: how it is drawn, lit and collided with. The low bits come from its model
// (celglist_tag::applyFlagsToObject, which Control_SetGList copies over all but EFFECT_STRADDLES_CELS and the two
// forced-list bits); the same word gives a cel its flags (cel_tag::flags, CelFlags). EFFECT_BITn: no reader.
typedef enum {
    EFFECT_BACK_TO_FRONT = 0x1,                 // drawn in list 1, sorted far to near
    EFFECT_SKY_AT_EYE = 0x2,                    // a sky model: placed at the eye
    EFFECT_JITTER = 0x4,                        // its scale shaken at random each draw
    EFFECT_BILLBOARD = 0x8,                     // drawn facing the view
    EFFECT_HIDDEN = 0x10,                       // not drawn, and no line of sight stops at it (Script_HideObj)
    EFFECT_NO_COLLISION = 0x20,                 // never a collision candidate, never in ForcedList
    EFFECT_NO_CONTACT = 0x40,                   // its hits are HIT_NO_CONTACT: bullets pass, nothing stands on it
    EFFECT_PLACED_LIGHTS = 0x80,                // lit by LIGHT_PLACED and LIGHT_CARRIED lights
    EFFECT_NO_TRANSIENT_LIGHTS = 0x100,         // not lit by LIGHT_TRANSIENT lights
    EFFECT_BIT9 = 0x200,
    EFFECT_XRAY_ONLY = 0x400,                   // drawn only in VISION_XRAY
    EFFECT_CEL_LIT = 0x800,                     // its ambient eases to its cel's, and tints it
    EFFECT_MASKABLE = 0x1000,                   // skipped by the tests with HITMASK_SKIP_MASKABLE
    EFFECT_DRAW_LAST = 0x2000,                  // drawn in list 2, after lists 0 and 1
    EFFECT_STRADDLES_CELS = 0x8000,             // drawn by View_AddForcedObjects (Controls_StraddleTest)
    EFFECT_SUNLIT = 0x10000,                    // its ambient doubled while player 0 can see the sun
    EFFECT_DIM_IN_NIGHT_VISION = 0x20000,       // its alpha a quarter in VISION_NIGHT
    EFFECT_NOT_IN_VIEW_4 = 0x40000,             // the model's: Control_SetGList takes view 4 out of displayMask
    EFFECT_FLASHES = 0x80000,                   // its tint times FlashMod
    EFFECT_FORCED_LISTED = 0x10000000,          // in ForcedList
    EFFECT_ALWAYS_FORCED = 0x20000000,          // in no cel: always in ForcedList (build_LinkToRoom)
    EFFECT_IN_FORCED_LIST = 0x30000000,         // either of the two
} ObjectEffectFlags;

// obj_tag::transformFlags bits: how the game's update derives an object's matrix and bounds each frame - in
// Control_GetObjMatrix (0x0002cf10), Control_BuildWorldSph (0x0002db20), control_movement_object_handler
// (0x0002dd10) and Player_Update. Ordinarily the matrix is rebuilt from rotation, position and scale, and the
// bounding sphere from the model. (The field was called transformFlags, a guess; Ghidra calls it rendererType.)
typedef enum {
    // Scale stretches the object along its own length (Z) only, rather than evenly: beams, lasers, tracers.
    TRANSFORM_SCALE_LENGTH_ONLY = 0x01,
    // The matrix's rotation is set elsewhere and kept; only its translation comes from position.
    TRANSFORM_KEEP_ROTATION = 0x02,
    // The whole matrix is set elsewhere and kept: neither rotation nor position is put into it (Player_Update
    // too). With TRANSFORM_KEEP_ROTATION, the last rotation is taken from the matrix's direction.
    TRANSFORM_MATRIX_PLACED = 0x04,
    // A skinned, animated object (set by AnimObjectNew): the bounding sphere comes from the animation rather
    // than the model, and collision and the view code treat it as such.
    TRANSFORM_ANIMATED = 0x08,
    // A platform particle system (Particles_Create): added to the views without a model, sorted 2000 further back,
    // and drawn by nothing on the Xbox (View_DrawList's profiling hook point)
    TRANSFORM_PARTICLES = 0x10,
    // Moved this frame: control_movement_object_handler wraps the rotation into +-pi, rebuilds the matrix, the
    // world bounds, the cel and the lighting, and keeps the position and rotation as the last ones.
    TRANSFORM_MOVED = 0x20,
    // After the frame's update, the position is read back out of the matrix's translation.
    TRANSFORM_POSITION_FROM_MATRIX = 0x40,
    TRANSFORM_80 = 0x80,                // Door_Create sets it; no reader found
    // The radius is kept as set, not worked out from the model and the scale; and the views add the object without a
    // model and draw it with Emitter_Draw (Emitter_Create sets it).
    TRANSFORM_FIXED_RADIUS = 0x100,
    // The scale is not applied to the matrix.
    TRANSFORM_NO_SCALE = 0x200,
    // Not a transform matter, but kept here with the rest: others may point at the object, so when it is
    // deleted, bullets, explosions and hit lists referring to it are cleared.
    TRANSFORM_REFERENCED = 0x400,
} ObjectTransformFlags;

//char (*__kaboom)[offsetof(obj_tag,objectType)] = 1;
LAYOUT_32(offsetof(obj_tag, position) == 0x24, "Offset of position not correct");
LAYOUT_32(offsetof(obj_tag, lastPosition) == 0x30, "Offset of lastPosition not correct");
LAYOUT_32(offsetof(obj_tag, rotation) == 0x3c, "Offset of rotation not correct");
LAYOUT_32(offsetof(obj_tag, lastRotation) == 0x48, "Offset of lastRotation not correct");
LAYOUT_32(offsetof(obj_tag, transformMatrix) == 0x70, "Offset of transformMatrix not correct");
LAYOUT_32(offsetof(obj_tag, extraObjectData) == 0xbc, "Offset of extraObjectData not correct");
LAYOUT_32(offsetof(obj_tag, scriptPlayer) == 0xc0, "Offset of scriptPlayer not correct");
LAYOUT_32(offsetof(obj_tag, effectFlags) == 0xcc, "Offset of effectFlags not correct");
LAYOUT_32(offsetof(obj_tag, transformFlags) == 0xd6, "Offset of transformFlags not correct");
LAYOUT_32(offsetof(obj_tag, flags) == 0xda, "Offset of flags not correct");
LAYOUT_32(offsetof(obj_tag, objectType) == 0xdb, "Offset of objectType not correct");
LAYOUT_32(offsetof(obj_tag, tweakB) == 0xe1, "Offset of tweakB not correct");

LAYOUT_32(sizeof(obj_tag) == 0xe4, "Size of obj_tag wrong");

#endif // OBJECT_H_