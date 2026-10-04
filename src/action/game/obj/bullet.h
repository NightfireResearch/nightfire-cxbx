#ifndef BULLET_H
#define BULLET_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A projectile's extra data (obj_tag.extraObjectData of an OBJECTTYPE_BULLET). Names in quotes below are what
// Ghidra's structure calls a field where this one is different; fields only Bullet_Update touches are named for
// what it does with them, and are still guesses where the comment says so. See docs/weapons.md for the flags.
typedef struct {
    _VECTOR direction;                // 0x00 unit direction of travel ("field0_0x0"); remote steering resets it
                                      //      from the object's matrix, gravity bends it
    _VECTOR pos;                      // 0x0c
    char unknown18[0x24-0x18];
    obj_tag *firedByObj;              // 0x24 the object that fired this bullet ("playerShooter"; eg. read by
                                      //      SP_GetHitDamage)
    obj_tag *maybeProxyObjectShooter; // 0x28
    obj_tag *swoosh;                  // 0x2c trail or, for the trip-bomb, its beam
    obj_tag *unknown30;               // 0x30
    obj_tag *heatseekTarget;          // 0x34 homing target; once stuck (state 3), what it is stuck to
    weapon_definition_tag *wpnDef;    // 0x38
    char unknown3c[0x58-0x3c];
    // What Bullet_Update sets up for this frame's collision test, which is a swept segment when the bullet
    // moves at least half its radius (collideMode 0x201) and a sphere at its position otherwise (0x101).
    // Probably read by Bullet_CollisionHandler; the names are inferred from how they are filled in.
    _VECTOR sweepStart;               // 0x58
    _VECTOR sweepEnd;                 // 0x64
    _VECTOR sweepDirection;           // 0x70
    _VECTOR spherePosition;           // 0x7c
    char unknown88[0x9c-0x88];
    obj_tag *collideIgnoreObj;        // 0x9c copied from maybeProxyObjectShooter
    int unknownA0;                    // 0xa0
    char unknownA4[4];
    cel_tag *collideCel;              // 0xa8
    char unknownAc[0xb4-0xac];
    float sweepLengthOrRadius;        // 0xb4 segment length (at least 1) or sphere radius
    float hitDamage;                  // 0xb8 the weapon's damage; 1 or 0 for some explosives
    ushort collideMode;               // 0xbc 0 none, 0x101 sphere, 0x201 swept segment
    char unknownBe[2];
    ushort unknownC0;                 // 0xc0
    ushort collideMaterialMask;       // 0xc2 0x20 unless projectile flag 0x800 is set
    char unknownC4[4];
    DYNAMICSOUNDS *soundHandle;       // 0xc8 flight sound, moved with the bullet
    float distanceTravelled;          // 0xcc compared with the weapon's range ("maybeAgeOrLifetime")
    float speed;                      // 0xd0 units per 60 Hz frame ("field100_0xd0")
    float fuseTimer;                  // 0xd4 frames ("explosiveTimerOrMaybeRange")
    ushort numRicochets;              // 0xd8 also slows the tumble of a bouncing projectile
    short unknownDa;                  // 0xda
    char gravityOff;                  // 0xdc while set, projectile flag 0x8 (gravity) is ignored
    char unknownDd[0xe0-0xdd];
} BU_tag;

static_assert(sizeof(BU_tag) == 0xe0, "Bad size for BU_tag");
static_assert(offsetof(BU_tag, firedByObj) == 0x24, "Bad offset of firedByObj");
static_assert(offsetof(BU_tag, wpnDef) == 0x38, "Bad offset of wpnDef");
static_assert(offsetof(BU_tag, sweepStart) == 0x58, "Bad offset of sweepStart");
static_assert(offsetof(BU_tag, collideIgnoreObj) == 0x9c, "Bad offset of collideIgnoreObj");
static_assert(offsetof(BU_tag, collideCel) == 0xa8, "Bad offset of collideCel");
static_assert(offsetof(BU_tag, sweepLengthOrRadius) == 0xb4, "Bad offset of sweepLengthOrRadius");
static_assert(offsetof(BU_tag, collideMode) == 0xbc, "Bad offset of collideMode");
static_assert(offsetof(BU_tag, unknownC0) == 0xc0, "Bad offset of unknownC0");
static_assert(offsetof(BU_tag, soundHandle) == 0xc8, "Bad offset of soundHandle");
static_assert(offsetof(BU_tag, distanceTravelled) == 0xcc, "Bad offset of distanceTravelled");
static_assert(offsetof(BU_tag, numRicochets) == 0xd8, "Bad offset of numRicochets");
static_assert(offsetof(BU_tag, gravityOff) == 0xdc, "Bad offset of gravityOff");

#pragma pack(pop)

// The states Bullet_Update moves a projectile through (obj_tag.curState).
enum BulletState {
    BulletState_New = 0,      // first frame: picks its graphics and speed, then flies
    BulletState_Flying = 1,
    BulletState_Spent = 2,    // marked for deletion on the next update
    BulletState_Stuck = 3,    // stuck to a surface or object (impact flag 0x100)
    BulletState_OutOfRange = 4,
};

// projectileFlags bits Bullet_Update reads (docs/weapons.md, "+0x06c projectileFlags").
enum {
    ProjectileFlag_RemoteControl = 0x4,
    ProjectileFlag_Gravity       = 0x8,
    ProjectileFlag_NoModel       = 0x10,
    ProjectileFlag_LaserBeam     = 0x100,
    ProjectileFlag_MaterialMask  = 0x800,
    ProjectileFlag_Tracer        = 0x1000,
    ProjectileFlag_Light         = 0x2000,
    ProjectileFlag_Tumbles       = 0x8000,
    ProjectileFlag_Homing        = 0x20000,
};

// impactFlags bits Bullet_Update reads.
enum {
    ImpactFlag_TripBomb = 0x80000,
};

obj_tag * Bullet_init(short plyNum, obj_tag *playerObj, obj_tag *param_3, weapon_definition_tag *weaponDef, _VECTOR *pos, _VECTOR *direction, ushort weaponSound);
void Bullet_Update(obj_tag *obj);

#endif // BULLET_H
