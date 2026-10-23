#include "../../engine/Camera.h"
#include "GT.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <bit>

#include "../../game.h"
#include "../../input.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "../../sound/music.h"
#include "../../sound/Sound.h"
#include "../weapon_stats.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"
#include "../mp/multiplayer.h"
#include "../sp/PlayerStats.h"
#include "bullet.h"
#include "control.h"
#include "Copter.h"
#include "Emitter.h"
#include "Explode.h"
#include "Gas.h"
#include "Pickup.h"
#include "Player.h"
#include "PlayerWeapon.h"
#include "../../util/Sort.h"
#include "../drone/BOT.h"

#pragma fp_contract(off)


// GT_Track's candidates
#define TARGETS 64
// XBE_GLOBAL(0x0025fc20, 0x200)
TARGETOBJ TargetList[TARGETS];
// How far a turret sees (squared); a fifth of it in multiplayer
// XBE_GLOBAL(0x0016199c, 0x4) readonly relocate
float TurretRange = 2500.0f;
// Seconds between shots
// XBE_GLOBAL(0x00161998, 0x4) readonly relocate
float TurretRefire = 0.1f;

// GT_Update's states (obj_tag::curState)
enum {
    GT_CLOSED = 0,
    GT_OPENING = 1,
    GT_RETURNING = 2,       // to where it pointed when it opened, then closes
    GT_CLOSING = 3,
    GT_ACTIVE = 4,
    GT_STUNNED = 5,         // by the taser or an explosion (GT_Disable); back to subState
    GT_DESTROYED = 6,
    GT_CONTROLLED = 7,      // by the player
};

#define GT_KIND_MINIGUN 0
#define GT_KIND_CEILING 1

#define MINIGUN_MODEL ((HASHCODE)0x02000359)
#define MINIGUN_PICKUP_MODEL ((HASHCODE)0x02000366)
#define MINIGUN_BODY ((HASHCODE)0x06000045)
#define MINIGUN_BASE ((HASHCODE)0x06000044)
#define TURRET_EXPLOSION ((HASHCODE)0x06000052)
#define TURRET_SMOKE ((HASHCODE)0x02000039)
#define TURRET_FIRE_EMITTER ((HASHCODE)0x0c000004)
#define MINIGUN_SOUND ((Action_SFX)0x10a)
#define CEILING_GUN_SOUND ((Action_SFX)0x4e7)
// The turrets' weapon, and the weapon a placed minigun is picked up as
#define TURRET_WEAPON 0x60
#define MINIGUN_WEAPON 0x52
#define RONIN_WEAPON 0x53

// The barrel's turn a 60 Hz frame while it fires
static constexpr float BarrelSpin = 12.566371f;
static_assert(std::bit_cast<uint32_t>(BarrelSpin) == 0x41490fdb, "not the original's 4 pi");

// An explosion's damage
// AUTOINJECT
void GT_ApplyDamage(obj_tag *obj, float damage) {
    ((GUNTURRET *)obj->extraObjectData)->health -= damage;
}

// The script's event 15: its streams 0-2 name the flash, the barrel and the third part
// AUTOINJECT
void GT_Register(obj_tag *obj, ushort stream, void *context) {
    if (context == NULL || stream >= 3)
        return;
    (&((GUNTURRET *)context)->muzzleFlash)[stream] = obj;
}

// A turret with its body and base scripts, playing at once. The muzzle is half as far again as the body's models
// reach. In multiplayer a minigun set down blows up the owner's others. Not made without both scripts.
// AUTOINJECT
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *quat, level_tag *lvl, celglist_tag *celgl, obj_tag* owner) {
    GTPlacement *keys = (GTPlacement *)lvl;
    if (keys->bodyScript == 0 || keys->baseScript == 0)
        return NULL;
    obj_tag *obj = Control_CreateObjEx(sizeof(GUNTURRET), pos, rot, NULL, celgl, NULL, 1, 4, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == NULL)
        return NULL;

    GUNTURRET *turret = (GUNTURRET *)obj->extraObjectData;
    memset(turret, 0, sizeof(GUNTURRET));
    obj->objectType = OBJECTTYPE_GUNTURRET;
    obj->effectFlags |= 0x810;
    turret->kind = (short)keys->kind;
    turret->key1 = (ushort)keys->key1;
    turret->health = 100.0f;
    turret->enableChannel = (ushort)keys->enableChannel;
    turret->disableChannel = (ushort)keys->disableChannel;
    turret->owner = owner != NULL ? owner : obj;
    turret->controlled = 0;
    turret->unknownD4 = 0.0f;
    turret->ammo = turret->kind != GT_KIND_MINIGUN ? -1 : 200;
    Control_ObjectReferenced(obj, 1);

    turret->body = Script_Load(keys->bodyScript, pos, rot, NULL, NULL, GT_Register, turret);
    turret->base = Script_Load(keys->baseScript, pos, rot, NULL, NULL, GT_Register, turret);
    if (turret->body == NULL || turret->base == NULL) {
        obj->flags |= 1;
        return NULL;
    }
    Script_Play(turret->body, 1);
    Script_Play(turret->base, 1);
    Script_Update(turret->body);
    Script_Update(turret->base);
    obj->curState = GT_CLOSED;

    _VECTOR lo, hi;
    lo.x = lo.y = lo.z = 9999999.0f;
    hi.x = hi.y = hi.z = -9999999.0f;
    for (ushort i = 0; turret->body != NULL && i < turret->body->numElements; i++) {
        obj_tag *part = Script_GetObj(turret->body, i);
        if (part == NULL || part->objGraphics == NULL)
            continue;
        Vec_Min(&lo, &part->objGraphics->extentMin, &lo);
        Vec_Max(&hi, &part->objGraphics->extentMax, &hi);
    }
    turret->muzzle = hi.z * 1.5f;
    if (turret->kind == GT_KIND_MINIGUN) {
        Mat_GetDir(&turret->facing, &obj->transformMatrix);
    } else {
        Mat_GetUp(&turret->facing, &obj->transformMatrix);
        Vec_Negate(&turret->facing, &turret->facing);
    }

    if (MPSettings.isMultiplayer && turret->kind == GT_KIND_MINIGUN) {
        for (obj_tag *other = control_first_object(); other != NULL; other = other->nextObject) {
            if (other == obj || other->objectType != OBJECTTYPE_GUNTURRET ||
                ((GUNTURRET *)other->extraObjectData)->owner != turret->owner)
                continue;
            other->flags |= 1;
            Explode_Create(other, &other->position, &CONST_UP_VECTOR, 5.0f, 5.0f, TURRET_EXPLOSION, 50.0f, 0xff, 0xff,
                           100, NULL, 0);
        }
    }
    return obj;
}

// The minigun set down where it fits (Collide_PlaceObject), facing the way the matrix does across the floor. NULL if
// it does not fit.
// AUTOINJECT
obj_tag* GT_DeployMiniGun(_VECTOR *pos, _MATRIX *mtx, obj_tag *owner) {
    _MATRIX placed;
    Mat_Copy(mtx, &placed);
    if (!Collide_PlaceObject(pos, &placed, 0.5f, 0.6f, owner))
        return NULL;
    _VECTOR dir, rot;
    Mat_GetDir(&dir, &placed);
    Vec_DirToRot(&dir, &rot);
    // The disable channel is uninitialised stack in the original
    GTPlacement keys;
    memset(&keys, 0, sizeof(keys));
    keys.kind = GT_KIND_MINIGUN;
    keys.enableChannel = 0;
    keys.key1 = 2;
    keys.bodyScript = MINIGUN_BODY;
    keys.baseScript = MINIGUN_BASE;
    quaternion_tag quat;
    Quat_MatToQuat(&quat, mtx);
    return GT_Create(pos, &rot, &quat, (level_tag *)&keys, hashtable_hashcode_to_celglist(MINIGUN_MODEL), owner);
}

// The target: the candidates within range, nearest first, that are in front of the turret and in sight of its
// muzzle. In multiplayer every player and bot but the owner; else player 0 for a turret not his, and for the minigun
// the drones up and about and the helicopters. The current target stays while it is the second seen.
// AUTOINJECT
obj_tag* GT_Track(GUNTURRET *turret, obj_tag *obj) {
    if (turret->target != NULL && (turret->target->flags & 1))
        turret->target = NULL;
    if (turret->target != NULL && turret->target->objectType == OBJECTTYPE_DRONE &&
        MPDrone_MaybeIsDyingOrDead(turret->target))
        turret->target = NULL;

    float range = TurretRange;
    ushort count = 0;
    for (int i = 0; i < TARGETS; i++)
        TargetList[i].distSq = 999999.0f;
    // Uninitialised in the original when no candidate has set it; the helicopters use the last candidate's
    _VECTOR offset;
    Vec_Zero(&offset);

    if (MPSettings.isMultiplayer) {
        range = range * 0.2f;
        for (ushort i = 0; i < NUM_AGENTS; i++) {
            obj_tag *other = MPGame.players[i].playerObj;
            if (other == NULL || turret->owner == other)
                continue;
            if (other->objectType == OBJECTTYPE_DRONE && ((Drone_tag *)other->extraObjectData)->health <= 0.0f)
                continue;
            if (other->objectType == OBJECTTYPE_PLAYER && ((BLData *)other->extraObjectData)->health <= 0.0f)
                continue;
            Vec_Subtract(&other->position, &obj->position, &offset);
            double distSq = Vec_SqMagnitude(&offset);
            TargetList[count].distSq = (float)distSq;
            if (distSq < range)
                TargetList[count++].obj = other;
            if (count >= TARGETS - 1)
                break;
        }
    } else {
        obj_tag *player = glb_players[0];
        if (turret->owner != player && player != NULL && glb_blokes[0]->health > 0.0f) {
            Vec_Subtract(&player->position, &obj->position, &offset);
            double distSq = Vec_SqDist3D(&glb_players[0]->position, &obj->position);
            TargetList[0].distSq = (float)distSq;
            if (distSq < range && glb_blokes[0]->active) {
                TargetList[0].obj = glb_players[0];
                count = 1;
            }
        }
        bool full = false;
        if (turret->kind == GT_KIND_MINIGUN) {
            for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL && !full; drone = drone->next) {
                obj_tag *other = drone->gameObj;
                if (other == NULL)
                    continue;
                Drone_tag *data = (Drone_tag *)other->extraObjectData;
                if ((data->flags & 0x600) || !(data->flags & 0x100))
                    continue;
                if (data->health <= 0.0f)
                    continue;
                if (other->objectType == OBJECTTYPE_DEAD_DRONE || (other->flags & 1) || (other->effectFlags & FLAG_HIDDEN))
                    continue;
                Vec_Subtract(&other->position, &obj->position, &offset);
                double distSq = Vec_SqMagnitude(&offset);
                TargetList[count].distSq = (float)distSq;
                if (distSq < range)
                    TargetList[count++].obj = other;
                if (count >= TARGETS - 1)
                    full = true;
            }
        }
        for (COPTER *copter = (COPTER *)CopterList.head; copter != NULL && !full;
             copter = (COPTER *)copter->node.next) {
            if (turret->kind != GT_KIND_MINIGUN)
                break;
            obj_tag *body = Copter_GetBody(copter);
            if (body == NULL)
                continue;
            double distSq = Vec_SqMagnitude(&offset) * 0.25f;
            TargetList[count].distSq = (float)distSq;
            if (distSq < range)
                TargetList[count++].obj = body;
            if (count >= TARGETS - 1)
                break;
        }
    }

    if (count == 0)
        return NULL;
    QuickSort(TargetList, count, sizeof(TARGETOBJ), Compare_TargetObj, NULL);

    _VECTOR muzzle;
    Mat_GetDir(&muzzle, &turret->barrel->transformMatrix);
    auxVec_AddMulR32((_VECTOR *)&turret->barrel->transformMatrix.m[12], &muzzle, turret->muzzle, &muzzle);
    obj_tag *first = NULL, *second = NULL;
    for (ushort i = 0; i < count; i++) {
        obj_tag *other = TargetList[i].obj;
        _VECTOR to;
        Vec_Subtract(&other->centrePoint, (_VECTOR *)&turret->body->maybeMatrix.m[12], &to);
        Vec_Normalise(&to, &to);
        double facing = (double)to.y * turret->facing.y + (double)to.z * turret->facing.z +
                        (double)to.x * turret->facing.x;
        if (facing < 0.01f)
            continue;
        if (!Collide_LineOfSight(&muzzle, &TargetList[i].obj->centrePoint, obj->inCel, obj, TargetList[i].obj, 0x72f))
            continue;
        if (first != NULL) {
            second = TargetList[i].obj;
            break;
        }
        first = TargetList[i].obj;
    }

    obj_tag *current = turret->target;
    if (current != NULL && (first == current || second == current))
        return current;
    return first;
}

// From one matrix's turn a step (4 x REC_FRAME_RATE) towards another's, into it, keeping the first's translation
bool GT_maybeInterpolateAimDir(_MATRIX *towards, _MATRIX *from) {
    quaternion_tag start, end, step;
    Quat_MatToQuat(&start, from);
    Quat_MatToQuat(&end, towards);
    Quat_Slerp_Acc(REC_FRAME_RATE * 4.0f, start.q, end.q, step.q);
    Quat_Normalise(&step);
    Quat_QuatToMat(&step, towards);
    Vec_Copy((_VECTOR *)&from->m[12], (_VECTOR *)&towards->m[12]);
    return false;
}

// The body, and the barrel, the third part and the flash with it, turned a step towards a point
void GT_AimAt(GUNTURRET *turret, _VECTOR *at) {
    _VECTOR up;
    Mat_GetUp(&up, &turret->aim);
    _MATRIX look;
    memset(&look, 0, sizeof(look));
    Mat_LookAt(&look, (_VECTOR *)&turret->aim.m[12], at, &up, &MAYBE_CONST_FORWARD_VECTOR);
    GT_maybeInterpolateAimDir(&look, &turret->aim);
    Mat_Copy(&look, &turret->aim);
    if (turret->barrel != NULL)
        Mat_Copy(&look, &turret->barrel->transformMatrix);
    if (turret->part != NULL)
        Mat_Copy(&look, &turret->part->transformMatrix);
    if (turret->muzzleFlash != NULL)
        Mat_Copy(&look, &turret->muzzleFlash->transformMatrix);
}

// Stunned: aimed at random about the way it faces
void GT_RandMove(GUNTURRET *turret) {
    _VECTOR turn;
    turn.x = (float)Rand_FRandHalf(3.1415927f);
    turn.y = (float)Rand_FRandHalf(6.2831855f);
    turn.z = 0.0f;
    // Its w column and translation are uninitialised stack in the original
    _MATRIX mtx;
    memset(&mtx, 0, sizeof(mtx));
    RotMatrix(&turn, &mtx);
    _VECTOR at;
    ApplyMatrixLV(&mtx, &turret->facing, &at);
    Vec_Add2(&at, (_VECTOR *)&turret->aim.m[12], &at);
    GT_AimAt(turret, &at);
}

// A shot from the barrel's muzzle, with the flash and, every other shot, smoke
static void GT_Fire(obj_tag *obj, GUNTURRET *turret, obj_tag *barrel, Action_SFX sound, obj_tag *from, int player) {
    _VECTOR dir, at;
    Mat_GetDir(&dir, &barrel->transformMatrix);
    auxVec_AddMulR32((_VECTOR *)&barrel->transformMatrix.m[12], &dir, turret->muzzle, &at);
    Sound_Play3D(sound, &at, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Bullet_init(-1, from, barrel, &weapon_data[TURRET_WEAPON], &at, &dir, 0xffff);
    // dir, done with, is the smoke's turn
    dir.z = (float)Float_FRand(6.2831855f);
    if (player >= 0)
        PlrStat_LogShotFired(player);
    obj_tag *flash = turret->muzzleFlash;
    if (flash == NULL)
        return;
    flash->subState ^= 1;
    flash->curState = 2;
    flash->effectFlags &= ~FLAG_HIDDEN;
    if (flash->subState != 0)
        Gas_Init(obj->inCel, &at, &dir, NULL, 0.05f, 0.1f, TURRET_SMOKE, 7, 0x48403080, 1, 0x80);
}

// The barrel turned on by step, about its length
static void GT_SpinBarrel(GUNTURRET *turret, obj_tag *barrel, double step) {
    float spin = normaliseToPlusMinusPi(turret->spin);
    turret->spin = spin;
    // Its w column is uninitialised stack in the original
    _MATRIX turn;
    memset(&turn, 0, sizeof(turn));
    RotMatrixZ(spin, &turn);
    Vec_Zero((_VECTOR *)&turn.m[12]);
    turret->spin = (float)(step + turret->spin);
    Concat(&barrel->transformMatrix, &turn);
    Mat_Copy(&turn, &barrel->transformMatrix);
}

// The shots on a script's models take the turret's health; a taser's dart in one of them stuns it
static void GT_TakeDamage(GUNTURRET *turret, SCRIPTINFO *script, obj_tag *obj, bool onlyActive, bool *stunned) {
    for (ushort i = 0; script != NULL && i < script->numElements; i++) {
        if (onlyActive && obj->curState != GT_ACTIVE)
            break;
        obj_tag *part = Script_GetObj(script, i);
        if (part == NULL)
            continue;
        turret->health = (float)(turret->health - Collide_GetDamageNObjects(part->hitList, NULL, NULL, 0));
        if (part->objectType == OBJECTTYPE_BULLET) {
            short weapon = ((BU_tag *)part->extraObjectData)->wpnDef->weaponVariantNum;
            if (weapon == 0x4a || weapon == 0x4c)
                *stunned = true;
        }
    }
}

// Opens (a ceiling gun once its channels and a target allow), tracks and fires at its targets, goes back and closes
// (the minigun becomes its pickup again), shakes when stunned, smokes when destroyed (and in multiplayer the minigun
// blows up), or is aimed and fired by the player
// AUTOINJECT
void GT_Update(obj_tag *obj) {
    GUNTURRET *turret = (GUNTURRET *)obj->extraObjectData;
    obj_tag *barrel = turret->barrel;
    BLData *ownerData = (BLData *)turret->owner->extraObjectData;
    obj_tag *flash = turret->muzzleFlash;
    Script_Update(turret->body);
    Script_Update(turret->base);
    if (flash != NULL) {
        if (flash->curState != 0)
            flash->curState--;
        if (flash->curState == 0)
            flash->effectFlags |= FLAG_HIDDEN;
    }

    bool stunned = false;
    GT_TakeDamage(turret, turret->body, obj, false, &stunned);
    GT_TakeDamage(turret, turret->base, obj, true, &stunned);
    if (stunned)
        GT_Disable(obj);

    if ((turret->health < 0.0f || turret->ammo == 0) && obj->curState != GT_DESTROYED) {
        if (obj->curState == GT_CONTROLLED)
            GT_LoseControl(obj);
        obj->curState = GT_DESTROYED;
        turret->timer = 0.0f;
        _VECTOR up;
        Mat_GetUp(&up, &barrel->transformMatrix);
        Emitter_Create(TURRET_FIRE_EMITTER, (_VECTOR *)&barrel->transformMatrix.m[12], &up, 10.0f, 0, 0, 0, 0.0f);
    }

    switch ((short)obj->curState) {
    case GT_CLOSED:
        if (turret->kind == GT_KIND_CEILING) {
            if (turret->enableChannel != 0 && !switch_channels[turret->enableChannel])
                return;
            if (turret->disableChannel != 0 && switch_channels[turret->disableChannel])
                return;
            if (GT_Track(turret, obj) == NULL)
                return;
        }
        if (turret->base != NULL)
            turret->base->waitFrames = 0;
        if (turret->body != NULL)
            turret->body->waitFrames = 0;
        turret->timer = 0.0f;
        obj->curState = GT_OPENING;
        return;

    case GT_OPENING:
        if (turret->base->waitFrames != 0 && turret->body->waitFrames != 0) {
            Mat_Copy(&turret->body->maybeMatrix, &turret->aim);
            Mat_Copy(&turret->body->maybeMatrix, &turret->rest);
            obj->curState = GT_ACTIVE;
        }
        return;

    case GT_RETURNING: {
        _MATRIX mtx;
        Mat_Copy(&turret->rest, &mtx);
        GT_maybeInterpolateAimDir(&mtx, &turret->body->maybeMatrix);
        double timer = (double)REC_FRAME_RATE + turret->timer;
        turret->timer = (float)timer;
        if (timer > 1.2f) {
            turret->timer = 0.0f;
            if (turret->base != NULL)
                turret->base->waitFrames = 0;
            if (turret->body != NULL)
                turret->body->waitFrames = 0;
            obj->curState = GT_CLOSING;
        }
        Script_SetPosRot(turret->body, &mtx);
        return;
    }

    case GT_CLOSING:
        if (turret->base->waitFrames == 0 || turret->body->waitFrames == 0)
            return;
        if (turret->controlled)
            GT_LoseControl(obj);
        if (turret->kind == GT_KIND_MINIGUN) {
            Pickup_Create((_VECTOR *)&obj->transformMatrix.m[12], NULL, &obj->transformMatrix,
                          hashtable_hashcode_to_celglist(MINIGUN_PICKUP_MODEL), 0, MINIGUN_WEAPON, 1, 0, 0xf5, 1, 0, 0,
                          0);
            obj->flags |= 1;
        } else {
            obj->curState = GT_CLOSED;
        }
        return;

    case GT_ACTIVE: {
        turret->target = GT_Track(turret, obj);
        if (turret->target != NULL) {
            Music_Event(2, 1);
            turret->timer = 0.0f;
            GT_AimAt(turret, &turret->target->position);
            double fireTimer = (double)REC_FRAME_RATE + turret->fireTimer;
            turret->fireTimer = (float)fireTimer;
            if (fireTimer > TurretRefire) {
                turret->fireTimer = 0.0f;
                if (turret->ammo > 0)
                    turret->ammo--;
                GT_Fire(obj, turret, barrel, turret->kind == GT_KIND_CEILING ? CEILING_GUN_SOUND : MINIGUN_SOUND,
                        turret->owner != NULL ? turret->owner : obj, -1);
            }
            if (barrel == NULL)
                return;
            GT_SpinBarrel(turret, barrel, (double)REC_FRAME_RATE * BarrelSpin);
            return;
        }

        _MATRIX mtx;
        Mat_Copy(&turret->aim, &mtx);
        GT_maybeInterpolateAimDir(&mtx, &turret->body->maybeMatrix);
        Script_SetPosRot(turret->body, &mtx);
        if (turret->kind != GT_KIND_CEILING)
            return;
        if (turret->enableChannel != 0 && switch_channels[turret->enableChannel] &&
            (turret->disableChannel == 0 || !switch_channels[turret->disableChannel]))
            return;
        double timer = (double)REC_FRAME_RATE + turret->timer;
        turret->timer = (float)timer;
        if (timer > 0.5f) {
            obj->curState = GT_RETURNING;
            turret->timer = 0.0f;
        }
        return;
    }

    case GT_STUNNED:
        turret->timer = REC_FRAME_RATE + turret->timer;
        GT_RandMove(turret);
        if (turret->timer > 3.0f)
            obj->curState = obj->subState;
        return;

    case GT_DESTROYED:
        if (!(turret->timer < 10.0f)) {
            if (MPSettings.isMultiplayer && turret->kind != GT_KIND_CEILING) {
                obj->flags |= 1;
                Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, 5.0f, 5.0f, TURRET_EXPLOSION, 50.0f, 0xff, 0xff,
                               100, NULL, 0);
            }
            return;
        }
        turret->timer = REC_FRAME_RATE + turret->timer;
        if (GameState.NumFramesUnpaused % 10 == 0) {
            _VECTOR rot;
            rot.y = 0.0f;
            rot.x = 0.0f;
            rot.z = (float)Float_FRand(6.2831855f);
            Gas_Init(obj->inCel, &obj->position, &rot, NULL, 0.05f, 0.25f, TURRET_SMOKE, 7, 0x48403080, 1, 0x3c);
        }
        return;

    case GT_CONTROLLED: {
        short playerNum = ownerData->playerNum;
        _VECTOR steer;
        steer.y = (float)Negate(Input_Actionf(playerNum, ACTION_TURRET_AIM_X, 1) *
                                ((double)FRAME_RATE_MUL * 0.024543693f));
        steer.z = 0.0f;
        steer.x = (float)Negate(Input_Actionf(playerNum, ACTION_TURRET_AIM_Y, 1) *
                                ((double)FRAME_RATE_MUL * 0.024543693f));
        Vec_Add2(&turret->control, &steer, &turret->control);
        if (turret->control.x < -0.5f)
            turret->control.x = -0.5f;
        else if (turret->control.x > 0.5f)
            turret->control.x = 0.5f;
        float yaw;
        if (turret->control.y < -1.5707964f)
            yaw = -1.5707964f;
        else if (turret->control.y > 1.5707964f)
            yaw = 1.5707964f;
        else
            yaw = turret->control.y;
        turret->control.y = yaw;
        // Their w columns and translations are uninitialised stack in the original
        _MATRIX yawMtx, pitchMtx;
        memset(&yawMtx, 0, sizeof(yawMtx));
        memset(&pitchMtx, 0, sizeof(pitchMtx));
        RotMatrixY(yaw, &yawMtx);
        RotMatrixX(turret->control.x, &pitchMtx);
        Concat(&obj->transformMatrix, &yawMtx);
        Concat(&yawMtx, &pitchMtx);
        GT_maybeInterpolateAimDir(&pitchMtx, &turret->body->maybeMatrix);
        Script_SetPosRot(turret->body, &pitchMtx);
        turret->spinSpeed = turret->spinSpeed * 0.9f;
        turret->fireTimer = REC_FRAME_RATE + turret->fireTimer;

        if (Input_Action(ownerData->playerNum, ACTION_FIRE, 1)) {
            if (turret->fireTimer > TurretRefire) {
                turret->fireTimer = 0.0f;
                if (turret->ammo > 0)
                    turret->ammo--;
                GT_Fire(obj, turret, barrel, MINIGUN_SOUND, turret->owner, ownerData->playerNum);
            }
            turret->spinSpeed = REC_FRAME_RATE * BarrelSpin;
        }
        if (barrel != NULL && turret->spinSpeed != 0.0f)
            GT_SpinBarrel(turret, barrel, turret->spinSpeed);
        if (Input_Action(ownerData->playerNum, ACTION_RELOAD, 4))
            GT_LoseControl(obj);
        return;
    }
    }
}

// The player lets go: his camera and substate back, the Ronin's weapon put away, and facing the way he did
// AUTOINJECT
void GT_LoseControl(obj_tag *gameObj) {
    GUNTURRET *turret = (GUNTURRET *)gameObj->extraObjectData;
    BLData *blData = (BLData *)turret->owner->extraObjectData;
    blData->remoteControlDevice = NULL;
    Player_SetCamMode(blData, CamMode_Default);
    glb_viewer[blData->playerNum]->someCel = NULL;
    Player_ChangeSubState(turret->owner, blData->previousSubState);
    Player_Enable(turret->owner, NULL, 0);
    turret->owner->animState->switchingToWeaponId = RONIN_WEAPON;
    turret->owner->animState->currentWeaponId = RONIN_WEAPON;
    blData->weaponObject->curState = 0xc;
    AnimScriptAdd(blData->weaponObject, weapon_data[RONIN_WEAPON].animFireAlt);
    blData->weaponStats[RONIN_WEAPON].enabled = 0;
    Input_SetAction(blData->playerNum, ACTION_RELOAD, 1);
    turret->owner->transformFlags &= ~(TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX);
    _VECTOR facing;
    Mat_GetDir(&facing, &turret->owner->transformMatrix);
    vecutil_cartesian_to_spherical_acc(&turret->owner->rotation, facing.x, facing.y, facing.z);
    turret->owner->rotation.z = 0.0f;
    turret->owner->rotation.x = 0.0f;
    if (gameObj->curState == GT_CONTROLLED)
        gameObj->curState = GT_ACTIVE;
    turret->controlled = 0;
}

// The player takes an active turret over
// AUTOINJECT
bool GT_TakeControl(obj_tag *gameObj, obj_tag *player) {
    if (gameObj->curState != GT_ACTIVE)
        return false;
    GUNTURRET *turret = (GUNTURRET *)gameObj->extraObjectData;
    BLData *blData = (BLData *)player->extraObjectData;
    blData->previousSubState = Player_ChangeSubState(player, MovementType_Ronin);
    blData->remoteControlDevice = gameObj;
    Player_SetCamMode(blData, CamMode_Ronin);
    glb_viewer[blData->playerNum]->someCel = NULL;
    gameObj->curState = GT_CONTROLLED;
    Input_SetAction(gameObj->subState, ACTION_RELOAD, 1);
    Player_WeaponNone(player);
    Vec_Zero(&turret->control);
    turret->controlled = 1;
    return true;
}

// AUTOINJECT
void GT_Delete(obj_tag *obj) {
    GUNTURRET *turret = (GUNTURRET *)obj->extraObjectData;
    Script_Free(turret->body);
    Script_Free(turret->base);
}

// The taser or an explosion: an active turret is stunned for a while
// AUTOINJECT
void GT_Disable(obj_tag *obj) {
    if (obj->curState != GT_ACTIVE)
        return;
    obj->subState = obj->curState;
    obj->curState = GT_STUNNED;
    ((GUNTURRET *)obj->extraObjectData)->timer = 0.0f;
}

// Used: an open minigun goes back and closes
// AUTOINJECT
void GT_Activate(obj_tag *gameObj) {
    GUNTURRET *turret = (GUNTURRET *)gameObj->extraObjectData;
    short state = gameObj->curState;
    if (state >= GT_CLOSED && (state <= GT_CLOSING || state == GT_DESTROYED))
        return;
    if (turret->kind != GT_KIND_MINIGUN)
        return;
    turret->timer = 0.0f;
    gameObj->curState = GT_RETURNING;
}

// The player who dies at a turret lets go of it
// AUTOINJECT
void GT_PlayerHasDied(obj_tag *player) {
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType == OBJECTTYPE_GUNTURRET && obj->curState == GT_CONTROLLED &&
            ((GUNTURRET *)obj->extraObjectData)->owner == player) {
            GT_LoseControl(obj);
            return;
        }
    }
}
