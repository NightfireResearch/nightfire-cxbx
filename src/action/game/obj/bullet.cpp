#include "bullet.h"

#include <bit>
#include <stdint.h>

#include "Player.h"
#include "Light.h"
#include "control.h"
#include "../view.h"
#include "../../game.h"
#include "../../input.h"
#include "../../math/math.h"
#include "../../engine/Collide.h"
#include "../../sound/Sound.h"
#include "../../util/hashtable.h"
#include "../../util/Random.h"

// AUTOGEN
obj_tag * Bullet_init(short plyNum, obj_tag *playerObj, obj_tag *param_3, weapon_definition_tag *weaponDef, _VECTOR *pos, _VECTOR *direction, ushort weaponSound);

// ---------------------------------------------------------------------------------------------------------------
// The original functions Bullet_Update calls that are not ours yet, and whose Ghidra prototypes the AUTOGEN
// stubs cannot use as they stand. Called rather than reproduced, so that naming them in Ghidra later does not
// leave two copies.
// ---------------------------------------------------------------------------------------------------------------

static bool Sound_IsFinnished(DYNAMICSOUNDS *handle) {
    return reinterpret_cast<bool (__cdecl *)(DYNAMICSOUNDS *)>(0x000cbac0)(handle);
}

// Ghidra's prototype has untyped arguments. Returns whether anything was hit, and the hits through hitList.
static bool Collide_SphereIntersect(_VECTOR *centre, float radius, cel_tag *cel, obj_tag *obj, obj_tag *ignore,
                                    HITDATA_tag **hitList, char param_7, char param_8, ushort collideFlags,
                                    int param_10) {
    return reinterpret_cast<bool (__cdecl *)(_VECTOR *, float, cel_tag *, obj_tag *, obj_tag *, HITDATA_tag **,
                                             char, char, ushort, int)>(0x0002c610)
        (centre, radius, cel, obj, ignore, hitList, param_7, param_8, collideFlags, param_10);
}

// Blows the bullet up if its weapon explodes on impact; returns whether it did. Ghidra calls it __stdcall, but
// it ends in a bare RET, so the caller cleans up. Bullet_Update pushes the shooter as a second argument as
// well, which the function never reads, so it is not passed here.
static bool Bullet_handle_object_destruction(obj_tag *obj) {
    return reinterpret_cast<bool (__cdecl *)(obj_tag *)>(0x000218f0)(obj);
}

// Trails, sounds, fuses and the rest of a bullet's per-frame effects. The object arrives in ECX and the bullet
// in EAX, the other two on the stack, removed by the caller (tools/abi_action.json: pops 0, regs_in eax ecx).
static void __declspec(naked) Bullet_DoTrails(obj_tag *obj, BU_tag *bullet, _VECTOR *startPosition, float step) {
    _asm {
        mov ecx, [esp + 4]          // obj
        mov eax, [esp + 8]          // bullet
        push dword ptr [esp + 16]   // step
        push dword ptr [esp + 16]   // startPosition (16 again: the push moved it along)
        mov edx, 0x00022210
        call edx
        add esp, 8
        ret
    }
}

// Steers a homing projectile at its target. The bullet arrives in ESI, the object on the stack, removed by the
// caller (pops 0, regs_in esi). ESI is callee-saved for our compiler, so it is put back afterwards.
static void __declspec(naked) Bullet_homing(BU_tag *bullet, obj_tag *obj) {
    _asm {
        push esi
        mov esi, [esp + 8]          // bullet
        push dword ptr [esp + 12]   // obj (12 now: the saved ESI moved it along)
        mov eax, 0x00022eb0
        call eax
        add esp, 4
        pop esi
        ret
    }
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

    MulMatrix0(headingMatrix.m, rollMatrix.m, matrix->m);
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
        Bullet_handle_object_destruction(obj);

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
            Bullet_handle_object_destruction(obj);
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

    bullet->collideMode = 0;

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
                MulMatrix0(current.m, spinMatrix.m, obj->transformMatrix.m);
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
                        Bullet_handle_object_destruction(obj);
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
            step = (float)(((double)Float_FRand(0.5f) + 0.5f) * bullet->speed);
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
        Bullet_DoTrails(obj, bullet, &startPosition, step);
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
        bullet->unknownC0 = 0;
        bullet->collideMaterialMask = (weapon->projectileFlags & ProjectileFlag_MaterialMask) ? 0 : 0x20;
        bullet->hitDamage = weapon->damage;

        if (weapon->explodeRadius > 0.0f) {
            switch (weapon->weaponVariantNum) {
                case 0x2a:
                case 0x2c: case 0x2d: case 0x2e: case 0x2f:
                    bullet->hitDamage = 1.0f;
                    break;
                case 0x2b:
                case 0x34: case 0x35: case 0x36: case 0x37:
                case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40:
                    bullet->hitDamage = 0.0f;
                    break;
            }
        }

        bullet->collideIgnoreObj = bullet->maybeProxyObjectShooter;
        bullet->unknownA0 = 0;
        bullet->collideCel = obj->inCel;

        float radius = obj->radius > 0.25f ? obj->radius : 0.25f;
        short variant = weapon->weaponVariantNum;
        if ((variant >= 0x2c && variant <= 0x31) || variant == 0x62 || variant == 0x6d)
            radius = 0.0f;

        if (radius * 0.5f > distance) {
            bullet->sweepLengthOrRadius = radius;
            bullet->collideMode = 0x101;
            Vec_Copy(&obj->position, &bullet->spherePosition);
        }
        else {
            bullet->collideMode = 0x201;
            float length = distance < 1.0f ? 1.0f : distance;
            bullet->sweepLengthOrRadius = length;
            Vec_Copy(&bullet->direction, &bullet->sweepDirection);
            Vec_Copy(&obj->position, &bullet->sweepStart);
            Vec_Copy(&obj->position, &bullet->sweepEnd);
            auxVec_AddMulR32(&bullet->sweepEnd, &bullet->direction, length, &bullet->sweepEnd);
        }
    }
    else {
        bullet->collideMode = 0;
    }

    obj->position.x = (float)((double)distance * bullet->direction.x + obj->position.x);
    obj->position.y = (float)((double)distance * bullet->direction.y + obj->position.y);
    obj->position.z = (float)((double)distance * bullet->direction.z + obj->position.z);
    obj->transformFlags |= TRANSFORM_MOVED;

    Bullet_DoTrails(obj, bullet, &startPosition, step);

    if (bullet->wpnDef->projectileFlags & ProjectileFlag_Homing)
        Bullet_homing(bullet, obj);

    obj->transformFlags |= TRANSFORM_MOVED;

}
