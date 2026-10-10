// The leaf layer of the drone state machine: starting a drone's machine, requesting a state change, the four
// ways of sending it a message, and the router they go through (Drone_SM_RouteMsg). See
// docs/drone/architecture/README.md section 3 - the dispatcher (Drone_SM_RouteMsgDCV) is still the game's.

#include "Drone.h"
#include "NDrone2.h"
#include "DroneTables.h"
#include "BOT.h"
#include "../../game.h"     // GameState, MPGame
#include "../mp/multiplayer.h"  // MPSettings

// Runs a message through the drone's current state's handler (NDrone2_StateFuncs, still the game's table) and says
// whether it was handled; a state beyond the table handles nothing. Every drone's state machine holds this as its
// processFunction, which the dispatcher (Drone_SM_RouteMsgDCV) calls. Ghidra calls it __stdcall, but it returns
// with a plain RET and the dispatcher cleans up after it: cdecl.
// AUTOINJECT
bool NDrone2_ProcessStateMachine(DCVars_tag *dcv, uint state, MsgObject *msg) {
    if (state >= NUM_DSTATES)
        return false;
    return NDrone2_StateFuncs[state](dcv, dcv->drone, dcv->gameObj, msg);
}

// AUTOGEN
undefined4 __cdecl BOT_validateStateChange(int *param_1, uint param_2);

// Drone_SM_RouteMsgDCV (0x4e410), the dispatcher (README 3.3), is not reimplemented yet and has a custom
// convention the AUTOGEN stubs cannot express: the DCVars arrive in ESI, the message is its one stack argument
// (read at [esp+4] on entry: sub esp,0x1c / push ebx / push ebp / mov ebp,[esp+0x28]), and the caller removes
// it (RET 0; tools/abi_action.json: pops 0, regs_in esi). It only reads ESI, never writes it. This shim gives it
// an ordinary cdecl face. ESI is callee-saved for our compiler, so it is put back afterwards.
// When the dispatcher itself is reimplemented, this shim goes and callers call ours.
void __declspec(naked) Drone_SM_RouteMsgDCV(DCVars_tag *dcVars, MsgObject *msg) {
    _asm {
        push esi
        mov esi, [esp + 8]          // dcVars
        push dword ptr [esp + 12]   // msg (12 now: the saved ESI moved it along)
        mov eax, 0x0004E410
        call eax
        add esp, 4
        pop esi
        ret
    }
}

// The drone whose state machine has this id, as FUN_0004e1b0 finds it (the id arrives in ECX there, so the
// original is not patched; Drone_SM_SetState inlines the same walk). NULL if no drone on the list has it.
static obj_tag *Drone_FindObjBySMId(uint id) {
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->sm.id == id)
            return drone->gameObj;
    }
    return NULL;
}

// AUTOGEN
void __cdecl FUN_0004e3c0(undefined4 *param_1);

// Delivers a message (README 3.2): one due later is queued (FUN_0004e3c0, the delayed list); a broadcast goes to
// every drone on the list, a negative receiver -1-n to every bot on team n (multiplayer with bots only), any other
// receiver to the drone with that id. A message is not delivered to a drone in a state that takes none: WaitSwitch,
// HostageDead, Dead and Fade, and also PlayScript (broadcasts and team messages) and FadeFast (team and addressed
// messages). Each broadcast or team delivery first rewrites msg->receiver to that drone's id.
// AUTOINJECT
void Drone_SM_RouteMsg(MsgObject *msg) {
    if (msg->handleOnFrame > GameState.NumFramesUnpaused) {
        FUN_0004e3c0((undefined4 *)msg);
        return;
    }

    DCVars_tag dcVars;
    int receiver = msg->receiver;
    if (receiver == 0) {
        for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
            switch (drone->sm.curState) {
            case DSTATE_WaitSwitch:
            case DSTATE_PlayScript:
            case DSTATE_HostageDead:
            case DSTATE_Dead:
            case DSTATE_Fade:
                break;
            default:
                dcVars.drone = drone;
                dcVars.gameObj = drone->gameObj;
                dcVars.aiStateMachine = &drone->sm;
                dcVars.cel = drone->gameObj->inCel;
                msg->receiver = drone->sm.id;
                Drone_SM_RouteMsgDCV(&dcVars, msg);
                break;
            }
        }
    } else if (MPSettings.maybeDroneAIEnabled && receiver < 0) {
        int team = -1 - receiver;
        for (int i = NUM_PLAYERS; i < NUM_AGENTS; i++) {
            obj_tag *bot = MPGame.players[i].playerObj;
            if (bot == NULL || MPSettings.Player[i].TeamId != team)
                continue;
            // Whether it worked is not tested
            Drone_DCVfromOBJ(bot, &dcVars);
            switch (dcVars.aiStateMachine->curState) {
            case DSTATE_WaitSwitch:
            case DSTATE_PlayScript:
            case DSTATE_HostageDead:
            case DSTATE_Dead:
            case DSTATE_Fade:
            case DSTATE_FadeFast:
                break;
            default:
                msg->receiver = dcVars.aiStateMachine->id;
                Drone_SM_RouteMsgDCV(&dcVars, msg);
                break;
            }
        }
    } else {
        obj_tag *gameObj = Drone_FindObjBySMId(receiver);
        if (gameObj == NULL)
            return;
        // Whether it worked is not tested
        Drone_DCVfromOBJ(gameObj, &dcVars);
        switch (dcVars.aiStateMachine->curState) {
        case DSTATE_WaitSwitch:
        case DSTATE_HostageDead:
        case DSTATE_Dead:
        case DSTATE_Fade:
        case DSTATE_FadeFast:
            break;
        default:
            Drone_SM_RouteMsgDCV(&dcVars, msg);
            break;
        }
    }
}

// Starts a drone's state machine (once, from NDrone2_PostLoad_Init): gives it its id and sends it Enter.
// AUTOINJECT
bool Drone_SM_InitObject(obj_tag *gameObj) {
    if (gameObj == NULL)
        return false;

    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    StateMachineInfo_tag *sm = &drone->sm;

    // The id is the 16-bit counter plus one, worked out in 32 bits (movzx; inc), and the counter then wraps in 16
    // bits: the 65536th drone would get id 0x10000 and the next one 1 again. Id 0 is never given out, which is
    // what lets 0 mean "broadcast" in MsgObject.receiver.
    sm->id = (uint)NPCGlobals.NumDrones + 1;
    NPCGlobals.NumDrones++;
    sm->changePending = 0;
    sm->stateParam = 0;

    // Every state field starts at obj->curState, which NDrone2_DefaultInit leaves at 0 (DSTATE_Global), so the
    // Enter below goes to DSTATE_Global, whose Enter handler picks the first real state (README 3.4). The game
    // reads the u16 sign-extended (movsx).
    uint first = (uint)(int)(short)gameObj->curState;
    sm->curState = first;
    sm->resumeState = first;
    sm->prevState = first;
    sm->nextState = first;
    // stateEnteredFrame and the two scratch words are left as they are.

    drone->processFunction = (void *)NDrone2_ProcessStateMachine;

    // To itself, any state, delivered now through Drone_SM_RouteMsg (so ignored if the system is disabled).
    Drone_SM_SendMsg(DRONE_MSG_Enter, 0, sm->id, sm->id);
    return true;
}

// Requests a change to state `next`; it happens when the current dispatch finishes (README 3.3/3.4). The only
// writer of nextState. Returns false only when a bot refuses the change and has nowhere to redirect it.
// AUTOINJECT
bool Drone_SM_SetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    // DSTATE_Global cannot be requested: nothing happens, and the caller is told it worked. The whole dword is
    // tested, although Ghidra types the argument as a 2-byte DSTATE.
    if (next == DSTATE_Global)
        return true;

    // Multiplayer bots may refuse a change. The machine is found again by its id (a walk of the drone list),
    // and the type is read from the found object's own drone rather than through sm.
    obj_tag *gameObj = Drone_FindObjBySMId(sm->id);
    if (gameObj != NULL && ((Drone_tag *)gameObj->extraObjectData)->dtype == DTYPE_Bot) {
        DCVars_tag dcVars;
        // Only the low byte of BOT_validateStateChange's result is tested (test al, al).
        if (Drone_DCVfromOBJ(gameObj, &dcVars) && !(uchar)BOT_validateStateChange((int *)&dcVars, next)) {
            // Refused: go where the bot says instead (BOT_validateStateChange writes it, 0 = nowhere). The u16
            // is read sign-extended (movsx), so a value of 0x8000 or more would become a negative state.
            short redirect = (short)dcVars.drone->botVars->redirectState;
            if (redirect == 0)
                return false;
            next = (DSTATE)redirect;
        }
    }

    sm->nextState = next;
    sm->changePending = 1;
    sm->stateParam = param;
    return true;
}

// Sends a message now (handleOnFrame = this frame) from one state machine to another, or to every drone
// (receiver 0), through Drone_SM_RouteMsg.
// AUTOINJECT
void Drone_SM_SendMsg(uint msgType, uint scope, uint sender, int receiver) {
    if (Drone_bDisableSystem)
        return;

    MsgObject msg;
    msg.msgType = msgType;
    msg.scope = scope;
    msg.sender = sender;
    msg.receiver = receiver;
    msg.createdFrame = GameState.NumFramesUnpaused;
    msg.handleOnFrame = GameState.NumFramesUnpaused;
    // extraData is deliberately NOT set: the original leaves that slot of its stack frame uninitialised too, so
    // whoever handles these messages (Enter from InitObject, 0xb to hostages, the anim events) gets garbage.
    Drone_SM_RouteMsg(&msg);
}

// Sends a message from a drone to itself. With no delay it is dispatched at once, straight to the drone's own
// state machine (Drone_SM_RouteMsgDCV: so it reaches the drone even in the states Drone_SM_RouteMsg skips, such
// as Dead and Fade); with a delay it goes through Drone_SM_RouteMsg, which queues it. Not stopped by
// Drone_bDisableSystem.
// AUTOINJECT
void Drone_SM_SendMsgSelf(uint msgType, void *extraData, uint delay, uint scope, DCVars_tag *dcVars) {
    MsgObject msg;
    msg.msgType = msgType;
    msg.scope = scope;
    msg.sender = dcVars->aiStateMachine->id;
    msg.receiver = dcVars->aiStateMachine->id;
    msg.createdFrame = GameState.NumFramesUnpaused;
    msg.handleOnFrame = GameState.NumFramesUnpaused + delay;
    msg.extraData = extraData;

    if (delay != 0)
        Drone_SM_RouteMsg(&msg);
    else
        Drone_SM_RouteMsgDCV(dcVars, &msg);
}

// Sends a message to every drone (receiver 0, any state), after `delay` frames. Not stopped by
// Drone_bDisableSystem.
// AUTOINJECT
void Drone_SM_BroadcastMsg(uint msgType, void *extraData, uint delay, uint sender) {
    MsgObject msg;
    msg.msgType = msgType;
    msg.scope = 0;
    msg.sender = sender;
    msg.receiver = 0;
    msg.createdFrame = GameState.NumFramesUnpaused;
    msg.handleOnFrame = GameState.NumFramesUnpaused + delay;
    msg.extraData = extraData;
    Drone_SM_RouteMsg(&msg);
}

// The drone system's entry for the rest of the game: a message to one drone (as if from itself), or to all of
// them when gameObj is NULL (sender 0). False only when the drone system is disabled.
// AUTOINJECT
bool Drone_Message(obj_tag *gameObj, uint msgType, void *extraData, uint delay) {
    if (Drone_bDisableSystem)
        return false;

    if (gameObj == NULL) {
        Drone_SM_BroadcastMsg(msgType, extraData, delay, 0);
        return true;
    }

    // The original inlines Drone_DCVfromOBJ (a drone or dead drone fills dcVars; the four fields and the type
    // test are the same) and ignores whether it worked: an object of any other type would leave dcVars
    // uninitialised and SendMsgSelf would read an id through garbage. Kept - every caller found passes a drone.
    DCVars_tag dcVars;
    Drone_DCVfromOBJ(gameObj, &dcVars);
    Drone_SM_SendMsgSelf(msgType, extraData, delay, 0, &dcVars);
    return true;
}

// The end of an animation call (DroneAnim_CallAnim and friends): change to newState if it is not 0, then send
// msgType to the drone itself if it is not 0 (now, any state, no payload).
// AUTOINJECT
void DroneAnim_SetEndAIState(Drone_tag *drone, short newState, uint msgType) {
    // Worked out first, whether or not it is needed, and whether or not it worked (the result is not tested).
    DCVars_tag dcVars;
    Drone_DCVfromOBJ(drone->gameObj, &dcVars);

    // SetState is given the drone's own machine, not dcVars.aiStateMachine; its result is ignored.
    if (newState != 0)
        Drone_SM_SetState(&drone->sm, (DSTATE)newState, 0);

    if (msgType != 0)
        Drone_SM_SendMsgSelf(msgType, NULL, 0, 0, &dcVars);
}
