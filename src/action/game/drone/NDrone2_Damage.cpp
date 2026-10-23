// What hits a drone. Its collision hands each hit to NDrone2_DealWithObjHit: the player's touch, hurt volumes, smoke,
// and shots, which send the drone an impact message (punch, taser, stun dart, or Drone_BulletHit's bullet message).
// The state running then takes the message through DroneFunc_HandleImpact: the impact states, or straight to
// NDrone2_PunchImpact, NDrone2_BulletImpact or NDrone2_ExplosiveImpact. Those alert the drone, take the damage
// (NDrone2_DoHitEffects, NDrone2_HitDamage: by the bone hit, the armour and the difficulty), tell the others it is
// hurt, and go on to a death, an impact anim or the next state.

#include "NDrone2_Damage.h"
#include "../obj/control.h"
#include "DroneTuning.h"
#include "BOT.h"
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_*, Atan2, Vec_AngleDifference (ours)
#include "../mp/multiplayer.h"          // MPSettings, MP_areObjectsOnSameTeam, MP_IsAssasin, MP_IsTarget (ours)
#include "../obj/Gas.h"
#include "../obj/Hurt.h"                // Hurt_GetDamage, Hurt_GetType (ours)
#include "../sp/PlayerStats.h"          // PlrStat_LogShotHitEnemy (ours)
#include "../../sound/Sound.h"          // Sound_Play3D
#include "NDrone2_Opponent.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include "../obj/Env.h"
#include "../../math/X87Ops.h"

// Drone_tag.armour bits
#define ARMOUR_VEST 1
#define ARMOUR_JACKET 2
#define ARMOUR_HELMET 4
#define ARMOUR_COMBAT 8

// The bones NDrone2_HitDamage tells apart (HITDATA_tag.hitBoneIdx)
#define BONE_TORSO 1
#define BONE_HEAD 5

// Drone_tag.flags: has told the others it is hurt; flags2: hurt
#define DRONE_FLAG_HURT_SENT 8
#define DRONE_FLAG2_HURT 2
// Drone_tag.flags: attacking (DroneFunc_CalmOnImpact)
#define DRONE_FLAG_ATTACK_MODE 0x4000000
#define DRONE_FLAG_2000000 0x2000000

// The message NDrone2_DroneAlertToPosition sends for a hurt drone (DRONE_PROP_ALERTED_BY_DRONE_SHOT's)
#define HURT_ALERT_MSG 0x12

// A shot pushes the drone this far along it, to this far at most
#define PUSH_PER_SHOT 0.175f

// Degrees per radian, as the original rounds it
static_assert(std::bit_cast<uint32_t>(RAD_TO_DEG) == 0x42652ee0, "not the original's 180/pi");
static_assert(std::bit_cast<uint32_t>(PUSH_PER_SHOT) == 0x3e333333, "not the original's 0.175");
static_assert(std::bit_cast<uint32_t>(0.333f) == 0x3eaa7efa, "not the original's 0.333");

// A state an impact may return to: any but none and the impact states themselves (FUN_0003a3f0, which takes the
// state in EAX; our name)
static bool DroneFunc_CanResumeState(uint state) {
    return state != 0 && (state < DSTATE_PunchImpact || state > DSTATE_BulletImpact);
}

// ---- the collision's hits

// Where the player touched the drone, as a direction from it, in its own space, and as a rotation. 0x00036350 in the
// original (our name), which takes the drone in ESI and is reached only from NDrone2_DealWithObjHit.
static bool NDrone2_PlayerContact(Drone_tag *drone, HITDATA_tag *hit) {
    if (drone == NULL)
        return false;
    Vec_Zero(&drone->contactLocal);
    Vec_Zero(&drone->contactRot);
    if (hit == NULL)
        return false;
    Vec_Subtract(&hit->hitPosition, &drone->gameObj->position, &drone->contactDir);
    _MATRIX inverse;
    Mat_Inverse(&drone->gameObj->transformMatrix, &inverse);
    ApplyMatrixLV(&inverse, &drone->contactDir, &drone->contactLocal);
    Vec_DirToRot(&drone->contactLocal, &drone->contactRot);
    return true;
}

// A hurt volume's damage, outside CastleExterior and for any skin class but 19; a drone left with none dies.
// 0x000363d0 in the original (our name), which takes the drone in EAX and is reached only from NDrone2_DealWithObjHit.
static void NDrone2_HurtVolumeDamage(Drone_tag *drone, HITDATA_tag *hit) {
    if (GameState.CurrentLevelHashcode == HT_Level_CastleExterior || drone->skinClass == SKINCLASS_KIKO_COMBAT)
        return;
    drone->health = drone->health - Hurt_GetDamage(hit->hitObj);
    if (drone->health <= 0.0f)
        Drone_SM_SetState(&drone->sm, DSTATE_Death_Anim, 0);
}

// The smoke where a shot hits the drone's body: a small puff for weapons 0x32 and 0x33, none for 0x2b and 0x34-0x37,
// and for the rest a larger one if the weapon does damage. Ghidra's name; the original takes the bullet in EAX, the
// hit in EDX and the object in ESI, and is reached only from NDrone2_DealWithObjHit. The puff is not checked for NULL.
static void NDrone2_BodyHitEffect(BU_tag *bullet, HITDATA_tag *hit, obj_tag *obj) {
    switch ((ushort)bullet->wpnDef->weaponVariantNum) {
    case 0x2b:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
        break;
    case 0x32:
    case 0x33:
        Gas_Init(obj->inCel, &hit->hitPosition, NULL, NULL, 1.0f, 16.0f, HT_Entity_taser_muzzle, 5, 0xffffffff,
                 0xffffff00,
                 0x3c);
        break;
    default:
        if (bullet->wpnDef->damage > 0.0f) {
            obj_tag *smoke = Gas_Init(obj->inCel, &hit->hitPosition, NULL, NULL, 3.0f, 6.0f, HT_Entity_BodyHit, 5,
                                      0xffffffff, 0xffffff00, 0x3c);
            smoke->effectFlags |= 8;
        }
        break;
    }
}

// A shot that does damage: the drone counts it, keeps who fired it, and is sent the bullet impact. FUN_00031f20 in
// Ghidra; the name is the PS2 build's.
// FUNC_AT(0x00031f20)
void Drone_BulletHit(obj_tag *obj, BU_tag *bullet, HITDATA_tag *hit) {
    if (obj == NULL || (obj->flags & 1))
        return;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    if (drone == NULL)
        return;
    if (hit->dmgAmt <= 0.0f)
        return;
    drone->hitsTaken++;
    drone->lastAttacker = bullet->firedByObj;
    Drone_Message(obj, DRONE_MSG_ImpactBullet, hit, 0);
}

// An enemy's hit counts for the player (PlrStat_LogShotHitEnemy) when its last attacker is its opponent.
// FUN_0003a240 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_LogHitByOpponent(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->lastAttacker == drone->opponent && drone->side == 1)
        PlrStat_LogShotHitEnemy(0);
}

// The same, and also while the enemy lives. FUN_0003a270 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
void NDrone2_LogHitByOpponentOrAlive(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->lastAttacker != drone->opponent && drone->health <= 0.0f)
        return;
    if (drone->side == 1)
        PlrStat_LogShotHitEnemy(0);
}

// An explosion's hit: kept as the drone's last attacker, and the impact sent on, but for a skin class 16 its own
// AUTOINJECT
void Drone_ExplosiveHit(obj_tag *obj, DroneImpact_tag *impact) {
    if (obj == NULL || obj->extraObjectData == NULL || impact == NULL)
        return;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    drone->lastAttacker = impact->source;
    if (drone->skinClass == SKINCLASS_ROOK && obj == impact->source)
        return;
    Drone_Message(obj, DRONE_MSG_ImpactExplosive, impact, 0);
}

// One hit from the drone's collision. Water underfoot ripples while it walks; an astronaut that hits anything else
// bounces back. Then by what hit it: the player's touch (message 0x22), a hurt volume's damage, smoke (message 0x17),
// or a shot from someone who may hurt it - the player and gun turrets always, a drone of the other side, and with bots
// anyone off its team or with friendly fire on. A shot puffs smoke, pushes the drone and sends it the punch, taser,
// stun dart or bullet impact by the weapon; a few weapons do nothing at all.
void NDrone2_DealWithObjHit(Drone_tag *drone, obj_tag *obj, HITDATA_tag *hit) {
    if (hit == NULL)
        return;

    uint material = hit->materialType & 0x3f;
    if (material >= 13 && material <= 14) {
        if (drone->gameObj->animState->animFlags & 4)
            Env_WaterRipple(hit);
    } else if (drone->dtype == DTYPE_Astronaut && (drone->gameObj->animState->animFlags & 0x10)) {
        Vec_MulR32(&drone->velocity, &drone->velocity, -1.0f);
    }

    if (hit->hitObj == NULL)
        return;
    drone->lastHit = hit;

    switch (hit->hitObj->objectType) {
    case OBJECTTYPE_DRONE: {
        DCVars_tag dcv;
        Drone_DCVfromOBJ(obj, &dcv);    // nothing is done with it
        return;
    }
    case OBJECTTYPE_PLAYER:
        drone->touchingPlayer = 1;
        NDrone2_PlayerContact(drone, hit);
        Drone_Message(obj, DRONE_MSG_ObjectHit, NULL, 0);
        return;
    case OBJECTTYPE_HURT: {
        if (MPSettings.dronesAreBots == 0) {
            NDrone2_HurtVolumeDamage(drone, hit);
            return;
        }
        if (BOTSTATE_getStateType(drone->sm.curState) == BOTSTATE_CLASS_DEATH)
            return;
        if (GameFlow_GetState() != 2)
            return;
        drone->health = drone->health - Hurt_GetDamage(hit->hitObj);
        Hurt_GetType(hit->hitObj);      // the answer is not used
        DCVars_tag dcv;
        Drone_DCVfromOBJ(obj, &dcv);
        BOT_soundEffect(&dcv, 0x1f);
        if (drone->health <= 0.0f)
            Drone_SM_SetState(&drone->sm, DSTATE_BotDeathAnim, 0);
        return;
    }
    case 0x29:
    case OBJECTTYPE_CREEPWALL:
    case OBJECTTYPE_THIRDCAM_OR_WIRE:
    case OBJECTTYPE_LADDER:
        return;
    case OBJECTTYPE_OCCLUDE:
        Drone_Message(obj, DRONE_MSG_ImpactSmoke, NULL, 0);
        return;
    case OBJECTTYPE_BULLET:
        break;
    default:
        if (drone->dtype == DTYPE_Astronaut) {
            Vec_MulR32(&drone->velocity, &drone->velocity, 0.0f);
            _VECTOR away;
            Vec_Subtract(&drone->gameObj->position, &hit->hitPosition, &away);
            Vec_Normalise(&away, &away);
            auxVec_AddMulR32(&drone->velocity, &away, 1.0f, &drone->velocity);
        }
        return;
    }

    BU_tag *bullet = (BU_tag *)hit->hitObj->extraObjectData;
    if (bullet == NULL)
        return;
    switch ((ushort)bullet->wpnDef->weaponVariantNum) {
    case 0x1b:
    case 0x2a:
    case 0x2b:
    case 0x2e:
    case 0x2f:
    case 0x30:
    case 0x31:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x3a:
    case 0x46:
    case 0x48:
    case 0x49:
    case 0x4e:
    case 0x50:
    case 0x51:
    case 0x5f:
        return;
    case 0x6e:
        if (drone->skinClass == SKINCLASS_ROOK)
            return;
        break;
    }

    obj_tag *shooter = bullet->firedByObj;
    if (shooter != NULL) {
        bool hurts = false;
        if (MPSettings.dronesAreBots != 0) {
            if (shooter->objectType == OBJECTTYPE_PLAYER || shooter->objectType == OBJECTTYPE_DRONE) {
                if (MPSettings.FriendlyFire != 0 || !MP_areObjectsOnSameTeam(shooter, drone->gameObj))
                    hurts = true;
            } else {
                hurts = true;
            }
        } else if (shooter->objectType == OBJECTTYPE_PLAYER || shooter->objectType == OBJECTTYPE_GUNTURRET) {
            hurts = true;
        } else if (shooter->objectType == OBJECTTYPE_DRONE) {
            // Enemies (and drones of no side) hurt allies and civilians, and the other way round
            bool enemy = drone->side != 2 && drone->side != 3;
            Drone_tag *shooterDrone = (Drone_tag *)shooter->extraObjectData;
            if (shooterDrone != NULL) {
                if (shooterDrone->side == 1)
                    hurts = !enemy;
                else if (shooterDrone->side == 2 || shooterDrone->side == 3)
                    hurts = enemy;
            }
        }
        if (drone->health <= 0.0f || !hurts)
            return;
    }

    NDrone2_BodyHitEffect(bullet, hit, obj);
    _VECTOR push;
    Vec_MulR32(&push, &hit->hitDirection, PUSH_PER_SHOT);
    Vec_Add2(&drone->pushOffset, &push, &drone->pushOffset);
    double pushed = Vec_NormaliseLen(&drone->pushOffset, &drone->pushOffset);
    float length = (float)pushed;
    if (pushed > PUSH_PER_SHOT)
        length = PUSH_PER_SHOT;
    Vec_MulR32(&drone->pushOffset, &drone->pushOffset, length);

    uint msgType;
    switch ((ushort)bullet->wpnDef->weaponVariantNum) {
    case 1:
    case 0x4b:
    case 0x4d:
        msgType = DRONE_MSG_ImpactPunch;
        break;
    case 0x4a:
    case 0x4c:
        msgType = DRONE_MSG_ImpactTaser;
        break;
    case 0x43:
    case 0x44:
        msgType = DRONE_MSG_ImpactStunDart;
        break;
    default:
        Drone_BulletHit(obj, bullet, hit);
        return;
    }
    drone->lastAttacker = bullet->firedByObj;
    drone->hitsTaken++;
    Drone_Message(obj, msgType, hit, 0);
}

// ---- the damage

// Takes the damage of a hit on the bone `location` (punches and tasers to the head count as body hits, as does
// anything but the head in SpaceStationD): the head ten times over by default (Ninjas, the 0x50000ba skin and skin
// class 16 in EvilBase take twice the body's instead), then the armour and the difficulty. A drone not vulnerable
// takes none unless forced. Bots take it through BOT_handlePain. A hit that leaves it alive makes it cry out.
// AUTOINJECT
void NDrone2_HitDamage(DCVars_tag *dcv, float damage, int location, int weapon, char force) {
    if (MPSettings.dronesAreBots != 0) {
        BOT_handlePain(dcv, damage, 0, location);
        return;
    }

    Drone_tag *drone = dcv->drone;
    bool logHit = true;
    if (location < 0) {
        location = BONE_TORSO;
    } else if (location == BONE_HEAD &&
               (weapon == 1 || weapon == 0x4a || weapon == 0x4c || weapon == 0x4b || weapon == 0x4d)) {
        logHit = false;
        location = BONE_TORSO;
    }
    if (GameState.CurrentLevelHashcode == HT_Level_SpaceStationD && location != BONE_HEAD)
        location = BONE_TORSO;

    if (!drone->vulnerable && !force) {
        drone->lastDamage = damage;
        return;
    }

    switch (location) {
    case BONE_HEAD:
        if (drone->dtype == DTYPE_Ninja || drone->skinHashcode == HT_Skin_Drake_Astronaut ||
            (drone->skinClass == SKINCLASS_ROOK && GameState.CurrentLevelHashcode == HT_Level_EvilBase)) {
            damage = (float)(DroneDamage_Torso * (double)damage * 2);
        } else {
            drone->headShot = 1;
            damage = DroneDamage_Head * damage;
        }
        if (drone->armour & ARMOUR_HELMET)
            damage = DroneArmour_Helmet * damage;
        if (logHit)
            NDrone2_LogHitByOpponent(dcv);
        break;
    case 0x14:
    case 0x15:
    case 0x17:
    case 0x20:
    case 0x23:
    case 0x27:
        // the arms
        damage = DroneDamage_Arms * damage;
        if (drone->armour & ARMOUR_COMBAT)
            damage = DroneArmour_Combat * damage;
        if (drone->armour & ARMOUR_JACKET)
            damage = DroneArmour_Jacket * damage;
        if (logHit)
            NDrone2_LogHitByOpponentOrAlive(dcv);
        break;
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
        // the legs
        damage = DroneDamage_Legs * damage;
        if (drone->armour & ARMOUR_COMBAT)
            damage = DroneArmour_Combat * damage;
        if (logHit)
            NDrone2_LogHitByOpponentOrAlive(dcv);
        break;
    default:
        damage = DroneDamage_Torso * damage;
        if (drone->armour & ARMOUR_COMBAT)
            damage = DroneArmour_Combat * damage;
        if (drone->armour & ARMOUR_JACKET)
            damage = DroneArmour_Jacket * damage;
        if (drone->armour & ARMOUR_VEST)
            damage = DroneArmour_Vest * damage;
        if (logHit)
            NDrone2_LogHitByOpponent(dcv);
        break;
    }

    float difficulty;
    if (GameState.difficultyModifier == 1)
        difficulty = DroneDamage_Easy;
    else if (GameState.difficultyModifier == 3 || GameState.difficultyModifier == 4)
        difficulty = DroneDamage_Hard;
    else
        difficulty = DroneDamage_Normal;
    damage = difficulty * damage;

    if (drone->vulnerable)
        dcv->drone->health = dcv->drone->health - damage;
    if (dcv->drone->health > 0.0f)
        NDrone2_PainTalk(dcv);
    drone->lastDamage = damage;
}

// A shot's damage, times scale, on the bone it hit; a skin class 12 drone shot by the player is hurt even when not
// vulnerable. With bots, a bot's traits strengthen its shots (fists and weapons 0x64-0x66 by half, the rest by a
// quarter), and in Assassin a shot by the assassin at the target kills it, in the head or from behind (within 70
// degrees of its facing) in bones 1-4. A shot over 0.5 that neither bounces nor sticks sounds its impact. The weapon is
// read for the sound without the NULL check the damage has.
// AUTOINJECT
void NDrone2_DoHitEffects(DCVars_tag *dcv, HITDATA_tag *hit, float scale) {
    Drone_tag *drone = dcv->drone;
    obj_tag *gameObj = dcv->gameObj;
    if (hit == NULL || hit->hitObj == NULL)
        return;
    BU_tag *bullet = (BU_tag *)hit->hitObj->extraObjectData;
    if (bullet == NULL)
        return;

    if (MPSettings.dronesAreBots != 0) {
        if (Control_Plr2Ind(bullet->firedByObj) >= 4 && bullet->firedByObj != NULL) {
            BOT_vars_t *bot = ((Drone_tag *)bullet->firedByObj->extraObjectData)->botVars;
            switch (bullet->wpnDef->weaponVariantNum) {
            case 1:
            case 0x64:
            case 0x65:
            case 0x66:
                if (bot->stats.traitFlags & 4)
                    hit->dmgAmt = hit->dmgAmt * 1.5f;
                break;
            default:
                if (bot->stats.traitFlags & 2)
                    hit->dmgAmt = hit->dmgAmt * 1.25f;
                break;
            }
        }
        if (MPSettings.GameMode == GM_ASSASSIN && MP_IsAssasin(bullet->firedByObj) && MP_IsTarget(gameObj)) {
            ushort bone = hit->hitBoneIdx;
            if (bone >= 1 && bone <= 4) {
                float shotYaw = Atan2(hit->hitDirection.x, hit->hitDirection.z);
                double degrees = Vec_AngleDifference(shotYaw, dcv->gameObj->rotation.y) * (double)RAD_TO_DEG;
                if (fabs(degrees) < 70.0f)
                    hit->dmgAmt = drone->health;
            } else if (bone == BONE_HEAD) {
                hit->dmgAmt = drone->health;
            }
        }
    }

    char force = 0;
    if (bullet->firedByObj != NULL && drone->skinClass == SKINCLASS_ZOE && bullet->firedByObj->objectType == OBJECTTYPE_PLAYER)
        force = 1;
    int weapon = bullet->wpnDef != NULL ? (ushort)bullet->wpnDef->weaponVariantNum : -1;
    NDrone2_HitDamage(dcv, scale * hit->dmgAmt, (ushort)hit->hitBoneIdx, weapon, force);

    if (hit->dmgAmt > 0.5f && !(bullet->wpnDef->impactFlags & 0x180))
        Sound_Play3D(hit->hitBoneIdx == BONE_HEAD ? SFX_IMPACT_DRONE_HEAD_SHOT : SFX_IMPACT_DRONE_BODY,
                     &hit->hitPosition, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (MPSettings.dronesAreBots != 0)
        MP_RegisterBulletHit((ushort)drone->botVars->playerIndex, hit->hitObj, drone->health);
}

// Once, a drone with property 0x32 tells those around it that it is hurt; never a sniper.
// AUTOINJECT
void DroneFunc_SendHurtMessage(DCVars_tag *dcv) {
    if (!behaviour_util_getProperty(DRONE_PROP_HURT_ALERTS, dcv->drone->currentBehaviour))
        return;
    Drone_tag *drone = dcv->drone;
    if ((drone->flags & DRONE_FLAG_HURT_SENT) || drone->dtype == DTYPE_Sniper)
        return;
    drone->flags2 |= DRONE_FLAG2_HURT;
    NDrone2_DroneAlertToPosition(dcv->gameObj, HURT_ALERT_MSG, &dcv->gameObj->position, dcv->gameObj->inCel, 0, 0,
                                 0);
    dcv->drone->flags |= DRONE_FLAG_HURT_SENT;
}

// ---- the impacts, as the states take them

// The death a punch gives
static DSTATE NDrone2_PunchDeath(Drone_tag *drone) {
    return drone->dtype == DTYPE_Bot ? DSTATE_BotDeathAnim : DSTATE_KnockedOut_Anim;
}

// A punch: alerts the drone and takes its damage. Unarmed drones punched in CastleIndoors1, and non-enemies punched
// unarmed outside TowerA and TowerB, fail the mission. One left with no health is knocked out (an enemy before it tells
// the others it is hurt, the rest after). With `anim` it plays the punch impact anim and goes back to the state it
// resumes; otherwise it goes to nextState unless that is the state it is in (or an impact state). True when it has
// moved on.
// AUTOINJECT
bool NDrone2_PunchImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim) {
    Drone_tag *drone = dcv->drone;
    if (drone->impactImmune)
        return true;

    uint resume = nextState != 0 ? nextState : drone->sm.curState;
    if (nextState == drone->sm.curState)
        nextState = 0;
    if (DroneFunc_CanResumeState(resume))
        drone->sm.resumeState = resume;

    dcv->drone->alertness = 1.0f;
    Drone_AlertStatusSet(2, dcv);
    NDrone2_DoHitEffects(dcv, hit, dcv->drone->hitDamageScale);
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && !NDrone2_ObjectIsArmed(drone->gameObj))
        DroneFunc_SetMissionFailReason(drone, 5);

    if (dcv->drone->side != 1) {
        if ((GameState.CurrentLevelHashcode < HT_Level_TowerA || GameState.CurrentLevelHashcode > HT_Level_TowerB) &&
            !NDrone2_ObjectIsArmed(drone->gameObj))
            DroneFunc_SetMissionFailReason(drone, 6);
        DroneFunc_SendHurtMessage(dcv);
        if (drone->health <= 0.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, NDrone2_PunchDeath(dcv->drone), 0);
    } else {
        if (drone->health <= 0.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, NDrone2_PunchDeath(dcv->drone), 0);
        DroneFunc_SendHurtMessage(dcv);
    }

    if (MPSettings.dronesAreBots == 0)
        NDrone2_PainTalk(dcv);
    if (anim) {
        DroneAnim_CallAnim(DANIM_IMPACT_PUNCH, 0, drone->sm.resumeState, 0, 0.0f, 0, dcv);
        drone->impactAnimFrame = GameState.NumFramesUnpaused;
        NPCGlobals.impactAnimFrame = GameState.NumFramesUnpaused;
        return true;
    }
    if (DroneFunc_CanResumeState(nextState))
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)nextState, 0);
    return false;
}

// Whether a shot may play the impact anim: not within 2 s of the drone's last one, or any drone's. Notes on the way
// whether it was a heavy hit. FUN_00036190 in the original (our name), reached only from NDrone2_BulletImpact.
static bool NDrone2_ImpactAnimDue(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    drone->heavyHit = drone->startHealth * (double)0.25f < drone->lastDamage &&
                     drone->startHealth * (double)0.333f < drone->health;
    uint gap = FRAME_RATE_INT * 2;
    if (drone->impactAnimFrame + gap > GameState.NumFramesUnpaused)
        return false;
    return GameState.NumFramesUnpaused >= NPCGlobals.impactAnimFrame + gap;
}

// A bullet: alerts the drone, takes the damage and tells the others it is hurt. One left with no health dies (abseilers
// and astronauts their own way). Otherwise, with `anim` and no bots, it plays the impact anim for where it was hit when
// it may, resuming afterwards, and answers false: the caller then goes to the bullet impact state. Ambushers waiting
// attack, and in UnderCoverIdle leave cover now. Then nextState, unless that is the state it is in (or an impact
// state).
// AUTOINJECT
bool NDrone2_BulletImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim) {
    Drone_tag *drone = dcv->drone;
    uint resume = nextState != 0 ? nextState : drone->sm.curState;
    if (nextState == drone->sm.curState)
        nextState = 0;

    drone->alertness = 1.0f;
    Drone_AlertStatusSet(2, dcv);
    NDrone2_DoHitEffects(dcv, hit, dcv->drone->hitDamageScale);
    DroneFunc_SendHurtMessage(dcv);
    if (drone->health <= 0.0f) {
        if (dcv->drone->dtype == DTYPE_Bot)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDeathAnim, 0);
        switch (drone->dtype) {
        case DTYPE_Abseil27:
        case DTYPE_Abseil28:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilDeath, 0);
        case DTYPE_Astronaut:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautDeath, 0);
        default:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
        }
    }

    DroneFunc_InjuredTalk(dcv);
    if (MPSettings.dronesAreBots == 0 && anim && NDrone2_ImpactAnimDue(dcv)) {
        if (DroneFunc_CanResumeState(resume))
            drone->sm.resumeState = resume;
        DroneAnim_LocationImpactAnim(dcv);
        drone->impactAnimFrame = GameState.NumFramesUnpaused;
        NPCGlobals.impactAnimFrame = GameState.NumFramesUnpaused;
        return false;
    }

    if (dcv->drone->dtype == DTYPE_Ambush) {
        switch (dcv->aiStateMachine->curState) {
        case DSTATE_AmbushWait:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        case DSTATE_UnderCoverIdle:
            behaviour_util_setProperty(DRONE_PROP_AMBUSH, dcv->drone->currentBehaviour, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        }
    }
    if (DroneFunc_CanResumeState(nextState))
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)nextState, 0);
    return true;
}

// An explosion: the 0x50000ba skin is not hurt by its own. Otherwise it alerts the drone and takes the damage (to the
// body; the hit scale is not applied). One left with no health dies (by the explosion, unless a Ninja, abseiler or
// astronaut). A drone with property 0x32 tells the others it is hurt, once (a sniper too). With `anim` it plays the
// explosion impact anim, turned by where the blast came from, and resumes - not Ninjas, astronauts or skin class 20;
// bots go on to BotAttack. Otherwise nextState, unless that is the state it is in (or an impact state). The impact is
// read before any NULL check.
// AUTOINJECT
bool NDrone2_ExplosiveImpact(DCVars_tag *dcv, DroneImpact_tag *impact, uint nextState, char anim) {
    Drone_tag *drone = dcv->drone;
    uint resume = nextState != 0 ? nextState : drone->sm.curState;
    if (nextState == drone->sm.curState)
        nextState = 0;

    if (impact->source == drone->gameObj && drone->skinHashcode == HT_Skin_Drake_Astronaut) {
        if (DroneFunc_CanResumeState(nextState))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)nextState, 0);
        return true;
    }

    drone->alertness = 1.0f;
    Drone_AlertStatusSet(2, dcv);
    // The damage: FUN_0003db50 in the original, which is handed hitDamageScale but does not use it
    if (impact != NULL && impact->damage > 0.0f) {
        NDrone2_HitDamage(dcv, impact->damage, 0, -1, 0);
        NDrone2_LogHitByOpponentOrAlive(dcv);
        if (MPSettings.dronesAreBots != 0)
            MP_RegisterBulletHit((ushort)dcv->drone->botVars->playerIndex, impact->source, dcv->drone->health);
    }

    if (drone->health <= 0.0f) {
        if (MPSettings.dronesAreBots != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotDeathByExplosion, (intptr_t)impact);
        switch (drone->dtype) {
        case DTYPE_Ninja:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Death_Anim, 0);
            return true;
        case DTYPE_Abseil27:
        case DTYPE_Abseil28:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AbseilDeath, 0);
            return true;
        case DTYPE_Astronaut:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AstronautDeath, 0);
            return true;
        default:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_DeathByExplosion, (intptr_t)impact);
        }
    }

    if (behaviour_util_getProperty(DRONE_PROP_HURT_ALERTS, dcv->drone->currentBehaviour) &&
        !(dcv->drone->flags & DRONE_FLAG_HURT_SENT)) {
        dcv->drone->flags2 |= DRONE_FLAG2_HURT;
        NDrone2_DroneAlertToPosition(dcv->gameObj, HURT_ALERT_MSG, &dcv->gameObj->position, dcv->gameObj->inCel, 0,
                                     0, 0);
        dcv->drone->flags |= DRONE_FLAG_HURT_SENT;
    }

    if (anim) {
        if (dcv->drone->dtype == DTYPE_Bot) {
            DroneAnim_CallAnim(DANIM_IMPACT_EXPLOSION, NDrone2_ExplosionDir(dcv, impact), DSTATE_BotAttack, 0, 0.0f, 0,
                               dcv);
            return true;
        }
        if (DroneFunc_CanResumeState(resume))
            drone->sm.resumeState = resume;
        if (drone->dtype == DTYPE_Astronaut || drone->skinClass == SKINCLASS_DRAKE || drone->dtype == DTYPE_Ninja)
            return true;
        DroneAnim_CallAnim(DANIM_IMPACT_EXPLOSION, NDrone2_ExplosionDir(dcv, impact), drone->sm.resumeState, 0, 0.0f, 0,
                           dcv);
        NDrone2_PainTalk(dcv);
        return true;
    }

    if (MPSettings.dronesAreBots == 0)
        NDrone2_PainTalk(dcv);
    if (DroneFunc_CanResumeState(nextState))
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)nextState, 0);
    return false;
}

// In TowerA and TowerB an impact takes an attacking drone out of its attack: back to a civilian that runs for the
// alarm, resuming at ReturnToPatrolPath; one not attacking resumes at CivilianPatrol. FUN_0003a410 in the original
// (our name), reached only from DroneFunc_HandleImpact.
static void DroneFunc_CalmOnImpact(DCVars_tag *dcv) {
    if (GameState.CurrentLevelHashcode != HT_Level_TowerA && GameState.CurrentLevelHashcode != HT_Level_TowerB)
        return;
    Drone_tag *drone = dcv->drone;
    if (!(drone->flags & DRONE_FLAG_ATTACK_MODE)) {
        dcv->aiStateMachine->resumeState = DSTATE_CivilianPatrol;
        return;
    }
    drone->flags &= ~DRONE_FLAG_2000000;
    dcv->drone->secondState = DSTATE_RunToAlarm;
    dcv->drone->unknown16 = 0;
    dcv->drone->flags2 = 0;
    if (dcv->drone->diVars.keys.mode == 100) {
        dcv->drone->currentBehaviour = dcv->drone->behaviour1;
        dcv->drone->usingSecondBehaviour = 0;
    }
    dcv->drone->dtype = DTYPE_Civilian;
    dcv->drone->dtype2 = DTYPE_RunToPoint;
    dcv->drone->flags &= ~DRONE_FLAG_ATTACK_MODE;
    dcv->drone->initialState = DSTATE_CivilianPatrol;
    dcv->aiStateMachine->resumeState = DSTATE_ReturnToPatrolPath;
}

// How a state takes an impact message, returning to nextState (0: the state it is in) afterwards; `anim` asks for the
// impact states and their anims rather than the impact taken where it stands. A punch, an explosion and a bullet go
// to their impact functions or states. Taser hits add up, each a frame's worth: past twice the health the drone is
// tasered, and on the first it goes to nextState unless attacking. Smoke (unless property 0x34 or 0x36), stun grenades
// (resisted with property 0x1c, flinching with anim set 5) and stun darts go to their states. Always true.
// AUTOINJECT
bool DroneFunc_HandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char anim) {
    Drone_tag *drone = dcv->drone;
    if (drone->impactImmune)
        return true;

    uint next = nextState == drone->sm.curState ? 0 : nextState;
    uint resume = nextState != 0 ? nextState : drone->sm.curState;

    switch (msg->msgType) {
    case DRONE_MSG_ImpactPunch:
        if (anim) {
            if (DroneFunc_CanResumeState(resume))
                drone->sm.resumeState = resume;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_PunchImpact, (intptr_t)msg->extraData);
            return true;
        }
        NDrone2_PunchImpact(dcv, (HITDATA_tag *)msg->extraData, resume, 0);
        return true;

    case DRONE_MSG_ImpactTaser: {
        if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && !NDrone2_ObjectIsArmed(drone->gameObj))
            DroneFunc_SetMissionFailReason(drone, 7);
        dcv->drone->stunFrames = Ftol(dcv->drone->stunFrames + (double)FRAME_RATE_MUL);
        Drone_tag *stunned = dcv->drone;
        if (stunned->stunFrames > (uint)Ftol(stunned->health) * 2) {
            if (DroneFunc_CanResumeState(resume))
                drone->sm.resumeState = resume;
            DroneFunc_CalmOnImpact(dcv);
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Taser, (intptr_t)msg->extraData);
        } else if (stunned->stunFrames == 1 && !(stunned->flags & DRONE_FLAG_ATTACK_MODE) && next != 0) {
            Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)next, 0);
        }
        return true;
    }

    case DRONE_MSG_ImpactBullet: {
        uint state = nextState;
        if (drone->skinClass == SKINCLASS_ZOE)
            state = DSTATE_CrouchCover;
        bool resumable = DroneFunc_CanResumeState(resume);
        if (resumable)
            state = resume;
        if (NDrone2_BulletImpact(dcv, (HITDATA_tag *)msg->extraData, state, anim))
            return true;
        if (resumable)
            drone->sm.resumeState = resume;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BulletImpact, (intptr_t)msg->extraData);
        return true;
    }

    case DRONE_MSG_ImpactExplosive:
        if (anim) {
            if (DroneFunc_CanResumeState(resume))
                drone->sm.resumeState = resume;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_ExplosiveImpact, (intptr_t)msg->extraData);
            return true;
        }
        NDrone2_ExplosiveImpact(dcv, (DroneImpact_tag *)msg->extraData, nextState, 0);
        return true;

    case DRONE_MSG_ImpactSmoke:
        if (behaviour_util_getProperty(DRONE_PROP_36, drone->currentBehaviour))
            return true;
        if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_SMOKE, dcv->drone->currentBehaviour))
            return true;
        if (DroneFunc_CanResumeState(resume))
            drone->sm.resumeState = resume;
        DroneFunc_CalmOnImpact(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SmokedOut, (intptr_t)msg->extraData);
        return true;

    case DRONE_MSG_ImpactStunGrenade:
        if (behaviour_util_getProperty(DRONE_PROP_RESISTS_STUN_GRENADE, drone->currentBehaviour)) {
            if (drone->animSet == 5) {
                if (DroneAnim_CanDoAnimState(dcv, 0x57, 0))
                    DroneAnim_CallAnim(0x57, 0, 0, 0, 0.0f, 0, dcv);
                else if (DroneAnim_CanDoAnimState(dcv, 0x11, 0))
                    DroneAnim_CallAnim(0x11, 0, 0, 0, 0.0f, 0, dcv);
                else
                    DroneAnim_CallAnim(0x19, 0, 0, 0, 0.0f, 0, dcv);
            }
            return true;
        }
        if (DroneFunc_CanResumeState(resume))
            drone->sm.resumeState = resume;
        DroneFunc_CalmOnImpact(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StunGrenadeImpact, (intptr_t)msg->extraData);
        return true;

    case DRONE_MSG_ImpactStunDart:
        PlrStat_LogShotHitEnemy(0);
        if (DroneFunc_CanResumeState(resume))
            drone->sm.resumeState = resume;
        DroneFunc_CalmOnImpact(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_StunDartImpact, 0);
        return true;
    }
    return true;
}
