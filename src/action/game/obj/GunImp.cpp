#include "GunImp.h"

#include <bit>
#include <stdint.h>
#include <string.h>

#include "Player.h"
#include "../mp/multiplayer.h"
#include "../../input.h"
#include "../../math/math.h"
#include "../../sound/Sound.h"
#include "../../engine/Camera.h"
#include "../../engine/viewFov.h"
#include "../../math/X87Ops.h"
#include "../weapon_stats.h"
#include "bullet.h"
#include "control.h"
#include "Effect.h"
#include "Explode.h"
#include "Gas.h"
#include "Light.h"

#pragma fp_contract(off)

// The multiplayer weapon sets' guns, then single player's
// XBE_GLOBAL(0x00163a30, 0x150) readonly relocate
GUNTYPE GunTypes[12] = {
    {{0.0f, 0.27f, 0.92f}, HT_Entity_RocketLauncherBarrel, 46, 180.0f, 268},
    {{0.0f, 0.23f, 1.65f}, HT_Entity_50calBarrel, 96, 10.0f, 266},
    {{0.17f, 0.23f, 0.94f}, HT_Entity_MinigunBarrels, 97, 5.0f, 1255},
    {{0.0f, 0.2f, 1.16f}, HT_Entity_FAMAS, 111, 180.0f, 1058},
    {{0.0f, 0.27f, 0.92f}, HT_Entity_RocketLauncherBarrel, 45, 180.0f, 268},
    {{0.0f, 0.21f, 0.51f}, HT_Entity_Grenadelauncher, 42, 120.0f, 1036},
    {{0.0f, 0.21f, 0.51f}, HT_Entity_Grenadelauncher, 43, 120.0f, 1036},
    {{0.0f, 0.33f, 1.61f}, HT_Entity_GunImpBarrel, 112, 1.0f, 1307},
    {{0.0f, 0.33f, 1.61f}, HT_Entity_GunImpBarrel, 112, 1.0f, 1307},
    {{0.0f, 0.33f, 1.61f}, HT_Entity_GunImpBarrel, 50, 180.0f, 1059},
    {{0.0f, 0.33f, 1.61f}, HT_Entity_GunImpBarrel, 50, 180.0f, 1059},
    {{0.0f, 0.0f, 1.71f}, HT_Entity_PowerStationGunImpBarrel, 103, 1.0f, 1307},
};

// The weapons with their own firing: a minigun's spin up and down, and two lasers
#define GUNIMP_MINIGUN 0x61
#define GUNIMP_LASER_A 0x67
#define GUNIMP_LASER_B 0x70

// Past this the gun overheats until it has cooled
#define GUNIMP_OVERHEAT 200.0f

// An emplacement and its gun: in multiplayer only when they are allowed, of the game's weapon set (any at random
// for set 10); single player's kind 11. Neither the gun nor its model is checked.
// AUTOINJECT
obj_tag* GunImp_Create(_VECTOR *pos, quaternion_tag *quat, celglist_tag *celgl) {
    short gunType;
    if (MPSettings.isMultiplayer) {
        if (!MPSettings.GunEmplacementsEnabled)
            return NULL;
        gunType = (short)MPSettings.weaponSet;
        if (gunType == 10)
            gunType = Rand_Rand(10);
    } else {
        gunType = 11;
    }

    // The w column is uninitialised stack in the original
    _MATRIX mtx;
    memset(&mtx, 0, sizeof(mtx));
    Quat_QuatToMat(quat, &mtx);
    Matrix_SetTrans(pos, &mtx);
    obj_tag *gunObj = Control_CreateObjEx(sizeof(GUNIMP), pos, NULL, &mtx, celgl, NULL, 1, 4, 1.0f, 0, 0xff, 0xff, 0xff);
    if (gunObj == NULL)
        return NULL;
    GUNIMP *gunImp = (GUNIMP *)gunObj->extraObjectData;
    gunObj->effectFlags |= EFFECT_MASKABLE;
    gunObj->objectType = OBJECTTYPE_GUNTURRET2;
    gunImp->userOffset.z = celgl->extentMin.z * 0.75f;
    gunImp->userOffset.y = 0.0f;
    gunImp->userOffset.x = 0.0f;
    gunImp->gunType = (uchar)gunType;
    gunImp->gun = Control_CreateObjEx(sizeof(GUNIMP), pos, NULL, &mtx,
                                      hashtable_hashcode_to_celglist(GunTypes[gunType].model), NULL, 1, 4, 1.0f, 0,
                                      0xff, 0xff, 0xff);
    gunImp->gun->effectFlags |= EFFECT_MASKABLE;
    Mat_Copy(&gunObj->transformMatrix, &gunImp->placement);
    gunImp->heat = 0.0f;
    gunImp->overheated = 0;
    return gunObj;
}

// The emplacement's field of view in single player: 30 degrees, its sights zoomed in to twice the game's own
// view. In multiplayer the player keeps the usual one.
static constexpr float EmplacementFov = (float)(M_PI / 6.0);
static_assert(std::bit_cast<uint32_t>(EmplacementFov) == 0x3f060a92, "not the original's pi/6");

// A player getting on an emplacement (not while crouching): the player is held still and puts the weapon away,
// and the camera moves to the gun (Player_PositionCamera, camera mode 14). From now on GunImp_Update places the
// player at the gun by writing the player's matrix, which the two transformFlags bits let it do. Reload gets off
// again (GunImp_Update); it is marked as held here, so that the press that got the player on does not count as
// one.
// AUTOINJECT
void GunImp_Activate(obj_tag *gunObj, obj_tag *player) {

    if (gunObj->curState != 0)
        return;

    GUNIMP *gunImp = (GUNIMP*)gunObj->extraObjectData;
    BLData *blData = (BLData*)player->extraObjectData;

    if (player->subState == MovementType_Crouch)
        return;

    blData->previousSubState = Player_ChangeSubState(player, MovementType_Emplacement);
    blData->remoteControlDevice = gunObj;
    Player_SetCamMode(blData, CamMode_GunImp);
    gunObj->curState = 1;
    gunObj->subState = blData->playerNum;
    Player_Disable(player, 1);
    gunImp->user = player;
    Input_SetAction(gunObj->subState, ACTION_RELOAD, 1);
    gunImp->user->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    Player_WeaponNone(player);

    if (MPSettings.isMultiplayer == 0)
        Camera_CalcViewAngles(gunObj->subState, EmplacementFov);
    else
        Camera_CalcViewAngles(gunObj->subState, ViewFov_PlayerFov());

}

// Getting off: the player gets his substate, camera, view and weapon back, and is left facing the way the gun
// was. Also run when the emplacement is reset with nobody on it, which only does the second half.
// AUTOINJECT
void GunImp_Deactivate(obj_tag *gunObj) {

    if (gunObj->curState == 1) {
        short playerNum = gunObj->subState;
        BLData *blData = glb_blokes[playerNum];

        blData->remoteControlDevice = NULL;
        Player_SetCamMode(blData, CamMode_Default);
        Player_ChangeSubState(glb_players[playerNum], blData->previousSubState);
        Player_Enable(glb_players[playerNum], NULL, 0);
        AnimState *anim = glb_players[playerNum]->animState;
        anim->switchingToWeaponId = anim->prevHeldWeaponId;
        Camera_CalcViewAngles(gunObj->subState, ViewFov_PlayerFov());
        blData->fallFrames = 0.0f;
        Vec_Zero(&blData->field_0x3c);
        Vec_Zero(&blData->movement);
    }

    Input_SetAction(gunObj->subState, ACTION_RELOAD, 1);

    GUNIMP *gunImp = (GUNIMP*)gunObj->extraObjectData;
    gunImp->user->transformFlags &= ~(TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX);

    _VECTOR facing;
    Mat_GetDir(&facing, &gunImp->user->transformMatrix);
    vecutil_cartesian_to_spherical_acc(&gunImp->user->rotation, facing.x, facing.y, facing.z);
    gunImp->user->rotation.z = 0.0f;
    gunImp->user->rotation.x = 0.0f;
    gunImp->user = NULL;
    gunObj->curState = 0;

    Sound_Stop(gunImp->firingSound);
    gunImp->firingSound = NULL;

}

// The player on an emplacement has died: off it
// AUTOINJECT
void GunImp_PlayerHasDied(obj_tag *player) {
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType == OBJECTTYPE_GUNTURRET2 && ((GUNIMP *)obj->extraObjectData)->user == player) {
            GunImp_Deactivate(obj);
            return;
        }
    }
}

// A muzzle flash or a laser's spark (spun, ten times the size) or beam, for a frame or so
static obj_tag *GunImp_Flash(_VECTOR *pos, HASHCODE gfx, short life, bool spun, cel_tag *cel) {
    obj_tag *flash = control_create_object(sizeof(Effect_tag), pos, NULL, NULL);
    Effect_tag *effect = (Effect_tag *)flash->extraObjectData;
    flash->objectType = OBJECTTYPE_EFFECT;
    effect->life = life;
    effect->fade = 0;
    hashtable_set_object_to_entity_gfx(flash, gfx);
    flash->tweakA = Rand_Rand(0x80) + 0x7f;
    if (spun) {
        flash->scale = 10.0f;
        flash->rotation.z = (float)Float_FRand(TWO_PI);
    }
    control_link_object_to_cel(flash, cel);
    return flash;
}

// The player on it aims (the stick turning the aim, the gun easing after it) and fires; it heats while firing and
// cools after, smoking while overheated; reload gets off. The player is held at the gun. Lasers hit what they point
// at, one of them blowing it up; the rest fire bullets. Nothing made for a shot is checked.
// AUTOINJECT
void GunImp_Update(obj_tag *gunObj) {
    GUNIMP *gunImp = (GUNIMP *)gunObj->extraObjectData;
    if (gunImp->heat > 0.0f) {
        gunImp->heat = gunImp->heat - FRAME_RATE_MUL;
    } else {
        gunImp->overheated = 0;
        gunImp->heat = 0.0f;
    }
    if (gunImp->refireTimer > 0.0f)
        gunImp->refireTimer = gunImp->refireTimer - FRAME_RATE_MUL;
    else
        gunImp->refireTimer = 0.0f;

    if ((short)gunObj->curState != 1)
        return;
    if (Input_Action(gunObj->subState, ACTION_RELOAD, 4)) {
        GunImp_Deactivate(gunObj);
        return;
    }

    gunImp->yawTarget = (float)(gunImp->yawTarget -
                                (double)Input_Actionf(gunObj->subState, ACTION_TURRET_AIM_X, 1) * FRAME_RATE_MUL * 0.015f);
    gunImp->unknown64 = 0.0f;
    gunImp->pitchTarget = (float)(gunImp->pitchTarget -
                                  (double)Input_Actionf(gunObj->subState, ACTION_TURRET_AIM_Y, 1) * FRAME_RATE_MUL * 0.015f);
    float pitchTarget;
    if (gunImp->pitchTarget < -HALF_PI)
        pitchTarget = -HALF_PI;
    else if (gunImp->pitchTarget > QUARTER_PI)
        pitchTarget = QUARTER_PI;
    else
        pitchTarget = gunImp->pitchTarget;
    gunImp->pitchTarget = pitchTarget;
    gunImp->pitch = moveTowardsValue(gunImp->pitch, pitchTarget, 0.15f);
    gunImp->yaw = moveTowardsValue(gunImp->yaw, gunImp->yawTarget, 0.15f);

    // The emplacement turns in yaw from where it was placed, the gun in pitch from there
    _MATRIX yawMtx, pitchMtx;
    Mat_IdentityT(&yawMtx);
    Mat_IdentityT(&pitchMtx);
    RotMatrixY(gunImp->yaw, &yawMtx);
    RotMatrixX(gunImp->pitch, &pitchMtx);
    Mat_Copy(&gunImp->placement, &gunObj->transformMatrix);
    Concat(&gunObj->transformMatrix, &yawMtx);
    Mat_CopyRot(&yawMtx, &gunObj->transformMatrix);
    Mat_Copy(&gunObj->transformMatrix, &gunImp->gun->transformMatrix);
    Concat(&gunImp->gun->transformMatrix, &pitchMtx);
    Mat_CopyRot(&pitchMtx, &gunImp->gun->transformMatrix);
    gunObj->transformFlags |= TRANSFORM_MOVED;
    gunImp->gun->transformFlags |= TRANSFORM_MOVED;

    Mat_Copy(&gunObj->transformMatrix, &gunImp->user->transformMatrix);
    _VECTOR offset;
    ApplyMatrixLV(&gunObj->transformMatrix, &gunImp->userOffset, &offset);
    Vec_Add2((_VECTOR *)&gunObj->transformMatrix.m[12], &offset, (_VECTOR *)&gunImp->user->transformMatrix.m[12]);
    Vec_Copy((_VECTOR *)&gunImp->user->transformMatrix.m[12], &gunImp->user->position);
    gunImp->user->transformFlags |= TRANSFORM_MOVED;
    ((BLData *)gunImp->user->extraObjectData)->pitchFromHorizontal = gunImp->pitch * -0.5f;

    _VECTOR local, muzzle;
    Vec_Copy(&GunTypes[gunImp->gunType].muzzle, &local);
    ApplyMatrixLV(&gunImp->gun->transformMatrix, &local, &muzzle);
    Vec_Add2((_VECTOR *)&gunImp->gun->transformMatrix.m[12], &muzzle, &muzzle);

    if (gunImp->overheated && GameState.NumFramesUnpaused % 6 == 0) {
        Control_BuildWorldSph(gunImp->gun);
        _VECTOR dir;
        Mat_GetDir(&dir, &gunImp->gun->transformMatrix);
        _VECTOR at;
        auxVec_AddMulR32(&muzzle, &dir, (float)-(Float_FRand(1.0f) * gunImp->gun->radius), &at);
        _VECTOR rot;
        rot.z = 0.0f;
        rot.y = 0.0f;
        rot.x = 0.0f;
        rot.z = (float)Float_FRand(TWO_PI);
        Gas_Init(gunObj->inCel, &at, &rot, NULL, 0.05f, 0.25f, HT_Entity_smoke_1_dust, 7, 0x484030e4, 1, 0x80);
    }

    if (Input_Action(gunObj->subState, ACTION_FIRE, 1) && gunImp->refireTimer <= 0.0f && !gunImp->overheated) {
        int sound;
        GUNTYPE *firing = &GunTypes[gunImp->gunType];
        if (firing->weapon == GUNIMP_MINIGUN) {
            sound = firing->sound;
            if (gunImp->firingSound == NULL) {
                Sound_Play3D(SFX_GADGET_RONIN_WHIR_START, &gunObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                gunImp->firingSound = Sound_Play3D(SFX_GADGET_RONIN_WHIR_LOOP, &gunObj->position, 100.0f, -1.0f, -1.0f,
                                                   0, 0, 0);
            }
            gunImp->heat = gunImp->heat + 8.0f;
            if (gunImp->heat > GUNIMP_OVERHEAT)
                gunImp->overheated = 1;
        } else if (firing->weapon == GUNIMP_LASER_A || firing->weapon == GUNIMP_LASER_B) {
            sound = 0xffff;
            if (gunImp->firingSound == NULL)
                gunImp->firingSound = Sound_Play3D((Action_SFX)firing->sound, &gunObj->position, 100.0f, -1.0f, -1.0f,
                                                   0, 0, 0);
            gunImp->heat = gunImp->heat + 2.0f;
            if (gunImp->heat > GUNIMP_OVERHEAT)
                gunImp->overheated = 1;
        } else {
            sound = firing->sound;
        }

        gunImp->refireTimer = GunTypes[gunImp->gunType].refire;
        _VECTOR dir, from;
        Mat_GetDir(&dir, &gunImp->gun->transformMatrix);
        Vec_Copy(&muzzle, &from);
        short playerNum = gunObj->subState;
        Bullet_init(playerNum, glb_players[playerNum], gunImp->gun, &weapon_data[GunTypes[gunImp->gunType].weapon],
                    &from, &dir, 0xffff);
        if (sound != 0xffff)
            Sound_Play3D((Action_SFX)sound, &from, 100.0f, -1.0f, -1.0f, 0, 0, 0);

        int weapon = GunTypes[gunImp->gunType].weapon;
        if (weapon != GUNIMP_LASER_A && weapon != GUNIMP_LASER_B) {
            GunImp_Flash(&muzzle, HT_Entity_Generic_muzzle, 1, true, gunObj->inCel);
            Light_Create(&from, 0xff, 0x60, 0, 9.0f, 0, 0, 1, (float)(Float_FRand(1.0f) + 1.5f), 0, 0, 0, 0xffff);
            return;
        }

        GunImp_Flash(&from, HT_Entity_taser_muzzle, 1, true, gunObj->inCel);
        Light_Create(&from, 0, 0xff, 0xff, 9.0f, 0, 0, 1, (float)(Float_FRand(1.0f) + 1.5f), 0, 0, 0, 0xffff);
        _VECTOR end;
        auxVec_AddMulR32(&from, &dir, 1000.0f, &end);
        // Uninitialised in the original; Collide_RayIntersect sets it
        HITDATA_tag *hits = NULL;
        if (Collide_RayIntersect(&from, &end, gunObj->inCel, gunObj, NULL, &hits, 0, 8, 0))
            Vec_Copy(&hits->hitPosition, &end);
        obj_tag *beam = GunImp_Flash(&from, HT_Entity_lazer_Big, 0, false, gunObj->inCel);
        beam->parent = gunObj;
        PositionBeam(beam, &from, &end);
        if (hits == NULL) {
            // Not used after
            Vec_Subtract(&end, &dir, &end);
        } else {
            // Nor is this
            Vec_Add2(&end, &hits->surfaceNormal.normal, &end);
            auxVec_AddMulR32(&hits->hitPosition, &dir, -0.2f, &from);
            obj_tag *spark = GunImp_Flash(&from, HT_Entity_taser_muzzle, 0, true, gunObj->inCel);
            if (GunTypes[gunImp->gunType].weapon == GUNIMP_LASER_A) {
                Explode_Propagate(&hits->hitPosition, 4.0f, spark->inCel, 5.0f, gunObj);
                Collide_FreeHitList(&hits);
                return;
            }
        }
        Collide_FreeHitList(&hits);
        return;
    }

    if (Input_Action(gunObj->subState, ACTION_FIRE, 1) && !gunImp->overheated)
        return;
    if (gunImp->firingSound != NULL) {
        if (GunTypes[gunImp->gunType].weapon == GUNIMP_MINIGUN)
            Sound_Play3D(SFX_GADGET_RONIN_WHIR_END, &gunObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Sound_Stop(gunImp->firingSound);
        gunImp->firingSound = NULL;
    }
}
