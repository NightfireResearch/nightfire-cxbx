// The drones' weapons. A combat state asks for fire with DroneWeap_Fire; each frame DroneWeap_HandleFiring keeps the
// aim on the opponent and, while firing, DroneWeap_DoFiring shoots a burst: DroneWeap_FireWeapon once per
// DroneWeap_FireDelay, DroneWeap_BurstLength shots, then the pause of DroneWeap_BurstDelay. Each shot misses by
// DroneWeap_DoBulletAccuracy's error unless it hits. Also the grenades, close combat, the weapon in hand, and the
// damage the drones' bullets do to the player. docs/drone/architecture/README.md has the fields.

#include "DroneWeapon.h"
#include "DroneTuning.h"
#include "NDrone2.h"
#include "BOT.h"
#include "Behaviour.h"
#include "DroneAnim.h"             // DroneAnim_CanDoAnimState
#include "DroneTables.h"           // DRONE_PROP
#include "../../game.h"            // GameState, weapon_definition_tag
#include "../weapon_stats.h"       // weapon_data
#include "../obj/Player.h"         // BLData
#include "../obj/bullet.h"         // BU_tag, Bullet_init
#include "../obj/Effect.h"         // Effect_tag
#include "../obj/Light.h"          // Light_Create
#include "../obj/control.h"        // control_create_object, control_link_object_to_cel
#include "../mp/multiplayer.h"     // MPSettings
#include "../sp/PlayerStats.h"     // PlrStat_OkToUpdate
#include "../../sound/Sound.h"     // Sound_Play3D
#include "../../engine/Anim.h"     // AnimGetBoneWorldTrans, AnimState
#include "../../engine/Collide.h"  // HITDATA_tag, Collide_RayIntersect
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../../util/Random.h"
#include "../../util/hashtable.h"  // hashtable_set_object_to_entity_gfx
#include "DroneVision.h"
#include "NDrone2_Opponent.h"

#include <bit>
#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// Originals called by address
#define BOT_opponentTargetting ((void (__cdecl *)(DCVars_tag *dcv))0x0001aee0)
#define BOTWEAP_AmmoInGun ((short (__cdecl *)(Drone_tag *drone, short weapon))0x0001e4d0)
#define BOTWEAP_tooCloseForWeapon ((bool (__cdecl *)(Drone_tag *drone, short weapon, float distance))0x0001f740)
#define BOTWEAP_decrRounds ((void (__cdecl *)(Drone_tag *drone, short rounds))0x0001e9a0)
#define BOTSTATE_changeWeapon ((uint (__cdecl *)(BOT_vars_t *bot, uint weapon, char param_3, char param_4))0x0001e3f0)
#define GT_DeployMiniGun ((obj_tag *(__cdecl *)(_VECTOR *pos, _MATRIX *mtx, obj_tag *owner))0x0009a9b0)
#define MPDrone_MaybeIsDyingOrDead ((bool (__cdecl *)(obj_tag *obj))0x0001a190)
#define Pickup_CreateSimple ((obj_tag *(__cdecl *)(_MATRIX *mtx, short weapon, short count, int param_4))0x000a7f10)

// The weapons with at least a second between shots and bursts of their own length (DroneWeap_FireDelay,
// DroneWeap_BurstLength, _DroneWeap_NextBulletTime)
#define SLOW_WEAPON_CASES case 6: case 10: case 14: case 15: case 16

// Where a grenade leaves the drone's hand
#define GRENADE_BONE 0x80000015

// The sound DroneWeap_FireWeapon does not play
#define SFX_NO_FIRE_SOUND 0x5ef

constexpr float kAimAngle = 0.34906587f;        // 20 degrees: how far the barrel may point off the target
constexpr float kNinjaAimAngle = 0.5235988f;    // 30 degrees, for DTYPE_Ninja
constexpr float kDegToRad = 0.017453292f;
constexpr float kHalfPi = 1.5707964f;
constexpr float kQuarterPi = 0.7853982f;
constexpr float kCloseCombatAngle = 1.0471976f;  // 60 degrees
static_assert(std::bit_cast<uint32_t>(kAimAngle) == 0x3eb2b8c3, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kNinjaAimAngle) == 0x3f060a92, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kDegToRad) == 0x3c8efa35, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3fc90fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kQuarterPi) == 0x3f490fdb, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(kCloseCombatAngle) == 0x3f860a92, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.9998f) == 0x3f7ff2e5, "not the original's constant");

static char HeldWeapon(DCVars_tag *dcv) {
    return dcv->gameObj->animState->currentWeaponId;
}

// ---- firing

// Keeps targetPos on the opponent, or where it was last seen. Not with bots.
// FUNC_AT(0x000666a0)
void DroneWeap_AimAtOpponent(DCVars_tag *dcv) {
    if (MPSettings.maybeDroneAIEnabled)
        return;
    Drone_tag *drone = dcv->drone;
    if (drone->opponent != NULL)
        Vec_Add2(&drone->opponent->position, &drone->aimOffset, &drone->targetPos);
    else
        Vec_Add2(&drone->opponentLastPos, &drone->aimOffset, &drone->targetPos);
}

// Each frame. Not firing, the drone only aims. Firing, it fires DroneWeap_DoFiring's bursts until one ends with
// oneBurst set.
// AUTOINJECT
void DroneWeap_HandleFiring(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    drone->readyToFire = 0;
    if (!drone->firing) {
        if (drone->opponent != NULL) {
            DroneWeap_AimAtOpponent(dcv);
            if (!MPSettings.maybeDroneAIEnabled)
                DroneWeap_DoOpponentTargetting(dcv);
        }
        return;
    }

    if (!drone->oneBurst) {
        drone->burstOver = 0;
    } else if (drone->burstOver) {
        drone->firing = 0;
        if (drone->opponent != NULL)
            DroneWeap_AimAtOpponent(dcv);
        return;
    }
    if (!MPSettings.maybeDroneAIEnabled)
        DroneWeap_DoOpponentTargetting(dcv);
    if (!_DroneWeap_DoFiring(dcv))
        drone->burstOver = 1;
}

// A shot when one is due and the drone is ready. False when the burst is over: its last shot, or the opponent out of
// sight for 16 frames (unless DRONE_PROP_17).
bool _DroneWeap_DoFiring(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled)
        BOT_opponentTargetting(dcv);
    if (drone->burstShots == 0)
        _DroneWeap_NextBulletTime(dcv, 1, 0);
    if (dcv->drone->unseenFrames > 15 && behaviour_util_getProperty(DRONE_PROP_17, drone->currentBehaviour) == 0)
        return false;
    if (GameState.NumFramesUnpaused < drone->nextShotFrame)
        return true;
    if (!DroneWeap_Ready2Fire(dcv))
        return true;
    drone->readyToFire = 1;
    if (drone->firesByAnim)
        return true;
    DroneWeap_FireWeapon(dcv);
    return _DroneWeap_ManageBurstCount(dcv);
}

// The original takes the DCVars in EDI.
// AUTOLTCG
void __declspec(naked) DroneWeap_DoFiring(void) {
    _asm {
        push edi
        call _DroneWeap_DoFiring
        add esp, 4
        ret
    }
}

// Asks for fire. Restarting, unless it already fires (bots always), the first shot waits for a burst's pause.
// AUTOINJECT
void DroneWeap_Fire(char restart, DCVars_tag *dcv) {
    if ((MPSettings.maybeDroneAIEnabled || !dcv->drone->firing) && restart)
        _DroneWeap_NextBulletTime(dcv, 0, 1);
    dcv->drone->firing = 1;
    dcv->drone->burstOver = 0;
    dcv->drone->oneBurst = 0;
}

// The frames between shots of the weapon in hand: its refire delay, at least 60 for the slow weapons, at the frame
// rate.
// FUNC_AT(0x00066e90)
short DroneWeap_FireDelay(DCVars_tag *dcv) {
    char weapon = HeldWeapon(dcv);
    short delay = (short)weapon_data[weapon].refireDelay;   // read as a word
    short least = 0;
    switch (weapon) {
    SLOW_WEAPON_CASES:
        least = 60;
        break;
    }
    if (delay < least)
        delay = least;
    return (short)(int)((double)delay * FRAME_RATE_DIV + 0.5f);
}

// The shots in a burst of the weapon in hand. Outside multiplayer the slow weapons draw theirs: 2 to 4 for 6 and 10
// when their own is under 2, 1 or 2 for 14 to 16.
// FUNC_AT(0x00066f10)
short DroneWeap_BurstLength(DCVars_tag *dcv) {
    char weapon = HeldWeapon(dcv);
    short shots = weapon_data[weapon].droneBurstLength;
    if (!MPSettings.maybeDroneAIEnabled) {
        switch (weapon) {
        case 6:
        case 10:
            if ((ushort)shots < 2)
                return (short)(Rand_Rand(3) + 2);
            break;
        case 14:
        case 15:
        case 16:
            return (short)(Rand_Rand(2) + 1);
        }
    }
    return shots;
}

// When the next shot is due: a fire delay after the last one, and not before now. A new burst (or the first) draws
// its length - more aggressive drones fire shorter bursts of the fast weapons, at least 3 shots - and adds the pause
// before it to the last shot. Bots fire bursts scaled by their aggression and wait a draw of 0 to 14 frames plus 60
// over the aggression.
void _DroneWeap_NextBulletTime(DCVars_tag *dcv, char newBurst, char start) {
    uint now = GameState.NumFramesUnpaused;
    Drone_tag *drone = dcv->drone;
    short delay = DroneWeap_FireDelay(dcv);
    uint wait = 0;
    uint pause = 0;

    if (newBurst || start) {
        drone->burstShots = DroneWeap_BurstLength(dcv);
        if (drone->burstShots < 1)
            drone->burstShots = 1;
        if (MPSettings.maybeDroneAIEnabled) {
            float aggression = (float)BOT_getAggressionMul(drone);
            uint draw = Rand_Rand(15);
            wait = draw - (int)(-60.0f / (double)aggression);
            double shots = (double)drone->burstShots * aggression;
            if (shots <= 1.0f)
                shots = 1.0f;
            drone->burstShots = (short)(int)shots;
            NDrone2_SetOpponentAimPos(drone);
        } else {
            float aggression = (float)BOT_getAggressionMul(drone);
            pause = _DroneWeap_BurstDelay(drone);
            switch (HeldWeapon(dcv)) {
            SLOW_WEAPON_CASES:
                break;
            default:
                if (drone->burstShots > 3) {
                    double shots = (double)drone->burstShots * aggression;
                    if (shots <= 3.0f)
                        shots = 3.0f;
                    drone->burstShots = (short)(int)shots;
                }
                break;
            }
        }
        drone->lastShotFrame += pause;
    }

    uint due = drone->lastShotFrame + delay;
    if (due < now + wait)
        drone->nextShotFrame = now + wait;
    else
        drone->nextShotFrame = due;
}

// The original takes the DCVars in EDI, the two flags on the stack.
// AUTOLTCG
void __declspec(naked) DroneWeap_NextBulletTime(char newBurst, char start) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        call _DroneWeap_NextBulletTime
        add esp, 12
        ret
    }
}

// The compiler's copy of DroneWeap_NextBulletTime for a new burst, which DroneWeap_DoFiring calls (Ghidra gives it
// the same name; ours is invented).
// FUNC_AT(0x00067c00)
void DroneWeap_NextBulletTimeNewBurst(DCVars_tag *dcv) {
    _DroneWeap_NextBulletTime(dcv, 1, 0);
}

// One shot fewer in the burst. False when that was the last; otherwise the next is due a fire delay after the last,
// and not before now.
bool _DroneWeap_ManageBurstCount(DCVars_tag *dcv) {
    dcv->drone->burstShots--;
    uint now = GameState.NumFramesUnpaused;
    Drone_tag *drone = dcv->drone;
    if (drone->burstShots < 1)
        return false;
    uint due = drone->lastShotFrame + DroneWeap_FireDelay(dcv);
    drone->nextShotFrame = due < now ? now : due;
    return true;
}

// The original takes the DCVars in EAX.
// AUTOLTCG
void __declspec(naked) DroneWeap_ManageBurstCount(void) {
    _asm {
        push eax
        call _DroneWeap_ManageBurstCount
        add esp, 4
        ret
    }
}

// The pause before a burst, in frames: the normal delay scaled by aggression (0 the longest, 4 the shortest) and a
// draw of 0.5 to 1, kept between the minimum and the maximum; the minimum when the opponent is nearer than MinDist,
// the maximum beyond MaxDist.
uint _DroneWeap_BurstDelay(Drone_tag *drone) {
    float delay = DroneFiring_BurstDelay_Normal;
    switch (drone->aggression) {
    case 0:
        delay = delay * 1.66f;
        break;
    case 1:
        delay = delay * 1.33f;
        break;
    case 3:
        delay = delay * 0.66f;
        break;
    case 4:
        delay = delay * 0.33f;
        break;
    }
    // Truncated and converted back as unsigned, twice
    double frames = (uint)(int)((Float_FRand(0.5f) + 0.5f) * delay);
    if (!(frames > DroneFiring_BurstDelay_Min))
        frames = DroneFiring_BurstDelay_Min;
    frames = (uint)(int)frames;
    if (!(frames < DroneFiring_BurstDelay_Max))
        frames = DroneFiring_BurstDelay_Max;

    if (drone->distanceToTarget < DroneFiring_BurstDelay_MinDist)
        return (uint)(int)((double)FRAME_RATE_DIV * DroneFiring_BurstDelay_Min);
    if (drone->distanceToTarget > DroneFiring_BurstDelay_MaxDist)
        frames = DroneFiring_BurstDelay_Max;
    return (uint)(int)(FRAME_RATE_DIV * frames);
}

// The original takes the drone in ESI.
// AUTOLTCG
void __declspec(naked) DroneWeap_BurstDelay(void) {
    _asm {
        push esi
        call _DroneWeap_BurstDelay
        add esp, 4
        ret
    }
}

// Whether the drone may shoot now: the opponent is alive (the player only while the mission counts, outside
// multiplayer) and the drone is in an aiming anim (mode 4, or 2 with no anim script; close up that is enough). With
// the anim's aim flag 0x1000 the barrel must also point within 20 degrees of targetPos (30 for DTYPE_Ninja) - or the
// drone be dead.
// AUTOINJECT
bool DroneWeap_Ready2Fire(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *opponent = drone->opponent;
    obj_tag *obj = dcv->gameObj;
    if (opponent == NULL)
        return false;
    if (opponent->objectType == OBJECTTYPE_DRONE) {
        if (((Drone_tag *)opponent->extraObjectData)->health <= 0.0f)
            return false;
    } else if (opponent->objectType == OBJECTTYPE_PLAYER || opponent->objectType == OBJECTTYPE_DEAD_PLAYER) {
        if (((BLData *)opponent->extraObjectData)->health <= 0.0f)
            return false;
        if (!MPSettings.maybeDroneAIEnabled && !PlrStat_OkToUpdate())
            return false;
    }

    if (drone->animMode == 4)
        return true;
    if (!MPSettings.maybeDroneAIEnabled) {
        if (drone->animMode != 2 || drone->unknown434 != NULL)
            return false;
        if (drone->distanceToTarget < 2.0f)
            return true;
    }
    if (!(obj->animState->animObj.flags & 0x1000))
        return true;
    if (dcv->drone->dtype == DTYPE_Bot && BOTWEAP_AmmoInGun(drone, drone->botVars->weapon) < 1)
        return false;

    int bone;
    uint flash;
    ushort sound;
    _VECTOR muzzle, barrel;
    if (!DroneWeap_GetWeaponInfo(drone, &bone, &flash, &sound, &muzzle, &barrel))
        return false;
    Vec_Normalise(&barrel, &barrel);
    _VECTOR target, toTarget;
    Vec_Copy(&drone->targetPos, &target);
    Vec_Subtract(&target, &muzzle, &toTarget);
    Vec_Normalise(&toTarget, &toTarget);
    // Summed z first, and compared before it is rounded to a float
    double dot = (double)barrel.z * toTarget.z + (double)barrel.y * toTarget.y + (double)barrel.x * toTarget.x;
    if (dot > 0.9998f)
        return true;
    float limit = drone->dtype == DTYPE_Ninja ? kNinjaAimAngle : kAimAngle;
    if (AbsFchs(CIAcos((float)dot)) < Extended(limit))   // the XBE's acos, unrounded
        return true;
    return drone->health <= 0.0f;
}

// One shot of the weapon in hand: a light and a muzzle flash at the muzzle, its bullets towards targetPos plus the
// aim error (along the barrel once the drone is out of action), a casing, a round from the clip and the sound. Bots
// fire only with ammo, in sight, in an attack state and not too close; a bot with weapon 0x52 sets it up instead.
// AUTOINJECT
void DroneWeap_FireWeapon(DCVars_tag *dcv) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (drone->dtype == DTYPE_Bot) {
        if (BOTWEAP_AmmoInGun(drone, drone->botVars->weapon) < 1)
            return;
        if (drone->opponent != NULL &&
            BOTWEAP_tooCloseForWeapon(drone, drone->botVars->weapon, drone->distanceToTarget))
            return;
        if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
            return;
        uint state = dcv->aiStateMachine->curState;
        if (state == DSTATE_BotAttackReload || state == DSTATE_BotAttackChangeWeapon)
            return;
        if (BOTSTATE_getStateType(state) != BOTSTATE_CLASS_ATTACK)
            return;
        if (drone->botVars->weapon == Weap_Ronin) {
            float yaw = obj->rotation.y;
            _VECTOR at;
            at.x = (float)(FSin(yaw) + Extended(obj->position.x));
            at.y = obj->position.y;
            at.z = (float)(FCos(yaw) + Extended(obj->position.z));
            if (GT_DeployMiniGun(&at, &drone->gameObj->transformMatrix, drone->gameObj) == NULL)
                return;
            BOTWEAP_decrRounds(drone, 1000);
            BOTSTATE_changeWeapon(drone->botVars, 0, 1, 0);
            return;
        }
    } else {
        DroneWeap_DoBulletAccuracy(dcv);
    }

    int bone;
    uint flash;
    ushort sound;
    _VECTOR muzzle, dir;
    if (!DroneWeap_GetWeaponInfo(drone, &bone, &flash, &sound, &muzzle, &dir))
        return;
    _VECTOR rot;
    Vec_Cartesian_2_Spherical(&rot, dir.x, dir.y, dir.z);
    rot.x = -rot.x;
    rot.z = 0.0f;
    weapon_definition_tag *wpn = &weapon_data[HeldWeapon(dcv)];
    Light_Create(&muzzle, wpn->muzzleFlash_r, wpn->muzzleFlash_g, wpn->muzzleFlash_b, 7.0f, 0, 0, 1, 1.0f, 0, 0, 0,
                 0xffff);

    // The flash: an effect object for two frames, turned at random about the barrel
    obj_tag *fx = control_create_object(sizeof(Effect_tag), &muzzle, NULL, NULL);
    if (fx != NULL) {
        Vec_Copy(&rot, &fx->rotation);
        fx->rotation.x = Float_FRand(6.2831855f) + fx->rotation.x;
        fx->rotation.y = (float)(Rand_FRandHalf(0.2f) + kHalfPi + fx->rotation.y);
        double roll = Rand_FRandHalf(0.2f);
        Effect_tag *effect = (Effect_tag *)fx->extraObjectData;
        fx->objectType = OBJECTTYPE_EFFECT;
        fx->rotation.z = roll + fx->rotation.z;
        effect->life = 2;
        effect->fade = 0;
        if (flash != 0)
            hashtable_set_object_to_entity_gfx(fx, (HASHCODE)flash);
        fx->tweakA = 0xff;
        control_link_object_to_cel(fx, dcv->gameObj->inCel);
    }

    Vec_Normalise(&dir, &dir);
    wpn = &weapon_data[HeldWeapon(dcv)];
    if (drone->health > 0.0f && !MPDrone_MaybeIsDyingOrDead(obj) && drone->opponent != NULL) {
        _VECTOR aimAt;
        Vec_Add2(&drone->targetPos, &drone->aimError, &aimAt);
        Vec_Subtract(&aimAt, &muzzle, &dir);
        Vec_Normalise(&dir, &dir);
    }
    for (short i = 0; i < wpn->numBulletsPerShot; i++)
        Bullet_init(-1, obj, obj, wpn, &muzzle, &dir, 0xffff);

    drone->lastShotFrame = GameState.NumFramesUnpaused;
    drone->fired = 1;
    drone->unknown41 = 0;
    if (!MPSettings.maybeDroneAIEnabled)
        Bullet_init_casing(obj);
    drone->clip--;
    if (dcv->drone->dtype == DTYPE_Bot)
        BOTWEAP_decrRounds(drone, 1);

    // The original also tests the sound against -1, which a ushort never is. The pitch is converted as unsigned:
    // a negative draw comes out near 4.29e9.
    if (sound != SFX_NO_FIRE_SOUND) {
        float pitch = (float)(uint)(Rand_Rand(500) - 250);
        Sound_Play3D((Action_SFX)sound, &obj->position, 25.0f, 100.0f, pitch, 0, 0, 0);
    }
    if (drone->firesByAnim && !_DroneWeap_ManageBurstCount(dcv))
        drone->burstOver = 1;
}

// Whether the next shot hits: a chance of 100 less 5 per accuracy point, scaled up close (twice), down while the
// player has just started moving or moves, by the difficulty and, in the Powerstation levels for anim set 0x13, just
// after the player stops. Beyond the too-close distance it never hits before the opponent has been seen for
// NewSighting_TimeToHit; within 3 m it always does. A miss aims off by an error that sways with time, turned towards
// the opponent.
// AUTOINJECT
void DroneWeap_DoBulletAccuracy(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->opponent == NULL)
        return;

    double chance = 100.0f - (double)drone->accuracy * 5.0f;
    if (drone->distanceToTarget < DroneFiring_TooClose_Distance)
        chance *= DroneFiring_TooClose_Accuracy;
    if (drone->targetStartedMoving)
        chance *= DroneFiring_TargetFirstMoved_Accuracy;
    if (drone->targetStopped &&
        (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
         GameState.CurrentLevelHashcode == HT_Level_PowerStationA2) &&
        drone->animSet == 0x13)
        chance *= DroneFiring_TargetFirstStopped_Accuracy;
    if (drone->targetMoving)
        chance *= DroneFiring_TargetMoving_Accuracy;
    float difficulty;
    switch (GameState.difficultyModifier) {
    case 1:
        difficulty = DroneFiring_Accuracy_Easy;
        break;
    case 3:
    case 4:
        difficulty = DroneFiring_Accuracy_Hard;
        break;
    default:
        difficulty = DroneFiring_Accuracy_Normal;
        break;
    }
    float hitChance = (float)(difficulty * chance);
    // The close-up factor again, as the original has it
    if (drone->distanceToTarget < DroneFiring_TooClose_Distance)
        hitChance = DroneFiring_TooClose_Accuracy * hitChance;

    bool hit = Float_FRand(100.0f) < hitChance;
    if (!(drone->distanceToTarget < DroneFiring_TooClose_Distance)) {
        uint sighting = (uint)(int)((double)_FRAME_RATE * DroneFiring_NewSighting_TimeToHit);
        if (drone->sightFrames < sighting)
            hit = false;
        if (drone->unknown41)
            hit = false;
    }
    if (drone->distanceToTarget < 3.0f || hit) {
        Vec_Copy(&CONST_ZERO_VECTOR, &drone->aimError);
        return;
    }

    double t = (uint)(dcv->gameObj->creationTimeFrames + GameState.NumFramesUnpaused);
    double a = 0.01f * t;
    _VECTOR error;
    error.x = (float)(FSin(a) * Extended(0.5f));
    error.y = (float)(FCos(a) * Extended(1.5f));
    error.z = (float)(FSin(t * 0.02f) * Extended(1.5f));
    _MATRIX turn;
    RotMatrix(&drone->opponentAngles, &turn);
    ApplyMatrixLV(&turn, &error, &drone->aimError);
}

// Follows the player: whether it moves in the plane, and when it started or stopped. Each lasts for its TimeToHit.
// AUTOINJECT
void DroneWeap_DoOpponentTargetting(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    obj_tag *opponent = drone->opponent;
    if (opponent == NULL)
        return;
    if (opponent->objectType == OBJECTTYPE_PLAYER) {
        _VECTOR movement = ((BLData *)opponent->extraObjectData)->movement;
        movement.y = 0.0f;
        drone->targetMoving = Vec_Magnitude(&movement) != 0.0f;
    }

    if (drone->targetMoving) {
        drone->targetStopped = 0;
        drone->targetStoppedSeen = 0;
        if (!drone->targetStartedMoving && !drone->targetStartedSeen) {
            drone->targetChangeFrame = GameState.NumFramesUnpaused;
            drone->targetStartedMoving = 1;
            drone->targetStartedSeen = 1;
        }
        uint frames = (uint)(int)((double)_FRAME_RATE * DroneFiring_TargetFirstMoved_TimeToHit);
        if (frames + drone->targetChangeFrame < GameState.NumFramesUnpaused)
            drone->targetStartedMoving = 0;
    } else {
        drone->targetStartedMoving = 0;
        drone->targetStartedSeen = 0;
        if (!drone->targetStopped && !drone->targetStoppedSeen) {
            drone->targetChangeFrame = GameState.NumFramesUnpaused;
            drone->targetStopped = 1;
            drone->targetStoppedSeen = 1;
        }
        uint frames = (uint)(int)((double)DroneFiring_TargetFirstStopped_TimeToHit * _FRAME_RATE);
        if (frames + drone->targetChangeFrame < GameState.NumFramesUnpaused)
            drone->targetStopped = 0;
    }
}

// ---- the opponent

// Turns to the opponent. With checkAngle, already facing it (within 0.9 rad) is enough, and further round only
// keepTurning turns. In anim states 0x1e and 0x1f it turns half way, noting whether it could step 0.4 m forward.
// AUTOINJECT
bool DroneWeap_AimTarget_IsOpponent(char checkAngle, char keepTurning, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->opponent == NULL || (drone->unknown3f0 & 0x8000000))
        return false;
    obj_tag *obj = dcv->gameObj;
    _VECTOR toOpponent;
    Vec_Subtract(&drone->opponent->position, &obj->position, &toOpponent);
    float yaw = maybeAtan2(toOpponent.x, toOpponent.z);
    float turn = Vec_AngleDifference(yaw, obj->rotation.y);
    if (checkAngle) {
        if (fabsf(turn) < 0.9f)
            return true;
        if (!keepTurning && fabsf(turn) > 0.9f)
            return false;
    }

    if (drone->animState == 0x1e || drone->animState == 0x1f) {
        drone->angleToSomePoint = obj->rotation.y - Vec_AngleDifference(yaw, obj->rotation.y) * 0.5f;
        if (NDrone2_CanMoveToRelativePos(drone, obj, 0.0f, 0.0f, 0.4f))
            drone->unknown38 = 1;
    } else {
        NDrone2_SetAngleToObj(dcv, drone->opponent, 0.0f, 0);
    }
    return true;
}

// The armed opponent aims at the drone: the player has it in his sights, or (bots) faces it within the angle.
// AUTOINJECT
bool DroneWeap_OpponentIsAimingAtMe(DCVars_tag *dcv, float angle) {
    obj_tag *opponent = dcv->drone->opponent;
    if (opponent == NULL)
        return false;
    if (opponent->objectType != OBJECTTYPE_PLAYER && opponent->objectType != OBJECTTYPE_DRONE)
        return false;
    if (!NDrone2_ObjectIsArmed(opponent))
        return false;
    if (dcv->drone->inPlayersSights)
        return true;
    return MPSettings.maybeDroneAIEnabled && NDrone2_BondIsFacingMe(dcv, angle);
}

// The opponent is the player or a drone, and armed
// AUTOINJECT
bool DroneWeap_OpponentHasWeapon(DCVars_tag *dcv) {
    obj_tag *opponent = dcv->drone->opponent;
    if (opponent == NULL)
        return false;
    if (opponent->objectType != OBJECTTYPE_PLAYER && opponent->objectType != OBJECTTYPE_DRONE)
        return false;
    return NDrone2_ObjectIsArmed(opponent);
}

// ---- grenades, close combat, the weapon in hand

// A grenade is worth throwing: the drone has some, wins a toss, its opponent is 8 to 20 m away and it has the throw
// anim (0x49). The toss is drawn before the opponent is looked at.
// FUNC_AT(0x00066c50)
bool DroneWeap_CanThrowGrenade(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->grenadeWeapon == 0 || drone->grenades == 0)
        return false;
    if (Rand_Rand(2) != 0)
        return false;
    if (drone->opponent == NULL)
        return false;
    if (drone->distanceToTarget < 8.0f || drone->distanceToTarget > 20.0f)
        return false;
    return DroneAnim_CanDoAnimState(dcv, 0x49, 0);
}

// Throws a grenade from the hand towards targetPos, 45 degrees down within 10 m and up beyond 18 m. A ninja throws
// weapon 53 or, one time in three, 54 (the stun and smoke grenades, docs/weapons.md). One grenade fewer either way.
// FUNC_AT(0x00066cd0)
void DroneWeap_ThrowGrenade(DCVars_tag *dcv) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    uint weapon = drone->grenadeWeapon;
    if (drone->grenades != 0)
        drone->grenades--;
    if (dcv->drone->dtype == DTYPE_Ninja) {
        weapon = 53;
        if (Rand_Rand(3) == 0)
            weapon = 54;
    } else if (weapon == 0) {
        return;
    }

    _VECTOR hand;
    _MATRIX aim;
    AnimGetBoneWorldTrans(dcv->gameObj, GRENADE_BONE, 0, &hand, &aim);
    Mat_Copy(&obj->transformMatrix, &aim);  // replaced below
    _VECTOR tilt = {0.0f, 0.0f, 0.0f};
    _VECTOR toTarget;
    Vec_Subtract(&drone->targetPos, &obj->position, &toTarget);
    float distance = Vec_Magnitude(&toTarget);
    _VECTOR heading = {0.0f, (float)FPatan(toTarget.x, toTarget.z), 0.0f};
    RotMatrix(&heading, &aim);
    if (distance < 10.0f || distance > 18.0f) {
        tilt.x = distance < 10.0f ? kQuarterPi : -kQuarterPi;
        _MATRIX pitch;
        RotMatrix(&tilt, &pitch);
        MulMatrix2(&pitch, &aim);
    }
    _VECTOR dir;
    Mat_GetDir(&dir, &aim);
    Bullet_init(-1, dcv->gameObj, dcv->gameObj, &weapon_data[weapon], &hand, &dir, 0xffff);
}

// The anim event that places the drone's third weapon (DroneAnim_EventFunc): a grenade
// FUNC_AT(0x00067bf0)
void DroneWeap_PlaceTertiary(DCVars_tag *dcv) {
    DroneWeap_ThrowGrenade(dcv);
}

// A blow (anim events): a projectile of weapon 100 (101 for kind 1, 102 for kind 2) along opponentDelta, when the
// opponent is within 1.75 m and 60 degrees of the drone's facing. Anim set 7 strikes with weapon 99 within 2 m, any
// way it faces.
// AUTOINJECT
void DroneWeap_CloseCombatImpact(DCVars_tag *dcv, int kind) {
    Drone_tag *drone = dcv->drone;
    obj_tag *obj = dcv->gameObj;
    int weapon = 100;
    _VECTOR pos, dir;
    if (drone->animSet == 7) {
        if (drone->distanceToTarget > 2.0f)
            return;
        Vec_Copy(&drone->opponentDelta, &dir);
        Vec_Copy(&obj->position, &pos);
        weapon = 99;
    } else {
        if (drone->distanceToTarget > 1.75f)
            return;
        if (fabsf(drone->opponentBearing) > kCloseCombatAngle)
            return;
        Vec_Copy(&drone->opponentDelta, &dir);
        Vec_Copy(&obj->position, &pos);
        switch (kind) {
        case 0:
            weapon = 100;
            break;
        case 1:
            weapon = 101;
            break;
        case 2:
            weapon = 102;
            break;
        }
    }
    Bullet_init(-1, dcv->gameObj, dcv->gameObj, &weapon_data[weapon], &pos, &dir, 0xffff);
}

// Puts a weapon in the drone's hand, with the anim set for it and a full clip: 1 and 54 empty-handed (set 0xc), 6
// (set 4, 16 rounds), 18 (set 6, 32), 24 and 25 (set 5, 32). Others change nothing.
// AUTOINJECT
void DroneWeap_ChangeWeapon(DCVars_tag *dcv, int weapon) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    short animSet, rounds;
    switch (weapon) {
    case 1:
    case 54:
        animSet = 0xc;
        rounds = 0;
        break;
    case 6:
        animSet = 4;
        rounds = 16;
        break;
    case 18:
        animSet = 6;
        rounds = 32;
        break;
    case 24:
    case 25:
        animSet = 5;
        rounds = 32;
        break;
    default:
        return;
    }
    drone->animSet = animSet;
    obj->animState->currentWeaponId = (char)weapon;
    drone->clipSize = rounds;
    drone->clip = rounds;
}

// Whether nothing is in the way from the drone to where it drops its weapon (DroneWeap_DropWeapon; our name)
// FUNC_AT(0x00067640)
bool DroneWeap_DropPathClear(Drone_tag *drone, _VECTOR *to) {
    obj_tag *obj = drone->gameObj;
    HITDATA_tag *hits = NULL;
    if (Collide_RayIntersect(&obj->position, to, obj->inCel, obj, NULL, &hits, 0, 0x125, 0)) {
        Collide_FreeHitList(&hits);
        return false;
    }
    return true;
}

// Drops the weapon in hand as a pickup at the hand's bone (at the drone's feet when something is in the way), with
// half to all of a clip of under 10, a quarter to three quarters of a bigger one. Base weapon 0x1a also drops
// ammo of weapon 0x1b. The hand is left empty.
// AUTOINJECT
void DroneWeap_DropWeapon(DCVars_tag *dcv) {
    obj_tag *obj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (obj == NULL || obj->animState == NULL)
        return;
    char weapon = obj->animState->currentWeaponId;
    if (weapon <= 0)
        return;

    weapon_definition_tag *wpn = &weapon_data[weapon];
    if (wpn->worldModelGfx != 0) {
        int bone = AnimDatumGetWeaponInfo(obj, NULL, NULL, NULL);
        if (bone < 0)
            bone = 0;
        _VECTOR hand;
        _MATRIX at;
        AnimGetBoneWorldTrans(dcv->gameObj, bone, 0, &hand, &at);
        if (!DroneWeap_DropPathClear(drone, Mat_Position(at)))
            Vec_Copy(&drone->gameObj->position, Mat_Position(at));

        double share = wpn->clipSize < 10 ? Float_FRand(0.5f) + 0.5f : Float_FRand(0.5f) + 0.25f;
        Pickup_CreateSimple(&at, wpn->weaponBaseNum, (short)(int)(wpn->clipSize * share), 0);
        if (wpn->weaponBaseNum == 0x1a) {
            double ammoShare = Float_FRand(0.5f) + 0.25f;
            obj_tag *ammo = Pickup_CreateSimple(&at, 0x1b, (short)(int)(weapon_data[0x1b].clipSize * ammoShare), 1);
            if (ammo != NULL)
                ammo->effectFlags |= 0x10;
        }
    }
    dcv->gameObj->animState->currentWeaponId = 0;
}

// ---- the damage drones do to the player

// Whether a drone's shots hurt more from behind: anim set 0x12, not a sniper, not in SpaceStationD.
// AUTOLTCG
bool __fastcall Drone_MayDealBackshotDamage(Drone_tag *drone) {
    if (drone->dtype == DTYPE_Sniper || drone->dtype == DTYPE_SniperAlert || drone->animSet != 0x12)
        return false;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        return true;
    case HT_Level_SpaceStationD:
        return false;
    default:
        return true;
    }
}

// The damage of a drone's bullet hitting the player: none from an unarmed drone (skin class 0xc), blows as they are,
// otherwise more close up, scaled by the drone's damageScale, and more again for a shot in the player's back.
// AUTOINJECT
double Drone_ModBulletDamage(Drone_tag *drone, obj_tag *player, HITDATA_tag *hit) {
    float damage = hit->dmgAmt;
    if (damage == 0.0f || drone->skinClass == 0xc)
        return 0.0f;
    ushort weapon = ((BU_tag *)hit->hitObj->extraObjectData)->wpnDef->weaponVariantNum;
    if (weapon >= 99 && weapon <= 102)
        return damage;

    double scaled = damage;
    if (drone->distanceToTarget < DroneFiring_TooClose_Distance)
        scaled *= DroneFiring_TooClose_Damage;
    scaled *= drone->damageScale;
    if (!Drone_MayDealBackshotDamage(drone))
        return scaled;
    // From here on, the damage rounded to a float
    float rounded = (float)scaled;
    float shotYaw = maybeAtan2(hit->maybehitDirection.x, hit->maybehitDirection.z);
    if (fabs(Vec_AngleDifference(shotYaw, player->rotation.y)) < kHalfPi)
        return (double)DroneFiring_PlayerBackShot_Damage * rounded;
    return rounded;
}

// Player_DealWithObjHit: the damage of a live drone's live bullet goes through Drone_ModBulletDamage. Not in
// multiplayer. Always true.
// AUTOINJECT
bool Drone_ModPlayerHitDamage(obj_tag *player, HITDATA_tag *hit) {
    if (MPSettings.maybeDroneAIEnabled || player == NULL || hit == NULL)
        return true;
    obj_tag *bullet = hit->hitObj;
    if (bullet == NULL || (bullet->flags & 1) || bullet->objectType != OBJECTTYPE_BULLET)
        return true;
    obj_tag *shooter = ((BU_tag *)bullet->extraObjectData)->firedByObj;
    if (shooter == NULL || shooter->objectType != OBJECTTYPE_DRONE)
        return true;
    Drone_tag *drone = (Drone_tag *)shooter->extraObjectData;
    if (drone == NULL || (shooter->flags & 1))
        return true;
    hit->dmgAmt = (float)Drone_ModBulletDamage(drone, player, hit);
    return true;
}
