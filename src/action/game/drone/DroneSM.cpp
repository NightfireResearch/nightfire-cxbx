// The drone state machine: the queue of delayed messages, the router (Drone_SM_RouteMsg) and the dispatcher
// (Drone_SM_RouteMsgDCV), starting a drone's machine, requesting a state change, and the four ways of sending it a
// message. See docs/drone/architecture/README.md section 3.

#include "Drone.h"
#include "NDrone2.h"
#include "DroneTables.h"
#include "BOT.h"
#include "../../game.h"             // GameState, MPGame
#include "../mp/multiplayer.h"      // MPSettings

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

// A message waiting for its frame: the head of one of Drone_SM_LLDelayed's lists, or one of its items
typedef struct DroneSMItem {
    DroneSMItem *next;
    MsgObject msg;
} DroneSMItem;

// The delayed messages: a list in the order they fall due, and the free items
typedef struct DroneSMLists {
    uint count;             // 0x0 - DRONE_SM_ITEMS
    DroneSMItem *items;     // 0x4 - Drone_SM_LLItems
    DroneSMItem *pending;   // 0x8 - the head of the waiting list (item 0)
    DroneSMItem *free;      // 0xc - the head of the free list (item 1)
} DroneSMLists;

static_assert(sizeof(DroneSMItem) == 0x20, "DroneSMItem is 0x20 bytes");
static_assert(sizeof(DroneSMLists) == 0x10, "DroneSMLists is 0x10 bytes");

#define DRONE_SM_ITEMS 0x400

// Only the four functions below use these. They stay the game's while the shadow tests run the original and ours
// on them.
// XBE_GLOBAL(0x001ee438, 0x8000)
#define Drone_SM_LLItems (*(DroneSMItem(*)[DRONE_SM_ITEMS])0x001ee438)
// XBE_GLOBAL(0x001f6438, 0x10)
#define Drone_SM_LLDelayed (*(DroneSMLists *)0x001f6438)

// The drone whose state machine has this id, as FUN_0004e1b0 finds it (the id arrives in ECX there, so the
// original is not patched; Drone_SM_SetState inlines the same walk). NULL if no drone on the list has it.
static obj_tag *Drone_FindObjBySMId(uint id) {
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->sm.id == id)
            return drone->gameObj;
    }
    return NULL;
}

// The two list heads take items 0 and 1; the other 1022 go on the free list.
// AUTOINJECT
bool Drone_SM_Init(void) {
    Drone_SM_LLDelayed.items = &Drone_SM_LLItems[0];
    Drone_SM_LLDelayed.pending = &Drone_SM_LLItems[0];
    Drone_SM_LLItems[0].next = NULL;
    Drone_SM_LLItems[1].next = NULL;
    Drone_SM_LLDelayed.count = DRONE_SM_ITEMS;
    Drone_SM_LLDelayed.free = &Drone_SM_LLItems[1];
    for (int i = 2; i < DRONE_SM_ITEMS; i++) {
        DroneSMItem *item = &Drone_SM_LLItems[i];
        item->next = Drone_SM_LLDelayed.free->next;
        Drone_SM_LLDelayed.free->next = item;
    }
    return true;
}

// Queues a message behind every one due the same frame or earlier. Our name: only Drone_SM_RouteMsg calls it.
// With no free item left, the copy goes through NULL, as the original's does.
// FUNC_AT(0x0004e3c0)
void Drone_SM_QueueMsg(MsgObject *msg) {
    DroneSMItem *item = Drone_SM_LLDelayed.free->next;
    if (item != NULL)
        Drone_SM_LLDelayed.free->next = item->next;
    else
        NF_WARN("Too many AI messages\n"); // GC check (0x800c3e14)
    item->msg = *msg;

    DroneSMItem *prev = Drone_SM_LLDelayed.pending;
    DroneSMItem *at = prev->next;
    while (at != NULL && at->msg.handleOnFrame <= item->msg.handleOnFrame) {
        prev = at;
        at = at->next;
    }
    prev->next = item;
    item->next = at;
}

// Once a frame: routes every queued message that has fallen due, in order, and frees its item.
// AUTOINJECT
void Drone_SM_SendDelayedMsgs(void) {
    uint now = GameState.NumFramesUnpaused;
    DroneSMItem *head = Drone_SM_LLDelayed.pending;
    DroneSMItem *item = head->next;
    while (item != NULL && item->msg.handleOnFrame <= now) {
        Drone_SM_RouteMsg(&item->msg);
        // Taken off the list only now: routing it may have queued others behind it
        DroneSMItem *next = item->next;
        head->next = next;
        item->next = Drone_SM_LLDelayed.free->next;
        Drone_SM_LLDelayed.free->next = item;
        item = next;
    }
}

// Delivers a message, or queues it if it is not due yet: to every drone (receiver 0), to the multiplayer bots on
// team n (receiver -1-n), or to the drone with that id. Drones waiting on a switch, dead or fading are skipped, and
// so are those playing a script, except when the message is for them alone. A message to several drones has each
// one's id written into it in turn.
// AUTOINJECT
void Drone_SM_RouteMsg(MsgObject *msg) {
    if (msg->handleOnFrame > GameState.NumFramesUnpaused) {
        Drone_SM_QueueMsg(msg);
        return;
    }

    if (msg->receiver == 0) {
        for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
            switch (drone->sm.curState) {
            case DSTATE_WaitSwitch:
            case DSTATE_PlayScript:
            case DSTATE_HostageDead:
            case DSTATE_Dead:
            case DSTATE_Fade:
                continue;
            }
            DCVars_tag dcv;
            dcv.gameObj = drone->gameObj;
            dcv.drone = drone;
            dcv.aiStateMachine = &drone->sm;
            dcv.cel = drone->gameObj->inCel;
            msg->receiver = drone->sm.id;
            _Drone_SM_RouteMsgDCV(&dcv, msg);
        }
        return;
    }

    if (MPSettings.maybeDroneAIEnabled != 0 && msg->receiver < 0) {
        // The bots follow the human players. Worked out before the loop, which overwrites the receiver.
        int team = -1 - msg->receiver;
        for (int i = NUM_PLAYERS; i < NUM_AGENTS; i++) {
            obj_tag *gameObj = MPGame.players[i].playerObj;
            if (gameObj == NULL || (int)MPSettings.Player[i].TeamId != team)
                continue;
            // Whether it worked is not tested
            DCVars_tag dcv;
            Drone_DCVfromOBJ(gameObj, &dcv);
            switch (dcv.aiStateMachine->curState) {
            case DSTATE_WaitSwitch:
            case DSTATE_PlayScript:
            case DSTATE_HostageDead:
            case DSTATE_Dead:
            case DSTATE_Fade:
            case DSTATE_FadeFast:
                continue;
            }
            msg->receiver = dcv.aiStateMachine->id;
            _Drone_SM_RouteMsgDCV(&dcv, msg);
        }
        return;
    }

    obj_tag *gameObj = Drone_FindObjBySMId(msg->receiver);
    if (gameObj == NULL)
        return;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(gameObj, &dcv);
    switch (dcv.aiStateMachine->curState) {
    case DSTATE_WaitSwitch:
    case DSTATE_HostageDead:
    case DSTATE_Dead:
    case DSTATE_Fade:
    case DSTATE_FadeFast:
        return;
    }
    _Drone_SM_RouteMsgDCV(&dcv, msg);
}

// Delivers a message to the drone's state machine (README 3.3), when it is for any state, for BotGlobal or for the
// current one: the current state's handler gets it, and if it does not handle it, DSTATE_Global (BotGlobal for a bot,
// whose BotGlobal sees every Update first anyway). Then the state changes asked for meanwhile happen, one after
// another: Exit to the old state, the drone's per-state fields reset, Enter to the new one.
void _Drone_SM_RouteMsgDCV(DCVars_tag *dcv, MsgObject *msg) {
    StateMachineInfo_tag *sm = dcv->aiStateMachine;
    if (msg->scope != 0 && msg->scope != DSTATE_BotGlobal && msg->scope != sm->curState)
        return;

    bool botUpdate = false;
    if (dcv->drone->dtype == DTYPE_Bot && msg->msgType == DRONE_MSG_Update) {
        if (sm->processFunction != NULL)
            sm->processFunction(dcv, DSTATE_BotGlobal, msg);
        botUpdate = true;
    }
    bool handled = sm->processFunction != NULL && sm->processFunction(dcv, sm->curState, msg);
    if (!handled && !botUpdate && sm->processFunction != NULL)
        sm->processFunction(dcv, dcv->drone->dtype == DTYPE_Bot ? DSTATE_BotGlobal : DSTATE_Global, msg);

    // Exit, BotStateChanged and Enter, one message reused. The original leaves its scope (until BotStateChanged
    // sets it) and both frames uninitialised.
    MsgObject own;
    own.scope = 0;
    own.createdFrame = 0;
    own.handleOnFrame = 0;
    int changes = 0;
    while (sm->changePending) {
        if (++changes > 1000)
            NF_WARN("Possible infinite loop\n"); // GC check (0x800c3fa8)
        own.sender = sm->id;
        own.receiver = sm->id;
        own.extraData = msg->extraData;
        sm->changePending = 0;

        // Where a bot is going decides whether its gun keeps firing
        bool burst = false;
        bool keepFiring = false;
        if (dcv->drone->dtype == DTYPE_Bot) {
            uint kind = BOTSTATE_getStateType(sm->nextState);
            if (kind == BOTSTATE_CLASS_IMPACT || kind == BOTSTATE_CLASS_DEATH) {
                burst = true;
            } else if (kind == BOTSTATE_CLASS_ATTACK) {
                uint next = sm->nextState;
                keepFiring = next != DSTATE_BotAttackNoOpponent &&
                             !(next >= DSTATE_BotAttackReload && next <= DSTATE_BotAttackUnarmed);
            }
        }
        switch (sm->nextState) {
        case DSTATE_Death_Anim:
        case DSTATE_SpecialDeath_Anim:
        case DSTATE_Dead:
        case DSTATE_BulletImpact:
            dcv->drone->oneBurst = 1;
            break;
        default:
            if (burst)
                dcv->drone->oneBurst = 1;
            else if (!keepFiring)
                dcv->drone->firing = 0;
            break;
        }

        own.msgType = DRONE_MSG_Exit;
        if (sm->processFunction != NULL)
            sm->processFunction(dcv, sm->curState, &own);

        sm->prevState = sm->curState;
        sm->curState = sm->nextState;
        dcv->gameObj->curState = (ushort)sm->curState;
        dcv->drone->flags &= ~0x10000;
        dcv->drone->flags &= ~0x2000;
        dcv->drone->hitDamageScale = 1.0f;
        dcv->drone->timerA.on = 0;
        dcv->drone->timerB.on = 0;

        if (dcv->drone->dtype == DTYPE_Bot) {
            own.msgType = DRONE_MSG_BotStateChanged;
            own.scope = sm->curState;
            if (sm->processFunction != NULL)
                sm->processFunction(dcv, DSTATE_BotGlobal, &own);
        }

        sm->stateEnteredFrame = GameState.NumFramesUnpaused;
        own.msgType = DRONE_MSG_Enter;
        if (sm->processFunction != NULL)
            sm->processFunction(dcv, sm->curState, &own);
    }
}

// The original takes the DCVars in ESI and the message on the stack, and its callers clean up.
// AUTOLTCG
void __declspec(naked) Drone_SM_RouteMsgDCV(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Drone_SM_RouteMsgDCV
        add esp, 8
        ret
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

    sm->processFunction = NDrone2_ProcessStateMachine;

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
        _Drone_SM_RouteMsgDCV(dcVars, &msg);
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
