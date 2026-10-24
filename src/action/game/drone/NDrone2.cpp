#include "NDrone2.h"
#include "NDrone2_Cover.h"
#include "../sp/SwitchChannels.h" // switch_channels, switch_channels_MusicVars
#include "../../sound/music.h"    // Music_Event (ours, sound/music.cpp)
#include "../../game.h"           // GameState
#include "../../util/Random.h"      // Rand_Rand (AUTOGEN'd in util/Random.cpp: still the original)
#include "../../engine/Collide.h"   // HITDATA_tag
#include "../obj/bullet.h"          // BU_tag
#include "Behaviour.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "../../engine/Anim.h"      // AnimGetBoneWorldTrans
#include "../../math/math.h"        // Vec_*, Atan2 (ours)
#include "../mp/multiplayer.h"      // MPSettings
#include "../obj/build.h"           // build_FindCel
#include "BOT.h"                    // BOT_vars_t
#include "DroneWeapon.h"
#include "NDrone2_Damage.h"
#include "DroneVision_Alerts.h"
#include "DroneVision.h"
#include "DroneInit.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include <string.h>

// XBE_GLOBAL(0x001e5630, 0x142c)
NPCGlobals_t NPCGlobals;

// MUSICEV_ALERT's value picks the cue: 3 for SWITCH_CASTLEEXTERIOR_ALARM, 1 for SWITCH_CASTLE_ALARM and
// SWITCH_FAIL_ALARM.
#define ALARM_MUSIC_CASTLE 3
#define ALARM_MUSIC_GENERIC 1

// switch_channels_MusicVars[ch] counts the frames a channel has been on, for this function only: it is the
// last user, and Init_SwitchChannels clears it per level. The original inlines this three times. A counter
// at 0 goes straight to 2 (set to 1, then incremented - so the first frame already counts as two), and it
// sticks at 0xffff rather than wrapping. Nothing ever resets it while the channel stays on, and nothing
// here resets it when the channel goes off either, so each cue can only play once per level.
static ushort DroneFunc_TickAlarmTimer(int ch) {
    ushort frames = switch_channels_MusicVars[ch];
    if (frames == 0)
        frames = 1;
    if (frames < 0xffff)
        frames++;
    switch_channels_MusicVars[ch] = frames; // the original skips this store when saturated; same value
    return frames;
}

// Called every frame. While an alarm channel is on, keep asserting the alarm music event: for the first
// second of SWITCH_CASTLEEXTERIOR_ALARM (CastleExterior only), else for the first ten seconds of
// SWITCH_CASTLE_ALARM (CastleExterior and CastleCourtyard) or SWITCH_FAIL_ALARM (any level). The earlier checks
// win and return, so a later channel's timer only advances on frames when no earlier cue fired.
//
// AUTOINJECT
void DroneFunc_CheckAlarmRaised(void) {

    uint frameRate = FRAME_RATE_INT; // read once, before any of the branches, as the original does

    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_CastleExterior:
        if (switch_channels[SWITCH_CASTLEEXTERIOR_ALARM] != 0 &&
            DroneFunc_TickAlarmTimer(SWITCH_CASTLEEXTERIOR_ALARM) < frameRate) {
            Music_Event(MUSICEV_ALERT, ALARM_MUSIC_CASTLE);
            return;
        }
        // fall through: the castle alarm has run its second (or is off), try SWITCH_CASTLE_ALARM
    case HT_Level_CastleCourtyard:
        if (switch_channels[SWITCH_CASTLE_ALARM] != 0 &&
            DroneFunc_TickAlarmTimer(SWITCH_CASTLE_ALARM) < frameRate * 10) {
            Music_Event(MUSICEV_ALERT, ALARM_MUSIC_GENERIC);
            return;
        }
        break;
    default:
        break;
    }

    if (switch_channels[SWITCH_FAIL_ALARM] != 0 && DroneFunc_TickAlarmTimer(SWITCH_FAIL_ALARM) < frameRate * 10)
        Music_Event(MUSICEV_ALERT, ALARM_MUSIC_GENERIC);
}

// ---------------------------------------------------------------------------------------------------------------

// Stun weapons with their own recovery base (weapon_definition_tag.weaponVariantNum). 0x43/0x44 and 0x4a/0x4c are
// the dart gun and taser and their upgrades (UpgradedDartGuns / UpgradedTasers in game/Upgrade.cpp); 0x35 is
// unnamed (the PS2 build has the same case, no name either).
#define WEAPVAR_STUN_0x35 0x35
#define WEAPVAR_DARTGUN 0x43
#define WEAPVAR_DARTGUN_UPGRADED 0x44
#define WEAPVAR_TASER 0x4a
#define WEAPVAR_TASER_UPGRADED 0x4c

#define RECOVER_BASE_SECONDS 60
#define REACTION_STAT_SCALE 200     // reaction and recovery stats count down from this: lower stat = slower

// How long, in frames, a stunned drone stays down (NDrone2_DSTATE_Taser, _Stunned, _StunGrenade*, _StunDart*
// store it at Drone_tag+0x938): (200 - recovery stat) percent of a base time. The base is 60 seconds, or per stun
// weapon when the hit came from a bullet object: 30 s, 60 s (dart), 120 s (upgraded dart), 10 s (either taser).
//
// The original works in x87 floats and truncates with __ftol2 (the high half in EDX is never used: the callers
// store EAX). Every product here is exact in a double (the frame counts are integers and 0.01f has a 24-bit
// mantissa), so double arithmetic truncates the same way - including the quirk that 0.01f is slightly under 0.01,
// so an exact result such as (200-80) x 3600 / 100 = 4320 comes out as 4319.
//
// AUTOINJECT
uint DroneFunc_RecoverTime(DCVars_tag *dcv, HITDATA_tag *hit) {
    Drone_tag *drone = dcv->drone;
    // FRAME_RATE_INT is unsigned: the original corrects the fild by 2^32 when it looks negative
    double baseFrames = (double)FRAME_RATE_INT * 60.0f;

    if (hit != NULL && hit->hitObj != NULL && hit->hitObj->objectType == OBJECTTYPE_BULLET) {
        BU_tag *bullet = (BU_tag *)hit->hitObj->extraObjectData;
        // In the weapon cases the multiply is an integer one (wrapping), then converted as unsigned
        switch ((ushort)bullet->wpnDef->weaponVariantNum) {
        case WEAPVAR_STUN_0x35:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 30);
            break;
        case WEAPVAR_DARTGUN:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 60);
            break;
        case WEAPVAR_DARTGUN_UPGRADED:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 120);
            break;
        case WEAPVAR_TASER:
        case WEAPVAR_TASER_UPGRADED:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 10);
            break;
        default:
            break;
        }
    }

    // ((200 - recover) * base) * 0.01f, in that order. A recovery stat over 200 gives a negative count (__ftol2 of
    // a negative value; only the low 32 bits are kept).
    double frames = (double)(REACTION_STAT_SCALE - (int)drone->recover) * baseFrames * (double)0.01f;
    return (uint)(long long)frames;
}

// How many frames of continuous sight a drone needs before it registers the opponent (DroneVision_HaveOpponentSight
// compares it, unsigned, with Drone_tag+0x200). Only on Henderson B/C, Castle Exterior and Tower A/B does it
// depend on the drone: 0 once it has reacted to a first sighting, or when it is already fully alert; otherwise
// (200 x FRAME_RATE_DIV - reaction) x (1 - alertness) x 30 / 100. Everywhere else a flat 10 x FRAME_RATE_DIV.
//
// x87 in the original, truncated with __ftol2 (EAX only; the return-0 path does not set EDX). The product of
// the last two multiplies can need more than 53 bits for odd alertness values, so a double could in principle
// round across an integer where the x87 did not - the shadow test covers it (notes.md).
//
// AUTOINJECT
uint DroneFunc_ReactionTime(Drone_tag *drone) {
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonB:
    case HT_Level_HendersonC:
    case HT_Level_CastleExterior:
    case HT_Level_TowerA:
    case HT_Level_TowerB:
        if (drone->sightFlags & DRONE_SIGHT_REACTED)
            return 0;
        // fcomp/test ah,1: computed when alertness < 1.0 or unordered (NaN), so only >= 1.0 returns 0
        if (drone->alertness >= 1.0f)
            return 0;
        {
            double a = (double)FRAME_RATE_DIV * 200.0f - (int)drone->reaction;
            double b = (1.0f - (double)drone->alertness) * 30.0f;
            return (uint)(long long)(a * b * (double)0.01f);
        }
    default:
        return (uint)(long long)((double)FRAME_RATE_DIV * 10.0f);
    }
}

// Arms the idle/patrol timer: PreDroneControl sends message 4 once GameState.NumFramesUnpaused passes
// Drone_tag.stateTimeoutFrame. The wait is minSeconds plus a random 0..randSeconds-1 whole seconds (callers:
// Idle, Alert, Patrol with (dcv, 45, 5) and similar; SpaceDrake).
//
// AUTOINJECT
void NDrone2_SetIdleTimeOut(DCVars_tag *dcv, int minSeconds, uint randSeconds) {
    uint seconds = Rand_Rand(randSeconds) + minSeconds;
    dcv->drone->stateTimeoutFrame = seconds * FRAME_RATE_INT + GameState.NumFramesUnpaused;
}

// Records a change of the drone's alert status: the old one, the frame it changed, and a changed flag. Setting
// the status it already has does nothing (the frame is not refreshed).
//
// AUTOINJECT
void Drone_AlertStatusSet(char newStatus, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if ((char)drone->alertStatus == newStatus)
        return;
    drone->prevAlertStatus = drone->alertStatus;
    drone->alertStatus = newStatus;
    drone->alertStatusChangedFrame = GameState.NumFramesUnpaused;
    drone->alertStatusChanged = 1;
}

bool NDrone2_AttackTalk(DCVars_tag *dcv);   // NDrone2_Combat.cpp

// The drone plays its placement's script (Drone_tag::playScript) and then goes to its initial state. A few scripts
// are handled on their own: 0x6000168 and 0x6000169 talk and attack at the end, 0x60007ac turns the drone round.
// The ids used with DroneAnim_SetScript and friends are as the original passes them.
// AUTOINJECT
bool NDrone2_DSTATE_PlayScript(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= DRONE_FLAG_LOOKS_FOR_OPPONENT;
        dcv->drone->flags |= DRONE_FLAG_ALERTABLE;
        if (dcv->drone->dtype == DTYPE_CastleChatGuard)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_CastleChatGuard1, 0);

        // stateScratch: where an impact sends it; stateScratch2: whether it looks for an opponent while playing
        drone->stateScratch = 0;
        drone->stateScratch2 = 1;
        switch ((uint)dcv->drone->playScript) {
        case HT_Script_HendersonCDroneIntroAttack:
        case HT_Script_Tower2BDroneIntroAttack:
            drone->stateScratch = DSTATE_Attack;
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_STAND_ALERT, 0, 0, 0, 0, 1, dcv);
            return true;

        case HT_Script_DroneAttackTalkA:
            drone->stateScratch = 0;
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_AIM_STAND, DROW_AIM_STAND, 8, 0, DRONE_MSG_ScriptEnd, 0,
                                dcv);
            NDrone2_AttackTalk(dcv);
            dcv->drone->flags |= DRONE_FLAG_DEAF;
            return true;

        case HT_Script_DroneAttackTalkB:
            drone->stateScratch = 0;
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_AIM_CROUCH, DROW_AIM_STAND, 0x1e, 0, DRONE_MSG_ScriptEnd,
                                0, dcv);
            NDrone2_AttackTalk(dcv);
            dcv->drone->flags |= DRONE_FLAG_DEAF;
            return true;

        case HT_Script_Tower2BDronePlay:
            drone->stateScratch = 0;
            drone->stateScratch2 = 0;
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_AIM_STAND, DROW_AIM_STAND, 0xf, 0, DRONE_MSG_ScriptEnd, 0,
                                dcv);
            dcv->drone->flags |= DRONE_FLAG_DEAF;
            behaviour_util_setProperty(0x3b, drone->currentBehaviour, 0);
            behaviour_util_setProperty(0x3a, drone->currentBehaviour, 0);
            drone->avoidLinks &= ~AILINK_EXPLOSIVE;
            return true;

        default:
            drone->stateScratch = DSTATE_Attack;
            DroneAnim_SetScript(dcv->drone->playScript, DANIM_STAND_IDLE, DROW_STAND_IDLE, 0x1e, 0, DRONE_MSG_ScriptEnd,
                                0, dcv);
            return true;
        }

    case DRONE_MSG_Update:
        if (drone->stateScratch2 != 0) {
            dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
            if (dcv->aiStateMachine->scratch1c != 0)
                return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        }
        if (dcv->drone->alertness >= 1.0f)
            NDrone2_SeenAndAttacking(dcv);
        if (drone->sm.stateEnteredFrame + 30 < GameState.NumFramesUnpaused &&
            dcv->drone->playScript >= HT_Script_DroneAttackTalkA
                && dcv->drone->playScript <= HT_Script_DroneAttackTalkB)
            NDrone2_AttackTalk(dcv);
        return true;

    case DRONE_MSG_ScriptEnd:
        switch ((uint)dcv->drone->playScript) {
        case HT_Script_DroneAttackTalkA:
            dcv->drone->flags &= ~DRONE_FLAG_DEAF;
            dcv->drone->playScript = (HASHCODE)0;
            DroneAnim_CallAnim(DANIM_AIM_STAND, 0, DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
            NDrone2_AttackTalk(dcv);
            return true;

        case HT_Script_DroneAttackTalkB:
            dcv->drone->flags &= ~DRONE_FLAG_DEAF;
            DroneAnim_CallAnim(DANIM_AIM_STAND, 0, DSTATE_AimStandFire, 0, 0.0f, 0, dcv);
            dcv->drone->playScript = (HASHCODE)0;
            NDrone2_AttackTalk(dcv);
            return true;

        case HT_Script_Tower2BDronePlay:
            dcv->drone->flags &= ~DRONE_FLAG_DEAF;
            drone->pendingTurn = PI;
            DroneAnim_SetDAnimNow(DROW_STAND_ALERT, 0, 1, dcv);
            break;
        }
        dcv->drone->playScript = (HASHCODE)0;
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->initialState, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, drone->stateScratch, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, drone->stateScratch, 1);
        return true;

    case DRONE_MSG_AlertSighting:
    case DRONE_MSG_AlertDroneShot:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case DRONE_MSG_AlertAttack:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, drone->stateScratch);
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------

// AnimGetBoneWorldTrans's bone argument, as these pass it: bit 31 set, and the bone
#define WORLD_BONE(bone) (0x80000000 | (bone))
#define HEAD_BONE 5

// Whether the object holds a weapon: 2-0x46 or 0x6a-0x71 (AnimState.currentWeaponId).
// AUTOINJECT
bool NDrone2_ObjectIsArmed(obj_tag *gameObj) {
    if (gameObj == NULL || gameObj->animState == NULL)
        return false;
    char weapon = gameObj->animState->currentWeaponId;
    if (weapon >= 0x6a && weapon < 0x72)
        return true;
    return weapon > 1 && weapon < 0x47;
}

// Eyes of an object without a head to read: its position, raised 0.6, or 0.2 for a drone crouched, not at all for
// one down (DroneAnim_CurrentMoveState).
static void NDrone2_EyesFromPosition(obj_tag *gameObj, _VECTOR *pos) {
    Vec_Copy(&gameObj->position, pos);
    uchar moveState = 0;
    if (gameObj->objectType == OBJECTTYPE_DRONE)
        moveState = DroneAnim_CurrentMoveState((Drone_tag *)gameObj->extraObjectData);
    switch (moveState) {
    case 3:
    case 5:
        pos->y += 0.2f;
        break;
    case 4:
    case 0x1a:
        break;
    default:
        pos->y += 0.6f;
        break;
    }
}

// The player the drones see: the dummy player while the dummy-player cheat has it on
// AUTOINJECT
obj_tag* NDrone2_Player(void) {
    if (NPCGlobals.dummyPlayerActive)
        return NPCGlobals.dummyPlayer;
    return glb_players[0];
}

// AUTOINJECT
cel_tag* NDrone2_FindCel(_VECTOR *pos) {
    return build_FindCel(pos, glb_world);
}

// An object's feet: its position less its animation's root height, plus 0.4 m. Without an animation, its position
// (false).
// AUTOINJECT
bool NDrone2_FeetPos(obj_tag *obj, _VECTOR *feet) {
    Vec_Copy(&obj->position, feet);
    if (obj->animState == NULL)
        return false;
    feet->y = (float)(feet->y - ((double)obj->animState->animObj.rootHeight - 0.4f));
    return true;
}

// Where the object's eyes are, and for a drone which way its head faces (Drone_tag.headPos and headYaw; true).
// For anything else the eyes are worked out from its position (false; pos must be given).
// AUTOINJECT
bool NDrone2_GetHeadPos(obj_tag *gameObj, _VECTOR *pos, float *yaw) {
    if (gameObj->objectType == OBJECTTYPE_DRONE) {
        Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
        if (pos != NULL)
            *pos = drone->headPos;
        if (yaw != NULL)
            *yaw = drone->headYaw;
        return true;
    }
    NDrone2_EyesFromPosition(gameObj, pos);
    return false;
}

// The same from the skeleton: the head bone of a drone animating this frame (true), except in SpaceStationD; else
// the eyes from the position, and the object's own facing (false).
// AUTOINJECT
bool NDrone2_GetHeadPosOld(obj_tag *gameObj, _VECTOR *pos, float *yaw) {
    if (GameState.CurrentLevelHashcode != HT_Level_SpaceStationD && gameObj != NULL && gameObj->animState != NULL &&
        gameObj->objectType == OBJECTTYPE_DRONE &&
        (((Drone_tag *)gameObj->extraObjectData)->flags & DRONE_FLAG_ANIM_UPDATED) &&
        (gameObj->animState->animObj.flags & ANIMOBJ_POSE_VALID)) {
        if (yaw != NULL) {
            _MATRIX mtx;
            AnimGetBoneWorldTrans(gameObj, WORLD_BONE(HEAD_BONE), 0, pos, &mtx);
            *yaw = gameObj->rotation.y;
        } else {
            AnimGetBoneWorldTrans(gameObj, WORLD_BONE(HEAD_BONE), 0, pos, NULL);
        }
        return true;
    }
    if (yaw != NULL)
        *yaw = gameObj->rotation.y;
    if (pos != NULL)
        NDrone2_EyesFromPosition(gameObj, pos);
    return false;
}

// Where a bone of an animated drone or player is (true); a drone's only while it animates, and not in
// SpaceStationD. Otherwise the object's position (false).
// AUTOINJECT
bool DroneFunc_GetBonePos(obj_tag *gameObj, uint bone, _VECTOR *pos) {
    if (gameObj == NULL)
        return false;
    AnimState *anim = gameObj->animState;
    if (anim != NULL && (gameObj->objectType == OBJECTTYPE_DRONE || gameObj->objectType == OBJECTTYPE_PLAYER)) {
        bool animating = true;
        if (gameObj->objectType == OBJECTTYPE_DRONE)
            animating = (((Drone_tag *)gameObj->extraObjectData)->flags & DRONE_FLAG_ANIM_UPDATED) &&
                        GameState.CurrentLevelHashcode != HT_Level_SpaceStationD;
        if (animating && (anim->animObj.flags & ANIMOBJ_POSE_VALID)) {
            AnimGetBoneWorldTrans(gameObj, WORLD_BONE(bone), 0, pos, NULL);
            return true;
        }
    }
    if (pos != NULL)
        Vec_Copy(&gameObj->position, pos);
    return false;
}

// Whether the drone sees an armed opponent: one that can spot opponents (property 0x3c) and sees its own, which
// has a weapon - in TowerA and TowerB, armed or not. Skin class 13 never does elsewhere.
// AUTOINJECT
bool NDrone2_NearArmedPerson(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (GameState.CurrentLevelHashcode >= HT_Level_TowerA && GameState.CurrentLevelHashcode <= HT_Level_TowerB)
        return behaviour_util_getProperty(DRONE_PROP_REACTS_TO_SIGHT, drone->currentBehaviour) != 0 &&
               (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT);

    if (drone->skinClass == SKINCLASS_PARTY_GIRL)
        return false;
    if (behaviour_util_getProperty(DRONE_PROP_REACTS_TO_SIGHT, drone->currentBehaviour) == 0)
        return false;
    if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT))
        return false;
    return DroneWeap_OpponentHasWeapon(dcv);
}

// Whether the player is within `angle` of the way the drone faces. An angle that does not compare (NaN) counts as
// within.
// AUTOINJECT
bool NDrone2_BondWithinAngle(DCVars_tag *dcv, float angle) {
    obj_tag *player = NDrone2_Player();
    if (player == NULL)
        return false;
    _VECTOR toPlayer;
    Vec_Subtract(&player->position, &dcv->gameObj->position, &toPlayer);
    float yaw = Atan2(toPlayer.x, toPlayer.z);
    double turn = Vec_AngleDifference(dcv->gameObj->rotation.y, yaw);
    return !(angle < fabs(turn));
}

// Whether the opponent faces the drone to within `degrees` (Drone_tag.opponentFacing)
// AUTOINJECT
bool NDrone2_BondIsFacingMe(DCVars_tag *dcv, float degrees) {
    return fabsf(dcv->drone->opponentFacing) < (double)degrees * DEG_TO_RAD;
}

// Turns the drone to face obj, plus `offset` (targetRot.y). Unless forced, not while its turning is held:
// unknown3f0 bit 0x8000000, a bot recovering, or the anim's flags 0x380.
// AUTOINJECT
void NDrone2_SetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force) {
    if (obj == NULL)
        return;
    Drone_tag *drone = dcv->drone;
    if ((drone->unknown3f0 & 0x8000000) && !force)
        return;
    if (MPSettings.dronesAreBots && (drone->botVars->flags & BOT_FLAG_RECOVERING) && !force)
        return;
    if ((drone->animFlags & DANIMFLAG_TURNS) && !force)
        return;
    _VECTOR toObj;
    Vec_Subtract(&obj->position, &dcv->gameObj->position, &toObj);
    dcv->drone->targetRot.y = Atan2(toObj.x, toObj.z) + offset;
}

// Turns the drone's head to obj (NULL lets it go) for `ticks` (at 60 a second), at `speed` (0 for the usual 0.07).
// Only for drones with head tracking (property 0x4c).
// AUTOINJECT
bool NDrone2_HeadTrackObj(obj_tag *obj, int ticks, float speed, DCVars_tag *dcv) {
    if (behaviour_util_getProperty(DRONE_PROP_HEAD_TRACK, dcv->drone->currentBehaviour) == 0)
        return false;
    AnimObjectSetHeadTracking(dcv->gameObj, obj, 3.0f, 0.14f, 1, 1, HEAD_BONE);
    dcv->gameObj->animState->animObj.headTrackSpeed = speed == 0.0f ? 0.07f : speed;
    dcv->drone->headTarget = obj;
    dcv->drone->headTracking = obj != NULL;
    // The ticks are converted unsigned, scaled on the x87 and truncated (__ftol2)
    uint frames = (uint)(long long)((double)(uint)ticks * FRAME_RATE_DIV);
    dcv->drone->headTrackUntil = frames + GameState.NumFramesUnpaused;
    return true;
}

// Message DRONE_MSG_ConsideredAlerted: a live, active drone set to attack when told (ALERTED_BY_DRONE, and not
// ATTACK_STARTED) attacks now. In CastleIndoors1, while NPCGlobals.interogating is set, only civilian and door guards
// do.
// AUTOINJECT
bool DroneFunc_DoForcedAttack(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0)
        return false;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && NPCGlobals.interogating &&
        drone->dtype != DTYPE_CivilianGuard && drone->dtype != DTYPE_CivDoorGuard)
        return false;
    if (MPDrone_IsDyingOrDead(dcv->gameObj))
        return false;
    if (!(drone->flags & DRONE_FLAG_ALERTED_BY_DRONE) || (drone->flags & DRONE_FLAG_ATTACK_STARTED))
        return false;

    drone->flags &= ~DRONE_FLAG_ALERTED_BY_DRONE;
    drone->flags |= DRONE_FLAG_ATTACK_MODE | DRONE_FLAG_ATTACK_ALERTED;
    dcv->aiStateMachine->scratch1c = DSTATE_Attack;
    if (dcv->aiStateMachine->scratch1c != 0)
        return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
    return false;
}

// Message DRONE_MSG_ExplosiveSeen: a drone that minds explosives (properties 0x34, 0x3a, 0x3b), and is not busy
// with one already or in a state that will not be interrupted, runs from it (DSTATE_RunAwayFromObject) for a time
// and distance that depend on what it is, then goes to explosiveState: by default hides, and where nextState is set,
// there (an aiming drone goes back to aiming). Grenades and the like send drones with property 0x3b to attack.
// AUTOINJECT
void DroneFunc_ConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    Drone_tag *drone = dcv->drone;
    if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_SMOKE, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES, dcv->drone->currentBehaviour) == 0 &&
        behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) == 0)
        return;

    switch (drone->sm.curState) {
    case DSTATE_AllyGoToGoalPosition:
    case DSTATE_GrenadeThrow:
    case DSTATE_Taser:
    case DSTATE_Stunned:
    case DSTATE_PunchImpact:
    case DSTATE_ExplosiveImpact:
    case DSTATE_BulletImpact:
    case DSTATE_GoToGoalPosition:
    case DSTATE_RunAwayFromObject:
    case DSTATE_HideFromScaryObject:
    case DSTATE_RecoverFromScaryObject:
        return;
    }
    if (nextState == DSTATE_RunAwayFromObject || nextState == DSTATE_HideFromScaryObject)
        return;
    if (drone->explosiveSeen)
        return;

    drone->explosivePos = explosive->position;
    drone->explosiveCel = explosive->inCel;
    drone->explosiveRadius = 14.0f;
    drone->explosiveState = DSTATE_HideFromScaryObject;
    drone->explosiveTimeout = GameState.NumFramesUnpaused + FRAME_RATE_INT * 10;
    if (nextState == 0 && (drone->sm.curState == DSTATE_AimStand || drone->sm.curState == DSTATE_AimStandFire))
        nextState = (short)drone->sm.curState;

    if (explosive->objectType == OBJECTTYPE_BULLET) {
        BU_tag *bullet = (BU_tag *)explosive->extraObjectData;
        if (bullet->wpnDef != NULL) {
            switch ((ushort)bullet->wpnDef->weaponVariantNum) {
            case 0x2a:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f:
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES, dcv->drone->currentBehaviour) == 0 &&
                    behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) == 0)
                    return;
                drone->explosiveRadius = 4.0f;
                drone->explosiveTimeout = GameState.NumFramesUnpaused + FRAME_RATE_INT * 2;
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) != 0)
                    drone->explosiveState = DSTATE_Attack;
                break;
            case 0x34:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f:
            case 0x40:
            case 0x6c: {
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES, dcv->drone->currentBehaviour) == 0 &&
                    behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) == 0)
                    return;
                drone->explosiveRadius = 14.0f;
                // Two seconds after the fuse runs out, on the x87 and truncated (__ftol2)
                double frames = (double)(uint)(FRAME_RATE_INT * 2) + bullet->fuseTimer;
                drone->explosiveTimeout = (uint)(long long)(frames + (double)(uint)GameState.NumFramesUnpaused);
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) != 0)
                    drone->explosiveState = DSTATE_Attack;
                break;
            }
            case 0x36:
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_SMOKE, dcv->drone->currentBehaviour) == 0 ||
                    behaviour_util_getProperty(DRONE_PROP_36, dcv->drone->currentBehaviour) != 0)
                    return;
                drone->explosiveRadius = 5.0f;
                drone->explosiveState = nextState;
                break;
            case 0x37:
                if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES, dcv->drone->currentBehaviour) == 0 &&
                    behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) == 0)
                    return;
                drone->explosiveRadius = 10.0f;
                drone->explosiveState = nextState;
                break;
            }
        }
    } else if (explosive->objectType == OBJECTTYPE_OCCLUDE) {
        // Type 0x2a is treated as weapon 0x36 is
        if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_SMOKE, dcv->drone->currentBehaviour) == 0 ||
            behaviour_util_getProperty(DRONE_PROP_36, dcv->drone->currentBehaviour) != 0)
            return;
        drone->explosiveRadius = 5.0f;
        drone->explosiveState = nextState;
    }

    if (nextState != DSTATE_RunAwayFromObject && nextState != DSTATE_HideFromScaryObject)
        drone->sm.resumeState = nextState;
    if (drone->explosiveState == 0)
        drone->explosiveState = nextState;
    drone->explosiveSeen = 1;
    Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunAwayFromObject, 0);
}

// After each frame's control: the first grenade-like aware point (type 2) the drone is within range of, if it minds
// such things (property 0x3a or 0x3b), and if it sees it well, is sent to itself as DRONE_MSG_ExplosiveSeen. That
// point ends the search whether it is seen or not. Not in multiplayer, nor while it is running from one already.
// AUTOINJECT
void DroneFunc_HandleExplosives(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (MPSettings.dronesAreBots != 0 || drone->explosiveSeen)
        return;

    for (uint i = 0; i < NPCGlobals.numAwarePoints; i++) {
        DroneAwarePoint *point = &NPCGlobals.awarePoints[i];
        if (point->type == 1) {
            // Asked, and the answers not used
            if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_SMOKE, dcv->drone->currentBehaviour) != 0)
                behaviour_util_getProperty(DRONE_PROP_36, dcv->drone->currentBehaviour);
        } else if (point->type == 2) {
            if (behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES_THEN_ATTACKS, dcv->drone->currentBehaviour) == 0 &&
                behaviour_util_getProperty(DRONE_PROP_RUNS_FROM_EXPLOSIVES, dcv->drone->currentBehaviour) == 0)
                continue;
            // The radius is squared on the x87, unrounded
            if (!(Vec_SqDist3D(&gameObj->position, &point->pos) < (double)point->radius * point->radius))
                continue;
            if (DroneVision_ObjectVisibility(drone, point->obj) >= 0.7f && NDrone2_CanSeeObject(drone, point->obj, HEAD_BONE, 0))
                Drone_SM_SendMsgSelf(DRONE_MSG_ExplosiveSeen, point->obj, 0, 0, dcv);
            return;
        }
    }
}

// The drones NDrone2_SeenAndAttacking has counted since the music code last started the count (only it uses them;
// the PS2 build's names)
// XBE_GLOBAL(0x001ee288, 0x190)
Drone_tag *pNPCPassed[100];
// XBE_GLOBAL(0x001ee418, 4)
// XBE_GLOBAL(0x001ee418, 0x4)
int32_t pDCVCount;

#define COMBAT_MUSIC 5      // the value with MUSICEV_ALERT

// Tells the music about an enemy (or a drone with runningToAlarm set) fighting: the nearest opponent distances and the
// latest sighting always, and if it saw its opponent lately (or is close while the combat music is on), counts it
// once. Enough of them, or any once the music is on, start the combat music.
// AUTOINJECT
void NDrone2_SeenAndAttacking(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (!drone->runningToAlarm && drone->side != DRONE_SIDE_ENEMY)
        return;
    if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 && !(drone->sightFlags & DRONE_SIGHT_REACTED))
        return;

    bool counts = FRAME_RATE_INT * MusicVars.seenSeconds + drone->opponentSeenFrame > GameState.NumFramesUnpaused;
    ushort combat = MusicVars.combat;
    if (drone->distanceToTarget < MusicVars.closeRange && combat != 0)
        counts = true;
    if (MusicVars.nearestOpponent > drone->distanceToTarget)
        MusicVars.nearestOpponent = drone->distanceToTarget;
    if (drone->sightFrames != 0 && MusicVars.nearestSeenOpponent > drone->distanceToTarget)
        MusicVars.nearestSeenOpponent = drone->distanceToTarget;
    if (drone->opponentSeenFrame > MusicVars.lastSeenFrame)
        MusicVars.lastSeenFrame = drone->opponentSeenFrame;
    if (!counts && !drone->runningToAlarm)
        return;

    if (!MusicVars.countingStarted) {
        if (pDCVCount > 0)
            memset(pNPCPassed, 0, pDCVCount * sizeof(pNPCPassed[0]));
        pDCVCount = 0;
        MusicVars.countingStarted = 1;
    }
    for (int i = 0; i < pDCVCount; i++) {
        if (pNPCPassed[i] == drone)
            return;
    }
    if (pDCVCount < 100)
        pNPCPassed[pDCVCount++] = drone;

    NPCGlobals.seenAttacking++;
    MusicVars.attackers++;
    if (MusicVars.attackers >= MusicVars.combatThreshold || combat != 0 || drone->runningToAlarm) {
        MusicVars.combat = 1;
        Music_Event(MUSICEV_ALERT, COMBAT_MUSIC);
        MusicVars.combatFrame = GameState.NumFramesUnpaused;
    }
}
