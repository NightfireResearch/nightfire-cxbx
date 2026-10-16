// The drone's side of animation: asking for an anim state (DroneAnim_CallAnim) and the idle and death anims built on
// it. See docs/drone/behaviours/README.md section 6.

#include "DroneAnim.h"
#include "NDrone2.h"
#include "DroneTables.h"
#include "Behaviour.h"
#include "../../game.h"             // GameState
#include "../../engine/Anim.h"      // AnimState
#include "../../engine/Collide.h"   // HITDATA_tag
#include "../../math/math.h"        // maybeAtan2, Vec_AngleDifference
#include "../../util/Random.h"      // Rand_Rand

#include <math.h>
#include <string.h>

// Whether the drone has a script for the variant of the anim state, in its own anim set or else in set 0. State 0
// always does; one beyond the table never.
// AUTOINJECT
bool DroneAnim_CanDoAnimState(DCVars_tag *dcv, short state, short variant) {
    if (state == 0)
        return true;
    if (state < 0 || state >= NUM_DRONE_ANIM_STATES)
        return false;
    ushort row = DroneAnimStates[state].firstRow + variant;
    return Drone_AnimTables[row][(ushort)dcv->drone->animSet].scriptLo != 0 || Drone_AnimTables[row][0].scriptLo != 0;
}

// The move state of the row playing (Drone_AnimInfo.moveState); 0 with no drone or no row.
// AUTOINJECT
uchar DroneAnim_CurrentMoveState(Drone_tag *drone) {
    if (drone == NULL || drone->animRow == 0)
        return 0;
    return Drone_AnimInfo[drone->animRow].moveState;
}

// Asks for an anim state, to be played by DroneAnim_CallHandler. One the drone has no script for gives way to a
// stand-in, and failing that to the stand idle. When it already plays, only the end action is taken, now: the
// state change and message DroneAnim_SetEndAIState makes. Asking again for what is already asked changes nothing.
// AUTOINJECT
bool DroneAnim_CallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                        DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;

    // Only these skin classes have the third variant of 0x3d
    if (state == 0x3d && variant == 3) {
        switch ((ushort)drone->skinClass) {
        case 1:
        case 5:
        case 6:
        case 7:
        case 16:
            break;
        default:
            variant = 2;
            break;
        }
    }

    if (state == 0 || !DroneAnim_CanDoAnimState(dcv, state, 0)) {
        switch (state) {
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x29:
        case 0x2a:
        case 0x3d:
        case 0x4a:
        case 0x4f:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x5d:
        case 0x5e:
        case 0x61:
            state = 0x0a;
            break;
        case 0x0f:
        case 0x36:
            state = 0x35;
            break;
        case 0x1f:
        case 0x62:
            state = 0x1e;
            break;
        case 0x22:
        case 0x60:
            state = 0x21;
            break;
        case 0x25:
        case 0x26:
        case 0x27:
            state = DANIM_STAND_IDLE;
            break;
        case 0x4c:
            state = (ushort)drone->animState;
            break;
        case 0x57:
            if (DroneAnim_CanDoAnimState(dcv, 0x11, 0))
                state = 0x11;
            else if (DroneAnim_CanDoAnimState(dcv, 0x19, 0))
                state = 0x19;
            else
                state = 0x21;
            break;
        default:
            NDrone2_ObjectIsArmed(drone->gameObj);  // the answer is not used
            state = 0x21;
            break;
        }
        if (!DroneAnim_CanDoAnimState(dcv, state, 0))
            state = DANIM_STAND_IDLE;
    }

    DroneAnimCall *call = &drone->animCall;
    if (state == (ushort)drone->animState) {
        drone->animEndState = endState;
        drone->animEndMsg = endMsg;
        drone->animCallSpeed = speed;
        memset(call, 0, sizeof(*call));
        if (endState != 0 || endMsg != 0) {
            DroneAnim_SetEndAIState(drone, drone->animEndState, drone->animEndMsg);
            drone->animEndState = 0;
            drone->animEndMsg = 0;
        }
        return true;
    }

    if (state == call->state && variant == call->variant && endState == drone->animEndState &&
        endMsg == drone->animEndMsg && speed == drone->animCallSpeed)
        return true;

    memset(call, 0, sizeof(*call));
    call->state = state;
    call->fromState = drone->animState;
    call->variant = variant;
    drone->animCallSpeed = speed;
    call->pending = 1;
    drone->animEndMsg = endMsg;
    drone->animEndState = endState;
    call->frame = GameState.NumFramesUnpaused;
    call->kind = DroneAnimStates[state].kind;
    call->priority = DroneAnimStates[state].priority;
    drone->unknown477 = 0;
    // The original ends with "if (sm.id == 3) sm.id = 3", which changes nothing.
    return true;
}

// A stand idle anim, at most one a while when `wait` is set: in Idle (and PartyGirl with property 0x47) one of four
// for 5 to 9 seconds, else the plain one; skin class 16 always 0x26 if it has it.
// AUTOINJECT
bool DroneAnim_SetStandIdleAnim(DCVars_tag *dcv, char wait) {
    Drone_tag *drone = dcv->drone;
    short state = DANIM_STAND_IDLE;
    uint frames = 0;
    bool partyVariations = behaviour_util_getProperty(DRONE_PROP_47, drone->currentBehaviour) != 0;

    if (wait && drone->idleAnimFrame > GameState.NumFramesUnpaused)
        return false;

    uint variations = 0;
    if (drone->sm.curState == DSTATE_Idle)
        variations = 3;
    else if (drone->sm.curState == DSTATE_PartyGirl && partyVariations)
        variations = 4;
    if (variations != 0) {
        state = DANIM_STAND_IDLE + Rand_Rand(variations);
        frames = (Rand_Rand(5) + 5) * FRAME_RATE_INT;
    }

    if (drone->skinClass == 16)
        state = 0x26;
    if (state < DANIM_STAND_IDLE || state > DANIM_STAND_IDLE + 3)
        state = DANIM_STAND_IDLE;
    else if (state != DANIM_STAND_IDLE && !DroneAnim_CanDoAnimState(dcv, state, 0))
        state = DANIM_STAND_IDLE;

    DroneAnim_CallAnim(state, 0, 0, 0, 0.0f, 0, dcv);
    drone->idleAnimFrame = GameState.NumFramesUnpaused + frames;
    return true;
}

// A walk idle anim: on patrol one of three by the drone's id, in EvilBase always 0x39 if it has it.
// AUTOINJECT
bool DroneAnim_SetWalkIdleAnim(DCVars_tag *dcv, char wait) {
    Drone_tag *drone = dcv->drone;
    short state = DANIM_WALK_IDLE;

    if (wait && drone->idleAnimFrame > GameState.NumFramesUnpaused)
        return false;

    if (drone->sm.curState == DSTATE_Patrol)
        state = DANIM_WALK_IDLE + (drone->sm.id + 1) % 3;

    if (GameState.CurrentLevelHashcode == HT_Level_EvilBase)
        state = 0x39;
    if (state < DANIM_WALK_IDLE || state > DANIM_WALK_IDLE + 2)
        state = DANIM_WALK_IDLE;
    else if (state != DANIM_WALK_IDLE && !DroneAnim_CanDoAnimState(dcv, state, 0))
        state = DANIM_WALK_IDLE;

    DroneAnim_CallAnim(state, 0, 0, 0, 0.0f, 0, dcv);
    drone->idleAnimFrame = GameState.NumFramesUnpaused;
    return true;
}

// The death anim, ending in endState. Shot in bone 4 or 5 while the player aims (glb_players[0] is not checked), it
// falls one way or the other by where the shot came from; otherwise the plain death, a random variant when the
// playing row allows it, with a death cry.
// AUTOINJECT
bool DroneAnim_LocationDeathAnim(DCVars_tag *dcv, short endState) {
    Drone_tag *drone = dcv->drone;
    HITDATA_tag *hit = drone->lastHit;
    if (hit != NULL && (glb_players[0]->animState->animFlags & 1) && (hit->hitBoneIdx == 5 || hit->hitBoneIdx == 4)) {
        float shotYaw = maybeAtan2(hit->maybehitDirection.x, hit->maybehitDirection.z);
        float turn = Vec_AngleDifference(shotYaw, dcv->gameObj->rotation.y);
        DroneAnim_CallAnim(DANIM_DEATH_SHOT, fabsf(turn) > (float)M_PI_2 ? 0 : 1, endState, 0, 0.0f, 0, dcv);
        return true;
    }

    drone->headShot = 0;
    uint variant = 0;
    if (drone->unknown481 != 0)
        variant = Rand_Rand(4);
    NDrone2_DeathTalk(dcv);
    DroneAnim_CallAnim(DANIM_DEATH, variant, endState, 0, 0.0f, 0, dcv);
    return true;
}
