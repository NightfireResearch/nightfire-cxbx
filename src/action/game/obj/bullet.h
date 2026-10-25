#ifndef BULLET_H
#define BULLET_H

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// A projectile's extra data (obj_tag.extraObjectData of an OBJECTTYPE_BULLET). Names in quotes below are what
// Ghidra's structure calls a field where this one is different. See docs/weapons.md for the flags.
typedef struct {
    _VECTOR direction;                // 0x00 unit direction of travel ("field0_0x0"); remote steering resets it
                                      //      from the object's matrix, gravity bends it
    _VECTOR startPos;                 // 0x0c where it was fired from
    char unknown18[0x24-0x18];
    obj_tag *firedByObj;              // 0x24 the object that fired this bullet ("playerShooter"; eg. read by
                                      //      SP_GetHitDamage)
    obj_tag *firedFromObj; // 0x28 Bullet_init's third argument: the collision test passes through it, and
                                      //      a car that is not it stops the bullet
    obj_tag *swoosh;                  // 0x2c trail or, for the trip-bomb, its beam
    obj_tag *occluder;                // 0x30 a smoke grenade's cloud (Occlude_Create), kept where the bullet is
    obj_tag *heatseekTarget;          // 0x34 homing target; once stuck (state 3), what it is stuck to
    weapon_definition_tag *wpnDef;    // 0x38
    // The frame's collision test (obj_tag::hitTest), which Bullet_Update sets up: a swept segment when the
    // bullet moves at least half its radius (HITTEST_RAY), a sphere at its position otherwise (HITTEST_SPHERE)
    HITTEST_tag test;                 // 0x3c
    DYNAMICSOUNDS *soundHandle;       // 0xc8 flight sound, moved with the bullet
    float distanceTravelled;          // 0xcc compared with the weapon's range
    float speed;                      // 0xd0 units per 60 Hz frame ("field100_0xd0")
    float fuseTimer;                  // 0xd4 frames
    ushort numRicochets;              // 0xd8 also slows the tumble of a bouncing projectile; a satchel's count of
                                      //      quarter seconds
    short charge;                     // 0xda a satchel's: shots at it take it away, and at 0 its fuse is set again
    char gravityOff;                  // 0xdc while set, projectile flag 0x8 (gravity) is ignored; set when a bounce
                                      //      pushes it up
    char unknownDd[0xe0-0xdd];
} BU_tag;

LAYOUT_32(sizeof(BU_tag) == 0xe0, "Bad size for BU_tag");
static_assert(offsetof(BU_tag, firedByObj) == 0x24, "Bad offset of firedByObj");
LAYOUT_32(offsetof(BU_tag, wpnDef) == 0x38, "Bad offset of wpnDef");
LAYOUT_32(offsetof(BU_tag, test) == 0x3c, "Bad offset of test");
LAYOUT_32(offsetof(BU_tag, soundHandle) == 0xc8, "Bad offset of soundHandle");
LAYOUT_32(offsetof(BU_tag, distanceTravelled) == 0xcc, "Bad offset of distanceTravelled");
LAYOUT_32(offsetof(BU_tag, numRicochets) == 0xd8, "Bad offset of numRicochets");
LAYOUT_32(offsetof(BU_tag, gravityOff) == 0xdc, "Bad offset of gravityOff");

// A spent case's extra data (OBJECTTYPE_CASING, 0x2c bytes): one of 32 kept for the whole level
typedef struct {
    _VECTOR vel;                      // 0x00 a frame
    _VECTOR spin;                     // 0x0c a frame
    float floorY;                     // 0x18 where it lands; far below once it has
    float timer;                      // 0x1c frames: before it is thrown (state 3), then its age
    obj_tag *owner;                   // 0x20 the gun's carrier
    HASHCODE model;                   // 0x24
    ushort material;                  // 0x28 of the floor under the carrier: the sound it lands with
    char _pad2a[2];
} CASING_tag;

LAYOUT_32(sizeof(CASING_tag) == 0x2c, "CASING_tag is 0x2c bytes");

#pragma pack(pop)

// The states Bullet_Update moves a projectile through (obj_tag.curState).
enum BulletState {
    BulletState_New = 0,      // first frame: picks its graphics and speed, then flies
    BulletState_Flying = 1,
    BulletState_Spent = 2,    // marked for deletion on the next update
    BulletState_Stuck = 3,    // stuck to a surface or object (IMPACTFLAG_STICKS)
    BulletState_OutOfRange = 4,
};

obj_tag * Bullet_init(short plyNum, obj_tag *playerObj, obj_tag *owner, weapon_definition_tag *weaponDef, _VECTOR *pos, _VECTOR *direction, ushort weaponSound);
void Bullet_Update(obj_tag *obj);
bool Bullet_handle_object_destruction(obj_tag *obj, obj_tag *from);
void Bullet_CollisionHandler(obj_tag *obj);
void Bullet_trail_effect_smoke(obj_tag *obj);
void Bullet_SpawnSmokeDust_SomeOtherProbability(obj_tag *obj);
void Bullet_SpawnSmokeDust_SomeProbability(obj_tag *obj);
void Bullet_DoTrails(obj_tag *obj, BU_tag *bullet, _VECTOR *startPosition, float step);
void Bullet_Delete(obj_tag *obj);
bool Bullet_heatseekable(obj_tag *obj);
void Bullet_acquiretarget(BU_tag *bullet, obj_tag *shooter);
void Bullet_homing(BU_tag *bullet, obj_tag *obj);
void Bullet_InitCasingLists(void);
void Bullet_init_casing(obj_tag *obj);
void Bullet_init_casing_ex(obj_tag *casing);
void Bullet_casing_update(obj_tag *casing);

#endif // BULLET_H
