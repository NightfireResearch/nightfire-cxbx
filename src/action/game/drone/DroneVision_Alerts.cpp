// What a drone does about alerts. Its states hand each message to DroneVision_EnemyAlerts, which answers the state to
// go to (0 for none): a sound heard (DroneVision_AlertSound), the global alarm, and the shouts of other drones, which
// come to nothing. Each frame DroneVision_EnemyLookForOpponent says whether a drone that sees its opponent attacks,
// and DroneFunc_HandleSoundAlerts takes in what it hears. Also the armed enemies an ally or hostage looks out for,
// and the interrogation flags. docs/drone/architecture/README.md has the fields.

#include "DroneVision_Alerts.h"
#include "NDrone2.h"
#include "Behaviour.h"
#include "DroneTables.h"                // DRONE_PROP
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_Copy, Vec_Dist3D (ours)
#include "../../sound/Sound.h"          // DYNAMICSOUNDS
#include "../mp/multiplayer.h"          // MPSettings, MP_areObjectsOnSameTeam
#include "../obj/build.h"               // build_FindCel
#include "DroneVision.h"

#include <bit>
#include <stdint.h>

#pragma fp_contract(off)

// Originals called by address
#define MPDrone_MaybeIsDyingOrDead ((bool (__cdecl *)(obj_tag *obj))0x0001a190)
#define BOT_reactToDroneAlertMsg ((bool (__cdecl *)(uint msgType, Drone_tag *drone, void *extra))0x0001a260)
// Returns in ST0; the level and the loudest sound through the pointers
#define Sound_Alertness ((float (__cdecl *)(_VECTOR *pos, float *level, DYNAMICSOUNDS **sound))0x000cbb10)


// While set, no drone sees or hears anything (the cheat)
#define switch_BLIND_DRONES U32_AT(0x001df9a0)

// The target bit of a drone's soundTarget
#define AITARGET_SOUND 0x4000000

// The bone NDrone2_CanSeeObject looks for
#define BONE_HEAD 5

constexpr float kHearingRange = 50.0f;
constexpr float kSoundAlertnessScale = 0.01f;
static_assert(std::bit_cast<uint32_t>(kSoundAlertnessScale) == 0x3c23d70a, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.7f) == 0x3f333333, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.66f) == 0x3f28f5c3, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(0.2f) == 0x3e4ccccd, "not the original's constant");

// ---- interrogation

// The name is the PS2 build's
// AUTOINJECT
void DroneVision_BeginInterogate(Drone_tag *drone) {
    drone->flags2 |= DRONE_FLAG2_INTEROGATING;
    NPCGlobals.unknown1392 = 1;
}

// AUTOINJECT
void DroneVision_EndInterogate(Drone_tag *drone) {
    drone->flags2 = (drone->flags2 & ~DRONE_FLAG2_INTEROGATING) | DRONE_FLAG2_INTEROGATED;
    if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors1)
        NPCGlobals.unknown1392 = 0;
}

// AUTOINJECT
void DroneVision_SetInterogateAssist(Drone_tag *drone) {
    drone->flags2 |= DRONE_FLAG2_INTEROGATING;
}

// AUTOINJECT
void DroneVision_EndInterogateAssist(Drone_tag *drone) {
    drone->flags2 = (drone->flags2 & ~(DRONE_FLAG2_INTEROGATING | 0x200000)) | DRONE_FLAG2_INTEROGATED;
    drone->alertedBy = NULL;
}

// False while the drone that alerted this one still interrogates: its flags2 is 0x20 or 0x21 (the test takes 0x200000
// too, but nothing else may be set). The name is the PS2 build's.
// AUTOINJECT
bool DroneVision_TestEndInterogationAssist(Drone_tag *drone) {
    Drone_tag *interogator = drone->alertedBy;
    if (interogator != NULL && (interogator->flags2 & 0x200020) && !(interogator->flags2 & ~0x21u))
        return false;
    return true;
}

// ---- armed enemies

// Whether an enemy drone other than this one, active and armed, within radius, is in sight; it is kept in
// nearArmedDrone. With check set, only for a drone with DRONE_PROP_50.
// AUTOINJECT
bool NDrone2_NearArmedDrone(DCVars_tag *dcv, float radius, char check) {
    Drone_tag *self = dcv->drone;
    self->nearArmedDrone = NULL;
    if (check && behaviour_util_getProperty(DRONE_PROP_50, dcv->drone->currentBehaviour) == 0)
        return false;

    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other == self)
            continue;
        obj_tag *obj = other->gameObj;
        if (obj == NULL || MPDrone_MaybeIsDyingOrDead(obj))
            continue;
        if (other->flags & 0x4200)
            continue;
        if (!NDrone2_ObjectIsArmed(obj) || other->side != 1)
            continue;
        if (Vec_Dist3D(&dcv->gameObj->position, &obj->position) > radius)
            continue;
        if (NDrone2_CanSeeObject(dcv->drone, obj, BONE_HEAD, 0)) {
            self->nearArmedDrone = obj;
            return true;
        }
    }
    return false;
}

// True unless an enemy drone other than this one, active, alive and armed, within radius, is the one
// NDrone2_NearArmedDrone found or is in sight. The name is the PS2 build's.
// AUTOINJECT
bool NDrone2_NotNearArmedDrone(DCVars_tag *dcv, float radius) {
    for (Drone_tag *other = NPCGlobals.NDrone2List; other != NULL; other = other->next) {
        if (other == dcv->drone)
            continue;
        obj_tag *obj = other->gameObj;
        if (other->flags & 0x4200)
            continue;
        if (other->health <= 0.0f)
            continue;
        if (!(other->flags & 0x100))
            continue;
        if (obj == NULL || !NDrone2_ObjectIsArmed(obj) || other->side != 1)
            continue;
        if (Vec_Dist3D(&obj->position, &dcv->gameObj->position) > radius)
            continue;
        if (obj == dcv->drone->nearArmedDrone)
            return false;
        if (NDrone2_CanSeeObject(dcv->drone, obj, BONE_HEAD, 0))
            return false;
    }
    return true;
}

// ---- alerts

// A sound heard (DRONE_MSG_HeardNoise), by a drone with hearing (DRONE_PROP_1F). A bot answers through
// BOT_reactToDroneAlertMsg. Otherwise nothing beyond 50 m of its opponent, or once it attacks; in EvilBase nothing
// in DSTATE_EnemyMission. The Powerstation guards (skin classes 5 and 6), at any distance, and the civilians and party
// girls past 0.7 alertness, attack; the civilians and party girls do nothing else. The rest go to DSTATE_HeardNoise
// for a loud enough sound or past 0.66 alertness.
ushort _DroneVision_AlertSound(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    bool attack = false;
    NDrone2_Player();
    if (behaviour_util_getProperty(DRONE_PROP_1F, dcv->drone->currentBehaviour) == 0)
        return 0;
    if (drone->flags & DRONE_FLAG_DEAF)
        return 0;
    if (drone->dtype == DTYPE_Bot)
        return BOT_reactToDroneAlertMsg(DRONE_MSG_HeardNoise, drone, NULL) ? DSTATE_BotHeardNoise : 0;

    uint level = GameState.CurrentLevelHashcode;
    if (level >= HT_Level_PowerStationA1 && level <= HT_Level_PowerStationA2 &&
        (drone->skinClass == 5 || drone->skinClass == 6)) {
        attack = true;
    } else {
        if (level == HT_Level_EvilBase && drone->sm.curState == DSTATE_EnemyMission)
            return 0;
        if (drone->distanceToTarget > kHearingRange)
            return 0;
    }
    if (drone->flags & DRONE_FLAG_ATTACK_MODE)
        return 0;

    bool investigates = true;
    switch (drone->dtype) {
    case DTYPE_Civilian:
    case DTYPE_PartyGirl:
    case DTYPE_CivilianGuard:
    case DTYPE_CivDoorGuard:
        if (drone->alertness > 0.7f)
            attack = true;
        investigates = false;
        break;
    }
    if (attack) {
        drone->flags |= DRONE_FLAG_ATTACK_MODE;
        dcv->drone->flags |= DRONE_FLAG_ATTACK_SOUND;
        return DSTATE_Attack;
    }
    if (investigates && (drone->soundAlertStep > 0.2f || drone->alertness > 0.66f))
        return DSTATE_HeardNoise;
    return 0;
}

// The original takes the DCVars in ESI.
// AUTOLTCG
void __declspec(naked) DroneVision_AlertSound(void) {
    _asm {
        push esi
        call _DroneVision_AlertSound
        add esp, 4
        ret
    }
}

// The global alarm (DRONE_MSG_GlobalAlarm): every drone but the civilians and party girls attacks. A bot becomes
// fully alert, and on a team (MP_areObjectsOnSameTeam with no other object) answers through
// BOT_reactToDroneAlertMsg. Our name.
ushort _DroneVision_GlobalAlarm(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->flags & DRONE_FLAG_NO_ALERTS)
        return 0;
    if (drone->dtype == DTYPE_Civilian || drone->dtype == DTYPE_PartyGirl)
        return 0;
    if (drone->dtype == DTYPE_Bot) {
        drone->alertness = 1.0f;
        if (!MP_areObjectsOnSameTeam(drone->gameObj, NULL))
            return 0;
        return BOT_reactToDroneAlertMsg(DRONE_MSG_GlobalAlarm, drone, NULL) ? DSTATE_BotAttack : 0;
    }
    drone->flags |= DRONE_FLAG_ATTACK_MODE;
    dcv->drone->flags |= DRONE_FLAG_ATTACK_ALARM;
    return DSTATE_Attack;
}

// The original takes the DCVars in EDX.
// AUTOLTCG
void __declspec(naked) DroneVision_GlobalAlarm(void) {
    _asm {
        push edx
        call _DroneVision_GlobalAlarm
        add esp, 4
        ret
    }
}

// Another drone's shout, heard by a drone with the property (nightfire-port's DroneVision_ShoutFromOtherDrone, inline
// in each caller on the Xbox). The original finds the drone that shouted from the alert and goes no further: a shout
// changes nothing.
static ushort HearShout(DCVars_tag *dcv, DRONE_PROP property) {
    behaviour_util_getProperty(property, dcv->drone->currentBehaviour);
    return 0;
}

// The four shouts EnemyAlerts tells apart (our names). Each original takes the DCVars in EAX and the alert in ESI.
ushort _DroneVision_ShoutMsg11(DCVars_tag *dcv) {
    return HearShout(dcv, DRONE_PROP_28);
}

ushort _DroneVision_ShoutMsg12(DCVars_tag *dcv) {
    return HearShout(dcv, DRONE_PROP_27);
}

ushort _DroneVision_ShoutMsg13(DCVars_tag *dcv) {
    return HearShout(dcv, DRONE_PROP_4F);
}

ushort _DroneVision_ShoutDroneAlert(DCVars_tag *dcv) {
    return HearShout(dcv, DRONE_PROP_29);
}

// AUTOLTCG
void __declspec(naked) DroneVision_ShoutMsg11(void) {
    _asm {
        push eax
        call _DroneVision_ShoutMsg11
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) DroneVision_ShoutMsg12(void) {
    _asm {
        push eax
        call _DroneVision_ShoutMsg12
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) DroneVision_ShoutMsg13(void) {
    _asm {
        push eax
        call _DroneVision_ShoutMsg13
        add esp, 4
        ret
    }
}

// AUTOLTCG
void __declspec(naked) DroneVision_ShoutDroneAlert(void) {
    _asm {
        push eax
        call _DroneVision_ShoutDroneAlert
        add esp, 4
        ret
    }
}

// The alert messages a state passes on: the state to go to, or 0. First, unless unknown33 or explosiveSeen is set,
// the drone keeps where it was (the state, its feet and cel) to come back to. Message 0x16 is a shout heard by all;
// it changes nothing either.
// AUTOINJECT
ushort DroneVision_EnemyAlerts(DCVars_tag *dcv, MsgObject *msg) {
    Drone_tag *drone = dcv->drone;
    if (drone == NULL)
        return 0;
    if (drone->flags & DRONE_FLAG_NO_ALERTS)
        return 0;
    if (!drone->unknown33 && !drone->explosiveSeen) {
        drone->returnState = (short)dcv->aiStateMachine->curState;
        NDrone2_FeetPos(dcv->gameObj, &dcv->drone->returnPos);
        dcv->drone->returnCel = dcv->gameObj->inCel;
    }

    switch (msg->msgType) {
    case 0x11:
        return _DroneVision_ShoutMsg11(dcv);
    case 0x12:
        return _DroneVision_ShoutMsg12(dcv);
    case 0x13:
        return _DroneVision_ShoutMsg13(dcv);
    case DRONE_MSG_HeardNoise:
        return _DroneVision_AlertSound(dcv);
    case DRONE_MSG_DroneAlert:
        return _DroneVision_ShoutDroneAlert(dcv);
    case DRONE_MSG_GlobalAlarm:
        return _DroneVision_GlobalAlarm(dcv);
    default:
        return 0;
    }
}

// Each frame, from the guards' states: DSTATE_Attack when the drone sees its opponent and may act on it, else 0.
// Never while blind, playing a script fully alert, or interrogating; civilians only in TowerA and TowerB; the player
// only past 0.66 alertness unless DRONE_PROP_51; the runners and alarm raisers not while running to their point or goal
// unless DRONE_PROP_57.
// AUTOINJECT
ushort DroneVision_EnemyLookForOpponent(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if ((ushort)dcv->aiStateMachine->curState == DSTATE_PlayScript && drone->alertness >= 1.0f)
        return 0;
    if (switch_BLIND_DRONES || drone->opponent == NULL)
        return 0;
    if (behaviour_util_getProperty(DRONE_PROP_3C, drone->currentBehaviour) == 0)
        return 0;
    if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
        return 0;
    uint level = GameState.CurrentLevelHashcode;
    if ((level < HT_Level_TowerA || level > HT_Level_TowerB) && dcv->drone->side == 3)
        return 0;
    if (drone->opponent == NDrone2_Player() && drone->alertness < 0.66f &&
        behaviour_util_getProperty(DRONE_PROP_51, dcv->drone->currentBehaviour) == 0)
        return 0;
    if ((dcv->drone->flags & 0x800) || (dcv->drone->flags2 & DRONE_FLAG2_INTEROGATING))
        return 0;
    if (behaviour_util_getProperty(DRONE_PROP_57, dcv->drone->currentBehaviour) == 0 &&
        (dcv->drone->dtype == DTYPE_RunToPoint || dcv->drone->dtype == DTYPE_AlarmRaiser) &&
        (dcv->aiStateMachine->curState == DSTATE_EnemyRunToPoint ||
         dcv->aiStateMachine->curState == DSTATE_GoToGoalPosition))
        return 0;

    if (!(dcv->drone->flags & DRONE_FLAG_ATTACK_MODE)) {
        dcv->drone->flags |= DRONE_FLAG_ATTACK_MODE;
        dcv->drone->flags |= DRONE_FLAG_ATTACK_SIGHT;
    }
    return DSTATE_Attack;
}

// Each frame, for a drone with hearing (DRONE_PROP_1F; in CastleIndoors1 only an armed one): what it hears where it
// stands raises its alertness by a hundredth. A sound in a known cel, a second (at 60 a second) or more after the
// last or louder than it, becomes soundTarget and a DRONE_MSG_HeardNoise to itself.
// AUTOINJECT
void DroneFunc_HandleSoundAlerts(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    DYNAMICSOUNDS *sound = NULL;
    float level = 0.0f;
    if (behaviour_util_getProperty(DRONE_PROP_1F, drone->currentBehaviour) == 0)
        return;
    if (switch_BLIND_DRONES)
        return;
    if (dcv->drone->flags & DRONE_FLAG_DEAF)
        return;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && !NDrone2_ObjectIsArmed(dcv->gameObj))
        return;

    dcv->drone->soundAlertness = Sound_Alertness(&dcv->gameObj->position, &level, &sound);
    if (!(dcv->drone->soundAlertness > 0.0f))
        return;
    // On the x87: the step is stored rounded, and the unrounded step added
    double step = (double)drone->soundAlertness * kSoundAlertnessScale;
    drone->soundAlertStep = (float)step;
    drone->alertness = (float)(step + drone->alertness);

    // The sound is not checked for NULL here, only below
    cel_tag *cel = build_FindCel(&sound->location, glb_world);
    if (cel == NULL)
        return;
    double due = (double)drone->heardFrame + (double)FRAME_RATE_DIV * 60.0f;
    if (!((double)GameState.NumFramesUnpaused > due) && !(level > drone->heardLevel))
        return;

    if (sound != NULL)
        drone->heardAlertness = sound->alertness;
    drone->soundTarget.targetBit = AITARGET_SOUND;
    drone->soundTarget.obj = NULL;
    Vec_Copy(&sound->location, &drone->soundTarget.goal.pos);
    drone->soundTarget.goal.cel = cel;
    drone->soundTarget.builtFrame = 0;
    drone->heardFrame = GameState.NumFramesUnpaused;
    drone->heardLevel = level;
    Drone_SM_SendMsgSelf(DRONE_MSG_HeardNoise, NULL, 0, 0, dcv);
}
