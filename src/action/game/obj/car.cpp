// Multiplayer, remote-controlled tank / helicopter
#include "car.h"

#include "build.h"
#include "bullet.h"
#include "control.h"
#include "Explode.h"
#include "object.h"
#include "Player.h"
#include "../view.h"
#include "../weapon_stats.h"
#include "../mp/multiplayer.h"
#include "../../game.h"
#include "../../input.h"
#include "../../engine/Camera.h"
#include "../../engine/psiInput.h"
#include "../../engine/viewFov.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../../sound/Sound.h"
#include "../../util/hashtable.h"
#include "../../util/Random.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

#define MAX_TANKS 8 // Locations defined on the map

#define CAR_GUN_WEAPON      96   // the gun of both (not named in the WeaponData sheet)
#define TANK_SHELL_WEAPON   42   // Militek MGL, Grenade
#define HELI_MISSILE_WEAPON 46   // AT-600 Scorpion, Heatseeker

#define PI     3.1415927f
#define TWO_PI 6.2831855f

// XBE_GLOBAL(0x001dc980, 0x20)
static obj_tag* Tanks[MAX_TANKS]; // the original Car_Init clears all 0x20 bytes
// XBE_GLOBAL(0x001dc798, 0x2)
static uint16_t NumTanks;
// XBE_GLOBAL(0x001dc7a0, 0x1e0)
static _MATRIX TankSpawns[8];

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

// AUTOINJECT
void Car_Reset(void) {

    for(int i = 0; i < NumTanks; i++) {

        obj_tag *gameObj = Tanks[i];
        if(gameObj == NULL)
            continue;

        CAR_INFO *carObj = (CAR_INFO*)gameObj->extraObjectData;

        gameObj->curState = CAR_IDLE;
        gameObj->subState = 0;
        carObj->playerController = NULL;
        carObj->damageAmt = 0;
        Vec_Zero(&carObj->aim);
        Mat_Copy(&TankSpawns[i], &gameObj->transformMatrix);
        build_LinkToRoom(gameObj, 0, glb_world);
    }

}


// Only used in here, no need to inject once we've reimplemented Car_Create
// + we cannot inject, LTCG has used special registers
// UNINJECTABLE - custom calling convention
void Car_InitBits(CAR_INFO *tankInfo, obj_tag *baseObj) {

    HASHCODE turretHashcode;
    HASHCODE barrelHashcode;

    if(tankInfo->isHeli == 0) {
        turretHashcode = GFX_RCCar_Turret;
        barrelHashcode = GFX_RCCar_Barrel;
    } else {
        turretHashcode = GFX_LittleNellie_Blades;
        barrelHashcode = GFX_LittleNellie_Prop;
    }

    tankInfo->turretGeom = Control_CreateObjEx(0, Mat_Position(baseObj->transformMatrix), NULL, NULL, hashtable_hashcode_to_celglist(turretHashcode), baseObj, 0, 4, 1.0, 0x00, 0xff, 0xff, 0xff);
    tankInfo->barrelGeom = Control_CreateObjEx(0, Mat_Position(baseObj->transformMatrix), NULL, NULL, hashtable_hashcode_to_celglist(barrelHashcode), baseObj, 0, 4, 1.0, 0x20, 0xff, 0xff, 0xff);

    // Meaning currently unknown - render mode?
    tankInfo->turretGeom->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;
    tankInfo->barrelGeom->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;

    Mat_Copy(&baseObj->transformMatrix, &tankInfo->turretGeom->transformMatrix);
    Vec_Copy(Mat_Position(baseObj->transformMatrix), Mat_Position(tankInfo->turretGeom->transformMatrix)); // This is pointless, it's the same matrix, but the game code does this?


}

// AUTOINJECT
void Car_PlayerHasDied(obj_tag *player) {

    obj_tag* objAt = control_first_object();

    while(objAt != NULL) {

        // TODO: Tidier to implement with Control_ReturnNextObjectOfType and just do the playerController check here

        CAR_INFO* car = (CAR_INFO*)(objAt->extraObjectData);

        if(objAt->objectType == OBJECTTYPE_CAR && car->playerController == player) {
            // We've found the car which is being remote controlled by the player
            Car_Deactivate(objAt);
            break;
        }

        objAt = objAt->nextObject;
    }

}

// AUTOINJECT
void __cdecl Car_Activate(obj_tag* carObj, obj_tag* playerObj) {

    if(carObj->curState != CAR_IDLE)
        return;

    BLData* playerData = (BLData*)(playerObj->extraObjectData);
    CAR_INFO* carData = (CAR_INFO*)(carObj->extraObjectData);

    playerData->previousSubState = Player_ChangeSubState(playerObj, 0x0b);

    // If it's a helicopter, control its body, otherwise control the barrel
    playerData->remoteControlDevice = carData->isHeli ? carObj : carData->barrelGeom;

    Player_SetCamMode(playerData, 0xd);
    carObj->curState = CAR_TAKEN;
    carObj->subState = playerData->playerNum;
    Player_Disable(playerObj, 1);
    carData->playerController = playerObj;
    carData->tankMachinegunTemperature = 0.0f;
    carData->machineGunOverheated = 0;

    if(carData->isHeli) {
        carData->soundHandle = Sound_Play3D(SFX_VEH_BELL_HELICOPTER_LOOP,&(carObj->position),25.0,-1.0,-1.0,0,0,0);
        carData->mainAmmo = 4;
    } else {
        carData->soundHandle = Sound_Play3D(SFX_VEH_MP_TANK_ENGINE_LOOP,&(carObj->position),100.0,-1.0,-1.0,0,0,0);
        carData->mainAmmo = 10;
    }

    Player_WeaponNone(playerObj);

    Camera_CalcViewAngles(carObj->subState, ViewFov_PlayerFov());

}

// AUTOINJECT
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level) {

    if(MPSettings.isMultiplayer && !MPSettings.MiniVehiclesEnabled)
        return NULL;

    if(GameState.CurrentLevelHashcode == HT_Level_Ravine)
        return NULL;

    if(NumTanks >= MAX_TANKS)
        return NULL;

    obj_tag* baseObj = Control_CreateObjEx(sizeof(CAR_INFO),pos,rot,NULL,celgl,NULL,'\x01',0x44,1.0,0,0xff,0xff,0xff);

    if(baseObj == NULL)
        return NULL;

    CAR_INFO *tankInfo = (CAR_INFO*)baseObj->extraObjectData;

    baseObj->objectType = OBJECTTYPE_CAR;
    baseObj->maybeCollision = &tankInfo->hitTest;
    Quat_MatToQuat(&tankInfo->orientation, &baseObj->transformMatrix);

    // Any value but 1 and 3 gives the helicopter, 0 included
    switch(MPSettings.MiniVehiclesEnabled) {
        case 1: // Tanks only
            tankInfo->isHeli = 0;
            break;
        case 3: // Both
            tankInfo->isHeli = (Rand_Rand(100) <= 50) ? 1 : 0;
            break;
        default: // 2, helicopters only
            tankInfo->isHeli = 1;
            break;
    }

    if(tankInfo->isHeli) {
        // Flying above the ground at the given location0
        baseObj->transformMatrix.m[0xd] += 1.5f;
        baseObj->position.y += 1.5f;
        hashtable_set_object_to_entity_gfx(baseObj, GFX_LittleNellie_Body);
    }

    Car_InitBits(tankInfo, baseObj);

    View_SetDrawInAllViews(baseObj);
    View_SetDrawInAllViews(tankInfo->turretGeom);
    View_SetDrawInAllViews(tankInfo->barrelGeom);
    View_SetDrawInAllViews(tankInfo->bodyGeom);

    Vec_Zero(&tankInfo->aim);

    baseObj->effectFlags |= ObjectEffectFlags::FLAG_UNKNOWN_40;

    // Take a copy of our initial position and orientation, so that we can respawn the tank at the right location
    // Could just as easily been stored in the CAR_INFO struct, but this is how the original code does it so we'll stick with that
    Mat_Copy(&baseObj->transformMatrix, &TankSpawns[NumTanks]);

    tankInfo->tankNum = NumTanks;
    Tanks[NumTanks] = baseObj;
    NumTanks++;

    tankInfo->machineGunOverheated = 0;
    tankInfo->tankMachinegunTemperature = 0.0f;


    return baseObj;
}

// AUTOINJECT
void Car_Init(void) {

    for(int i = 0; i < MAX_TANKS; i++) {
        Tanks[i] = NULL;
    }
    NumTanks = 0;

}


// AUTOINJECT
void Car_Deactivate(obj_tag *carObj) {

    if(carObj->curState == CAR_DESTROYED)
        return;

    if(carObj->curState == CAR_DRIVEN) {

        BLData* playerData = glb_blokes[carObj->subState];
        obj_tag* playerObj = glb_players[carObj->subState];

        playerData->remoteControlDevice = NULL;
        Player_SetCamMode(playerData, 0);
        Player_ChangeSubState(playerObj, playerData->previousSubState);
        Player_Enable(playerObj, NULL, 0);
        playerObj->animState->switchingToWeaponId = playerObj->animState->prevHeldWeaponId; // Bring the weapon up after the car has been deactivated

    }

    carObj->curState = CAR_DESTROYED;

    CAR_INFO* carInfo = (CAR_INFO*)(carObj->extraObjectData);

    carInfo->lastController = carInfo->playerController;
    carInfo->playerController = NULL;
    carInfo->damageAmt = 0;

    if (carInfo->soundHandle != NULL) {
      Sound_Stop(carInfo->soundHandle);
      carInfo->soundHandle = NULL;
    }

    Explode_Create(carObj, &carObj->position, &CONST_UP_VECTOR, 10.0, 10.0, (HASHCODE)0x6000052, 50.0, 0xff, 0xff, 100, carObj, 6);
    Vec_Zero(&carInfo->aim);
    Vec_Zero(&carInfo->fall);
    Vec_Zero(&carInfo->velocity);
    Mat_Copy(&TankSpawns[carInfo->tankNum], &carObj->transformMatrix);
    build_LinkToRoom(carObj, 0, glb_world);


    carObj->subState = 1800; // Respawn timer

}

// Eased rate of the way to target, and stopped once nearly still with the stick let go
static float Car_Ease(float speed, float target, float rate) {
    float eased = moveTowardsValue(speed, target, rate);
    if (target <= 0.0002f && target >= -0.0002f && AbsFchs((double)eased) < 0.01f)
        return 0.0f;
    return eased;
}

static void Car_LimitSpeed(float *speed, double least, double most) {
    if (least > *speed)
        *speed = (float)least;
    else if (most < *speed)
        *speed = (float)most;
}

static float Car_LimitFall(float fall) {
    if (fall < -45.0f)
        return -45.0f;
    if (fall > 45.0f)
        return 45.0f;
    return fall;
}

// Each shot heats the gun by 10; past 100 it has overheated
static void Car_HeatGun(CAR_INFO *car) {
    double heat = (double)car->tankMachinegunTemperature + 10.0f;
    car->tankMachinegunTemperature = (float)heat;
    if (heat > 100.0f)
        car->machineGunOverheated = 1;
}

// The tank's turret and barrel, turned by the player driving it, and its gun and shell fired from the barrel's end.
// The barrel dips less far the further the turret is turned from straight ahead.
void Car_SetBits(CAR_INFO *car, obj_tag *obj) {
    obj_tag *barrel = car->barrelGeom;
    obj_tag *turret = car->turretGeom;
    bool fireGun = false;
    bool fireShell = false;
    if (obj->curState == CAR_DRIVEN) {
        if (Input_Action(obj->subState, ACTION_FIRE, 1))
            fireGun = true;
        if (Input_Action(obj->subState, ACTION_ALTFIRE, 1))
            fireShell = true;
        car->aim.y = (float)(Extended(car->aim.y) - Extended(Input_Actionf(obj->subState, ACTION_AIM_L_R, 1)) *
                                                        Extended(FRAME_RATE_MUL) * Extended(0.05f));
        car->aim.x = (float)(Extended(car->aim.x) - Extended(Input_Actionf(obj->subState, ACTION_AIM_U_D, 1)) *
                                                        Extended(FRAME_RATE_MUL) * Extended(0.05f));
        car->aim.z = 0.0f;
        double heading = normaliseToPlusMinusPi(car->aim.y);
        car->aim.y = (float)heading;
        Extended highest =
            (Extended(1.0f) - AbsFchs(Extended(heading) * Extended(1.2732395f))) * Extended(0.12900001f) + Extended(0.08f);
        if (car->aim.x < -0.705f)
            car->aim.x = -0.705f;
        else if (highest < Extended(car->aim.x))
            car->aim.x = (float)highest;
    }

    // Their w columns are uninitialised stack in the original
    _MATRIX turretTurn, barrelTurn;
    memset(&turretTurn, 0, sizeof(turretTurn));
    memset(&barrelTurn, 0, sizeof(barrelTurn));
    RotMatrixY(car->aim.y, &turretTurn);
    RotMatrixXY(car->aim.x, car->aim.y, &barrelTurn);
    Concat(&obj->transformMatrix, &turretTurn);
    Concat(&obj->transformMatrix, &barrelTurn);
    Mat_CopyRot(&turretTurn, &turret->transformMatrix);
    Mat_CopyRot(&barrelTurn, &barrel->transformMatrix);
    Vec_Copy(Mat_Position(obj->transformMatrix), Mat_Position(turret->transformMatrix));
    turret->transformFlags |= TRANSFORM_MOVED;
    barrel->transformFlags |= TRANSFORM_MOVED;

    // The barrel at the turret's front, a little up
    _VECTOR up, dir;
    Mat_GetUp(&up, &turret->transformMatrix);
    Mat_GetDir(&dir, &turret->transformMatrix);
    auxVec_AddMulR32(Mat_Position(turret->transformMatrix), &dir, turret->objGraphics->extentMax.z * 0.9f,
                     Mat_Position(barrel->transformMatrix));
    auxVec_AddMulR32(Mat_Position(barrel->transformMatrix), &up, 0.162f, Mat_Position(barrel->transformMatrix));

    if (car->gunWait != 0.0f)
        car->gunWait -= FRAME_RATE_MUL;
    if (car->mainWait != 0.0f)
        car->mainWait -= FRAME_RATE_MUL;

    _VECTOR from;
    if (fireGun && car->gunWait <= 0.0f && !car->machineGunOverheated) {
        Mat_GetDir(&dir, &barrel->transformMatrix);
        auxVec_AddMulR32(Mat_Position(barrel->transformMatrix), &dir, barrel->radius, &from);
        Sound_Play3D(SFX_WEAPON_MINI_GUN_FIRE, &from, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Bullet_init(obj->subState, glb_players[(short)obj->subState], turret, &weapon_data[CAR_GUN_WEAPON], &from, &dir,
                    0xffff);
        car->gunWait = 6.0f;
        Car_HeatGun(car);
    }
    if (fireShell && car->mainWait <= 0.0f && car->mainAmmo != 0) {
        Mat_GetDir(&dir, &barrel->transformMatrix);
        auxVec_AddMulR32(Mat_Position(barrel->transformMatrix), &dir, barrel->radius, &from);
        Sound_Play3D(SFX_WEAPON_OICW_GRENADE_SHOT_01, &from, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Bullet_init(obj->subState, glb_players[(short)obj->subState], turret, &weapon_data[TANK_SHELL_WEAPON], &from,
                    &dir, 0xffff);
        car->mainAmmo--;
        car->mainWait = 120.0f;
    }
}

// Carried along by what it touches, unless that is a drone, a player, a bullet, a dead drone or a rigid body. Not
// named on PS2; the name is ours.
bool Car_InheritVelocity(obj_tag *car, obj_tag *by) {
    if (by == NULL)
        return false;
    switch ((uchar)by->objectType) {
        case OBJECTTYPE_DRONE:
        case OBJECTTYPE_PLAYER:
        case OBJECTTYPE_BULLET:
        case OBJECTTYPE_DEAD_DRONE:
        case OBJECTTYPE_RIGIDBODY:
            return false;
    }
    Control_InheritVelocity(car, by, 3.0f);
    return true;
}

// The helicopter's update while it is driven: the sticks turn and roll it and move it about at its height, its nose
// kept from pointing more than three quarters straight up or down; its gun and missiles fire from each side in turn
static void Car_FlyHeli(obj_tag *obj, CAR_INFO *car) {
    _VECTOR steer;
    Vec_Zero(&steer);
    steer.y = (float)(Extended(Input_Actionf(obj->subState, ACTION_AIM_L_R, 1)) * Extended(FRAME_RATE_MUL) *
                      Extended(0.05));
    steer.x = (float)(-Extended(Input_Actionf(obj->subState, ACTION_AIM_U_D, 1)) * Extended(FRAME_RATE_MUL) *
                      Extended(0.05));
    float drive = (float)((double)Input_Actionf(obj->subState, ACTION_WALK_F_B, 1) * FRAME_RATE_MUL * 2.0);
    car->velocity.z = Car_Ease(car->velocity.z, drive, 0.15f);
    Car_LimitSpeed(&car->velocity.z, FRAME_RATE_MUL * -0.125, FRAME_RATE_MUL * 0.125);
    float strafe = (float)((double)Input_Actionf(obj->subState, ACTION_WALK_L_R, 1) * FRAME_RATE_MUL * 2.0);
    car->velocity.x = Car_Ease(car->velocity.x, strafe, 0.15f);
    Car_LimitSpeed(&car->velocity.x, FRAME_RATE_MUL * -0.1, FRAME_RATE_MUL * 0.1);

    _VECTOR dir;
    Mat_GetDir(&dir, &obj->transformMatrix);
    if ((dir.y < -0.75f && steer.x > 0.0f) || (dir.y > 0.75f && steer.x < 0.0f))
        steer.x = 0.0f;
    _VECTOR at, angles;
    Mat_GetPosition(&at, &obj->transformMatrix);
    vecutil_cartesian_to_spherical_acc(&angles, dir.x, dir.y, dir.z);
    angles.x = (float)(Negate(angles.x) + steer.x);
    angles.y = angles.y - steer.y;
    angles.z = 0.0f;
    _MATRIX heading, roll;
    RotMatrixZYX(&angles, &heading);
    angles.z = steer.y + obj->rotation.z;
    angles.y = 0.0f;
    angles.x = 0.0f;
    RotMatrix(&angles, &roll);
    MulMatrix0(&heading, &roll, &obj->transformMatrix);
    Matrix_SetTrans(&at, &obj->transformMatrix);
    obj->rotation.z = angles.z * 0.9f;
    _VECTOR move;
    ApplyMatrixLV(&obj->transformMatrix, &car->velocity, &move);
    Vec_Add2(Mat_Position(obj->transformMatrix), &move, Mat_Position(obj->transformMatrix));

    if (car->gunWait != 0.0f)
        car->gunWait -= FRAME_RATE_MUL;
    if (car->mainWait != 0.0f)
        car->mainWait -= FRAME_RATE_MUL;

    _VECTOR from;
    if (Input_Action(obj->subState, ACTION_FIRE, 1) && car->gunWait <= 0.0f && !car->machineGunOverheated) {
        _VECTOR gun = {0.1f, -0.05f, 0.1f};
        if (car->fireSide & HELI_GUN_LEFT) {
            gun.x = -0.1f;
            car->fireSide &= ~HELI_GUN_LEFT;
        } else {
            car->fireSide |= HELI_GUN_LEFT;
        }
        ApplyMatrixLV(&obj->transformMatrix, &gun, &from);
        Vec_Add2(Mat_Position(obj->transformMatrix), &from, &from);
        Sound_Play3D(SFX_VEH_HELICOPTER_CHAINGUN_SHOT, &from, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Bullet_init(obj->subState, glb_players[(short)obj->subState], obj, &weapon_data[CAR_GUN_WEAPON], &from, &dir,
                    0xffff);
        car->gunWait = 6.0f;
        Car_HeatGun(car);
    }
    if (Input_Action(obj->subState, ACTION_ALTFIRE, 1) && car->mainWait <= 0.0f && car->mainAmmo != 0) {
        _VECTOR launcher = {0.15f, -0.1f, 0.1f};
        if (car->fireSide & HELI_MISSILE_LEFT) {
            launcher.x = -0.15f;
            car->fireSide &= ~HELI_MISSILE_LEFT;
        } else {
            car->fireSide |= HELI_MISSILE_LEFT;
        }
        ApplyMatrixLV(&obj->transformMatrix, &launcher, &from);
        Vec_Add2(Mat_Position(obj->transformMatrix), &from, &from);
        Sound_Play3D(SFX_WEAPON_ROCKET_LAUNCHER_SHOOT, &from, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Bullet_init(obj->subState, glb_players[(short)obj->subState], obj, &weapon_data[HELI_MISSILE_WEAPON], &from,
                    &dir, 0xffff);
        car->mainAmmo--;
        car->mainWait = 120.0f;
    }
}

// The tank's update: driven, the stick drives it (only while on a floor) and turns it; it eases upright towards
// the floor it last stood on, and falls
static void Car_DriveTank(obj_tag *obj, CAR_INFO *car) {
    float turn = 0.0f;
    float drive = 0.0f;
    if (obj->curState == CAR_DRIVEN) {
        if (car->onGround)
            drive = (float)((double)Input_Actionf(obj->subState, ACTION_WALK_F_B, 1) * FRAME_RATE_MUL * 2.0);
        turn = (float)(Extended(Input_Actionf(obj->subState, ACTION_WALK_L_R, 1)) * Extended(FRAME_RATE_MUL) *
                       Extended(0.05f));
    }
    if (car->onGround) {
        car->velocity.z = Car_Ease(car->velocity.z, drive, 0.15f);
        Car_LimitSpeed(&car->velocity.z, REC_FRAME_RATE * -5.0, REC_FRAME_RATE * 5.0);
        car->velocity.y = Car_Ease(car->velocity.y, 0.0f, 0.3f);
        Car_LimitSpeed(&car->velocity.y, REC_FRAME_RATE * -5.0, REC_FRAME_RATE * 5.0);
        car->velocity.x = Car_Ease(car->velocity.x, 0.0f, 0.3f);
        Car_LimitSpeed(&car->velocity.x, REC_FRAME_RATE * -5.0, REC_FRAME_RATE * 5.0);
    }

    _VECTOR move, dir, up;
    quaternion_tag now, eased;
    ApplyMatrixLV(&obj->transformMatrix, &car->velocity, &move);
    Vec_Add2(Mat_Position(obj->transformMatrix), &move, Mat_Position(obj->transformMatrix));
    Mat_GetDir(&dir, &obj->transformMatrix);
    Quat_MatToQuat(&now, &obj->transformMatrix);
    Quat_Slerp_Acc(REC_FRAME_RATE * 3.0f, (float *)&now, (float *)&car->orientation, (float *)&eased);
    Quat_QuatToMat(&eased, &obj->transformMatrix);
    Mat_GetUp(&up, &obj->transformMatrix);
    Mat_Align2Up(obj->transformMatrix.m, &up.x, &dir.x);

    car->turnSpeed = Car_Ease(car->turnSpeed, turn, 0.5f);
    Car_LimitSpeed(&car->turnSpeed, REC_FRAME_RATE * -PI, REC_FRAME_RATE * PI);
    if (car->turnSpeed != 0.0f) {
        // Its w column is uninitialised stack in the original
        _MATRIX spin;
        memset(&spin, 0, sizeof(spin));
        RotMatrixY(car->turnSpeed, &spin);
        Concat(&obj->transformMatrix, &spin);
        Mat_CopyRot(&spin, &obj->transformMatrix);
    }

    auxVec_AddMulR32(&car->fall, &GRAVITY_VECTOR, REC_FRAME_RATE, &car->fall);
    car->fall.x = Car_LimitFall(car->fall.x);
    car->fall.y = Car_LimitFall(car->fall.y);
    car->fall.z = Car_LimitFall(car->fall.z);
    auxVec_AddMulR32(Mat_Position(obj->transformMatrix), &car->fall, REC_FRAME_RATE, Mat_Position(obj->transformMatrix));
}

// Destroyed, it is hidden until its time is up. Otherwise its gun cools; the driver can get out; the tank or the
// helicopter moves; the helicopter bobs and spins its blades and propeller; and its collision sphere follows it.
// AUTOINJECT
void Car_Update(obj_tag *obj) {
    CAR_INFO *car = (CAR_INFO *)obj->extraObjectData;
    if (obj->curState == CAR_DESTROYED) {
        car->hitTest.type = 0;
        car->damageAmt = 0.0f;
        View_SetDrawInNoViews(obj);
        View_SetDrawInNoViews(car->turretGeom);
        View_SetDrawInNoViews(car->barrelGeom);
        View_SetDrawInNoViews(car->bodyGeom);
        obj->subState = (ushort)Ftol((short)obj->subState - (double)FRAME_RATE_MUL);
        if ((short)obj->subState <= 0)
            obj->curState = CAR_IDLE;
        return;
    }

    if (car->tankMachinegunTemperature > 0.0f) {
        car->tankMachinegunTemperature -= FRAME_RATE_MUL;
    } else {
        car->machineGunOverheated = 0;
        car->tankMachinegunTemperature = 0.0f;
    }
    View_SetDrawInAllViews(obj);
    View_SetDrawInAllViews(car->turretGeom);
    View_SetDrawInAllViews(car->barrelGeom);
    View_SetDrawInAllViews(car->bodyGeom);
    if (obj->curState == CAR_STOPPED)
        return;
    if (obj->curState == CAR_DRIVEN && Input_Action(obj->subState, ACTION_RELOAD, 4)) {
        Car_Deactivate(obj);
        return;
    }
    if (car->soundHandle != NULL)
        Sound_SetPosition(car->soundHandle, Mat_Position(obj->transformMatrix));

    if (!car->isHeli) {
        Car_DriveTank(obj, car);
    } else {
        if (obj->curState == CAR_DRIVEN)
            Car_FlyHeli(obj, car);
        obj_tag *blades = car->turretGeom;
        obj_tag *prop = car->barrelGeom;
        double frames = (double)(uint)GameState.NumFramesUnpaused;
        obj->transformMatrix.m[13] = (float)(FCos(fmod(frames * 0.033333335f, (double)TWO_PI)) * Extended(0.001f) +
                                             Extended(obj->transformMatrix.m[13]));
        float spin = (float)fmod((double)(uint)GameState.NumFramesUnpaused * 0.43478262f, (double)TWO_PI);
        // Its w column is uninitialised stack in the original
        _MATRIX turn;
        memset(&turn, 0, sizeof(turn));
        RotMatrixY(spin, &turn);
        Concat(&obj->transformMatrix, &turn);
        Mat_CopyRot(&turn, &blades->transformMatrix);
        Matrix_SetTrans(Mat_Position(obj->transformMatrix), &blades->transformMatrix);
        RotMatrixZ(spin, &turn);
        Concat(&obj->transformMatrix, &turn);
        Mat_CopyRot(&turn, &prop->transformMatrix);
        Matrix_SetTrans(Mat_Position(obj->transformMatrix), &prop->transformMatrix);
        blades->transformFlags |= TRANSFORM_MOVED;
        prop->transformFlags |= TRANSFORM_MOVED;
    }

    // Its sphere: the tank's sits on its base, the helicopter's half the size at its centre
    car->hitTest.type = HITTEST_SPHERE;
    car->hitTest.radius = 0.55f;
    car->hitTest.cel = obj->inCel;
    car->hitTest.mask = 0;
    car->hitTest.ignore = car->turretGeom;
    obj->transformFlags |= TRANSFORM_MOVED;
    auxVec_AddMulR32(Mat_Position(obj->transformMatrix), &CONST_UP_VECTOR,
                     (float)(car->hitTest.radius - AbsFchs((double)obj->objGraphics->extentMin.y)), &car->hitTest.pos);
    if (car->isHeli) {
        car->hitTest.radius = 0.275f;
        Vec_Copy(Mat_Position(obj->transformMatrix), &car->hitTest.pos);
    }
    if (obj->curState == CAR_TAKEN)
        obj->curState = CAR_DRIVEN;
    if (obj->curState == CAR_DESTROYED)
        obj->curState = CAR_IDLE;
}

// The helicopter's collision: anything but a drone, a player or a bullet that it touches destroys it (a hurt volume
// or another car at once), as does more than 1 damage. Its own missiles do not count.
// AUTOINJECT
void Car_MaybeHandleDamage(obj_tag *obj) {
    CAR_INFO *car = (CAR_INFO *)obj->extraObjectData;
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        obj_tag *by = hit->hitObj;
        if (by != NULL && (by->objectType == OBJECTTYPE_HURT || by->objectType == OBJECTTYPE_CAR)) {
            Car_Deactivate(obj);
            return;
        }
        if (hit->flags & HIT_NO_CONTACT)
            continue;
        if (by != NULL && by->objectType == OBJECTTYPE_BULLET &&
            ((BU_tag *)by->extraObjectData)->maybeProxyObjectShooter == obj)
            continue;
        car->damageAmt += hit->dmgAmt;
        by = hit->hitObj;
        if (by == NULL ||
            !(by->objectType == OBJECTTYPE_DRONE || by->objectType == OBJECTTYPE_PLAYER || by->objectType == OBJECTTYPE_BULLET)) {
            Car_Deactivate(obj);
            return;
        }
    }
    if (car->damageAmt > 1.0f)
        Car_Deactivate(obj);
}

// The tank's collision: pushed out of what it touches, it loses its speed into each surface; a floor under it stops
// its fall, and the first fairly level one is what it turns upright towards. What it touches carries it along and
// damages it; a hurt volume or another car, or more than 200 damage, destroys it. The helicopter's is
// Car_MaybeHandleDamage.
// AUTOINJECT
void Car_CollisionHandler(obj_tag *obj) {
    CAR_INFO *car = (CAR_INFO *)obj->extraObjectData;
    HITDATA_tag *hit = obj->hitList;
    if (obj->curState == CAR_STOPPED || obj->curState == CAR_DESTROYED)
        return;
    if (car->isHeli) {
        Car_MaybeHandleDamage(obj);
        return;
    }

    bool levelled = false;
    _VECTOR move, up, dir, gap;
    _MATRIX level;
    ApplyMatrixLV(&obj->transformMatrix, &car->velocity, &move);
    Vec_Add2(Mat_Position(obj->transformMatrix), &car->hitTest.push, Mat_Position(obj->transformMatrix));
    Mat_GetUp(&up, &obj->transformMatrix);
    Mat_GetDir(&dir, &obj->transformMatrix);
    car->onGround = 0;
    for (; hit != NULL; hit = hit->next) {
        obj_tag *by = hit->hitObj;
        if (by != NULL && (by->objectType == OBJECTTYPE_HURT || by->objectType == OBJECTTYPE_CAR)) {
            Car_Deactivate(obj);
            return;
        }
        if (hit->flags & HIT_NO_CONTACT)
            continue;
        car->damageAmt += hit->dmgAmt;
        by = hit->hitObj;
        if (by != NULL &&
            (by->objectType == OBJECTTYPE_DRONE || by->objectType == OBJECTTYPE_PLAYER || by->objectType == OBJECTTYPE_BULLET))
            continue;

        _VECTOR *normal = &hit->surfaceNormal.normal;
        Extended into = Extended(move.y) * Extended(normal->y) + Extended(move.z) * Extended(normal->z) +
                        Extended(move.x) * Extended(normal->x);
        if (into < Extended(0.0f))
            auxVec_AddMulR32(&move, normal, (float)-into, &move);
        Vec_Subtract(&hit->hitPosition, &car->hitTest.pos, &gap);
        if (gap.y < 0.0f && normal->y > 0.5f) {
            car->onGround = 1;
            Vec_Zero(&car->fall);
        }
        if (!levelled && normal->y > 0.64f) {
            Mat_Align2Up(level.m, &normal->x, &dir.x);
            levelled = true;
            Quat_MatToQuat(&car->orientation, &level);
        }
        Car_InheritVelocity(obj, hit->hitObj);
        by = hit->hitObj;
        if (by != NULL && (by->objectType == OBJECTTYPE_HURT || by->objectType == OBJECTTYPE_CAR))
            Car_Deactivate(obj);
    }
    Mat_Copy(&obj->transformMatrix, &level);
    Mat_InverseSelf(&level);
    ApplyMatrixLV(&level, &move, &car->velocity);
    obj->transformFlags |= TRANSFORM_MOVED;
    Car_SetBits(car, obj);
    if (car->damageAmt > 200.0f)
        Car_Deactivate(obj);
}

// Its delete function: deactivated. The name is ours.
// FUNC_AT(0x00027fb0)
void Car_Delete(obj_tag *obj) {
    Car_Deactivate(obj);
}
