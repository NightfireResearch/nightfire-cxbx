#include "bullet.h"

#include <bit>
#include <math.h>
#include <stdint.h>

#include "Player.h"
#include "Light.h"
#include "control.h"
#include "Copter.h"
#include "Effect.h"
#include "Emitter.h"
#include "Explode.h"
#include "Gas.h"
#include "GT.h"
#include "Hurt.h"
#include "Swoosh.h"
#include "../view.h"
#include "../weapon_stats.h"
#include "../drone/NDrone2.h"
#include "../mp/multiplayer.h"
#include "../../game.h"
#include "../../input.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"
#include "../../util/hashtable.h"
#include "../../util/Random.h"

#pragma fp_contract(off)


// The spent cases, used in turn
#define NUM_CASINGS 32
// XBE_GLOBAL(0x001dc678, 0x80)
static obj_tag *CasingList[NUM_CASINGS];
// XBE_GLOBAL(0x001dc6f8, 0x2)
short NextCasing;

// A satchel's digits, 0 to 9

// The explosions bullets set off
#define BULLET_EXPLOSION ((HASHCODE)0x06000052)
#define BULLET_BEAM_EXPLOSION ((HASHCODE)0x060007c4)
// Trails: the beam weapons' (bases 0x33, 0x6a and 0x6e) and the others', first and later segments
#define SWOOSH_BEAM ((HASHCODE)0x020005cd)
#define SWOOSH_TRAIL_FIRST ((HASHCODE)0x02000918)
#define SWOOSH_TRAIL ((HASHCODE)0x02000481)
#define BULLET_SMOKE ((HASHCODE)0x0200001f)
#define BULLET_DUST ((HASHCODE)0x02000039)
#define SHOTGUN_CASING ((HASHCODE)0x02000548)
#define SATCHEL_SHOT_SPARKS ((HASHCODE)0x0c00000a)

// Sounds without a name in assets.h
#define SFX_BULLET_WHIZZ ((Action_SFX)0x1dc)
#define SFX_FLASH_GRENADE ((Action_SFX)0x16)
#define SFX_SMOKE_GRENADE ((Action_SFX)0x14)
#define SFX_SATCHEL_DISARMED ((Action_SFX)0x5dd)

// What the remote mine's fuse is set to when the player lays it: it waits for the detonator
static constexpr float kMineWaitsForDetonator = 7777.0f;
static_assert(std::bit_cast<uint32_t>(kMineWaitsForDetonator) == 0x45f30800);

// The weapons bullets know by their base number
enum {
    WEAPON_BASE_BEAM_33 = 0x33,
    WEAPON_BASE_SATCHEL = 0x3b,
    WEAPON_BASE_MINE = 0x37,
    WEAPON_BASE_SATCHEL_LAST = 0x6c,    // a variant: the satchel with 15 seconds
    WEAPON_BASE_ODDJOB_HAT = 0x45,
    WEAPON_BASE_SHOOTS_SATCHEL_4E = 0x4e,
    WEAPON_BASE_SHOOTS_SATCHEL_4F = 0x4f,
    WEAPON_BASE_GRAPPLE_50 = 0x50,
    WEAPON_BASE_GRAPPLE_51 = 0x51,
    WEAPON_BASE_DETONATED_59 = 0x59,
    WEAPON_BASE_DETONATED_5B = 0x5b,
    WEAPON_BASE_SMOKE = 0x69,
    WEAPON_BASE_BEAM_6A = 0x6a,
    WEAPON_BASE_BEAM_6E = 0x6e,
};

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

static bool Bullet_IsBeamWeapon(short base) {
    return base == WEAPON_BASE_BEAM_33 || base == WEAPON_BASE_BEAM_6A || base == WEAPON_BASE_BEAM_6E;
}

static void Bullet_PlaySound(Action_SFX sfx, _VECTOR *pos) {
    Sound_Play3D(sfx, pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
}

// A projectile from pos along dir, scattered by the weapon's spread: the player's widens with the shots of the burst,
// a drone's with its own accuracy, and a sniper rifle has none, scoped or in a drone's hands
// AUTOINJECT
obj_tag * Bullet_init(short plyNum, obj_tag *playerObj, obj_tag *owner, weapon_definition_tag *weaponDef, _VECTOR *pos, _VECTOR *direction, ushort weaponSound) {

    int bloom = 0;
    float accuracy = 1.0f;

    // GC check (0x80079b64)
    if (direction == NULL) {
        NF_WARN("Oops, null bullet vector\n");
        return NULL;
    }

    obj_tag *obj = control_create_object_xbox(sizeof(BU_tag), XBOX_SIZEOF(BU_tag), pos, NULL, NULL);
    if (obj == NULL)
        return NULL;

    obj->transformFlags |= TRANSFORM_KEEP_ROTATION;
    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    Vec_Copy(pos, &bullet->startPos);
    obj->curState = BulletState_New;
    obj->subState = 0;
    obj->scale = 1.0f;
    obj->objectType = OBJECTTYPE_BULLET;
    obj->unknown_0xd9 = 2;
    obj->maybeCollision = &bullet->test;
    bullet->test.type = 0;
    bullet->occluder = NULL;
    bullet->maybeProxyObjectShooter = owner;
    bullet->firedByObj = playerObj;
    bullet->wpnDef = weaponDef;
    bullet->fuseTimer = 210.0f;
    bullet->numRicochets = 0;
    bullet->distanceTravelled = 0.0f;
    if (playerObj != NULL)
        playerObj->transformFlags |= TRANSFORM_REFERENCED;

    if (bullet->wpnDef->projectileFlags & ProjectileFlag_Swoosh) {
        if (Bullet_IsBeamWeapon(bullet->wpnDef->weaponBaseNum))
            bullet->swoosh = Swoosh_Create(pos, pos, bullet->wpnDef->swooshLifetime, SWOOSH_BEAM, 0);
        else
            bullet->swoosh = Swoosh_Create(pos, pos, bullet->wpnDef->swooshLifetime, SWOOSH_TRAIL_FIRST, 1);
    } else {
        bullet->swoosh = NULL;
    }

    if (playerObj != NULL) {
        if (owner != NULL && owner->objectType == OBJECTTYPE_PLAYER) {
            BLData *blData = (BLData *)playerObj->extraObjectData;
            AnimState *anim = playerObj->animState;
            int mode = blData->weaponStats[anim->currentWeaponId].fireModeIndex;
            short shots = weaponDef->shotsPerTrigger[(uchar)mode] - blData->burstShotsLeft;
            if (shots >= weaponDef->clipSize)
                shots = weaponDef->clipSize;
            bloom = Ftol((double)shots * weaponDef->spreadPerShot);
            if ((weaponDef->weaponFlags & WeaponFlag_PerfectWhenScoped) && (anim->animFlags & 1))
                accuracy = 0.0f;
        } else if (playerObj->objectType == OBJECTTYPE_DRONE) {
            accuracy = ((Drone_tag *)playerObj->extraObjectData)->accuracyF;
            if (weaponDef->weaponFlags & WeaponFlag_PerfectWhenScoped)
                accuracy = 0.0f;
        }
    }

    float spread = (float)(((short)bloom + (double)weaponDef->baseSpread) * accuracy);
    float pitch = (float)Float_FRand(3.1415927f);
    float yaw = (float)Float_FRand(6.2831855f);
    float length = (float)(Rand_FRand_MVar2(spread + spread, spread) * 0.0014f);
    _VECTOR scatter;
    Vec_Spherical_2_Cartesian((float *)&scatter, length, yaw, pitch);

    // The scatter's y and z go to the direction's z and y
    Vec_Copy(direction, &bullet->direction);
    Vec_Normalise(&bullet->direction, &bullet->direction);
    bullet->direction.x = scatter.x + bullet->direction.x;
    bullet->direction.z = scatter.y + bullet->direction.z;
    bullet->direction.y = scatter.z + bullet->direction.y;
    Vec_Normalise(&bullet->direction, &bullet->direction);
    Mat_Align2Dir(&obj->transformMatrix, &bullet->direction, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    build_LinkToRoom(obj, 0, glb_world);

    weapon_definition_tag *weapon = bullet->wpnDef;
    if (weapon->projectileFlags & ProjectileFlag_Accelerates)
        bullet->speed = weaponDef->projectileSpeed * 0.25f;
    else
        bullet->speed = weaponDef->projectileSpeed;
    if (weapon->impactFlags & ImpactFlag_LongFuse)
        bullet->fuseTimer = 300.0f;

    // A satchel's fuse by its variant: 5, 10, 15 seconds...
    if (weapon->weaponBaseNum == WEAPON_BASE_SATCHEL) {
        if (weapon->weaponVariantNum == WEAPON_BASE_SATCHEL_LAST)
            bullet->fuseTimer = 900.0f;
        else
            bullet->fuseTimer = (float)(((ushort)weapon->weaponVariantNum - 0x3a) * 300);
        bullet->charge = 100;
    }
    if (weapon->weaponBaseNum == WEAPON_BASE_SMOKE)
        bullet->fuseTimer = 5.0f;
    if (weapon->weaponBaseNum == WEAPON_BASE_MINE) {
        if (bullet->firedByObj != NULL && bullet->firedByObj->objectType == OBJECTTYPE_PLAYER)
            bullet->fuseTimer = kMineWaitsForDetonator;
        else
            bullet->fuseTimer = 420.0f;
    }

    // The guided missile: its player steers it
    if (weapon->projectileFlags & ProjectileFlag_RemoteControl) {
        obj_tag *shooter = bullet->firedByObj;
        if (shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER
            && shooter->subState != MovementType_RemoteControl) {
            short previous = Player_ChangeSubState(shooter, MovementType_RemoteControl);
            BLData *blData = (BLData *)bullet->firedByObj->extraObjectData;
            blData->previousSubState = previous;
            blData->remoteControlDevice = obj;
            Player_SetCamMode(blData, CamMode_Redeemer);
            blData->remoteControlTimer = 0;
        }
    }

    // The grapple: the player is held while its line flies
    if (bullet->wpnDef->projectileFlags & ProjectileFlag_GrappleLine) {
        obj_tag *shooter = bullet->firedByObj;
        if (shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER) {
            BLData *blData = (BLData *)shooter->extraObjectData;
            blData->movementDisabled = 1;
            Vec_Copy(&obj->position, &blData->grapplePoint);
        }
    }

    if (weaponSound != 0xffff)
        bullet->soundHandle = Sound_Play3D((Action_SFX)weaponSound, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    else
        bullet->soundHandle = NULL;
    if (weaponDef->ammoType == 0xe)
        bullet->soundHandle = Sound_Play3D(SFX_WEAPON_ROCKET_MISSILE_01_LOOP, &obj->position, 100.0f, -1.0f, -1.0f, 0,
                                           0, 0);
    if (weaponDef->weaponBaseNum == Weap_OddjobHat)
        bullet->soundHandle = Sound_Play3D(SFX_WEAPON_ODDJOB_HAT_LOOP, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    return obj;
}

// A projectile that can be shot (weapon flag 0x4000) and explodes on contact or is a laser: blown up where it is. True
// when it was. The original tests 0x1000000 of a byte for anything else, so nothing else ever explodes here.
// AUTOINJECT
bool Bullet_handle_object_destruction(obj_tag *obj, obj_tag *from) {

    if (obj == NULL)
        return false;
    if (obj->objectType != OBJECTTYPE_BULLET)
        return false;

    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    if (!(bullet->wpnDef->weaponFlags & WeaponFlag_CanBeShot)
        || !(bullet->wpnDef->impactFlags & (ImpactFlag_TripBomb | ImpactFlag_Fuse | ImpactFlag_Explodes)))
        return false;

    obj->flags |= 1;
    weapon_definition_tag *weapon = bullet->wpnDef;
    Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, weapon->explodeRadius, weapon->explodeRadius,
                   BULLET_EXPLOSION, weapon->damage, 0xff, 0xff, 100, bullet->firedByObj, 0);
    if ((bullet->wpnDef->impactFlags & ImpactFlag_TripBomb) && bullet->swoosh != NULL)
        bullet->swoosh->flags |= 1;
    return true;
}

// The objects a trip-bomb or a remote mine counts as its kind: the same weapon from the same shooter. Over five, the
// oldest goes.
static void Bullet_LimitLaid(BU_tag *bullet) {
    short count = 0;
    obj_tag *oldest = NULL;
    for (obj_tag *other = control_first_object(); other != NULL; other = other->nextObject) {
        if (other->objectType != OBJECTTYPE_BULLET)
            continue;
        BU_tag *laid = (BU_tag *)other->extraObjectData;
        if (laid->wpnDef != bullet->wpnDef || laid->firedByObj != bullet->firedByObj)
            continue;
        count++;
        if (oldest == NULL || (uint)other->creationTimeFrames < (uint)oldest->creationTimeFrames)
            oldest = other;
    }
    if (count > 5)
        oldest->flags |= 1;
}

// The frame's hits, nearest first. A hit it passes through (one that makes no contact, objects with effect flags
// 0x60, the 0x10 material without projectile flag 0x800, materials with 0x40 and, at random, 0x80) lets it go on to
// the next; the first solid one ends it. Each makes its impact effect, and the solid one the weapon's explosions, and
// sticks, bounces or ricochets the bullet.
// AUTOINJECT
void Bullet_CollisionHandler(obj_tag *obj) {

    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    uint effect = 0;
    if (obj->curState == BulletState_Stuck)
        return;

    bullet->gravityOff = 0;
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {

        cel_tag *cel;
        if (hit->hitCel != NULL)
            cel = hit->hitCel;
        else
            cel = hit->hitObj != NULL ? hit->hitObj->inCel : NULL;

        uchar passes = (hit->flags >> 1) & 1;
        obj_tag *target = hit->hitObj;
        if (target != NULL) {
            if (target->effectFlags & 0x60)
                passes = 1;
            if (target != bullet->maybeProxyObjectShooter && target->objectType == OBJECTTYPE_CAR
                && !(bullet->wpnDef->impactFlags & ImpactFlag_Sticks))
                passes = 0;
        }
        if (!(bullet->wpnDef->projectileFlags & ProjectileFlag_MaterialMask) && (hit->materialType & 0x3f) == 0x10)
            passes = 1;
        short base = bullet->wpnDef->weaponBaseNum;
        if (base != WEAPON_BASE_GRAPPLE_50 && base != WEAPON_BASE_GRAPPLE_51) {
            if (hit->materialType & 0x40)
                passes |= 1;
            if (hit->materialType & 0x80)
                passes |= Rand_Rand(0x3f) & 1;
        }

        if (hit->hitObj != NULL && !passes && (bullet->wpnDef->impactFlags & ImpactFlag_Bounces))
            Control_InheritVelocity(obj, hit->hitObj, 10.0f);
        obj->curState = BulletState_Spent;
        if (!passes && bullet->wpnDef->damage > 0.0f)
            Bullet_handle_object_destruction(hit->hitObj, bullet->firedByObj);

        weapon_definition_tag *weapon = bullet->wpnDef;
        if (weapon->impactFlags & ImpactFlag_Marks)
            effect |= EFFECT_BULLET | EFFECT_SPARKS | EFFECT_SOUND;
        if (weapon->projectileFlags & ProjectileFlag_LaserBeam)
            effect = EFFECT_LASER;
        if (weapon->impactFlags & ImpactFlag_LaserEffect)
            effect = EFFECT_LASER;
        Effect_Create(hit, obj, effect, 1.0f);

        if ((bullet->wpnDef->impactFlags & ImpactFlag_Explodes) && !passes) {
            weapon = bullet->wpnDef;
            Explode_Create(obj, &hit->hitPosition, &hit->surfaceNormal.normal, weapon->explodeRadius,
                           weapon->explodeRadius, BULLET_EXPLOSION, weapon->damage, 0xff, 0xff, 100, bullet->firedByObj,
                           hit->materialType & 0x3f);
        }
        if ((bullet->wpnDef->impactFlags & ImpactFlag_BeamExplodes) && !passes) {
            weapon = bullet->wpnDef;
            Explode_Create(obj, &hit->hitPosition, &hit->surfaceNormal.normal, 1.0f, weapon->explodeRadius,
                           BULLET_BEAM_EXPLOSION, weapon->damage, 0xff, 0xff, 100, bullet->firedByObj,
                           hit->materialType & 0x3f);
        }
        if ((bullet->wpnDef->impactFlags & ImpactFlag_BeamExplodes2) && !passes) {
            weapon = bullet->wpnDef;
            Explode_Create(obj, &hit->hitPosition, &hit->surfaceNormal.normal, 1.0f, weapon->explodeRadius,
                           BULLET_BEAM_EXPLOSION, weapon->damage, 0xff, 0xff, 100, bullet->firedByObj,
                           hit->materialType & 0x3f);
        }

        // Sticking: a character (or type 21) does not hold it, and it bounces off instead
        uchar bouncesOff = 0;
        if ((bullet->wpnDef->impactFlags & ImpactFlag_Sticks) && !passes) {
            target = hit->hitObj;
            if (target != NULL && (target->objectType == OBJECTTYPE_DRONE || target->objectType == OBJECTTYPE_PLAYER
                                   || target->objectType == OBJECTTYPE_ANIMDEBUGHANDLER)) {
                obj->curState = BulletState_Flying;
                bouncesOff = 1;
            } else {
                if (target != NULL) {
                    bullet->heatseekTarget = target;
                    Control_ObjectReferenced(target, 1);
                }
                obj->curState = BulletState_Stuck;
                _VECTOR into;
                Vec_Copy(&hit->surfaceNormal.normal, &into);
                float out = 0.04f;
                into.x = into.x * -1.0f;
                into.y = into.y * -1.0f;
                into.z = into.z * -1.0f;
                Mat_Align2Dir(&obj->transformMatrix, &into, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);

                weapon = bullet->wpnDef;
                if (weapon->impactFlags & ImpactFlag_TripBomb) {
                    Bullet_PlaySound(SFX_WEAPON_TRIPBOMB_PLANT, &obj->position);
                    bullet->fuseTimer = 120.0f;
                    out = 0.002f;
                    obj->subState = 0;
                    Bullet_LimitLaid(bullet);
                } else if (weapon->weaponBaseNum == WEAPON_BASE_SATCHEL) {
                    Bullet_PlaySound(SFX_ENV_LAND_ON_ELEVATOR, &obj->position);
                } else if (weapon->weaponBaseNum == WEAPON_BASE_MINE) {
                    Bullet_LimitLaid(bullet);
                } else if (weapon->weaponBaseNum == WEAPON_BASE_ODDJOB_HAT) {
                    bullet->fuseTimer = 600.0f;
                    Bullet_PlaySound(SFX_ENV_LAND_ON_ELEVATOR, &obj->position);
                }
                auxVec_AddMulR32(&hit->hitPosition, &hit->surfaceNormal.normal, out, &obj->position);
            }
        }

        // Bouncing, up to 20 times, off what it meets head on
        uint impact = bullet->wpnDef->impactFlags;
        if (((impact & ImpactFlag_Bounces) && !passes) || bouncesOff) {
            if (impact & ImpactFlag_FlashBang) {
                if (bullet->numRicochets == 0)
                    Bullet_PlaySound(SFX_OBJ_FLASH_GRENADE_ROLL, &obj->position);
            } else if (impact & ImpactFlag_SmokeCloud) {
                if (bullet->numRicochets == 0)
                    Bullet_PlaySound(SFX_OBJ_SMOKE_GRENADE_ROLL, &obj->position);
            } else if (!(bullet->numRicochets & 1) && bullet->numRicochets < 15) {
                Bullet_PlaySound(SFX_WEAPON_GRENADE_BOUNCE, &obj->position);
            }

            bullet->numRicochets++;
            if (bullet->numRicochets < 20) {
                auxVec_AddMulR32(&obj->position, &bullet->test.push, FRAME_RATE_DIV, &obj->position);
                if (bullet->test.push.y > 0.0f)
                    bullet->gravityOff = 1;
                _VECTOR *normal = &hit->surfaceNormal.normal;
                double along = ((double)normal->z * bullet->direction.z + (double)normal->y * bullet->direction.y)
                               + (double)normal->x * bullet->direction.x;
                along = along + along;
                if (along <= 0.0) {
                    auxVec_AddMulR32(&bullet->direction, normal, (float)-along, &bullet->direction);
                    Vec_Normalise(&bullet->direction, &bullet->direction);
                    double speed = (double)EffectInfo[hit->materialType & 0x3f].bounce * bullet->speed;
                    bullet->speed = (float)speed;
                    if (speed < (double)FRAME_RATE_MUL * 0.01f) {
                        Mat_Align2Dir(&obj->transformMatrix, &bullet->direction, &CONST_UP_VECTOR,
                                      &MAYBE_CONST_FORWARD_VECTOR);
                        bullet->speed = 0.0f;
                    }
                }
            } else {
                Mat_Align2Dir(&obj->transformMatrix, &bullet->direction, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
                bullet->speed = 0.0f;
            }
            obj->curState = BulletState_Flying;
        }

        // Ricochets, up to three, as the material allows (more often off cels with flag 0x400000)
        if (bullet->wpnDef->impactFlags & ImpactFlag_Ricochets) {
            ushort mask = 8;
            if (cel != NULL && (cel->bitsFromPlacementTag & 0x400000))
                mask = 9;
            if (passes)
                mask = 0;
            if (bullet->numRicochets < 3 && Effect_RicochetProb(hit->materialType, mask)) {
                _VECTOR dir = bullet->direction;
                _VECTOR normal = hit->surfaceNormal.normal;
                double along = ((double)dir.z * normal.z + (double)dir.y * normal.y) + (double)dir.x * normal.x;
                float k = (float)-(((double)EffectInfo[hit->materialType & 0x3f].bounce + 1.0f) * along);
                auxVec_AddMulR32(&dir, &normal, k, &bullet->direction);
                Vec_Normalise(&bullet->direction, &bullet->direction);
                Vec_Copy(&hit->hitPosition, &obj->position);
                Mat_Align2Dir(&obj->transformMatrix, &bullet->direction, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
                bullet->numRicochets++;
                obj->curState = BulletState_Flying;
            }
        }

        if (bullet->wpnDef->onImpact != NULL) {
            bullet->wpnDef->onImpact(obj, &hit->surfaceNormal, &hit->hitPosition);
            obj->flags |= 1;
        }

        if (!passes)
            break;
        obj->curState = BulletState_Flying;
    }

    if (bullet->occluder != NULL) {
        Vec_Copy(&obj->position, &bullet->occluder->position);
        bullet->occluder->transformFlags |= TRANSFORM_MOVED;
    }
    if (obj->curState == BulletState_Spent)
        obj->effectFlags |= FLAG_HIDDEN;
}

// Smoke behind the projectile: a puff every fourth frame in single player; once it has gone off (a smoke grenade's
// substate 1), a cloud every fifteenth
void _Bullet_trail_effect_smoke(obj_tag *obj) {

    _VECTOR vel = {}, rot = {};
    if (obj->subState == 0) {
        if (MPSettings.isMultiplayer || ((obj->creationTimeFrames + GameState.NumFramesUnpaused) & 3))
            return;
        rot.z = (float)Float_FRand(6.2831855f);
        Gas_Init(obj->inCel, &obj->position, &rot, &vel, 0.1f, 0.5f, BULLET_SMOKE, 7, 0x404040ff, 0x40404001, 0x78);
        return;
    }

    if (((obj->creationTimeFrames & 0xff) + GameState.NumFramesUnpaused) % 15 != 0)
        return;
    vel.x = (float)(Rand_FRand_MVar2(0.3f, 0.15f) + 0.6f);
    vel.z = (float)(Rand_FRand_MVar2(0.3f, 0.15f) + 0.3f);
    vel.y = (float)(Rand_FRand_MVar2(0.1f, 0.05f) + 0.5f);
    rot.z = (float)Float_FRand(6.2831855f);
    Gas_Init(obj->inCel, &obj->position, &rot, &vel, 0.2f, 5.0f, BULLET_SMOKE, 11, 0x404040ff, 0x404040ff, 0xff);
}

#ifndef NIGHTFIRE_STANDALONE
// The object in ESI
// AUTOLTCG
void __declspec(naked) Bullet_trail_effect_smoke(void) {
    _asm {
        push esi
        call _Bullet_trail_effect_smoke
        add esp, 4
        ret
    }
}
#endif

// Dust behind the projectile, every third frame in single player
void _Bullet_SpawnSmokeDust_SomeOtherProbability(obj_tag *obj) {
    if (MPSettings.isMultiplayer || ((obj->creationTimeFrames & 0xff) + GameState.NumFramesUnpaused) % 3 != 0)
        return;
    _VECTOR rot;
    rot.x = 0.0f;
    rot.y = 0.0f;
    rot.z = (float)Float_FRand(6.2831855f);
    Gas_Init(obj->inCel, &obj->position, &rot, NULL, 0.1f, 0.4f, BULLET_DUST, 0, 0x7f7f7f7f, 0x80808000, 0x40);
}

#ifndef NIGHTFIRE_STANDALONE
// The object in ESI
// AUTOLTCG
void __declspec(naked) Bullet_SpawnSmokeDust_SomeOtherProbability(void) {
    _asm {
        push esi
        call _Bullet_SpawnSmokeDust_SomeOtherProbability
        add esp, 4
        ret
    }
}
#endif

// Finer dust, every other frame in single player
void _Bullet_SpawnSmokeDust_SomeProbability(obj_tag *obj) {
    if (MPSettings.isMultiplayer || ((obj->creationTimeFrames + GameState.NumFramesUnpaused) & 1))
        return;
    _VECTOR rot;
    rot.x = 0.0f;
    rot.y = 0.0f;
    rot.z = (float)Float_FRand(6.2831855f);
    Gas_Init(obj->inCel, &obj->position, &rot, NULL, 0.05f, 0.1f, BULLET_DUST, 7, 0x7f7f7f7f, 0x80808000, 0x20);
}

#ifndef NIGHTFIRE_STANDALONE
// The object in ESI
// AUTOLTCG
void __declspec(naked) Bullet_SpawnSmokeDust_SomeProbability(void) {
    _asm {
        push esi
        call _Bullet_SpawnSmokeDust_SomeProbability
        add esp, 4
        ret
    }
}
#endif

// A flash grenade going off: each player within 30 units is dazzled by how near it is (5 to 90 units), how directly
// he faces it (30 to about 100 degrees) and, half as much, without a line of sight; drones are told, turrets disabled
static void Bullet_FlashBang(obj_tag *obj, BU_tag *bullet) {

    _VECTOR *pos = &obj->position;
    Gas_Init(obj->inCel, pos, &obj->rotation, NULL, 0.5f, 1.67f, BULLET_SMOKE, 11, 0xffffff7f, 0x80808000, 0x7f);
    Camera_Shake(pos, 2.0f);
    Bullet_PlaySound(SFX_FLASH_GRENADE, pos);
    Light_Create(pos, 0xff, 0xff, 0xff, 10.0f, 0, 10, 1, 5.0f, 0, 0, 0, 0xffff);

    HITDATA_tag *hitList = NULL;
    Collide_SphereIntersect((_VECTOR *)&obj->transformMatrix.m[12], 30.0f, obj->inCel, obj, NULL, &hitList, 0, 1, 0xca, 4);
    for (HITDATA_tag *hit = hitList; hit != NULL; hit = hit->next) {

        obj_tag *target = hit->hitObj;
        if (target == NULL)
            continue;

        switch (target->objectType) {

        case OBJECTTYPE_PLAYER: {
            _VECTOR head;
            Player_GetHeadPos(&head, target, NULL);
            double nearness = 1.0f - ((double)Vec_Dist3D(&head, pos) - 5.0f) * 0.0117647061f;
            float flash = (float)nearness;
            if (nearness < 0.0)
                break;
            if (flash > 1.0f)
                flash = 1.0f;
            else if (!(flash > 0.0f))
                break;

            double facing;
            if (obj->position.x == target->position.x && obj->position.z == target->position.z) {
                facing = 1.0f;
            } else {
                float angle = (float)FPatan((double)obj->position.x - target->position.x,
                                            (double)obj->position.z - target->position.z);
                double off;
                if (Vec_AngleDifference(angle, target->rotation.y) < 0.0f)
                    off = -Vec_AngleDifference(angle, target->rotation.y);
                else
                    off = Vec_AngleDifference(angle, target->rotation.y);
                facing = 1.0f - (off - 0.5235988f) * 0.8185111f;
                if (facing < 0.0)
                    facing = 0.0f;
                else if (facing > 1.0)
                    facing = 1.0f;
            }
            double dazzle = facing * flash;
            flash = (float)dazzle;
            if (!(dazzle > 0.0))
                break;

            double strength = flash;
            if (!Collide_LineOfSight(&head, pos, obj->inCel, obj, target, 10))
                strength = strength * 0.5f;
            uchar blur = (uchar)Ftol(255.0f * strength);
            Player_SetFlashBang((BLData *)target->extraObjectData, (float)((double)_FRAME_RATE * strength * 10.0f), blur);
            break;
        }

        case OBJECTTYPE_DRONE:
            if (bullet->firedByObj == NULL)
                break;
            if (MPSettings.maybeDroneAIEnabled || bullet->firedByObj->objectType == OBJECTTYPE_PLAYER)
                Drone_MessageObjVicinity(DRONE_MSG_ImpactStunGrenade, obj, 20.0f, NULL, 0);
            break;

        case OBJECTTYPE_GUNTURRET:
            GT_Disable(target);
            break;
        }
    }
    Collide_FreeHitList(&hitList);
    obj->flags |= 1;
}

// One of a satchel's two digits, at offset in its space
static void Bullet_SatchelDigit(obj_tag *obj, _MATRIX *rot, _VECTOR *offset, int digit) {
    _VECTOR at;
    ApplyMatrixLV(&obj->transformMatrix, offset, &at);
    Vec_Add2(&obj->position, &at, &at);
    obj_tag *shown = control_create_object(sizeof(Effect_tag), &at, NULL, NULL);
    MulMatrix0(&obj->transformMatrix, rot, &shown->transformMatrix);
    Matrix_SetTrans(&at, &shown->transformMatrix);
    Effect_tag *effect = (Effect_tag *)shown->extraObjectData;
    shown->transformFlags |= TRANSFORM_MATRIX_PLACED;
    shown->objectType = OBJECTTYPE_EFFECT;
    effect->life = 0;
    effect->fade = 0;
    hashtable_set_object_to_entity_gfx(shown, SatchelDigits[digit]);
    shown->effectFlags &= ~0x2000;
    control_link_object_to_cel(shown, obj->inCel);
}

// What goes with the flight: the grapple's point, acceleration, smoke and dust above water, the trail, the whizz near
// the camera, the fuse (a flash, a smoke cloud or an explosion), and a satchel's count and its digits
void _Bullet_DoTrails(obj_tag *obj, BU_tag *bullet, _VECTOR *startPosition, float step) {

    if (obj == NULL || bullet == NULL || bullet->wpnDef == NULL)
        return;
    cel_tag *cel = obj->inCel;
    if (cel == NULL)
        return;

    if (bullet->firedByObj != NULL && (bullet->wpnDef->projectileFlags & ProjectileFlag_GrappleLine))
        Vec_Copy(&obj->position, &((BLData *)bullet->firedByObj->extraObjectData)->grapplePoint);

    weapon_definition_tag *weapon = bullet->wpnDef;
    if ((weapon->projectileFlags & ProjectileFlag_Accelerates) && bullet->speed < weapon->projectileSpeed)
        bullet->speed = (float)((double)weapon->projectileSpeed * 0.02f + bullet->speed);

    // Out of water, by where it started the frame
    if (startPosition != NULL) {
        if ((weapon->projectileFlags & ProjectileFlag_Smoke) && !(cel->shearHeight > startPosition->y))
            _Bullet_trail_effect_smoke(obj);
        if ((bullet->wpnDef->projectileFlags & ProjectileFlag_Dust) && !(cel->shearHeight > startPosition->y))
            _Bullet_SpawnSmokeDust_SomeOtherProbability(obj);
        if ((bullet->wpnDef->projectileFlags & ProjectileFlag_FineDust) && !(cel->shearHeight > startPosition->y))
            _Bullet_SpawnSmokeDust_SomeProbability(obj);
    }

    // A new segment of trail every swooshInterval frames; the newest follows the bullet
    weapon = bullet->wpnDef;
    if ((weapon->projectileFlags & ProjectileFlag_Swoosh) && bullet->swoosh != NULL) {
        SWOOSH_tag *last = (SWOOSH_tag *)bullet->swoosh->extraObjectData;
        if ((uint)(GameState.NumFramesUnpaused - obj->creationTimeFrames) % (uint)(int)weapon->swooshInterval == 0) {
            if (Bullet_IsBeamWeapon(weapon->weaponBaseNum))
                bullet->swoosh = Swoosh_Create(&obj->position, &last->from, weapon->swooshLifetime, SWOOSH_BEAM, 0);
            else
                bullet->swoosh = Swoosh_Create(&obj->position, &last->from, weapon->swooshLifetime, SWOOSH_TRAIL, 1);
        } else {
            Vec_Copy(&obj->position, &last->from);
        }
    }

    // A bullet passing within 3 units of the camera, now and then
    if ((bullet->wpnDef->projectileFlags & ProjectileFlag_FlyBy) && !MPSettings.isMultiplayer
        && bullet->firedByObj != glb_players[0]) {
        _VECTOR nearest;
        vecutil_calculate_closest_point_on_line(&nearest, &obj->position, &obj->lastPosition, &glb_viewer[0]->pos);
        if (Vec_SqDist3D(&nearest, &glb_viewer[0]->pos) < 9.0f && Rand_Rand(5) == 0)
            Bullet_PlaySound(SFX_BULLET_WHIZZ, &nearest);
    }

    // The fuse; the player's remote mine and the detonated ones wait
    weapon = bullet->wpnDef;
    if (weapon->impactFlags & ImpactFlag_Fuse) {
        if (weapon->weaponBaseNum == WEAPON_BASE_MINE) {
            if (bullet->fuseTimer != kMineWaitsForDetonator)
                bullet->fuseTimer -= FRAME_RATE_MUL;
        } else if (weapon->weaponBaseNum != WEAPON_BASE_DETONATED_59 && weapon->weaponBaseNum != WEAPON_BASE_DETONATED_5B) {
            bullet->fuseTimer -= FRAME_RATE_MUL;
        }

        if (bullet->fuseTimer < 0.0f) {
            obj->curState = BulletState_Spent;
            obj->flags |= 1;
            char flags = obj->flags;
            uint impact = bullet->wpnDef->impactFlags;
            if (impact & ImpactFlag_FlashBang) {
                Bullet_FlashBang(obj, bullet);
            } else if (impact & ImpactFlag_SmokeCloud) {
                // The first time a cloud for 20 seconds (40 for the smoke grenade), and it stays; the second, it goes
                if (obj->subState == 0) {
                    obj->subState = 1;
                    obj->curState = BulletState_Flying;
                    obj->flags = flags & ~1;
                    bullet->fuseTimer = 1200.0f;
                    if (bullet->wpnDef->weaponBaseNum == WEAPON_BASE_SMOKE)
                        bullet->fuseTimer = 2400.0f;
                    bullet->occluder = Occlude_Create(&obj->position, Ftol(bullet->fuseTimer), 1);
                    Bullet_PlaySound(SFX_SMOKE_GRENADE, &obj->position);
                }
            } else {
                weapon = bullet->wpnDef;
                Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, weapon->explodeRadius, weapon->explodeRadius,
                               BULLET_EXPLOSION, weapon->damage, 200, 200, 0, bullet->firedByObj, 0);
            }
        }
    }

    if (bullet->wpnDef->weaponBaseNum != WEAPON_BASE_SATCHEL)
        return;

    // A satchel beeps faster as its seconds run out, and shows them. Shot enough, it is disarmed and sets itself
    // again, 1 to 91 seconds.
    short seconds = Ftol((double)bullet->fuseTimer * 0.016666668f);
    short quarters = Ftol((double)bullet->fuseTimer * 0.06666667f);
    if (bullet->charge <= 0) {
        bullet->fuseTimer = (float)(uint)(Rand_Rand(0x1518) + 0x3c);
    } else {
        if (quarters != bullet->numRicochets) {
            bullet->numRicochets = quarters;
            if (quarters % 4 == 0 || (quarters % 2 == 0 && seconds < 10) || seconds < 5)
                Bullet_PlaySound(SFX_WEAPON_SATCHEL_SWITCH, &obj->position);
        }
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            if (hit->hitObj == NULL || hit->hitObj->objectType != OBJECTTYPE_BULLET)
                continue;
            short base = ((BU_tag *)hit->hitObj->extraObjectData)->wpnDef->weaponBaseNum;
            short shot = 0;
            if (base == WEAPON_BASE_SHOOTS_SATCHEL_4E)
                shot = Ftol(FRAME_RATE_MUL);
            if (base == WEAPON_BASE_SHOOTS_SATCHEL_4F)
                shot = Ftol((double)FRAME_RATE_MUL * 10.0f);
            if (shot == 0)
                continue;
            bullet->charge -= shot;
            Emitter_Create(SATCHEL_SHOT_SPARKS, &hit->hitPosition, &hit->surfaceNormal.normal, 0.0f, 0, 0, 0, 0.0f);
            if (bullet->charge <= 0)
                Bullet_PlaySound(SFX_SATCHEL_DISARMED, &obj->position);
        }
    }

    _MATRIX rot;
    _VECTOR turn = {1.5707964f, 0.0f, 3.1415927f};
    _VECTOR tens = {0.0936933f, -0.0022869301f, -0.122736f};     // where the digits are, in its space
    _VECTOR units = {0.0321801f, -0.0022869301f, -0.122736f};
    RotMatrix(&turn, &rot);
    Bullet_SatchelDigit(obj, &rot, &tens, seconds / 10);
    Bullet_SatchelDigit(obj, &rot, &units, seconds % 10);
}

#ifndef NIGHTFIRE_STANDALONE
// The object in ECX, the bullet in EAX
// AUTOLTCG
void __declspec(naked) Bullet_DoTrails(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call _Bullet_DoTrails
        add esp, 16
        ret
    }
}
#endif

// On deletion: the sound stopped, a guided missile's player given back control, the grapple let go (and, stopped at
// a grapple point, the player hung from it), the trip-bomb's beam deleted
// AUTOINJECT
void Bullet_Delete(obj_tag *obj) {

    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    BLData *blData = NULL;

    if (bullet->soundHandle != NULL) {
        Sound_Stop(bullet->soundHandle);
        bullet->soundHandle = NULL;
    }
    if (bullet->firedByObj != NULL && bullet->firedByObj->objectType == OBJECTTYPE_PLAYER)
        blData = (BLData *)bullet->firedByObj->extraObjectData;

    if ((bullet->wpnDef->projectileFlags & ProjectileFlag_RemoteControl) && blData != NULL) {
        blData->remoteControlDevice = NULL;
        blData->remoteControlTimer = 15;
    }

    if ((bullet->wpnDef->impactFlags & ImpactFlag_Grapple) && blData != NULL) {
        blData->movementDisabled = 0;
        if (obj->curState == BulletState_Spent) {
            if (obj->hitList == NULL)
                return;
            obj_tag *point = obj->hitList->hitObj;
            if (point == NULL || point->objectType != OBJECTTYPE_GRAPPLE)
                return;
            _VECTOR *at = (_VECTOR *)&obj->transformMatrix.m[12];
            Bullet_PlaySound(SFX_GADGET_GRAPPLE_ATTACH, at);
            Player_SetGrapplePoint(blData, at, point);
        }
    }

    if ((bullet->wpnDef->impactFlags & ImpactFlag_TripBomb) && bullet->swoosh != NULL)
        bullet->swoosh->flags |= 1;
}

// Whether a homing projectile may chase the object: a helicopter's body drawn lately, a live drone drawn lately and
// not in state 1, a live player, a car drawn lately
bool _Bullet_heatseekable(obj_tag *obj) {
    switch (obj->objectType) {
    case 0:
        if (!obj->drawnCountdown)
            return false;
        for (COPTER *copter = (COPTER *)CopterList.head; copter != NULL; copter = (COPTER *)copter->node.next) {
            if (Copter_GetBody(copter) == obj)
                return true;
        }
        return false;
    case OBJECTTYPE_DRONE:
        if (!obj->drawnCountdown || ((Drone_tag *)obj->extraObjectData)->health <= 0.0f)
            return false;
        return obj->curState != 1;
    case OBJECTTYPE_PLAYER:
        return !(((BLData *)obj->extraObjectData)->health <= 0.0f);
    case OBJECTTYPE_CAR:
        return obj->drawnCountdown != 0;
    default:
        return false;
    }
}

#ifndef NIGHTFIRE_STANDALONE
// The object in EDI
// AUTOLTCG
void __declspec(naked) Bullet_heatseekable(void) {
    _asm {
        push ecx
        push edx
        push edi
        call _Bullet_heatseekable
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}
#endif

// A homing projectile's target: the nearest object it may chase (other than who fired it) within 1000, ahead of the
// shooter (within about 49 degrees of where he faces) and in his line of sight.
// AUTOINJECT
void Bullet_acquiretarget(BU_tag *bullet, obj_tag *shooter) {
    float nearest = 1000.0f;
    bullet->heatseekTarget = NULL;
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (!_Bullet_heatseekable(obj) || obj == bullet->firedByObj)
            continue;
        float distance = Vec_Dist3D(&obj->position, &shooter->position);
        if (!(distance < nearest))
            continue;
        _VECTOR facing, towards;
        Mat_GetDir(&facing, &shooter->transformMatrix);
        Vec_Subtract(&obj->position, &shooter->position, &towards);
        Vec_Normalise(&towards, &towards);
        double dot = (double)towards.z * facing.z + (double)towards.y * facing.y + (double)towards.x * facing.x;
        if (!(dot > 0.66f))
            continue;
        if (Collide_LineOfSight(&shooter->position, &obj->position, shooter->inCel, shooter, obj, 8)) {
            bullet->heatseekTarget = obj;
            nearest = distance;
        }
    }
}

// Turns a homing projectile a little towards its target (finding one first), by at most 0.01 on each axis a frame
void _Bullet_homing(BU_tag *bullet, obj_tag *obj) {

    if (bullet->heatseekTarget != NULL && (bullet->heatseekTarget->flags & 1))
        bullet->heatseekTarget = NULL;
    if (bullet->heatseekTarget == NULL)
        Bullet_acquiretarget(bullet, obj);
    if (bullet->heatseekTarget == NULL || obj->subState != 0)
        return;

    _VECTOR *target = &bullet->heatseekTarget->position;
    Mat_GetDir(&bullet->direction, &obj->transformMatrix);
    _VECTOR turn;
    Vec_Subtract(target, &obj->position, &turn);
    Vec_Normalise(&turn, &turn);
    turn.x = (float)(((double)turn.x - bullet->direction.x) * 0.0333f);
    turn.y = (float)(((double)turn.y - bullet->direction.y) * 0.0333f);
    turn.z = (float)(((double)turn.z - bullet->direction.z) * 0.0333f);
    if (turn.x < -0.01f)
        turn.x = -0.01f;
    else if (turn.x > 0.01f)
        turn.x = 0.01f;
    if (turn.y < -0.01f)
        turn.y = -0.01f;
    else if (turn.y > 0.01f)
        turn.y = 0.01f;
    if (turn.z < -0.01f)
        turn.z = -0.01f;
    else if (turn.z > 0.01f)
        turn.z = 0.01f;
    Vec_Add2(&bullet->direction, &turn, &bullet->direction);
    Vec_Normalise(&bullet->direction, &bullet->direction);
    Mat_Align2Dir(&obj->transformMatrix, &bullet->direction, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
}

#ifndef NIGHTFIRE_STANDALONE
// The bullet in ESI, the object on the stack
// AUTOLTCG
void __declspec(naked) Bullet_homing(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Bullet_homing
        add esp, 8
        ret
    }
}
#endif

// The level's 32 spent cases, hidden until used
// AUTOINJECT
void Bullet_InitCasingLists(void) {
    for (int i = 0; i < NUM_CASINGS; i++) {
        CasingList[i] = control_create_object_xbox(sizeof(CASING_tag), XBOX_SIZEOF(CASING_tag), NULL, NULL, NULL);
        View_SetDrawInNoViews(CasingList[i]);
        CasingList[i]->curState = 2;
    }
    NextCasing = 0;
}

// The next case, for the gun obj holds: thrown out after the weapon's delay (Bullet_casing_update)
// AUTOINJECT
void Bullet_init_casing(obj_tag *obj) {

    if (obj == NULL)
        return;

    weapon_definition_tag *weapon = &weapon_data[obj->animState->currentWeaponId];
    HASHCODE model = (HASHCODE)ammo_data[weapon->ammoType].casingHashcode;
    if (model == 0)
        return;

    obj_tag *casing = CasingList[NextCasing];
    NextCasing = (NextCasing + 1) % NUM_CASINGS;
    if (casing == NULL)
        return;

    CASING_tag *data = (CASING_tag *)casing->extraObjectData;
    casing->objectType = OBJECTTYPE_CASING;
    casing->curState = 3;
    data->owner = obj;
    data->model = model;
    data->timer = weapon->casingDelayFrames;
    hashtable_set_object_to_entity_gfx(casing, model);
    View_SetDrawInNoViews(casing);
}

// Hidden until used again
static void Bullet_PutCasingAway(obj_tag *casing) {
    casing->curState = 2;
    View_SetDrawInNoViews(casing);
}

// Throws the case out of the gun: from the ejection bone (the player's view model's, a drone's weapon datum), along
// the weapon's ejection velocity turned by the bone with a little at random, plus the carrier's own movement
// AUTOINJECT
void Bullet_init_casing_ex(obj_tag *casing) {

    if (casing == NULL)
        return;

    CASING_tag *data = (CASING_tag *)casing->extraObjectData;
    obj_tag *owner = data->owner;
    if (owner == NULL) {
        Bullet_PutCasingAway(casing);
        return;
    }
    weapon_definition_tag *weapon = &weapon_data[owner->animState->currentWeaponId];
    if (ammo_data[weapon->ammoType].casingHashcode == 0) {
        Bullet_PutCasingAway(casing);
        return;
    }

    // (The original copies the owner's rotation and turns it by the player's pitch here, then overwrites it unread.)
    _VECTOR bonePos;
    _MATRIX bone;
    if (owner->objectType == OBJECTTYPE_PLAYER) {
        BLData *blData = (BLData *)owner->extraObjectData;
        if ((weapon->weaponFlags & WeaponFlag_TwinGuns) && !blData->twinGunHand)
            AnimGetBoneWorldTrans(blData->weaponObject, 5, 0, &bonePos, &bone);
        else
            AnimGetBoneWorldTrans(blData->weaponObject, 1, 0, &bonePos, &bone);
    } else {
        int datum = AnimDatumGetWeaponInfo(owner, NULL, NULL, NULL);
        if (datum < 0)
            return;
        if (AnimDatumGetIndex(owner, datum + 2) >= 0)
            AnimGetBoneWorldTrans(owner, datum, 2, &bonePos, &bone);
        else
            AnimGetBoneWorldTrans(owner, datum, 1, &bonePos, &bone);
    }

    _VECTOR eject;
    eject.x = weapon->casingEjectVelocity[0];
    eject.y = weapon->casingEjectVelocity[1] * -1.0f;
    eject.z = weapon->casingEjectVelocity[2];
    ApplyMatrixLVI(&bone, &eject);
    data->vel = eject;

    data->spin.x = (float)Rand_FRand_MVar2(0.1f, 0.05f);
    data->spin.y = (float)Rand_FRand_MVar2(0.1f, 0.05f);
    data->spin.z = (float)Rand_FRand_MVar2(0.1f, 0.05f);
    data->vel.x = (float)(Rand_FRand_MVar2(0.01f, 0.005f) + data->vel.x);
    data->vel.y = (float)(Rand_FRand_MVar2(0.01f, 0.005f) + data->vel.y);
    data->vel.z = (float)(Rand_FRand_MVar2(0.01f, 0.005f) + data->vel.z);
    Vec_Scalef(&data->vel, FRAME_RATE_MUL, &data->vel);
    Vec_Scalef(&data->spin, FRAME_RATE_MUL, &data->spin);
    data->vel.x = (float)((double)owner->position.x - owner->lastPosition.x + data->vel.x);
    data->vel.y = (float)((double)owner->position.y - owner->lastPosition.y + data->vel.y);
    data->vel.z = (float)((double)owner->position.z - owner->lastPosition.z + data->vel.z);
    Vec_Copy2(&bonePos, &casing->position, &casing->lastPosition);

    // Turned along the bone
    _VECTOR along;
    along.x = 0.0f;
    along.y = 0.0f;
    along.z = 1.0f;
    ApplyMatrixLVI(&bone, &along);
    _VECTOR angles;
    vecutil_cartesian_to_spherical_acc(&angles, along.x, along.y, along.z);
    casing->rotation.x = -angles.x;
    casing->rotation.y = angles.y;
    casing->rotation.z = 0.0f;
    casing->curState = 0;
    control_unlink_object(casing);
    control_link_object_to_cel(casing, owner->inCel);
    casing->effectFlags |= 0x900;
    if (owner->objectType == OBJECTTYPE_PLAYER)
        casing->effectFlags |= 0x2000;

    data->timer = 0.0f;
    data->floorY = (float)(owner->position.y - (Float_FRand(0.5f) + 1.0f));
    AnimState *anim = owner->animState;
    if (anim != NULL)
        data->material = anim->floor.materialType & 0x3f;
    else
        data->material = 0;
    View_SetDrawInAllViews(casing);
}

// The sound a case lands with on the material
static Action_SFX Bullet_CasingSound(CASING_tag *data) {
    if (data->model == SHOTGUN_CASING && data->material != 13 && data->material != 14)
        return SFX_WEAPON_SHELL_SHOTGUN;
    switch (data->material) {
    case 1: case 20:
        return SFX_WEAPON_SHELL_FABRIC;
    case 3: case 4:
        return SFX_WEAPON_SHELL_WOOD;
    case 5:
        return SFX_WEAPON_SHELL_METAL_LIGHT;
    case 6:
        return SFX_WEAPON_SHELL_METAL_HEAVY;
    case 10: case 21:
        return SFX_WEAPON_SMALL_SHELL_HARD_FLOOR;
    case 13: case 14:
        return SFX_WEAPON_SHELL_WATER;
    case 15: case 16: case 17: case 18:
        return SFX_WEAPON_SHELL_TILE;
    default:
        return (Action_SFX)0;
    }
}

// Waiting (state 3), the delay; flying, it falls, spins and lands with its material's sound, and after four seconds
// is hidden (state 2) until used again
// AUTOINJECT
void Bullet_casing_update(obj_tag *casing) {

    CASING_tag *data = (CASING_tag *)casing->extraObjectData;
    if (casing->curState == 3) {
        double left = (double)data->timer - FRAME_RATE_MUL;
        data->timer = (float)left;
        if (left <= 0.0)
            Bullet_init_casing_ex(casing);
        return;
    }
    if (casing->curState == 2)
        return;

    _VECTOR next;
    next.x = casing->position.x + data->vel.x;
    next.y = data->vel.y + casing->position.y;
    next.z = data->vel.z + casing->position.z;

    double age = (double)FRAME_RATE_MUL + data->timer;
    data->timer = (float)age;
    if (age > 5.0f)
        casing->effectFlags &= ~0x2000;

    if (casing->position.y <= data->floorY) {
        data->floorY = -1000000.0f;
        Action_SFX sound = Bullet_CasingSound(data);
        if (sound != 0)
            Sound_Play3D(sound, &casing->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }

    if (data->timer > 240.0f) {
        casing->curState = 2;
        View_SetDrawInNoViews(casing);
        data->timer = 32767.0f;
    }

    casing->position = next;
    Vec_Add2(&data->spin, &casing->rotation, &casing->rotation);
    double gravity = (double)REC_FRAME_RATE * REC_FRAME_RATE;
    if (casing->curState == 1)
        gravity = gravity * 0.1f;
    auxVec_AddMulR32(&data->vel, &GRAVITY_VECTOR, (float)gravity, &data->vel);
    casing->transformFlags |= TRANSFORM_MOVED;
}

// ---------------------------------------------------------------------------------------------------------------
// Bullet_Update (0x00023670), once a frame for every projectile.
//
// Most of what it does is move the bullet along its direction by its speed and set up the frame's collision
// test, which Bullet_CollisionHandler then runs; a few kinds of projectile do something extra first. The one
// that matters for the controls is the Sentinel's guided missile (projectile flag 0x4), which the player who
// fired it steers with the aim stick for as long as he is in the remote-control substate - see
// Bullet_SteerRemoteControl.
//
// x87: the original keeps several intermediate results in 80-bit registers. Where a chain of more than one
// operation is stored to a float, it is computed here in double, which matches it for every value these see.
// ---------------------------------------------------------------------------------------------------------------

// The Sentinel's turn at full deflection of the stick: pi/128 radians per 60 Hz frame.
static constexpr float SteerRatePerFrame = 0.024543693f;
static_assert(std::bit_cast<uint32_t>(SteerRatePerFrame) == 0x3cc90fdb);

// How close to straight up or straight down the missile may point before it stops pitching further that way.
static constexpr float SteerPitchLimit = 0.995f;
static_assert(std::bit_cast<uint32_t>(SteerPitchLimit) == 0x3f7eb852);

// How much of the missile's roll is kept from one frame to the next. Steering banks it, and this levels it
// out again once the turn stops.
static constexpr float SteerRollDecay = 0.965f;
static_assert(std::bit_cast<uint32_t>(SteerRollDecay) == 0x3f770a3d);

// The steering of a remote-controlled missile, by the player whose object is shooter.
static void Bullet_SteerRemoteControl(obj_tag *obj, BU_tag *bullet, obj_tag *shooter) {

    BLData *shooterData = (BLData*)shooter->extraObjectData;
    _MATRIX *matrix = &obj->transformMatrix;

    // Pushing the stick up gives a negative pitch: turn.x is added to the missile's own pitch below, which
    // the spherical conversion counts the other way round from the player's view. (The mouse steers through
    // these same two channels - engine/mouseSteer.cpp.)
    _VECTOR turn;
    turn.y = (float)((double)FRAME_RATE_MUL * SteerRatePerFrame
                     * Input_Actionf(shooterData->playerNum, ACTION_AIM_L_R, 1));
    turn.x = -(float)((double)FRAME_RATE_MUL * SteerRatePerFrame
                      * Input_Actionf(shooterData->playerNum, ACTION_AIM_U_D, 1));

    _VECTOR direction;
    Mat_GetDir(&direction, matrix);

    // Once pointing (almost) straight up or down, it cannot pitch any further that way.
    if ((direction.y < -SteerPitchLimit && turn.x > 0.0f) || (direction.y > SteerPitchLimit && turn.x < 0.0f)) {
        turn.x = 0.0f;
    }

    _VECTOR position;
    Mat_GetPosition(&position, matrix);

    // Rebuild the matrix from the turned heading, then roll it into the turn.
    _VECTOR heading;
    vecutil_cartesian_to_spherical_acc(&heading, direction.x, direction.y, direction.z);
    heading.x = -heading.x + turn.x;
    heading.z = 0.0f;
    heading.y = heading.y - turn.y;

    _MATRIX headingMatrix;
    RotMatrixZYX(&heading, &headingMatrix);

    _VECTOR roll;
    roll.z = turn.y + obj->rotation.z;
    roll.y = 0.0f;
    roll.x = 0.0f;

    _MATRIX rollMatrix;
    RotMatrix(&roll, &rollMatrix);

    MulMatrix0(&headingMatrix, &rollMatrix, matrix);
    Matrix_SetTrans(&position, matrix);
    obj->rotation.z = roll.z * SteerRollDecay;
    Mat_GetDir(&bullet->direction, matrix);

    // Steering does not go through the usual collision sweep, so check for having flown into something. The
    // shooter is left out for the first unit of flight, so the missile does not hit him on the way out.
    obj_tag *ignore = bullet->firedByObj;
    HITDATA_tag *hitList = NULL;
    if (bullet->distanceTravelled > 1.0f)
        ignore = NULL;

    Collide_SphereIntersect((_VECTOR*)&matrix->m[12], 0.1f, obj->inCel, obj, ignore, &hitList, 0, 0, 0xa0a, 4);
    bool hit = hitList != NULL;
    Collide_FreeHitList(&hitList);

    if (hit)
        Bullet_handle_object_destruction(obj, bullet->firedByObj);

}

// The trip-bomb's beam, once it has stuck: 200 units straight out from where it is facing, set off by a player,
// drone or car crossing it.
static void Bullet_TripBombBeam(obj_tag *obj, BU_tag *bullet) {

    if (bullet->swoosh == NULL) {
        bullet->swoosh = control_create_object(0, &obj->position, NULL, NULL);
        hashtable_set_object_to_entity_gfx(bullet->swoosh, (HASHCODE)0x2000133);
        bullet->swoosh->objectType = OBJECTTYPE_GFX;
        bullet->swoosh->effectFlags |= 0x20;
        bullet->swoosh->maybeParent = obj;
        bullet->swoosh->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
        Sound_Play3D(SFX_WEAPON_GENERIC_LASER_SIGHT_ON, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }

    _VECTOR end;
    Mat_GetDir(&end, &obj->transformMatrix);
    Vec_Scalef(&end, -200.0f, &end);
    Vec_Add2(&end, &obj->position, &end);

    HITDATA_tag *hitList = NULL;
    Collide_RayIntersect(&obj->position, &end, obj->inCel, obj, NULL, &hitList, 0, 0x20a, 0);
    if (hitList != NULL) {
        Vec_Copy(&hitList->hitPosition, &end);

        obj_tag *crossing = hitList->hitObj;
        if (crossing != NULL && (crossing->objectType == OBJECTTYPE_PLAYER || crossing->objectType == OBJECTTYPE_DRONE
                                 || crossing->objectType == OBJECTTYPE_CAR)) {
            Bullet_handle_object_destruction(obj, bullet->firedByObj);
        }
    }
    Collide_FreeHitList(&hitList);

    PositionBeam(bullet->swoosh, &obj->position, &end);

}

// AUTOINJECT
void Bullet_Update(obj_tag *obj) {

    BU_tag *bullet = (BU_tag*)obj->extraObjectData;
    weapon_definition_tag *weapon;
    _VECTOR startPosition = obj->position;

    if (bullet->soundHandle != NULL) {
        if (Sound_IsFinnished(bullet->soundHandle))
            bullet->soundHandle = NULL;
        else
            Sound_SetPosition(bullet->soundHandle, (_VECTOR*)&obj->transformMatrix.m[12]);
    }

    bullet->test.type = 0;

    if (obj->flags & 1)
        return;

    // How far the bullet moves this frame, before the frame rate is applied.
    float step;

    switch ((short)obj->curState) {

        case BulletState_Flying:

            step = bullet->speed;

            if (bullet->distanceTravelled > bullet->wpnDef->range) {
                obj->curState = BulletState_OutOfRange;
                obj->flags |= 1;
                return;
            }

            // Tumbling: a roll about its own axis, slower after every bounce.
            if (bullet->wpnDef->projectileFlags & ProjectileFlag_Tumbles) {
                int bounces = bullet->numRicochets + 1;

                _VECTOR spin;
                spin.y = 0.0f;
                spin.x = 0.0f;
                spin.z = (float)((double)FRAME_RATE_MUL * 0.1f / bounces);

                _MATRIX spinMatrix, current;
                RotMatrix(&spin, &spinMatrix);
                Mat_Copy(&obj->transformMatrix, &current);
                MulMatrix0(&current, &spinMatrix, &obj->transformMatrix);
            }

            if (bullet->wpnDef->projectileFlags & ProjectileFlag_RemoteControl) {
                obj_tag *shooter = bullet->firedByObj;
                if (shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER
                    && shooter->subState == MovementType_RemoteControl) {
                    Bullet_SteerRemoteControl(obj, bullet, shooter);
                }
            }

            // Gravity bends the direction and changes the speed, which are kept apart as a unit direction and
            // a length.
            if ((bullet->wpnDef->projectileFlags & ProjectileFlag_Gravity) && !bullet->gravityOff) {
                Vec_Scalef(&bullet->direction, bullet->speed, &bullet->direction);
                auxVec_AddMulR32(&bullet->direction, &GRAVITY_VECTOR,
                                 (float)((double)REC_FRAME_RATE * REC_FRAME_RATE * FRAME_RATE_DIV), &bullet->direction);
                bullet->speed = Vec_NormaliseLen(&bullet->direction, &bullet->direction);
            }

            weapon = bullet->wpnDef;
            if (weapon->projectileFlags & ProjectileFlag_Light) {
                Light_Create(&obj->position, weapon->muzzleFlash_r, weapon->muzzleFlash_g, weapon->muzzleFlash_b,
                             4.0f, 0, 0, 1, 2.0f, 0, 0, 0, 0xffff);
            }
            break;

        case BulletState_Spent:
        case BulletState_OutOfRange:
            obj->flags |= 1;
            return;

        case BulletState_Stuck:

            if (bullet->soundHandle != NULL) {
                Sound_Stop(bullet->soundHandle);
                bullet->soundHandle = NULL;
            }

            // Stuck to something that has since gone: a trip-bomb goes off, anything else flies on.
            if (bullet->heatseekTarget != NULL) {
                if (bullet->heatseekTarget->flags & 1) {
                    if (bullet->wpnDef->impactFlags & ImpactFlag_TripBomb) {
                        Bullet_handle_object_destruction(obj, bullet->firedByObj);
                        bullet->heatseekTarget = NULL;
                        return;
                    }
                    obj->curState = BulletState_Flying;
                    bullet->heatseekTarget = NULL;
                    return;
                }
                Control_InheritVelocity(obj, bullet->heatseekTarget, 30.5f);
            }

            step = 0.0f;
            bullet->speed = 0.0f;
            Vec_Zero(&bullet->direction);

            if (bullet->wpnDef->impactFlags & ImpactFlag_TripBomb) {
                // Armed once the fuse has run down; until then the beam is not there.
                if (bullet->fuseTimer > 0.0f) {
                    bullet->fuseTimer -= FRAME_RATE_MUL;
                    break;
                }
                Bullet_TripBombBeam(obj, bullet);
            }

            // Oddjob's hat only lasts so long once it has stuck.
            if (bullet->wpnDef->weaponBaseNum == Weap_OddjobHat) {
                bullet->fuseTimer -= FRAME_RATE_MUL;
                if (bullet->fuseTimer <= 0.0f)
                    obj->flags |= 1;
            }
            break;

        default: // BulletState_New

            // Speeds vary a little from one bullet to the next: half to one and a half times the weapon's.
            step = (float)((Float_FRand(0.5f) + 0.5f) * bullet->speed);
            obj->curState++;

            if ((bullet->wpnDef->projectileFlags & (ProjectileFlag_NoModel | ProjectileFlag_Tracer)) == 0) {
                obj->scale = 1.0f;
                HASHCODE gfx = bullet->wpnDef->projectileGfx;
                if (gfx != 0) {
                    hashtable_set_object_to_entity_gfx(obj, gfx);
                    if (bullet->wpnDef->projectileFlags & ProjectileFlag_LaserBeam) {
                        obj->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
                        obj->scale = bullet->speed;
                    }
                }
                if (obj->objGraphics != NULL) {
                    View_SetDrawInAllViews(obj);
                    obj->effectFlags &= ~0x2010;
                }
            }
            else if ((bullet->firedByObj != NULL && bullet->firedByObj->objectType == OBJECTTYPE_DRONE)
                     || (bullet->wpnDef->projectileFlags & ProjectileFlag_Tracer)) {
                View_SetDrawInAllViews(obj);
                hashtable_set_object_to_entity_gfx(obj, (HASHCODE)0x2000123);
                obj->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY;
                obj->scale = bullet->speed;
            }
            break;

    }

    if (obj->curState == BulletState_Stuck) {
        _Bullet_DoTrails(obj, bullet, &startPosition, step);
        obj->transformFlags |= TRANSFORM_MOVED;
        return;
    }

    // The distance flown this frame, cut short at the end of the weapon's range.
    weapon = bullet->wpnDef;
    double frameStep = (double)FRAME_RATE_MUL * step;
    step = (float)frameStep;
    double travelled = frameStep + bullet->distanceTravelled;
    bullet->distanceTravelled = (float)travelled;

    float distance = step;
    if (travelled > weapon->range)
        distance = (float)(step - (travelled - weapon->range));

    // This frame's collision test, for Bullet_CollisionHandler: a sweep along the path when the bullet moves
    // at least half its radius, otherwise a sphere where it is now.
    if ((short)obj->curState < BulletState_Spent) {
        bullet->test.mask = 0;
        bullet->test.flags = (weapon->projectileFlags & ProjectileFlag_MaterialMask) ? 0 : 0x20;
        bullet->test.damage = weapon->damage;

        if (weapon->explodeRadius > 0.0f) {
            switch (weapon->weaponVariantNum) {
                case 0x2a:
                case 0x2c: case 0x2d: case 0x2e: case 0x2f:
                    bullet->test.damage = 1.0f;
                    break;
                case 0x2b:
                case 0x34: case 0x35: case 0x36: case 0x37:
                case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40:
                    bullet->test.damage = 0.0f;
                    break;
            }
        }

        bullet->test.ignoreA = bullet->maybeProxyObjectShooter;
        bullet->test.ignore = 0;
        bullet->test.cel = obj->inCel;

        float radius = obj->radius > 0.25f ? obj->radius : 0.25f;
        short variant = weapon->weaponVariantNum;
        if ((variant >= 0x2c && variant <= 0x31) || variant == 0x62 || variant == 0x6d)
            radius = 0.0f;

        if (radius * 0.5f > distance) {
            bullet->test.radius = radius;
            bullet->test.type = HITTEST_SPHERE;
            Vec_Copy(&obj->position, &bullet->test.pos);
        }
        else {
            bullet->test.type = HITTEST_RAY;
            float length = distance < 1.0f ? 1.0f : distance;
            bullet->test.radius = length;
            Vec_Copy(&bullet->direction, &bullet->test.dir);
            Vec_Copy(&obj->position, &bullet->test.rayStart);
            Vec_Copy(&obj->position, &bullet->test.rayEnd);
            auxVec_AddMulR32(&bullet->test.rayEnd, &bullet->direction, length, &bullet->test.rayEnd);
        }
    }
    else {
        bullet->test.type = 0;
    }

    obj->position.x = (float)((double)distance * bullet->direction.x + obj->position.x);
    obj->position.y = (float)((double)distance * bullet->direction.y + obj->position.y);
    obj->position.z = (float)((double)distance * bullet->direction.z + obj->position.z);
    obj->transformFlags |= TRANSFORM_MOVED;

    _Bullet_DoTrails(obj, bullet, &startPosition, step);

    if (bullet->wpnDef->projectileFlags & ProjectileFlag_Homing)
        _Bullet_homing(bullet, obj);

    obj->transformFlags |= TRANSFORM_MOVED;

}
