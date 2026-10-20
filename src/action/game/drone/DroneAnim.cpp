// The drone's side of animation: asking for an anim state (DroneAnim_CallAnim) and the idle and death anims built on
// it. See docs/drone/behaviours/README.md section 6.

#include "DroneAnim.h"
#include "NDrone2.h"
#include "DroneTables.h"
#include "Behaviour.h"
#include "../../game.h"             // GameState
#include "../../engine/Anim.h"      // AnimState
#include "../../engine/Collide.h"   // HITDATA_tag
#include "../../util/Random.h"      // Rand_Rand
#include "DroneWeapon.h"            // DroneWeap_FireWeapon
#include "BOT.h"                    // BOT_getMovementSpeedMul
#include "../mp/multiplayer.h"     // MPSettings
#include "../../math/math.h"        // maybeAtan2, Mat_GetDir, RotMatrix, ApplyMatrixLV

#include <math.h>
#include <string.h>

// Originals called by address
// DroneWeap_PlaceTertiary: a jump to DroneWeap_ThrowGrenade, which Ghidra names the same
#define DroneWeap_PlaceTertiary ((void (__cdecl *)(DCVars_tag *dcv))0x00067bf0)

// The entity events 3 and 4 move between datums 0x28 and 100 (our name)
#define ENTITY_SWAPPED ((HASHCODE)0x02000312)

// The DAnim of a shot fired through the anim (DroneAnim_PlayFiringAnim)
#define DANIM_NOW_FIRE 0x5c

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
    drone->animCallTaken = 0;
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
        double turn = Vec_AngleDifference(shotYaw, dcv->gameObj->rotation.y);
        DroneAnim_CallAnim(DANIM_DEATH_SHOT, fabs(turn) > (float)M_PI_2 ? 0 : 1, endState, 0, 0.0f, 0, dcv);
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

// The impact anim for where the last shot hit (lastHit's bone) while the player aims, else a random one, ending in
// the state the drone resumes. The abseilers (DTYPEs 27 and 28) have variants of their own. The original builds the
// abseilers' table on the stack each call; neither the last hit nor glb_players[0] is checked.
// AUTOINJECT
bool DroneAnim_LocationImpactAnim(DCVars_tag *dcv) {
    static const uint abseilVariants[19] = {0, 0, 0, 0, 0, 1, 1, 2, 1, 2, 3, 4, 3, 4, 0, 0, 0, 0, 0};
    Drone_tag *drone = dcv->drone;
    uint variant;
    if (glb_players[0]->animState->animFlags & 1) {
        switch ((ushort)drone->lastHit->hitBoneIdx) {
        case 4:
        case 5:
            variant = 5;
            break;
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
            variant = 8;
            break;
        case 0x1e:
        case 0x1f:
        case 0x20:
        case 0x23:
            variant = 9;
            break;
        case 0x31:
        case 0x32:
        case 0x33:
            variant = 0xd;
            break;
        case 0x35:
        case 0x36:
        case 0x37:
            variant = 0xc;
            break;
        default:
            variant = Rand_Rand(2) + 6;
            break;
        }
    } else {
        variant = Rand_Rand(8) + 5;
    }

    if (drone->dtype == DTYPE_Abseil27 || drone->dtype == DTYPE_Abseil28)
        variant = abseilVariants[variant];
    DroneAnim_CallAnim(DANIM_IMPACT, variant, dcv->aiStateMachine->resumeState, 0, 0.0f, 0, dcv);
    return true;
}

// For a drone whose shots come from the anim (firesByAnim): once DroneWeap_DoFiring has found it ready, with no anim
// call or script pending and its aim event (unknown21) set, plays the firing DAnim and fires.
// AUTOINJECT
bool DroneAnim_PlayFiringAnim(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (!drone->firesByAnim || drone->animCall.state != 0 || drone->unknown434 != NULL || !drone->readyToFire ||
        !drone->unknown21)
        return false;
    DroneAnim_FreeScript(dcv);
    DroneAnim_SetDAnimNow(DANIM_NOW_FIRE, 0, 0, dcv);
    DroneWeap_FireWeapon(dcv);
    drone->fired = 0;
    return true;
}

// ---- Playing a row: the anim call protocol (docs/drone/behaviours/README.md 6.2)

// The script blending out (unknown434), freed.
// AUTOINJECT
void DroneAnim_FreeScript(DCVars_tag *dcv) {
    sAnimScript_tag **slot = (sAnimScript_tag **)&dcv->drone->unknown434;
    if (*slot != NULL) {
        (*slot)->ownerSlot = NULL;
        AnimScriptDelete(dcv->gameObj, slot);
        *slot = NULL;
    }
}

// Plays a row now, with no end action.
// AUTOINJECT
bool DroneAnim_SetDAnimNow(ushort row, ushort blend, uchar callFlag, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    drone->animEndState = 0;
    drone->animEndMsg = 0;
    if (!DroneAnim_SetDAnimInternal(row, blend, callFlag, drone))
        return false;
    drone->animCallTaken = 1;
    return true;
}

// DroneAnim_SetDAnimNow for a drone.
// AUTOINJECT
void DroneAnim_SetAnimD(ushort row, ushort blend, Drone_tag *drone) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    drone = dcv.drone;
    drone->animEndState = 0;
    drone->animEndMsg = 0;
    if (DroneAnim_SetDAnimInternal(row, blend, 0, drone))
        drone->animCallTaken = 1;
}

// The call states that play as DANIM_DEATH: 0x41-0x44, 0x50 and 0x6f.
ushort _DroneAnim_BaseCallState(ushort state) {
    switch (state) {
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x50:
    case 0x6f:
        return DANIM_DEATH;
    default:
        return state;
    }
}

// AUTOLTCG
void __declspec(naked) DroneAnim_BaseCallState(void) {
    _asm {
        movzx eax, ax
        push eax
        call _DroneAnim_BaseCallState
        add esp, 4
        ret
    }
}

// Fills the drone's anim slot (animMode to animNextRow) from the row and the anim set's script.
void _DroneAnim_SetDAnimVeryInternal(Drone_tag *drone, ushort row, ushort animSet) {
    const DroneAnimInfo *info = &Drone_AnimInfo[row];
    const DroneAnimScript *script = &Drone_AnimTables[row][animSet];
    drone->prevAnimFlags = drone->unknown46c;
    drone->animRow = row;
    drone->animStartFrame = script->startFrame;
    drone->animNextRow = info->nextRow == row ? 0 : info->nextRow;
    drone->animMode = info->mode;
    drone->animPriority = DroneAnimStates[info->animState].priority;
    drone->animHashcode = (HASHCODE)(0x06000000 + script->scriptLo);
    drone->animState = info->animState;
    drone->animOnce = 0;
    drone->animPlaying = 0;
    drone->unknown46c = info->htFlags;
    drone->animMoveSpeed = info->moveSpeed;
    drone->animSpeed = info->speed;
    drone->unknown481 = info->moveState;
    drone->animEndBlend = info->blendFrames;
    drone->aimMode = info->aimMode;
    drone->animCallFlag = 0;
}

static_assert(offsetof(Drone_tag, animMode) == 0x464, "DroneAnim_SetDAnimVeryInternal's adaptor");

// The original takes the slot, &drone->animMode.
// AUTOLTCG
void __declspec(naked) DroneAnim_SetDAnimVeryInternal(void) {
    _asm {
        movzx ecx, word ptr [esp + 4]
        push ecx
        movzx edx, dx
        push edx
        sub eax, 0x464
        push eax
        call _DroneAnim_SetDAnimVeryInternal
        add esp, 0xc
        ret
    }
}

// Whether the row is passed over at once: it has no script, or it is in one of the modes not played while the
// drone is not drawn.
bool _DroneAnim_SkipAnim(Drone_tag *drone) {
    if (drone->animHashcode == 0x06000000)
        return true;
    if (drone->gameObj->drawnCountdown == 0 && (drone->unknown46c & DANIMFLAG_SKIP_UNSEEN)) {
        switch (drone->animMode) {
        case 0:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 9:
            return true;
        }
    }
    return false;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_SkipAnim(void) {
    _asm {
        push eax
        call _DroneAnim_SkipAnim
        add esp, 4
        ret
    }
}

// Once the new row plays the anim state called for (or none), the call is taken: its speed kept, the call cleared.
// False, after moving on to the next row, when the row is skipped.
bool _DroneAnim_TakeCall(Drone_tag *drone) {
    if (drone->animState == 0 || (ushort)drone->animState == drone->animCall.state) {
        drone->animCallTaken = 1;
        drone->animCallSpeedTaken = drone->animCallSpeed;
        memset(&drone->animCall, 0, sizeof(drone->animCall));
    }
    if (_DroneAnim_SkipAnim(drone)) {
        DroneAnim_SetEndAnim(drone, 30);
        return false;
    }
    return true;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_TakeCall(void) {
    _asm {
        push esi
        call _DroneAnim_TakeCall
        add esp, 4
        ret
    }
}

// Plays a row: fills the slot, takes the call, ends the old row's functions, starts the script and the new row's
// functions. False when it is skipped, or with bots while a script blends out and the drone animates.
// AUTOINJECT
bool DroneAnim_SetDAnimInternal(ushort row, ushort blend, uchar callFlag, Drone_tag *drone) {
    if (MPSettings.maybeDroneAIEnabled && drone->unknown434 != NULL && NDrone2_DoAnimation(drone))
        return false;

    ushort animSet = drone->animSet;
    if (Drone_AnimTables[row][animSet].scriptLo == 0)
        animSet = 0;
    _DroneAnim_SetDAnimVeryInternal(drone, row, animSet);
    drone->animCallFlag = callFlag;
    drone->animCall.state = _DroneAnim_BaseCallState(drone->animCall.state);
    if (!_DroneAnim_TakeCall(drone))
        return false;

    if (drone->animStateFunc != NULL)
        drone->animStateFunc(drone, DANIMFUNC_END);
    if (drone->animFunc != NULL)
        drone->animFunc(drone, DANIMFUNC_END);
    DroneAnim_SetHTAnim(drone->animHashcode, drone->unknown46c, drone->animStartFrame, drone, blend, drone->animSpeed);
    const DroneDataFunc *func = DroneData_GetAnimFuncInfo(row);
    if (func != NULL)
        drone->animFunc = func->func;
    func = DroneData_GetDAStateFuncInfo(drone->animState);
    if (func != NULL)
        drone->animStateFunc = func->func;
    DroneAnim_OnStartAnim(drone);
    return true;
}

// Starts the row's script: blended from the one playing over `blend` frames when the drone animates, else at once.
// Blending out of a turning row (DANIMFLAG_TURN_180) that has not ended stops it, blends over 30 frames and keeps
// the turn its root has still to make (unknown448, for DroneAnim_SnapRotate). The script's events go to
// DroneAnim_EventFunc; one that does not loop sets animOnce and animPlaying until DroneAnim_AnimEndCallback. Frame
// 999 holds the last frame. The script blending out is not checked again after AnimScriptAppendBlend.
// AUTOINJECT
void DroneAnim_SetHTAnim(HASHCODE hashcode, uint flags, ushort frame, Drone_tag *drone, ushort blend, float speed) {
    if (NDrone2_DoAnimation(drone) && blend != 0 && drone->animScript != NULL) {
        sAnimScript_tag **blendSlot = (sAnimScript_tag **)&drone->unknown434;
        *blendSlot = drone->animScript;
        (*blendSlot)->ownerSlot = blendSlot;
        drone->animScript = AnimScriptAppendBlend(drone->gameObj, hashcode);
        if (*blendSlot != NULL) {
            (*blendSlot)->fadeLen = blend;
            (*blendSlot)->fadeTime = (*blendSlot)->fadeLen;
            AnimScriptCallOnStop(*blendSlot, NULL);
            AnimScriptCallOnEvent(drone->animScript, NULL);
        }
        sAnimScript_tag *old = *blendSlot;
        if (old->numFrames > old->curFrame && (drone->prevAnimFlags & DANIMFLAG_TURN_180)) {
            obj_tag *obj = drone->gameObj;
            old->speed = 0.0f;
            (*blendSlot)->fadeLen = 30.0f;
            (*blendSlot)->fadeTime = 30.0f;
            AnimScriptSetAnimRotation(*blendSlot, 1);
            _VECTOR pos, dir, rot;
            _MATRIX root;
            AnimGetBoneWorldTrans(obj, 0x80000000, 0, &pos, &root);
            Mat_GetDir(&dir, &root);
            Vec_DirToRot(&dir, &rot);
            drone->unknown448 = rot.y - obj->rotation.y;
            (*blendSlot)->flags |= ANIMSCRIPT_SHARE_ROOT;
        }
    } else {
        drone->animScript = AnimScriptAdd(drone->gameObj, hashcode);
    }

    sAnimScript_tag *script = drone->animScript;
    if (script == NULL)
        return;
    if (frame == 999) {
        frame = script->numFrames - 1;
        script->speed = 0.0f;
    } else if (speed != 1.0f) {
        script->speed = speed;
    }
    if (frame != 0)
        drone->animScript->curFrame = frame;
    drone->animScript->ownerSlot = &drone->animScript;
    AnimScriptCallOnEvent(drone->animScript, DroneAnim_EventFunc);
    drone->animScript->flags |= ANIMSCRIPT_GAME_FLAG;
    if (flags & DANIMFLAG_LOOP) {
        drone->animScript->flags |= ANIMSCRIPT_LOOP;
        drone->flags &= ~0x8000;
        drone->animOnce = 0;
        drone->animPlaying = 0;
    } else {
        drone->animScript->flags &= ~ANIMSCRIPT_LOOP;
        drone->flags |= 0x8000;
        drone->animOnce = 1;
        drone->animPlaying = 1;
        AnimScriptCallOnStop(drone->animScript, DroneAnim_AnimEndCallback);
    }
    drone->animHashcode = hashcode;
    drone->unknown47c = GameState.NumFramesUnpaused;
    drone->animFunc = NULL;
    drone->animStateFunc = NULL;
}

// Moves on to the row after the one that ended (animNextRow), if it is another; gives the row back when that fails.
// AUTOINJECT
bool DroneAnim_SetEndAnim(Drone_tag *drone, uchar blend) {
    ushort next = drone->animNextRow;
    if (next == 0)
        return false;
    uchar endBlend = drone->animEndBlend;
    drone->animNextRow = 0;
    drone->animEndBlend = 0;
    if (drone->animRow == next)
        return false;
    if (DroneAnim_SetDAnimInternal(next, DroneAnim_OnEndAnim(drone, drone->unknown46c, blend), 0, drone))
        return true;
    drone->animNextRow = next;
    drone->animEndBlend = endBlend;
    return false;
}

// The row's end: its functions are told, and a turning row sets the turn still to make (DroneAnim_SnapRotate) and
// is not blended out of.
// AUTOINJECT
uchar DroneAnim_OnEndAnim(Drone_tag *drone, uint flags, uchar blend) {
    if (drone->animStateFunc != NULL)
        drone->animStateFunc(drone, DANIMFUNC_END);
    if (drone->animFunc != NULL)
        drone->animFunc(drone, DANIMFUNC_END);
    if (flags & DANIMFLAG_TURN_180) {
        drone->unknown448 = (float)M_PI;
        blend = 0;
    }
    if (flags & DANIMFLAG_TURN_90) {
        drone->unknown448 = (float)M_PI_2;
        blend = 0;
    }
    if (flags & DANIMFLAG_TURN_MINUS_90) {
        drone->unknown448 = -(float)M_PI_2;
        blend = 0;
    }
    return blend;
}

// The row's start: a mode 6 row, unless the last hit was heavy, plays 15 frames at most and blends 5 frames into
// the next; its functions are told. The script is not checked.
// AUTOINJECT
void DroneAnim_OnStartAnim(Drone_tag *drone) {
    if (drone->animMode == 6 && !drone->heavyHit) {
        sAnimScript_tag *script = drone->animScript;
        if (script->numFrames > 15)
            script->numFrames = 15;
        drone->animEndBlend = 5;
    }
    if (drone->animStateFunc != NULL)
        drone->animStateFunc(drone, DANIMFUNC_START);
    if (drone->animFunc != NULL)
        drone->animFunc(drone, DANIMFUNC_START);
}

// Makes the turn a row left to make (unknown448) at once, with the script's anim rotation off meanwhile.
// AUTOINJECT
void DroneAnim_SnapRotate(Drone_tag *drone) {
    if (drone == NULL)
        return;
    if (drone->animScript != NULL)
        AnimScriptSetAnimRotation(drone->animScript, 0);
    if (drone->unknown448 != 0.0f) {
        obj_tag *obj = drone->gameObj;
        drone->angleToSomePoint = drone->unknown448 + drone->angleToSomePoint;
        obj->rotation.y = drone->angleToSomePoint;
        Drone_GetOpponentInfo(drone, obj);
        if (drone->animScript != NULL)
            AnimScriptSetAnimRotation(drone->animScript, 1);
        drone->unknown448 = 0.0f;
    }
}

// The row for a call from anim state `state`: the first transition to the call, or to any (toState 0), with the
// variant's row when it has that many. -1 for none.
short _DroneAnim_GetDAnimForAnimState(ushort state, ushort call, ushort variant, ushort *blend, uchar *callFlag) {
    if (state >= NUM_DRONE_ANIM_STATES)
        state = 0;
    const DroneAnimTransition *t = DroneAnimStates[state].transitions;
    if (t == NULL)
        return -1;
    for (; t->toState != 0xff; t++) {
        if (t->toState == call || t->toState == 0) {
            if (blend != NULL)
                *blend = t->blendFrames;
            if (callFlag != NULL)
                *callFlag = t->callFlag;
            if (variant >= t->variants)
                variant = 0;
            if (t->row == 0 || t->row == -1)
                return t->row;
            return t->row + variant;
        }
    }
    return -1;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_GetDAnimForAnimState(void) {
    _asm {
        push edi
        push esi
        movzx ecx, word ptr [esp + 0xc]
        push ecx
        movzx edx, dx
        push edx
        movzx eax, ax
        push eax
        call _DroneAnim_GetDAnimForAnimState
        add esp, 0x14
        ret
    }
}

// The row for the call from the anim state playing, else from state 0's list (for the drone's own call then,
// blending over 8 frames). False for none, or for row 0.
bool _DroneAnim_GetDAnimForCall(Drone_tag *drone, ushort call, ushort variant, ushort *row, ushort *blend,
                                uchar *callFlag) {
    *row = _DroneAnim_GetDAnimForAnimState(drone->animState, call, variant, blend, callFlag);
    if (*row == 0)
        return false;
    if (*row != 0xffff)
        return true;
    if (blend != NULL)
        *blend = 8;
    if (callFlag != NULL)
        *callFlag = 0;
    *row = _DroneAnim_GetDAnimForAnimState(0, drone->animCall.state, drone->animCall.variant, blend, callFlag);
    return *row != 0 && *row != 0xffff;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_GetDAnimForCall(void) {
    _asm {
        push ecx
        push dword ptr [esp + 0xc]
        push dword ptr [esp + 0xc]
        movzx eax, ax
        push eax
        movzx edx, dx
        push edx
        push ebx
        call _DroneAnim_GetDAnimForCall
        add esp, 0x18
        ret
    }
}

// Whether the call may start now: there is one; with bots, no script blends out; a mode 5 row is left only for a
// call made while it played, and in anim states 0xc, 0xd, 0x29 and 0x2a not for call 10.
bool _DroneAnim_CanSetAnimCall(Drone_tag *drone) {
    if (drone->animCall.state == 0)
        return false;
    if (MPSettings.maybeDroneAIEnabled && drone->unknown434 != NULL)
        return false;
    if ((ushort)drone->animState != drone->animCall.fromState && drone->animMode == 5)
        return false;
    switch ((ushort)drone->animState) {
    case 0x0c:
    case 0x0d:
    case 0x29:
    case 0x2a:
        if (drone->animCall.state == 10 && drone->animMode == 5)
            return false;
        break;
    }
    return true;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_CanSetAnimCall(void) {
    _asm {
        push eax
        call _DroneAnim_CanSetAnimCall
        add esp, 4
        ret
    }
}

// The call has played out: cleared, and its end action taken (DroneAnim_SetEndAIState).
void _DroneAnim_CallFullyComplete(Drone_tag *drone) {
    memset(&drone->animCall, 0, sizeof(drone->animCall));
    DroneAnim_SetEndAIState(drone, drone->animEndState, drone->animEndMsg);
    drone->animEndState = 0;
    drone->animEndMsg = 0;
}

// AUTOLTCG
void __declspec(naked) DroneAnim_CallFullyComplete(void) {
    _asm {
        push esi
        call _DroneAnim_CallFullyComplete
        add esp, 4
        ret
    }
}

// Once a frame (Drone_Control): a row that does not loop and has ended goes on to its next row; a call whose anim
// state plays has its end action taken once its row is done (mode 1 once it has ended, modes 2 and 9 at once);
// else a call that may start is looked up and played.
// AUTOINJECT
void DroneAnim_CallHandler(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (drone->animCallFlag && drone->unknown434 == NULL)
        drone->animCallFlag = 0;

    if (drone->animOnce && !drone->animPlaying) {
        drone->animOnce = 0;
        if (DroneAnim_SetEndAnim(drone, drone->animEndBlend))
            return;
    }

    if (drone->animCallTaken && !drone->animCallFlag) {
        if ((drone->animMode == 1 && !drone->animOnce) || drone->animMode == 2 || drone->animMode == 9) {
            _DroneAnim_CallFullyComplete(drone);
            drone->animCallTaken = 0;
            return;
        }
    }

    if (!_DroneAnim_CanSetAnimCall(drone))
        return;
    ushort row = 0;
    ushort blend = 8;
    uchar callFlag = 0;
    if (!_DroneAnim_GetDAnimForCall(drone, drone->animCall.state, drone->animCall.variant, &row, &blend, &callFlag))
        return;
    dcv->drone->animCallSpeedTaken = dcv->drone->animCallSpeed;
    DroneAnim_SetDAnimInternal(row, blend, callFlag, dcv->drone);
}

// ---- The scripts' callbacks

// The drone's script events. The grenades, blows and messages (1, 5, 6, 9, 10, 0xb, 0xe) only while it lives and
// only from its own script (animScript), not one blending out.
// AUTOINJECT
void DroneAnim_EventFunc(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, ushort event) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(obj, &dcv);
    Drone_tag *drone = dcv.drone;
    char secondWeapon = drone->secondWeapon;
    switch (event) {
    case 1:
        if (drone->health > 0.0f && script == drone->animScript)
            DroneWeap_ThrowGrenade(&dcv);
        break;
    case 2:
        NF_WARN_IF(secondWeapon < 2, "ERROR: Drone has no second weapon"); // GC check (0x80089678)
        DroneWeap_ChangeWeapon(&dcv, secondWeapon);
        break;
    case 3:
        AnimDatumSetEntity(obj, 0x28, ENTITY_SWAPPED, 0xff);
        AnimDatumSetEntity(obj, 100, HASHCODE_NONE, 0xff);
        break;
    case 4:
        AnimDatumSetEntity(obj, 0x28, HASHCODE_NONE, 0xff);
        AnimDatumSetEntity(obj, 100, ENTITY_SWAPPED, 0xff);
        break;
    case 5:
    case 10:
        if (drone->health > 0.0f && script == drone->animScript)
            DroneWeap_CloseCombatImpact(&dcv, 1);
        break;
    case 6:
        if (drone->health > 0.0f && script == drone->animScript)
            Drone_SM_SendMsg(DRONE_MSG_AnimEvent, 0, dcv.aiStateMachine->id, dcv.aiStateMachine->id);
        break;
    case 7:
        drone->animPlaying = 0;
        break;
    case 9:
        if (drone->health > 0.0f && script == drone->animScript)
            DroneWeap_PlaceTertiary(&dcv);
        break;
    case 0xb:
        if (drone->health > 0.0f && script == drone->animScript)
            DroneWeap_CloseCombatImpact(&dcv, 2);
        break;
    case 0xe:
        if (drone->unknown230 != NULL && drone->health > 0.0f && script == drone->animScript) {
            Drone_tag *other = (Drone_tag *)drone->unknown230->extraObjectData;
            Drone_SM_SendMsg(DRONE_MSG_ToHostages, DSTATE_Hostage, dcv.aiStateMachine->id, other->sm.id);
        }
        break;
    case 0xf:
        drone->aimEvent = 1;
        break;
    case 0x10:
        drone->aimEvent = 0;
        drone->unknown21 = 0;
        break;
    case 0x12:
        drone->aimEvent = 1;
        drone->unknown21 = 1;
        break;
    }
}

// The end of a row's script that does not loop.
// AUTOINJECT
void DroneAnim_AnimEndCallback(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script) {
    ((Drone_tag *)obj->extraObjectData)->animPlaying = 0;
}

// What the root's move (delta) does to a drone before it is applied: the feet follow the anim's root height (0.2 at
// least); with unknown1a the height stays and the moves add up in scriptedMovePos; a bot's move is scaled by its
// speed; one standing still (unknown38) or flying does not move; one touching the player does not move towards it
// (contactDir); and the bounds (boundaryFlags) stop it, in its own frame.
// AUTOINJECT
void DroneAnim_PreTransCallback(obj_tag *obj, AnimObj *animObj, _VECTOR *delta) {
    if (obj->objectType != OBJECTTYPE_DRONE)
        return;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    drone->animRootHeight = animObj->rootHeight;
    drone->prevRootHeight = drone->rootHeight;
    if (drone->animRootHeight != 0.0f) {
        // Compared before it is rounded to a float
        double height = (double)drone->animRootHeight - 0.02f;
        drone->rootHeight = (float)height;
        if (height < 0.2f)
            drone->rootHeight = 0.2f;
        drone->rootHeightSet = 1;
    }
    if (!drone->unknown1a)
        delta->y = drone->rootHeight - drone->prevRootHeight;
    if (drone->dtype == DTYPE_Bot) {
        float speed = BOT_getMovementSpeedMul(drone);
        Vec_Mul_F3(delta, delta, speed, 1.0f, speed);
    }
    if (drone->unknown1a)
        Vec_Add2(&drone->scriptedMovePos, delta, &drone->scriptedMovePos);

    bool still = drone->unknown38 || drone->flies;
    if (drone->touchingPlayer) {
        _VECTOR rot, local;
        _MATRIX contact, inverse;
        Vec_DirToRot(&drone->contactDir, &rot);
        RotMatrix(&rot, &contact);
        Mat_Inverse(&contact, &inverse);
        ApplyMatrixLV(&inverse, delta, &local);
        if (local.z > 0.0f) {
            local.z = 0.0f;
            ApplyMatrixLVI(&contact, &local);
            delta->x = local.x;
            delta->z = local.z;
        }
    } else if (still) {
        Vec_Zero(delta);
        return;
    }

    if ((drone->flags & 1) && drone->door == NULL) {
        _MATRIX inverse;
        _VECTOR local;
        Mat_Inverse(&obj->transformMatrix, &inverse);
        ApplyMatrixLV(&inverse, delta, &local);
        uint bounds = drone->boundaryFlags;
        if ((bounds & 2) && local.z > 0.0f)
            local.z = 0.0f;
        if ((bounds & 4) && local.z < 0.0f)
            local.z = 0.0f;
        if ((bounds & 8) && local.x > 0.0f)
            local.x = 0.0f;
        if ((bounds & 0x10) && local.x < 0.0f)
            local.x = 0.0f;
        ApplyMatrixLV(&obj->transformMatrix, &local, delta);
    }
}

// ---- The rows' and anim states' functions

// AUTOINJECT
const DroneDataFunc* DroneData_GetAnimFuncInfo(ushort row) {
    for (const DroneDataFunc *f = DroneData_AnimFunc; f->id != 0xffff; f++)
        if (f->id == row)
            return f;
    return NULL;
}

// AUTOINJECT
const DroneDataFunc* DroneData_GetDAStateFuncInfo(ushort state) {
    for (const DroneDataFunc *f = DroneData_DAStateFunc; f->id != 0xffff; f++)
        if (f->id == state)
            return f;
    return NULL;
}

// Row 0xdc's function: in TowerA and TowerB its script plays at 0.7 (the script is not checked).
// AUTOINJECT
void DAnimFunc_Run(Drone_tag *drone, ushort phase) {
    if (phase == DANIMFUNC_START &&
        (GameState.CurrentLevelHashcode == HT_Level_TowerA || GameState.CurrentLevelHashcode == HT_Level_TowerB))
        drone->animScript->speed = 0.7f;
}
