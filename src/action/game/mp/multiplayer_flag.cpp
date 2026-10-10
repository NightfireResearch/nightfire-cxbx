// The carried scenario objects: the Capture the Flag flags and the Espionage blueprint.

#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../sound/Sound.h"
#include "../view.h"
#include "../drone/Drone.h"
#include "../../../driving/platform/X87.h" // Ftol
#include <stdio.h>

#pragma fp_contract(off)

// Defined in multiplayer.cpp's AUTOGEN
short Control_Plr2Ind(obj_tag* a);
// Defined in multiplayer_modes.cpp's AUTOGEN
void MP_sendBotMessage(obj_tag *obj, uint msg, uint param3, uint param4);

#define MP_setPlayerStatus ((void (__cdecl *)(obj_tag *obj, ushort status, uint msg, uint param4, obj_tag *target, bool clear))0x0009e490)
#define MPSound_Play ((void (__cdecl *)(Action_SFX sfx))0x0009ec60)
#define MP_sendTeamBotMessage ((void (__cdecl *)(MPTeam team, uint msg, obj_tag *obj, uint param4, uint senderId))0x0009e430)
#define MP_BluePrintReachedBase ((void (__cdecl *)(obj_tag *gameObj, MPOBJECT *mpObj, int param3))0x000a0ec0)

// mpObj in ECX, gameObj in EBX, stayPut in AL, the state on the stack, removed by the caller. Unless stayPut, the
// object goes back to the matrix at mpObj + 0x10.
static void __declspec(naked) MP_ResetMPObject(MPOBJECT *mpObj, ushort state, obj_tag *gameObj, bool stayPut) {
    _asm {
        push ebx
        mov ecx, [esp + 8]          // mpObj
        mov ebx, [esp + 16]         // gameObj
        mov al, [esp + 20]          // stayPut
        push dword ptr [esp + 12]   // state
        mov edx, 0x0009ecb0
        call edx
        add esp, 4
        pop ebx
        ret
    }
}

// Puts gameObj on its holder: gameObj in ESI and on the stack (removed by the caller), holder in EBX, attach in EAX.
static void __declspec(naked) MP_SetUpPlayerSomehow(obj_tag *gameObj, obj_tag *holder, int attach) {
    _asm {
        push ebx
        push esi
        mov esi, [esp + 12]         // gameObj
        mov ebx, [esp + 16]         // holder
        mov eax, [esp + 20]         // attach
        push esi
        mov edx, 0x0009c7e0
        call edx
        add esp, 4
        pop esi
        pop ebx
        ret
    }
}

// A message to every player naming team. (Our helper; the original repeats it inline.)
static void MP_TeamMessage(Action_TranslatedText format, ushort team) {
    char *str = Txt_GetStringFromHeap(0);
    const char *teamName = Txt_BindLabel(team == PHOENIX ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0);
    sprintf(str, Txt_BindLabel(format, 0), teamName);
    Text_AddMsg(-1, 0, 4, str, 0, Ftol(FRAME_RATE_INT * 1.5));
}

// The holder no longer carries the object: its status bit, its team's objective bits and its goal go. (Our
// helper; the original repeats it inline.)
static void MP_ClearCarrier(obj_tag *holder, ushort status, ushort objectiveBits, uint goalMsg, uint msg) {
    if (holder == NULL)
        return;
    short idx = Control_Plr2Ind(holder);
    if (idx == -1)
        return;

    MPGame.players[idx].flags &= ~status;
    MPTeam team = MPSettings.Player[idx].TeamId;
    if (holder->objectType == OBJECTTYPE_DRONE) {
        DCVars_tag dcVars;
        Drone_DCVfromOBJ(holder, &dcVars);
    }
    MPGame.teamObjectiveFlags[team] &= ~objectiveBits;
    MP_sendBotMessage(holder, goalMsg, 1, 0);
    MPGame.teamObjectiveFlags[NO_TEAM] = 0;
    MP_sendBotMessage(holder, msg, 0, 0);
}

// Drops the object where its holder was, on the floor.
static void MP_DropToFloor(MPOBJECT *mpObj, obj_tag *gameObj) {
    _VECTOR *pos = Mat_Position(gameObj->transformMatrix);
    build_PointOnFloor(gameObj->inCel, gameObj, pos, gameObj->objGraphics->extentMin.y, NULL);
    Vec_Copy(pos, &gameObj->position);
    MP_ResetMPObject(mpObj, 2, gameObj, true);
}

// gameObj->curState: 0 at its base, 1 carried (by gameObj->maybeParent), 2 dropped. mpObj->num is the flag's team.
// The other team takes it from its base and scores by bringing it to its own; its own team returns it once
// dropped, and it goes back by itself after 30 seconds on the floor.
void _MP_FlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped) {
    MPTeam other = mpObj->num == PHOENIX ? MI6 : PHOENIX;

    View_SetDrawInAllViews(gameObj);
    switch (gameObj->curState) {
    case 0:
        mpObj->holder = _MP_HitBy(gameObj, other, NULL, NULL);
        if (mpObj->holder == NULL)
            return;
        MP_setPlayerStatus(mpObj->holder, MPPLAYER_FLAG_1, 0x2f, mpObj->num, gameObj, false);
        gameObj->curState = 1;
        gameObj->maybeParent = mpObj->holder;
        MP_TeamMessage(NOTIF_X_FLAG_CAPTURED, mpObj->num);
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
        break;

    case 1: {
        if (dropped) {
            MP_ClearCarrier(gameObj->maybeParent, MPPLAYER_FLAG_1, 1, 0x3c, 0x30);
            MP_DropToFloor(mpObj, gameObj);
            gameObj->curState = 2;
            MP_TeamMessage(NOTIF_X_FLAG_DROPPED, mpObj->num);
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            return;
        }

        MP_SetUpPlayerSomehow(gameObj, mpObj->holder, 0x15);
        _VECTOR goal = *Mat_Position(Bases[other].gameObj->transformMatrix);
        goal.y += 1.0f;
        if (!(Vec_SqDist3D(Mat_Position(gameObj->transformMatrix), &goal) < 2.0f))
            return;

        MP_ClearCarrier(gameObj->maybeParent, MPPLAYER_FLAG_1, 3, 0x3b, 0x31);
        MPGame.teamScore[other] += 1.0f;
        short idx = Control_Plr2Ind(gameObj->maybeParent);
        if (idx != -1)
            MPGame.players[idx].points += 1.0f;
        MP_ResetMPObject(mpObj, 0, gameObj, false);
        MP_TeamMessage(NOTIF_X_TEAM_SCORED, other);
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_TRUCK, 100.0f, 0, 0);
        break;
    }

    case 2: {
        if (_MP_HitBy(gameObj, mpObj->num, NULL, NULL) != NULL) {
            mpObj->unknown04 = 0;
            MP_ResetMPObject(mpObj, 0, gameObj, false);
            MP_TeamMessage(NOTIF_X_FLAG_RETURNED, mpObj->num);
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            return;
        }

        mpObj->holder = _MP_HitBy(gameObj, other, NULL, NULL);
        if (mpObj->holder == NULL) {
            ushort frames = mpObj->unknown04++;
            if (frames <= FRAME_RATE_INT * 30)
                return;
            mpObj->unknown04 = 0;
            MP_ResetMPObject(mpObj, 0, gameObj, false);
            MP_TeamMessage(NOTIF_X_FLAG_RETURNED, mpObj->num);
            MPSound_Play(SFX_BOND_MOMENT_BM_CAS_BEAM);
            return;
        }

        MP_setPlayerStatus(mpObj->holder, MPPLAYER_FLAG_1, 0x2f, mpObj->num, gameObj, false);
        gameObj->curState = 1;
        gameObj->maybeParent = mpObj->holder;
        MP_TeamMessage(NOTIF_X_FLAG_PICKED_UP, mpObj->num);
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
        break;
    }
    }
}

// gameObj in EAX, mpObj in EDI, dropped on the stack, removed by the caller.
// AUTOLTCG
void __declspec(naked) MP_FlagUpdate(obj_tag *gameObj, MPOBJECT *mpObj, bool dropped) {
    _asm {
        push dword ptr [esp + 4]    // dropped
        push edi                    // mpObj
        push eax                    // gameObj
        call _MP_FlagUpdate
        add esp, 12
        ret
    }
}

// An agent takes the blueprint. (Our helper; the original repeats it inline.)
static void MP_BluePrintTaken(obj_tag *gameObj, obj_tag *taker) {
    short idx = Control_Plr2Ind(taker);
    if (idx == -1)
        return;

    MPGame.players[idx].flags |= MPPLAYER_FLAG_2;
    MPTeam team = MPSettings.Player[idx].TeamId;
    uint senderId = 0;
    if (taker->objectType == OBJECTTYPE_DRONE) {
        DCVars_tag dcVars;
        Drone_DCVfromOBJ(taker, &dcVars);
        senderId = dcVars.aiStateMachine->id;
    }
    MPGame.teamObjectiveFlags[team] |= 1;
    MP_sendBotMessage(taker, 0x3b, 1, 0);
    MP_sendTeamBotMessage(team, 0x3d, gameObj, 0, senderId);
    MPGame.teamObjectiveFlags[NO_TEAM] = 0;
    MP_sendBotMessage(taker, 0x32, 0, 0);
}

// gameObj->curState: 0 at its spawn place, 1 carried (by gameObj->maybeParent), 2 dropped. Either team takes it,
// and mpObj->num becomes the taker's team, which scores by bringing it to its espionage base. Dropped, it goes
// back after 30 seconds on the floor unless someone takes it.
void _MP_BluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    View_SetDrawInAllViews(gameObj);
    switch (gameObj->curState) {
    case 0:
        mpObj->holder = _MP_HitBy(gameObj, NO_TEAM, NULL, &mpObj->num);
        if (mpObj->holder == NULL)
            return;
        MP_BluePrintTaken(gameObj, mpObj->holder);
        MP_TeamMessage(NOTIF_X_PICKED_UP_BLUEPRINT, mpObj->num);
        gameObj->curState = 1;
        gameObj->maybeParent = mpObj->holder;
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
        break;

    case 1:
        if (dropped) {
            MP_ClearCarrier(gameObj->maybeParent, MPPLAYER_FLAG_2, 1, 0x3c, 0x33);
            MP_DropToFloor(mpObj, gameObj);
            MP_TeamMessage(NOTIF_X_DROPPED_BLUEPRINT, mpObj->num);
            gameObj->curState = 2;
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            return;
        }

        MP_SetUpPlayerSomehow(gameObj, mpObj->holder, 0x15);
        for (HITDATA_tag *hit = EsponageBase[mpObj->num].gameObj->hitList; hit != NULL; hit = hit->next) {
            if (hit->hitObj == mpObj->holder) {
                MP_BluePrintReachedBase(gameObj, mpObj, 0);
                return;
            }
        }
        break;

    case 2:
        mpObj->holder = _MP_HitBy(gameObj, NO_TEAM, NULL, &mpObj->num);
        if (mpObj->holder == NULL) {
            ushort frames = mpObj->unknown04++;
            if (frames <= FRAME_RATE_INT * 30)
                return;
            mpObj->unknown04 = 0;
            MP_ResetMPObject(mpObj, 0, gameObj, false);
            char *str = Txt_GetStringFromHeap(0);
            sprintf(str, Txt_BindLabel(NOTIF_BLUEPRINT_RETURNED, 0));
            Text_AddMsg(-1, 0, 4, str, 0, Ftol(FRAME_RATE_INT * 1.5));
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            return;
        }

        MP_BluePrintTaken(gameObj, mpObj->holder);
        gameObj->curState = 1;
        gameObj->maybeParent = mpObj->holder;
        MP_TeamMessage(NOTIF_X_PICKED_UP_BLUEPRINT, mpObj->num);
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
        break;
    }
}

// gameObj in EAX, the other two on the stack, removed by the caller.
// AUTOLTCG
void __declspec(naked) MP_BluePrintUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj (12 again: the push moved it along)
        call _MP_BluePrintUpdate
        add esp, 12
        ret
    }
}
