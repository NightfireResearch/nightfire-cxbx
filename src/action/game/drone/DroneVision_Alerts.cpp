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
#include "NDrone2_Combat.h"              // NDrone2_CheckSurrender, NDrone2_ChooseCombatMove
#include "NDrone2_Opponent.h"            // NDrone2_DroneAlertToObject
#include "../../assets.h"               // HT_Level_*
#include "../../util/Random.h"          // Rand_Rand

#include <bit>
#include <stdint.h>
#include "BOT.h"

#pragma fp_contract(off)

// While set, no drone sees or hears anything (the cheat)
extern uint32_t switch_BLIND_DRONES;

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

#ifndef NIGHTFIRE_STANDALONE
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
#endif

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

#ifndef NIGHTFIRE_STANDALONE
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
#endif

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

#ifndef NIGHTFIRE_STANDALONE
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
#endif

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

// ---- the alerts of the older handler (the bots' DSTATE_BotGlobal still uses it)

constexpr float kPowerstationShout = 40.0f;     // in PowerStationA1 to Cut_Level2: heard anywhere within this,
constexpr float kPowerstationShoutInCel = 100.0f;   // and within this in the same cel
constexpr float kShout = 15.0f;                 // elsewhere
constexpr float kShoutInCel = 20.0f;

// Drone_tag.sightFlags: NDrone2_ReactToOpponentSighted has reacted once (our name)
#define DRONE_SIGHT_REACTED_ONCE 0x20
// Drone_tag.flags
#define DRONE_FLAG_HAS_COVER 0x800
#define DRONE_FLAG_ALARM_SENT 0x200000

// Whether the drone hears a shout from obj: within 15 m, or 20 m in the same cel (40 and 100 m in PowerStationA1,
// PowerStationA2, Cut_Level1 and Cut_Level2).
// AUTOINJECT
bool NDrone2_InShoutingRange(DCVars_tag *dcv, obj_tag *obj) {
    double distance = Vec_Dist3D(&dcv->gameObj->position, &obj->position);
    uint level = GameState.CurrentLevelHashcode;
    float near, inCel;
    if (level >= HT_Level_PowerStationA1 && level <= HT_Level_Cut_Level2) {
        near = kPowerstationShout;
        inCel = kPowerstationShoutInCel;
    } else {
        near = kShout;
        inCel = kShoutInCel;
    }
    if (distance < near)
        return true;
    return distance < inCel && dcv->gameObj->inCel == obj->inCel;
}

// Leaves the state for an alert, as a state may answer it: the state given (or DSTATE_Attack) for a civilian,
// party girl or guard civilian, another state for the rest, through BOT_reactToDroneAlertMsg for a bot. Unless
// unknown33 or explosiveSeen is set, the drone first keeps where it was. True when the alert was taken (whether or
// not the state changed). Messages: a sound heard (0x14), the global alarm (0x1e), and other drones' shouts (0x11-0x16)
// from a drone on the same path within shouting range. FUN_00038ff0 in Ghidra. Not named on PS2; the name is ours.
// AUTOINJECT
bool NDrone2_HandleDroneAlert(DCVars_tag *dcv, MsgObject *msg, short state) {
    DroneAlert_tag *alert = (DroneAlert_tag *)msg->extraData;
    Drone_tag *drone = dcv->drone;
    Drone_tag *sender = NULL;
    if (alert != NULL) {
        if (alert->sender == dcv->gameObj)
            return false;
        if (alert->sender != NULL && alert->sender->objectType == OBJECTTYPE_DRONE)
            sender = (Drone_tag *)alert->sender->extraObjectData;
    }
    if (!drone->unknown33 && !drone->explosiveSeen) {
        drone->returnState = (short)dcv->aiStateMachine->curState;
        NDrone2_FeetPos(dcv->gameObj, &dcv->drone->returnPos);
        dcv->drone->returnCel = dcv->gameObj->inCel;
    }

    StateMachineInfo_tag *sm = dcv->aiStateMachine;
    switch (msg->msgType) {
    case DRONE_MSG_HeardNoise: {
        if (behaviour_util_getProperty(DRONE_PROP_1F, dcv->drone->currentBehaviour) == 0)
            return false;
        Drone_tag *self = dcv->drone;
        if (self->flags & DRONE_FLAG_DEAF)
            return false;
        if (self->dtype == DTYPE_Bot) {
            if (!BOT_reactToDroneAlertMsg(msg->msgType, drone, sender))
                return false;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotHeardNoise, 0);
            return true;
        }
        if (self->distanceToTarget > kHearingRange)
            return false;
        if (sm->curState == DSTATE_Interogate || sm->curState == DSTATE_InterogateWalk)
            return true;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_PartyGirl:
        case DTYPE_CivilianGuard:
        case DTYPE_CivDoorGuard:
            if (!(drone->alertness > 0.7f))
                return false;
            Drone_SM_SetState(sm, state != 0 ? (DSTATE)state : DSTATE_Attack, 0);
            return true;
        default:
            if (dcv->gameObj->curState == 0x63)
                return false;
            Drone_SM_SetState(sm, DSTATE_HeardNoise, 0);
            return true;
        }
    }

    case DRONE_MSG_GlobalAlarm:
        if (dcv->drone->flags & DRONE_FLAG_NO_ALERTS)
            return false;
        dcv->drone->alertness = 1.0f;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_PartyGirl:
            return false;
        case DTYPE_CivilianGuard:
            Drone_SM_SetState(dcv->aiStateMachine, state != 0 ? (DSTATE)state : DSTATE_Attack, 0);
            return true;
        case DTYPE_Bot:
            // the sender must be a drone (the original reads its object regardless)
            if (!MP_areObjectsOnSameTeam(drone->gameObj, sender->gameObj))
                return false;
            if (!BOT_reactToDroneAlertMsg(msg->msgType, drone, sender))
                return false;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
            return true;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        }

    default:
        break;
    }

    // The other drones' shouts: only from a drone on the same AI path
    if (alert == NULL || sender == NULL)
        return false;
    if (dcv->drone->route1.path != sender->route1.path)
        return false;
    switch (msg->msgType) {
    case 0x11:
        if (behaviour_util_getProperty(DRONE_PROP_28, dcv->drone->currentBehaviour) == 0)
            return false;
        if (dcv->drone->flags & DRONE_FLAG_NO_ALERTS)
            return false;
        if (sm->curState == DSTATE_Interogate || sm->curState == DSTATE_InterogateWalk)
            return true;
        if (!NDrone2_InShoutingRange(dcv, alert->sender))
            return true;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_PartyGirl:
        case DTYPE_CivilianGuard:
            if (state != 0) {
                Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)state, 0);
                return true;
            }
            break;
        case DTYPE_Bot:
            if (!MP_areObjectsOnSameTeam(drone->gameObj, sender->gameObj))
                return false;
            if (!BOT_reactToDroneAlertMsg(msg->msgType, drone, sender))
                return false;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
            return true;
        }
        dcv->drone->alertness = 1.0f;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case 0x12:
        if (behaviour_util_getProperty(DRONE_PROP_27, dcv->drone->currentBehaviour) == 0)
            return false;
        if (dcv->drone->flags & DRONE_FLAG_NO_ALERTS)
            return false;
        if (!NDrone2_InShoutingRange(dcv, alert->sender))
            return true;
        dcv->drone->alertness = 1.0f;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_CivilianScared:
        case DTYPE_PartyGirl:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        case DTYPE_Bot:
            if (!MP_areObjectsOnSameTeam(drone->gameObj, sender->gameObj))
                return false;
            if (!BOT_reactToDroneAlertMsg(msg->msgType, drone, sender))
                return false;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotSeenDroneShot, 0);
            return true;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SeenDroneShot, 0);
            return true;
        }

    case 0x13:
        if (!NDrone2_InShoutingRange(dcv, alert->sender) && !(dcv->drone->flags & DRONE_FLAG_NO_ALERTS))
            return false;
        if (behaviour_util_getProperty(DRONE_PROP_4F, dcv->drone->currentBehaviour) == 0)
            return false;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_CivilianScared:
        case DTYPE_PartyGirl:
        case DTYPE_CivilianGuard:
            Drone_SM_SetState(dcv->aiStateMachine, state != 0 ? (DSTATE)state : DSTATE_Attack, 0);
            return true;
        case DTYPE_Bot:
            return false;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SeenSurrenderedDrone, 0);
            return true;
        }

    case DRONE_MSG_DroneAlert:
        if (behaviour_util_getProperty(DRONE_PROP_29, dcv->drone->currentBehaviour) == 0)
            return false;
        if (dcv->drone->flags & DRONE_FLAG_NO_ALERTS)
            return false;
        if (!NDrone2_InShoutingRange(dcv, alert->sender))
            return false;
        dcv->drone->alertness = 1.0f;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_CivilianScared:
        case DTYPE_PartyGirl:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        case DTYPE_Bot:
            return false;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SeenDeadBody, 0);
            return true;
        }

    case 0x16:
        if (dcv->drone->flags & DRONE_FLAG_NO_ALERTS)
            return false;
        if (!NDrone2_InShoutingRange(dcv, alert->sender))
            return false;
        dcv->drone->alertness = 1.0f;
        switch (drone->dtype) {
        case DTYPE_Civilian:
        case DTYPE_CivilianScared:
        case DTYPE_PartyGirl:
        case DTYPE_CivilianGuard:
            Drone_SM_SetState(dcv->aiStateMachine, state != 0 ? (DSTATE)state : DSTATE_Attack, 0);
            return true;
        case DTYPE_Bot:
            if (!MP_areObjectsOnSameTeam(drone->gameObj, sender->gameObj))
                return false;
            if (!BOT_reactToDroneAlertMsg(msg->msgType, drone, sender))
                return false;
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_BotAttack, 0);
            return true;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        }

    default:
        return false;
    }
}

// ---- reacting to the opponent

constexpr float kReactAlertness = 0.75f;
constexpr float kAlarmShout = 0.76f;
static_assert(std::bit_cast<uint32_t>(kAlarmShout) == 0x3f428f5c, "not the original's 0.76");

// With property 0x33, a drone that has not yet raised the alarm shouts its opponent to those within 20 m (message
// 0x11) and raises it
static void ShoutOpponent(DCVars_tag *dcv, float strength) {
    if (behaviour_util_getProperty(DRONE_PROP_33, dcv->drone->currentBehaviour) == 0)
        return;
    if (dcv->drone->flags & DRONE_FLAG_ALARM_SENT)
        return;
    NDrone2_DroneAlertToObject(dcv, 0x11, dcv->drone->opponent, 20.0f, 20.0f, strength);
    dcv->drone->flags |= DRONE_FLAG_ALARM_SENT;
}

// The state a drone that sees its opponent goes to, `state` for the plain attack, or 0 to stay: none while the drones
// are blinded, without an opponent, for a neutral, or while it has cover. A drone barely alert to the player (and
// without property 0x51) stays, as does a run-to-point or alarm-raising drone without property 0x57 on its way. One that
// surrenders, surrenders. The first time it reacts it becomes alert (0.75), shouting with property 0x33, and by its
// properties interrogates, attacks, makes a combat move or throws a grenade; after that it is fully alert, shouting
// again. Ghidra splits it: FUN_00064910 is the entry, NDrone2_ReactToOpponentSighted (0x6491d) the rest.
// AUTOINJECT
short NDrone2_ReactToOpponentSighted(DCVars_tag *dcv, short state) {
    if (switch_BLIND_DRONES)
        return 0;
    Drone_tag *drone = dcv->drone;
    if (drone->opponent == NULL)
        return 0;
    if (drone->side == 3)
        return 0;
    if (drone->flags & DRONE_FLAG_HAS_COVER)
        return 0;
    if (drone->opponent == NDrone2_Player() && drone->alertness < 0.66f &&
        behaviour_util_getProperty(DRONE_PROP_51, dcv->drone->currentBehaviour) == 0)
        return 0;
    if (behaviour_util_getProperty(DRONE_PROP_57, dcv->drone->currentBehaviour) == 0 &&
        (dcv->drone->dtype == DTYPE_RunToPoint || dcv->drone->dtype == DTYPE_AlarmRaiser) &&
        (dcv->aiStateMachine->curState == DSTATE_EnemyRunToPoint ||
         dcv->aiStateMachine->curState == DSTATE_GoToGoalPosition))
        return 0;
    if (NDrone2_CheckSurrender(dcv)) {
        dcv->drone->alertness = 1.0f;
        return DSTATE_Surrender_Anim;
    }

    if (dcv->drone->sightFlags & DRONE_SIGHT_REACTED_ONCE) {
        ShoutOpponent(dcv, 1.0f);
        dcv->drone->alertness = 1.0f;
        return state;
    }
    dcv->drone->sightFlags |= DRONE_SIGHT_REACTED_ONCE;
    if (!(drone->alertness <= kReactAlertness))
        return state;
    if (behaviour_util_getProperty(DRONE_PROP_3C, dcv->drone->currentBehaviour) == 0)
        return 0;
    dcv->drone->alertness = kReactAlertness;
    ShoutOpponent(dcv, kAlarmShout);
    if (drone->dtype == DTYPE_Interogator)
        return DSTATE_Interogate;
    if (behaviour_util_getProperty(DRONE_PROP_18, dcv->drone->currentBehaviour) != 0)
        return DSTATE_Attack;
    if (behaviour_util_getProperty(DRONE_PROP_19, dcv->drone->currentBehaviour) != 0)
        return NDrone2_ChooseCombatMove(dcv);
    if (behaviour_util_getProperty(DRONE_PROP_1A, dcv->drone->currentBehaviour) != 0)
        return DSTATE_Interogate;
    if (behaviour_util_getProperty(DRONE_PROP_4A, dcv->drone->currentBehaviour) != 0)
        return DSTATE_GrenadeThrow;
    if (behaviour_util_getProperty(DRONE_PROP_1B, dcv->drone->currentBehaviour) != 0) {
        if (Rand_Rand(2) == 1)
            return NDrone2_ChooseCombatMove(dcv);
        return DSTATE_Attack;
    }
    return state;
}
