// Taking cover: running to a cover node, the anims at it (DroneAnim_CoverAnim) and the states that wait, aim, fire and
// leave there. Also doors and kicked objects on a route, the snipers, the grenade throw and the action anims a route
// node starts (NDrone2_ReachedDestNode).

#include "NDrone2_Cover.h"
#include "DroneAnim.h"
#include "DroneTables.h"
#include "DroneWeapon.h"
#include "DroneVision_Alerts.h"
#include "NDrone2_Damage.h"
#include "NDrone2_Route.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "../../util/Random.h"          // Rand_Rand, Float_FRand
#include "../mp/multiplayer.h"          // MPSettings
#include "../obj/control.h"             // control_first_object
#include "../obj/Door.h"                // Door_Activate
#include "../obj/Drone_AIHints.h"       // CoverNode_tag
#include "../obj/ScriptPlayer.h"        // SP_UnPause
#include "../sp/SwitchChannels.h"

#include <bit>
#include <math.h>
#include <string.h>

// The originals, called by address
#define NDrone2_DisownCover ((void (__cdecl *)(DCVars_tag *dcv))0x00044210)
#define NDrone2_CoverNodeOK ((ushort (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node, uint side, char flag))0x00044310)
#define NDrone2_CoverAvailable ((bool (__cdecl *)(DCVars_tag *dcv, char flag))0x00046a10)
#define DroneAnim_SetCombatMoveAnim ((void (__cdecl *)(DCVars_tag *dcv, float distance))0x000354c0)
#define DroneAnim_ForceCrouchCover ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x00033870)
#define DroneAnim_CanCrouchLean ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x000338b0)
#define DroneAnim_CanCrouchStepOut ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x00033990)
#define DroneAnim_CanStandLean ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x00033ac0)
#define DroneAnim_CanStandStepOut ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x00033b70)
#define DroneAnim_CanUseCornerAsLowCover ((bool (__cdecl *)(DCVars_tag *dcv, CoverNode_tag *node))0x00033bf0)
#define NDrone2_AttackTalk ((bool (__cdecl *)(DCVars_tag *dcv))0x0003ba80)
#define NDrone2_CanAltAttack ((bool (__cdecl *)(DCVars_tag *dcv, char flag))0x00043750)
#define DroneFunc_ChooseSFX ((uint (__cdecl *)(uint first, uint last))0x00036270)
#define DroneFunc_ChooseSFXByID ((uint (__cdecl *)(Drone_tag *drone, uint first, uint last))0x00036290)
#define DroneFunc_InjuredSFX ((bool (__cdecl *)(DCVars_tag *dcv, uint sfx))0x0003b830)
#define NDrone2_maybeCalcDistanceAndDirToTarget ((void (__cdecl *)(DCVars_tag *dcv, CelPos_tag *target))0x00046080)
// Returns in ST0, unrounded
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)

// The corner anims, a list per step for each mode and side: the entry for mode m and side s is at m * 2 + s. The sides
// are 1 and 2, so the table proper starts at 0x00164030.
// XBE_GLOBAL(0x0016402c, 0x2c)
#define DroneCoverCornerAnims ((const ushort *const *)0x0016402c)
// The low nodes' anims, by step
#define DroneCoverLowAnims (*(const ushort *const *)0x00164064)

// A node a drone waits at once (its wait flag), and a kick node whose object has been kicked (our names)
#define AINODE_WAIT 0x1
#define AINODE_ACTION_ANIM 0x8
#define AINODE_MISSION_WAIT 0x20
#define AINODE_FIDDLE 0x40
#define AINODE_KICKED 0x8000

// While switch channel 0x99 is off, a mission wait node holds the drone (in DSTATE_AllyLeadMissionWait)
#define MISSION_WAIT_CHANNEL 0x99

// Anim states (our names)
#define DANIM_COVER_FIRE_LOW 0x58
#define DANIM_CROUCH_AIM 5          // DroneAnim_ValidateCoverAnim gives way to 7, or else 10
#define DANIM_CROUCH_AIM_ALT 7
#define DANIM_STAND_AIM 10
#define DANIM_STAND_ALERT 0x21
#define DANIM_THROW_GRENADE 0x49
#define DANIM_SNIPER_IDLE 0xe
#define DANIM_RELOAD 0x52
#define DANIM_USE_DOOR 0x4e
#define DANIM_IDLE_FIDDLE 0x23

#define SFX_DOOR_OPEN 0x27
#define SFX_KICK 0x28

// UnderCoverLeaveNow: from these anim rows it steps out to the side of its opponent
#define DANIM_ROW_COVER_A 0x87
#define DANIM_ROW_COVER_B 0xab

constexpr float kPi = 3.14159274f;
constexpr float kHalfPi = 1.57079637f;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "not the original's pi");
static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3fc90fdb, "not the original's pi/2");
static_assert(std::bit_cast<uint32_t>(0.08f) == 0x3da3d70a, "not the original's 0.08");
static_assert(std::bit_cast<uint32_t>(0.33f) == 0x3ea8f5c3, "not the original's 0.33");
static_assert(std::bit_cast<uint32_t>(0.66f) == 0x3f28f5c3, "not the original's 0.66");
static_assert(std::bit_cast<uint32_t>(0.025f) == 0x3ccccccd, "not the original's 0.025");

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

static void SetTimer(DroneTimer *timer, uint dueFrame) {
    timer->dueFrame = dueFrame;
    timer->unknown8 = 0;
    timer->on = 1;
}

// Anim state 5 the drone has no script for becomes 7, or failing that 10; any other is kept as it is
ushort _DroneAnim_ValidateCoverAnim(DCVars_tag *dcv, ushort state) {
    if (!DroneAnim_CanDoAnimState(dcv, state, 0) && state == DANIM_CROUCH_AIM)
        return DroneAnim_CanDoAnimState(dcv, DANIM_CROUCH_AIM_ALT, 0) ? DANIM_CROUCH_AIM_ALT : DANIM_STAND_AIM;
    return state;
}

// The original takes the dcv in ESI and the anim state in DI.
// AUTOLTCG
void __declspec(naked) DroneAnim_ValidateCoverAnim(void) {
    _asm {
        movzx eax, di
        push eax
        push esi
        call _DroneAnim_ValidateCoverAnim
        add esp, 8
        ret
    }
}

// The anim state for the drone's cover step, in a mode at a corner (the mode's list for the corner's side), or at a
// low node
// AUTOINJECT
ushort DroneAnim_CoverModeAnim(DCVars_tag *dcv, uchar mode) {
    Drone_tag *drone = dcv->drone;
    if (drone->coverNode->type == COVER_CORNER)
        return DroneCoverCornerAnims[mode * 2 + (drone->coverFlags & DRONE_COVER_SIDE_MASK)][drone->coverStep];
    return DroneCoverLowAnims[drone->coverStep];
}

// Asks for the anim of a cover step, which ends in endState. At a low node the leave step goes straight to the state.
// Arriving at a corner draws the mode from those its behaviour and the corner allow (crouched or standing, leaning or
// stepping out, or the corner as low cover); the change step, one time in two, draws again (crouched ones too with
// the opponent over 8 m off) and plays the old mode's leave anim into the new one. False when there is no anim.
// AUTOINJECT
bool DroneAnim_CoverAnim(DCVars_tag *dcv, uchar step, short endState) {
    Drone_tag *drone = dcv->drone;
    ushort options[5] = {0, 0, 0, 0, 0};
    ushort count = 0;
    ushort anim;

    if (drone->coverNode->type == COVER_LOW) {
        switch (step) {
        case COVER_STEP_ENTER:
        case COVER_STEP_ARRIVE:
            drone->coverFlags &= DRONE_COVER_SIDE_MASK;
            dcv->drone->coverMode = 0;
            dcv->drone->coverStep = COVER_STEP_ENTER;
            anim = _DroneAnim_ValidateCoverAnim(dcv, drone->coverNode->lowAnim1);
            break;
        case COVER_STEP_IDLE:
        case COVER_STEP_RETURN:
            drone->coverStep = step;
            anim = _DroneAnim_ValidateCoverAnim(dcv, drone->coverNode->lowAnim1);
            break;
        case COVER_STEP_AIM:
            drone->coverStep = step;
            anim = _DroneAnim_ValidateCoverAnim(dcv, drone->coverNode->lowAnim2);
            break;
        case COVER_STEP_FIRE:
            drone->coverStep = step;
            anim = DANIM_COVER_FIRE_LOW;
            break;
        case COVER_STEP_LEAVE:
            drone->coverFlags |= DRONE_COVER_LEFT;
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)endState, 0);
        default:
            return false;
        }
        if (anim == 0xffff)
            return false;
        DroneAnim_CallAnim(anim, 0, endState, 0, 0.0f, 0, dcv);
        return true;
    }

    if (step == COVER_STEP_ARRIVE) {
        drone->coverFlags &= DRONE_COVER_SIDE_MASK;
        dcv->drone->coverMode = COVER_MODE_STAND_LEAN;
        if (DroneAnim_ForceCrouchCover(dcv, drone->coverNode)) {
            if (DroneAnim_CanCrouchLean(dcv, drone->coverNode))
                options[count++] = COVER_MODE_CROUCH_LEAN;
            if (DroneAnim_CanCrouchStepOut(dcv, drone->coverNode))
                options[count++] = COVER_MODE_CROUCH_STEP_OUT;
        } else {
            if (DroneAnim_CanStandLean(dcv, drone->coverNode))
                options[count++] = COVER_MODE_STAND_LEAN;
            if (DroneAnim_CanStandStepOut(dcv, drone->coverNode))
                options[count++] = COVER_MODE_STAND_STEP_OUT;
        }
        if (count == 0) {
            if (!DroneAnim_CanUseCornerAsLowCover(dcv, drone->coverNode)) {
                NF_WARN("Doing CornerCover with no behaviour"); // GC check (0x80089e54)
                return false;
            }
            options[count++] = COVER_MODE_AS_LOW;
        }
        dcv->drone->coverMode = (uchar)options[Rand_Rand(count)];
        dcv->drone->coverStep = COVER_STEP_ENTER;
        anim = DroneAnim_CoverModeAnim(dcv, dcv->drone->coverMode);
    } else if (step == COVER_STEP_CHANGE) {
        if (Rand_Rand(2) != 0)
            return false;
        if (DroneAnim_ForceCrouchCover(dcv, drone->coverNode)) {
            if (DroneAnim_CanCrouchLean(dcv, drone->coverNode))
                options[count++] = COVER_MODE_CROUCH_LEAN;
            if (DroneAnim_CanCrouchStepOut(dcv, drone->coverNode))
                options[count++] = COVER_MODE_CROUCH_STEP_OUT;
        }
        if (count == 0) {
            if (dcv->drone->distanceToTarget > 8.0f) {
                if (DroneAnim_CanCrouchLean(dcv, drone->coverNode))
                    options[count++] = COVER_MODE_CROUCH_LEAN;
                if (DroneAnim_CanCrouchStepOut(dcv, drone->coverNode))
                    options[count++] = COVER_MODE_CROUCH_STEP_OUT;
            }
            if (DroneAnim_CanStandLean(dcv, drone->coverNode))
                options[count++] = COVER_MODE_STAND_LEAN;
            if (DroneAnim_CanStandStepOut(dcv, drone->coverNode))
                options[count++] = COVER_MODE_STAND_STEP_OUT;
            if (count == 0) {
                if (!DroneAnim_CanUseCornerAsLowCover(dcv, drone->coverNode)) {
                    NF_WARN("Doing CornerCover with no behaviour"); // GC check (0x80089e54)
                    return false;
                }
                options[count++] = COVER_MODE_AS_LOW;
            }
        }
        ushort mode = options[Rand_Rand(count)];
        if (mode == dcv->drone->coverMode)
            return false;
        dcv->drone->coverStep = COVER_STEP_LEAVE;
        Drone_tag *changing = dcv->drone;
        anim = DroneAnim_CoverModeAnim(dcv, changing->coverMode);
        if (anim == 0xffff) {
            changing->coverStep = COVER_STEP_ENTER;
            anim = DroneAnim_CoverModeAnim(dcv, (uchar)mode);
            dcv->drone->coverStep = COVER_STEP_LEAVE;
        }
        dcv->drone->coverMode = (uchar)mode;
    } else {
        drone->coverStep = step;
        anim = DroneAnim_CoverModeAnim(dcv, dcv->drone->coverMode);
    }

    if (anim == 0xffff || !DroneAnim_CanDoAnimState(dcv, anim, 0))
        return false;
    DroneAnim_CallAnim(anim, 0, endState, 0, 0.0f, 0, dcv);
    return true;
}

// Turns the drone to face along its cover: a corner's facing (a quarter turn less on side 1, a half turn less as low
// cover), true; at a low node, or a corner used as one, towards its opponent instead, false. False with no node or
// no opponent. The angle passed is not read.
// AUTOINJECT
bool NDrone2_SetAngleToCover(DCVars_tag *dcv, float unused) {
    Drone_tag *drone = dcv->drone;
    if (drone->coverNode == NULL || drone->opponent == NULL)
        return false;
    CoverNode_tag *node = drone->coverNode;
    float turn = 0.0f;
    if (node->type == COVER_LOW)
        turn = -kPi;
    if ((drone->coverFlags & DRONE_COVER_SIDE_MASK) == 1)
        turn = -kHalfPi;
    float yaw = turn + node->rot.y;
    if (drone->coverMode == COVER_MODE_AS_LOW)
        yaw = yaw - kPi;
    _VECTOR toOpponent;
    Vec_Subtract(&drone->opponent->position, &node->pos, &toOpponent);
    if (dcv->drone->coverNode->type != COVER_LOW && dcv->drone->coverMode != COVER_MODE_AS_LOW) {
        dcv->drone->angleToSomePoint = yaw;
        return true;
    }
    dcv->drone->angleToSomePoint = maybeAtan2(toOpponent.x, toOpponent.z);
    return false;
}

// Moves the object the fraction of the way to pos in the plane (and in height with withHeight); within a centimetre
// it is put there
// AUTOINJECT
void NDrone2_SlideToPosition(_VECTOR *pos, float fraction, char withHeight, DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    if (Vec_Dist3D(&gameObj->position, pos) > 0.01f) {
        gameObj->position.x = (float)(((double)pos->x - gameObj->position.x) * fraction + gameObj->position.x);
        gameObj->position.z = (float)(((double)pos->z - gameObj->position.z) * fraction + gameObj->position.z);
        if (withHeight)
            gameObj->position.y = (float)(((double)pos->y - gameObj->position.y) * fraction + gameObj->position.y);
    } else {
        gameObj->position = *pos;
    }
}

// The cover lines, in multiplayer only (as the original has it), not while it talks or once dead: by level, the Tower
// levels and the PowerStation ones, and for RunForCover the Evil levels too (none for skin class 16)
// AUTOINJECT
void DroneFunc_RunForCoverTalk(DCVars_tag *dcv) {
    if (MPSettings.maybeDroneAIEnabled == 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone) || drone->health <= 0.0f)
        return;
    uint sfx;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        DroneFunc_InjuredSFX(dcv, 0xbd);
        return;
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        sfx = DroneFunc_ChooseSFX(0xd0, 0xd1);
        break;
    case HT_Level_EvilBase:
    case HT_Level_EvilSilo:
    case HT_Level_EvilBaseC:
        if (drone->skinClass == 16)
            return;
        sfx = DroneFunc_ChooseSFXByID(drone, 0x11b, 0x121);
        break;
    default:
        return;
    }
    if (sfx != 0)
        DroneFunc_InjuredSFX(dcv, sfx);
}

// AUTOINJECT
void DroneFunc_UnderCoverTalk(DCVars_tag *dcv) {
    if (MPSettings.maybeDroneAIEnabled == 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone) || drone->health <= 0.0f)
        return;
    uint sfx;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        sfx = DroneFunc_ChooseSFX(0xbd, 0xbe);
        break;
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        sfx = DroneFunc_ChooseSFX(0xcf, 0xd1);
        break;
    default:
        return;
    }
    if (sfx != 0)
        DroneFunc_InjuredSFX(dcv, sfx);
}

// AUTOINJECT
void DroneFunc_UnderCoverAimTalk(DCVars_tag *dcv) {
    if (MPSettings.maybeDroneAIEnabled == 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone) || drone->health <= 0.0f)
        return;
    uint sfx;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        sfx = DroneFunc_ChooseSFX(0xba, 0xbb);
        break;
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        sfx = DroneFunc_ChooseSFX(0xd0, 0xd1);
        break;
    default:
        return;
    }
    if (sfx != 0)
        DroneFunc_InjuredSFX(dcv, sfx);
}

// Kicks the object at the drone's kick node: its script plays on
// AUTOINJECT
void NDrone2_KickObject(DCVars_tag *dcv) {
    obj_tag *obj = dcv->drone->door;
    if (obj != NULL && obj->objectType == OBJECTTYPE_SCRIPTPLAYER)
        SP_UnPause(obj);
}

// Opens the drone's door, if closed or closing, as if it used it
// AUTOINJECT
void NDrone2_OpenDoor(DCVars_tag *dcv) {
    obj_tag *door = dcv->drone->door;
    if (door == NULL || door->objectType != OBJECTTYPE_DOOR || !(door->curState & (DoorState_Closed | DoorState_Closing)))
        return;
    Door_Activate(door, dcv->gameObj, 0);
    dcv->drone->doorFrame = GameState.NumFramesUnpaused;
}

// Whether the drone's door is open; true with no route or no door
// AUTOINJECT
bool NDrone2_DoorIsOpen(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->activeRoute == NULL || drone->door == NULL || drone->door->objectType != OBJECTTYPE_DOOR)
        return true;
    return drone->door->curState == DoorState_Open;
}

// The closed door nearest the drone, within 2 m, becomes its door (or none)
// AUTOINJECT
void NDrone2_FindElevatorDoor(DCVars_tag *dcv) {
    obj_tag *nearest = NULL;
    float nearestSq = 4.0f;
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType != OBJECTTYPE_DOOR || !(obj->curState & DoorState_Closed))
            continue;
        double dz = (double)obj->position.z - dcv->gameObj->position.z;
        double dy = (double)obj->position.y - dcv->gameObj->position.y;
        double dx = (double)obj->position.x - dcv->gameObj->position.x;
        double sq = dx * dx + dy * dy + dz * dz;
        if (sq < nearestSq) {
            nearestSq = (float)sq;
            nearest = obj;
        }
    }
    dcv->drone->door = nearest;
}

// Moves towards the route's node offset places on from the current one (before the first, the route's start; after
// the last, its destination)
// AUTOINJECT
void NDrone2_MoveTo_RouteNode(DCVars_tag *dcv, short offset, AIRoute_tag *route) {
    CelPos_tag target;
    short index = AINetwork_RouteNodeOffset(route->current, offset, route);
    if (index < 0) {
        target = route->start;
    } else if (index >= route->count) {
        target = route->dest;
    } else {
        MAPNODE_tag *node = route->path->runtimeNodes[route->nodes[index]].fileNode;
        Vec_Copy(&node->pos, &target.pos);
        target.cel = node->cel;
    }
    NDrone2_maybeCalcDistanceAndDirToTarget(dcv, &target);
}

// A route reaching one of its special nodes (LinkCreep_Handler): Mayhew's lines, a wait (once per node, while calm),
// a door to open (with the next node a door node too) or let go of once open, an object to kick (once), an action
// anim, a mission wait while channel 0x99 is off, or a fiddle. The state left is kept for the door, kick, action and
// fiddle states to go back to. Past the route's end the creep is cleared. False only before the first node.
// AUTOINJECT
bool NDrone2_ReachedDestNode(AIRoute_tag *route, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    short current = route->current;
    if (current < 0)
        return false;
    if ((ushort)current >= route->count) {
        memset(&route->creep, 0, sizeof(route->creep));
        return true;
    }

    AINode_tag *nodes = route->path->runtimeNodes;
    MAPNODE_tag *node = nodes[route->nodes[current]].fileNode;
    MAPNODE_tag *next = NULL;
    if ((ushort)(current + 1) < route->count)
        next = nodes[route->nodes[current + 1]].fileNode;

    if (node->flags & 0x10000)
        Drone_MayhewTalk(0x44);
    if (node->flags & 0x40000)
        Drone_MayhewTalk(0x46);
    if (node->flags & 0x20000)
        Drone_MayhewTalk(0x45);
    if (node->flags & 0x80000)
        Drone_MayhewTalk(0x47);

    if ((node->flags & AINODE_WAIT) && dcv->drone->alertness < 0.33f) {
        if ((ushort)route->current != drone->waitNode) {
            drone->waitNode = current;
            Drone_SM_SendMsgSelf(DRONE_MSG_StateTimeout, NULL, 1, dcv->drone->sm.curState, dcv);
            return true;
        }
        drone->waitNode = 0xffff;
    }

    StateMachineInfo_tag *sm = dcv->aiStateMachine;
    if (node->flags & AINODE_DOOR) {
        if (next != NULL && (next->flags & AINODE_DOOR)) {
            obj_tag *door = nodes[route->nodes[current]].door;
            if (door == drone->door)
                return true;
            drone->door = door;
            drone->actionNode = current;
            if (sm->curState != DSTATE_OpenDoor)
                drone->nodeReturnState = (short)sm->curState;
            Drone_SM_SetState(sm, DSTATE_OpenDoor, 0);
            return true;
        }
        if (drone->door != NULL && NDrone2_DoorIsOpen(dcv))
            drone->door = NULL;
        return true;
    }
    if (node->flags & AINODE_KICK) {
        if (next == NULL || !(next->flags & AINODE_KICK)) {
            drone->door = NULL;
            return true;
        }
        obj_tag *obj = nodes[route->nodes[current]].door;
        if (obj == drone->door)
            return true;
        drone->door = obj;
        drone->actionNode = current;
        if (node->flags & AINODE_KICKED)
            return true;
        node->flags |= AINODE_KICKED;
        if (sm->curState != DSTATE_KickObject)
            drone->nodeReturnState = (short)sm->curState;
        Drone_SM_SetState(sm, DSTATE_KickObject, 0);
        return true;
    }
    if (node->flags & AINODE_ACTION_ANIM) {
        drone->actionNode = current;
        if (sm->curState != DSTATE_ActionAnim)
            drone->nodeReturnState = (short)sm->curState;
        Drone_SM_SetState(sm, DSTATE_ActionAnim, 0);
        return true;
    }
    if ((node->flags & AINODE_MISSION_WAIT) && switch_channels[MISSION_WAIT_CHANNEL] == 0) {
        if (sm->curState == DSTATE_AllyLeadMissionWait)
            return true;
        drone->initialState = (short)sm->curState;
        Drone_SM_SetState(sm, DSTATE_AllyLeadMissionWait, 0);
        return true;
    }
    if (node->flags & AINODE_FIDDLE) {
        drone->actionNode = current;
        if (sm->curState != DSTATE_StandFiddle)
            drone->nodeReturnState = (short)sm->curState;
        Drone_SM_SetState(sm, DSTATE_StandFiddle, 0);
    }
    return true;
}

// Plays a script outside the anim tables: the call is cleared and the slot set to the anim state, the row after it
// and the end state and message. False when there is no script.
// AUTOINJECT
bool DroneAnim_SetScript(HASHCODE script, ushort animState, ushort nextRow, uchar blend, short endState, uint endMsg,
                         uint flags, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    memset(&drone->animCall, 0, sizeof(drone->animCall));
    drone->animNextRow = nextRow;
    drone->animState = animState;
    drone->animEndState = endState;
    drone->animEndBlend = blend;
    drone->animCallSpeedTaken = 0.0f;
    drone->animCallSpeed = 0.0f;
    drone->animMode = 1;
    drone->animPriority = 99;
    drone->animRow = 0;
    drone->animEndMsg = endMsg;
    drone->animMoveSpeed = 0.0f;
    drone->unknown481 = 0;
    drone->aimMode = 0;
    drone->unknown46c = flags;
    drone->animCallTaken = 1;
    if (script == 0)
        return false;
    DroneAnim_SetHTAnim(script, flags, 0, drone, 8, 1.0f);
    drone->animEndMsg = endMsg;
    DroneAnim_OnStartAnim(drone);
    return true;
}

// ---- the cover states

// Runs to the cover node found (aiPoint) with the combat moves, sliding into place over the last metre, and takes cover
// there (UnderCoverInit); held up, it fires at its opponent. It attacks instead once the node is no good, or the
// opponent is seen within 4 m.
// AUTOINJECT
bool NDrone2_DSTATE_RunForCover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        if (!dcv->drone->aiPoint.set)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        DroneFunc_RunForCoverTalk(dcv);
        return true;

    case DRONE_MSG_Exit:
        if (drone->sm.nextState != DSTATE_UnderCoverInit)
            NDrone2_DisownCover(dcv);
        dcv->drone->flags &= ~0x800;
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        Drone_tag *self = dcv->drone;
        if (!self->aiPoint.set)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        if (!NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        self = dcv->drone;
        if ((self->sightFlags & DRONE_SIGHT_SEES_OPPONENT) && self->distanceToTarget < 4.0f)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        switch (NDrone2_MoveToGoalPosition(dcv, (CelPos_tag *)&self->aiPoint.pos, self->aiPoint.radius, 0)) {
        case AIROUTE_FOLLOWING:
        case AIROUTE_DIRECT:
            if (drone->activeRoute->remaining < 1.0f)
                NDrone2_SlideToPosition(&drone->coverPos.pos, 0.025f, 1, dcv);
            DroneAnim_SetCombatMoveAnim(dcv, dcv->drone->activeRoute->remaining);
            return true;
        case AIROUTE_ARRIVED:
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverInit, 0);
        case AIROUTE_DOOR:
            return true;
        case AIROUTE_CREEP_FAILED2:
            NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
            if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)) {
                drone->firing = 0;
                DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
                return true;
            }
            drone->firing = drone->gunThroughWall == 0;
            DroneAnim_CallAnim(DANIM_STAND_AIM, 0, 0, 0, 0.0f, 0, dcv);
            if (dcv->drone->burstOver) {
                if (drone->firing)
                    DroneWeap_Fire(1, dcv);
            } else if (drone->firing) {
                DroneAnim_PlayFiringAnim(dcv);
            }
            return true;
        default:
            Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
            return true;
        }
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case 0x1b:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// At the cover node: the anim into cover, after which it waits there (UnderCoverIdle), sliding into place and turning
// along the cover.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_CoverAnim(dcv, COVER_STEP_ARRIVE, DSTATE_UnderCoverIdle);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        dcv->drone->flags &= ~0x800;
        DroneFunc_UnderCoverTalk(dcv);
        drone->animCallSpeedTaken = 0.0f;
        return true;

    case DRONE_MSG_Exit:
        if (drone->sm.nextState != DSTATE_UnderCoverIdle)
            NDrone2_DisownCover(dcv);
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        dcv->drone->unknown38 = 1;
        NDrone2_SlideToPosition(&drone->coverPos.pos, 0.08f, 0, dcv);
        NDrone2_SetAngleToCover(dcv, kPi);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// Behind cover with a full clip (but skin class 0x13 in the PowerStation levels). Every few seconds (timer A) it aims
// out (UnderCoverAim) while the node is good, may run for other cover, and after three tries attacks. With property
// 0x10 it stays until it sees its opponent or the player comes within 3 m. It leaves (UnderCoverLeaveNow) once the node
// is no good or it is over 1.5 m from it.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_CoverAnim(dcv, COVER_STEP_IDLE, 0);
        if (drone->animSet != 0x13 || (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
                                       GameState.CurrentLevelHashcode != HT_Level_PowerStationA2))
            dcv->drone->clip = dcv->drone->clipSize;
        if (behaviour_util_getProperty(DRONE_PROP_10, dcv->drone->currentBehaviour) == 0)
            SetTimer(&dcv->drone->timerA,
                     Ftol(Float_FRand(150.0f) * FRAME_RATE_DIV + (double)GameState.NumFramesUnpaused));
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        if (drone->animMode == 2)
            dcv->drone->unknown38 = 0;
        dcv->drone->stateScratch = 0;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        behaviour_util_setProperty(DRONE_PROP_10, dcv->drone->currentBehaviour, 0);
        return true;

    case DRONE_MSG_Update: {
        if (behaviour_util_getProperty(DRONE_PROP_10, dcv->drone->currentBehaviour) != 0) {
            if (!(dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)) {
                if (NDrone2_Player() == NULL)
                    return true;
                if (!(Vec_Dist2D(&dcv->gameObj->position, &NDrone2_Player()->position) < 3.0f))
                    return true;
            }
            behaviour_util_setProperty(DRONE_PROP_10, dcv->drone->currentBehaviour, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        }
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SlideToPosition(&drone->coverPos.pos, 0.08f, 0, dcv);
        NDrone2_SetAngleToCover(dcv, kPi);
        if (drone->unknown434 != NULL)
            return true;
        Drone_tag *self = dcv->drone;
        if (NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 0) &&
            !(Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->aiPoint.pos) > 1.5f))
            return true;
        return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
    }

    case DRONE_MSG_TimerA: {
        if (Rand_Rand(5) == 0 && NDrone2_CoverAvailable(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunForCover, 0);
        Drone_tag *self = dcv->drone;
        if (NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverAim, 0);
        if (NDrone2_CoverAvailable(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunForCover, 0);
        if (dcv->drone->stateScratch++ > 2)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_Attack, 0);
        SetTimer(&dcv->drone->timerA, Ftol(Float_FRand(150.0f) * FRAME_RATE_DIV + (double)GameState.NumFramesUnpaused));
        return true;
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_UnderCoverLeaveNow);
        return true;

    default:
        return false;
    }
}

// Out of cover to aim (the aim step), then UnderCoverFire.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverAim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_CoverAnim(dcv, COVER_STEP_AIM, DSTATE_UnderCoverFire);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        DroneFunc_UnderCoverAimTalk(dcv);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_UnderCoverLeaveNow);
        return true;

    default:
        return false;
    }
}

// Fires from cover until timer A (set by the firing), or now and then while the player aims at it, and goes back
// (UnderCoverReturn); once the node is no good it goes back from a lean, or leaves. Skin class 0x13 in the
// PowerStation levels fires as a sniper (UnderCoverSniperFire).
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (drone->animSet == 0x13 && (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
                                       GameState.CurrentLevelHashcode == HT_Level_PowerStationA2))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverSniperFire, 0);
        dcv->drone->flags |= 0x10000;
        DroneWeap_Fire(1, dcv);
        NDrone2_AttackTalk(dcv);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        Drone_tag *self = dcv->drone;
        if (!NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 1)) {
            if (drone->coverNode->type == COVER_CORNER &&
                (drone->coverMode == COVER_MODE_CROUCH_LEAN || drone->coverMode == COVER_MODE_STAND_LEAN))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        }
        if (!dcv->drone->burstOver && drone->firing) {
            DroneAnim_PlayFiringAnim(dcv);
            return true;
        }
        if (drone->inPlayersSights && Rand_Rand(3) == 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            DroneWeap_Fire(1, dcv);
        return true;
    }

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_UnderCoverLeaveNow);
        return true;

    default:
        return false;
    }
}

// A sniper firing from cover: a shot a second (timer A) for six seconds (timer B), then back; with the clip empty
// once it has fired, it reloads (UnderCoverSniperReload). At the end of a burst it goes back early when its opponent
// aims at it, or now and then when the player faces it.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverSniperFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        NDrone2_AttackTalk(dcv);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT);
        SetTimer(&dcv->drone->timerB, GameState.NumFramesUnpaused + FRAME_RATE_INT * 6);
        drone->firing = 0;
        drone->fired = 0;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        if (drone->fired && dcv->drone->clip <= 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverSniperReload, 0);
        if (drone->animMode != 2 || !drone->firing)
            return true;
        Drone_tag *self = dcv->drone;
        if (!NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 0)) {
            if (drone->coverNode->type == COVER_CORNER &&
                (drone->coverMode == COVER_MODE_CROUCH_LEAN || drone->coverMode == COVER_MODE_STAND_LEAN))
                return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        }
        if (!drone->burstOver)
            return true;
        if (DroneWeap_OpponentIsAimingAtMe(dcv, 7.5f) || (NDrone2_BondIsFacingMe(dcv, 45.0f) && Rand_Rand(3) == 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
        return true;
    }

    case DRONE_MSG_TimerA:
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerB:
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverReturn, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_UnderCoverLeaveNow);
        return true;

    default:
        return false;
    }
}

// The reload anim, then back to sniping from cover. An impact takes it to state 0. Its Exit is not handled.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverSniperReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        dcv->drone->firing = 0;
        DroneAnim_CallAnim(DANIM_RELOAD, 0, DSTATE_UnderCoverSniperFire, 0, 0.0f, 0, dcv);
        dcv->drone->clip = dcv->drone->clipSize;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Global, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Global, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Global);
        return true;

    default:
        return false;
    }
}

// Back into cover (the return step), then a change of mode (UnderCoverTypeChange); with no anim, it leaves. Each
// update marks the cover lost once the node is no good or it is over 1.5 m from it.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverReturn(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        if (!DroneAnim_CoverAnim(dcv, COVER_STEP_RETURN, DSTATE_UnderCoverTypeChange))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeave, 0);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        DroneFunc_UnderCoverTalk(dcv);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update: {
        NDrone2_SeenAndAttacking(dcv);
        Drone_tag *self = dcv->drone;
        if (!NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 0) ||
            Vec_Dist2D(&dcv->gameObj->position, &dcv->drone->aiPoint.pos) > 1.5f)
            dcv->drone->coverFlags |= DRONE_COVER_LOST;
        return true;
    }

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_UnderCoverLeaveNow, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_UnderCoverLeaveNow);
        return true;

    default:
        return false;
    }
}

// Back in cover: with the cover lost it leaves; with the node no good it runs for other cover if there is any;
// otherwise it may change mode (the change step), and waits again (UnderCoverIdle). No impact is handled here.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverTypeChange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        Drone_tag *self = dcv->drone;
        if (self->coverFlags & DRONE_COVER_LOST)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        if (!NDrone2_CoverNodeOK(dcv, self->coverNode, self->coverFlags & DRONE_COVER_SIDE_MASK, 1) &&
            NDrone2_CoverAvailable(dcv, 1))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_RunForCover, 0);
        if (!DroneAnim_CoverAnim(dcv, COVER_STEP_CHANGE, DSTATE_UnderCoverIdle))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverIdle, 0);
        dcv->drone->atCornerCover = dcv->drone->coverNode->type != COVER_LOW;
        return true;
    }

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    default:
        return false;
    }
}

// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverLeave(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_UnderCoverLeaveNow, 0);
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->atCornerCover = 0;
        return true;

    default:
        return false;
    }
}

// Gives up the cover node and steps out into the attack: from the cover rows 0x87 and 0xab to the side its opponent
// is on.
// AUTOINJECT
bool NDrone2_DSTATE_UnderCoverLeaveNow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->coverFlags |= DRONE_COVER_LEFT;
        NDrone2_DisownCover(dcv);
        dcv->drone->atCornerCover = 0;
        if (drone->animRow != DANIM_ROW_COVER_A && drone->animRow != DANIM_ROW_COVER_B) {
            drone->animState = 0;
            DroneAnim_CallAnim(DANIM_STAND_AIM, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
            return true;
        }
        DroneAnim_CallAnim(DANIM_STAND_AIM, dcv->drone->opponentBearing > 0.0f ? 1 : 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Exit:
        NDrone2_DisownCover(dcv);
        dcv->drone->atCornerCover = 0;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Attack, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_Attack);
        return true;

    default:
        return false;
    }
}

// ---- the grenade throw and the snipers

// The throw anim, facing its opponent, then the attack. Impacts take it to state 0; no smoke, stun or explosive
// message is handled here.
// AUTOINJECT
bool NDrone2_DSTATE_GrenadeThrow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(DANIM_THROW_GRENADE, 0, DSTATE_Attack, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_SetAngleToObj(dcv, dcv->drone->opponent, 0.0f, 0);
        return true;

    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Global, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_Global, 1);
        return true;

    default:
        return false;
    }
}

// A sniper waiting for its opponent, or an alert.
// AUTOINJECT
bool NDrone2_DSTATE_SniperIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x2000;
        dcv->drone->flags |= 0x10;
        DroneAnim_CallAnim(DANIM_SNIPER_IDLE, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_Update:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyLookForOpponent(dcv);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ConsideredAlerted:
        DroneFunc_DoForcedAttack(dcv);
        return true;

    case 0x11:
    case 0x12:
    case DRONE_MSG_HeardNoise:
    case DRONE_MSG_DroneAlert:
    case 0x16:
    case DRONE_MSG_GlobalAlarm:
        dcv->aiStateMachine->scratch1c = (short)DroneVision_EnemyAlerts(dcv, msg);
        if (dcv->aiStateMachine->scratch1c != 0)
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->aiStateMachine->scratch1c, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_SniperAim, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_SniperAim, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_SniperAim);
        return true;

    default:
        return false;
    }
}

// Aiming at its opponent (as far as it can see), until it sees it (SniperFire). It takes the nearest closed door for
// its own, and in Tower2Elevator keeps opening it.
// AUTOINJECT
bool NDrone2_DSTATE_SniperAim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x10;
        NDrone2_FindElevatorDoor(dcv);
        Drone_AlertStatusSet(1, dcv);
        DroneAnim_CallAnim(DANIM_STAND_AIM, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->combatRanges[4] = 1000.0f;
        dcv->drone->viewRange = 1000.0f;
        NDrone2_AttackTalk(dcv);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        if (dcv->drone->sightFlags & DRONE_SIGHT_SEES_OPPONENT)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperFire, 0);
        if (GameState.CurrentLevelHashcode == HT_Level_Tower2Elevator)
            NDrone2_OpenDoor(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_SniperFire, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_SniperFire);
        return true;

    default:
        return false;
    }
}

// Firing at its opponent, a shot at least a second apart (timer A): the alternative attack when it can, a reload with
// the clip empty, aiming again once unseen for over 15 frames. In Tower2Elevator it keeps opening its door.
// AUTOINJECT
bool NDrone2_DSTATE_SniperFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10;
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        DroneAnim_CallAnim(DANIM_STAND_AIM, 0, 0, 0, 0.0f, 0, dcv);
        dcv->drone->flags |= 0x10000;
        drone->firing = 0;
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT);
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        NDrone2_AttackTalk(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        if (GameState.CurrentLevelHashcode == HT_Level_Tower2Elevator)
            NDrone2_OpenDoor(dcv);
        if (NDrone2_CanAltAttack(dcv, 0))
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AltAttack, 0);
        if (dcv->drone->clip <= 0)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperReload, 0);
        if (dcv->drone->unseenFrames > 15)
            return Drone_SM_SetState(dcv->aiStateMachine, DSTATE_SniperAim, 0);
        if (!dcv->drone->burstOver && drone->firing) {
            DroneAnim_PlayFiringAnim(dcv);
            return true;
        }
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_TimerA:
        DroneWeap_Fire(1, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactBullet:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Twenty seconds (timer A), then the reload anim and back to firing; it aims meanwhile and keeps opening its door.
// The clip is filled on leaving.
// AUTOINJECT
bool NDrone2_DSTATE_SniperReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        dcv->drone->flags |= 0x10;
        SetTimer(&dcv->drone->timerA,
                 Ftol((double)FRAME_RATE_DIV * 20.0f + (double)GameState.NumFramesUnpaused));
        dcv->drone->firing = 0;
        return true;

    case DRONE_MSG_Exit:
        dcv->drone->clip = dcv->drone->clipSize;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        DroneWeap_AimTarget_IsOpponent(1, 1, dcv);
        NDrone2_OpenDoor(dcv);
        return true;

    case DRONE_MSG_TimerA:
        DroneAnim_CallAnim(DANIM_RELOAD, 0, DSTATE_SniperFire, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_SniperFire, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, DSTATE_SniperFire, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, DSTATE_SniperFire);
        return true;

    default:
        return false;
    }
}

// ---- the route's door, kick, action and fiddle nodes

// Opens its door and waits for it, then goes back to the state it left. An alert drone with one of the door
// properties (0x14-0x16) at a swing door walks through with the door anim; any other plays a stand anim. Every two
// seconds (timer A) it tries the door again.
// AUTOINJECT
bool NDrone2_DSTATE_OpenDoor(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter: {
        dcv->drone->flags |= 0x10000;
        if (NDrone2_DoorIsOpen(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        dcv->drone->unknown38 = 1;
        Drone_tag *self = dcv->drone;
        obj_tag *door = self->door;
        if (door != NULL && door->subState == DoorKind_Swing &&
            (self->baseAlertness1 >= 0.66f || self->alertness >= 0.66f) &&
            (behaviour_util_getProperty(DRONE_PROP_14, self->currentBehaviour) != 0 ||
             behaviour_util_getProperty(DRONE_PROP_15, dcv->drone->currentBehaviour) != 0 ||
             behaviour_util_getProperty(DRONE_PROP_16, dcv->drone->currentBehaviour) != 0)) {
            NDrone2_MoveTo_RouteNode(dcv, 1, dcv->drone->activeRoute);
            DroneAnim_CallAnim(DANIM_USE_DOOR, 0, 0, 0, 0.0f, 0, dcv);
            NDrone2_OpenDoor(dcv);
        } else {
            if (dcv->drone->alertness < 1.0f)
                DroneAnim_CallAnim(DANIM_STAND_IDLE, 1, 0, 0, 0.0f, 0, dcv);
            else
                DroneAnim_CallAnim(DANIM_STAND_ALERT, 0, 0, 0, 0.0f, 0, dcv);
            NDrone2_OpenDoor(dcv);
        }
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;
    }

    case DRONE_MSG_Exit:
        dcv->drone->unknown38 = 0;
        return true;

    case DRONE_MSG_Update:
        dcv->drone->unknown38 = 1;
        if (NDrone2_DoorIsOpen(dcv))
            return Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        return true;

    case DRONE_MSG_TimerA:
        NDrone2_OpenDoor(dcv);
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;

    case DRONE_MSG_AnimEvent:
        NDrone2_PlaySFX(dcv, SFX_DOOR_OPEN, NULL, 1, 1.0f);
        NDrone2_OpenDoor(dcv);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// The kick anim; its event kicks the object and goes back to the state it left. Timer A kicks again every two seconds
// once set, which nothing here does.
// AUTOINJECT
bool NDrone2_DSTATE_KickObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_CallAnim(DANIM_USE_DOOR, 0, 0, 0, 0.0f, 0, dcv);
        return true;

    case DRONE_MSG_TimerA:
        NDrone2_KickObject(dcv);
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;

    case DRONE_MSG_AnimEvent:
        NDrone2_PlaySFX(dcv, SFX_KICK, NULL, 1, 1.0f);
        NDrone2_KickObject(dcv);
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Plays the drone's own script (key 1); at its end it crouches to aim (AimCrouch), and an anim event takes it back to
// the state it left.
// AUTOINJECT
bool NDrone2_DSTATE_ActionAnim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_SetScript(dcv->drone->playScript, DANIM_STAND_AIM, 0x32, 0x1e, 0, DRONE_MSG_ScriptEnd, 0, dcv);
        dcv->drone->flags |= 0x400000;
        return true;

    case DRONE_MSG_Update:
        NDrone2_SeenAndAttacking(dcv);
        return true;

    case DRONE_MSG_ScriptEnd:
        dcv->drone->flags &= ~0x400000;
        Drone_SM_SetState(dcv->aiStateMachine, DSTATE_AimCrouch, 0);
        return true;

    case DRONE_MSG_TimerA:
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 2);
        return true;

    case DRONE_MSG_AnimEvent:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}

// Fiddles with something for five seconds (timer A), then goes back to the state it left.
// AUTOINJECT
bool NDrone2_DSTATE_StandFiddle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {
    switch (msg->msgType) {
    case DRONE_MSG_Null:
        return true;

    case DRONE_MSG_Enter:
        dcv->drone->flags |= 0x10000;
        DroneAnim_CallAnim(DANIM_IDLE_FIDDLE, 0, 0, 0, 0.0f, 0, dcv);
        SetTimer(&dcv->drone->timerA, GameState.NumFramesUnpaused + FRAME_RATE_INT * 5);
        return true;

    case DRONE_MSG_Update:
        dcv->drone->unknown38 = 1;
        return true;

    case DRONE_MSG_TimerA:
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)dcv->drone->nodeReturnState, 0);
        return true;

    case DRONE_MSG_ImpactPunch:
    case DRONE_MSG_ImpactBullet:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 0);
        return true;

    case DRONE_MSG_ImpactTaser:
    case DRONE_MSG_ImpactExplosive:
    case DRONE_MSG_ImpactSmoke:
    case DRONE_MSG_ImpactStunGrenade:
    case DRONE_MSG_ImpactStunDart:
        DroneFunc_HandleImpact(dcv, msg, dcv->aiStateMachine->curState, 1);
        return true;

    case DRONE_MSG_ExplosiveSeen:
        DroneFunc_ConsiderExplosive(dcv, (obj_tag *)msg->extraData, (short)dcv->aiStateMachine->curState);
        return true;

    default:
        return false;
    }
}
