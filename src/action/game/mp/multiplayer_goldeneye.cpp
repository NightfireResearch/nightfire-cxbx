// GoldenEye: the key and the crystal, taken, carried, dropped and returned, and the GoldenEye's ray fired at the other
// team once one team holds both.

#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Text.h"
#include "../../sound/Sound.h"
#include "../../util/Random.h"
#include "../obj/Player.h"
#include "../obj/ScriptPlayer.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"
#include "../view.h"
#include "../../../driving/platform/X87.h" // Ftol
#include <stdio.h>

// Defined in multiplayer.cpp's AUTOGEN
short Control_Plr2Ind(obj_tag* a);
// Defined in multiplayer_modes.cpp's AUTOGEN
void MP_sendBotMessage(obj_tag *obj, uint msg, uint param3, uint param4);

// AUTOGEN
void Player_Kill(obj_tag *obj);
// AUTOGEN
undefined MPSound_Play(Action_SFX sfx);

#define MP_sendTeamBotMessage ((void (__cdecl *)(MPTeam team, uint msg, obj_tag *obj, uint param4, uint senderId))0x0009e430)

// The texts (Ghidra's names)
#define NOTIF_X_CAPTURED_GOLDENEYE_KEY ((Action_TranslatedText)0x0200003e)
#define NOTIF_X_CAPTURED_GOLDENEYE_CRYSTAL ((Action_TranslatedText)0x0200003f)
#define NOTIF_X_DROPPED_GOLDENEYE_KEY ((Action_TranslatedText)0x02000040)
#define NOTIF_X_DROPPED_GOLDENEYE_CRYSTAL ((Action_TranslatedText)0x02000041)
#define NOTIF_X_ACTIVATED_GOLDENEYE ((Action_TranslatedText)0x02000042)
#define NOTIF_GOLDENEYE_KEY_RETURNED ((Action_TranslatedText)0x02000043)
#define NOTIF_GOLDENEYE_CRYSTAL_RETURNED ((Action_TranslatedText)0x02000044)

// The ray's script (name ours)
#define HT_Script_GoldenEyeRay ((HASHCODE)0x0600003e)

// mpObj in ECX, gameObj in EBX, keepPlace in AL, state on the stack, removed by the caller. EBX is callee-saved
// for our compiler, so it is put back afterwards.
static void __declspec(naked) MP_ResetMPObject(MPOBJECT *mpObj, short state, obj_tag *gameObj, bool keepPlace) {
    _asm {
        push ebx
        mov ecx, [esp + 8]          // mpObj
        mov ebx, [esp + 16]         // gameObj
        mov al, [esp + 20]          // keepPlace
        push dword ptr [esp + 12]   // state
        mov edx, 0x0009ecb0
        call edx
        add esp, 4
        pop ebx
        ret
    }
}

// obj on the stack, removed by the caller; holder in EBX, gameObj in ESI, point in EAX. EBX and ESI are put back.
static void __declspec(naked) MP_SetUpPlayerSomehow(obj_tag *obj, obj_tag *holder, obj_tag *gameObj, int point) {
    _asm {
        push ebx
        push esi
        mov ebx, [esp + 16]         // holder
        mov esi, [esp + 20]         // gameObj
        mov eax, [esp + 24]         // point
        push dword ptr [esp + 12]   // obj
        mov edx, 0x0009c7e0
        call edx
        add esp, 4
        pop esi
        pop ebx
        ret
    }
}

// Bot messages sent here: 0x35 / 0x36 when the key / the crystal is taken, after 0x3b and 0x3d (see
// multiplayer_modes.cpp); 0x37 / 0x38 when it is let go, after 0x3c.

// How long the GoldenEye's messages stay up, in frames
static short MP_GoldenEyeMsgFrames(void) {
    return (short)Ftol(FRAME_RATE_INT * 1.5);
}

// The part and its team's name, into a message. (Our helper; the original repeats it inline.)
static void MP_GoldenEyeMsg(MPOBJECT *mpObj, Action_TranslatedText key, Action_TranslatedText crystal) {
    char *str = Txt_GetStringFromHeap(0);
    const char *team = Txt_BindLabel(mpObj->num == PHOENIX ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0);
    const char *format = Txt_BindLabel(mpObj->type == GOLDENEYE_KEY ? key : crystal, 0);
    sprintf(str, format, team);
    Text_AddMsg(-1, 0, 4, str, 0, MP_GoldenEyeMsgFrames());
}

// An agent takes a part. (Our helper; the original repeats it for each part.)
static void MP_GoldenEyeTake(obj_tag *gameObj, obj_tag *taker, ushort playerFlag, ushort objectiveFlag, uint msg) {
    short idx = Control_Plr2Ind(taker);
    if (idx == -1)
        return;
    MPGame.players[idx].flags |= playerFlag;
    MPTeam team = MPSettings.Player[idx].TeamId;
    uint senderId = 0;
    if (taker->objectType == OBJECTTYPE_DRONE) {
        DCVars_tag dcVars;
        Drone_DCVfromOBJ(taker, &dcVars);
        senderId = dcVars.aiStateMachine->id;
    }
    MPGame.teamObjectiveFlags[team] |= objectiveFlag;
    MP_sendBotMessage(taker, 0x3b, 1, 0);
    MP_sendTeamBotMessage(team, 0x3d, gameObj, 0, senderId);
    MPGame.unknown_9 = 0;
    MP_sendBotMessage(taker, msg, 0, 0);
}

// An agent lets go of a part. (Our helper; the original repeats it inline.)
static void MP_GoldenEyeLetGo(obj_tag *holder, ushort playerFlag, ushort objectiveFlag, uint msg) {
    if (holder == NULL)
        return;
    short idx = Control_Plr2Ind(holder);
    if (idx == -1)
        return;
    MPGame.players[idx].flags &= ~playerFlag;
    MPTeam team = MPSettings.Player[idx].TeamId;
    if (holder->objectType == OBJECTTYPE_DRONE) {
        DCVars_tag dcVars;
        Drone_DCVfromOBJ(holder, &dcVars);
    }
    MPGame.teamObjectiveFlags[team] &= ~objectiveFlag;
    MP_sendBotMessage(holder, 0x3c, 1, 0);
    MPGame.unknown_9 = 0;
    MP_sendBotMessage(holder, msg, 0, 0);
}

// mpObj->holder has just taken the part.
static void MP_GoldenEyeTaken(MPOBJECT *mpObj, obj_tag *gameObj) {
    if (mpObj->type == GOLDENEYE_KEY)
        MP_GoldenEyeTake(gameObj, mpObj->holder, MPPLAYER_GOLDENEYE_KEY, 1, 0x35);
    else
        MP_GoldenEyeTake(gameObj, mpObj->holder, MPPLAYER_GOLDENEYE_CRYSTAL, 2, 0x36);
    gameObj->curState = 1;
    gameObj->maybeParent = mpObj->holder;
    MP_GoldenEyeMsg(mpObj, NOTIF_X_CAPTURED_GOLDENEYE_KEY, NOTIF_X_CAPTURED_GOLDENEYE_CRYSTAL);
    Sound_PlayExt(SFX_PICKUP_DM_COLLECT_GOLDEN_GUN_PART, 100.0f, 0, 0);
}

// Back to a random one of the part's places: 0-7 are the key's, 8-15 the crystal's. unused is not read (callers
// pass 99, 2 or 0); state goes to MP_ResetMPObject.
// AUTOINJECT
void MP_GoldeneyeResetObject(MPOBJECT *mpObj, obj_tag *gameObj, int unused, char state) {
    ushort set = mpObj->type != GOLDENEYE_KEY;
    ushort place = Rand_Rand(set ? GoldenEyeNonKeyCount : GoldenEyeKeyCount);
    Mat_Copy(&GoldenEyeSpawns[place + set * 8].mtx, &mpObj->resetMtx);
    MP_ResetMPObject(mpObj, state, gameObj, false);
}

// gameObj->curState: 0 at its place, 1 carried (3 while the ray is out), 2 dropped, 99 to be put back to 0.
// gameObj->subState is the part's index in GoldenEye.keys; mpObj->num the team of whoever last took it.
void _MP_GoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    MPOBJECT *other = (MPOBJECT *)GoldenEye.keys[gameObj->subState == 0].gameObj->extraObjectData;

    if (GoldenEye.deathRay != NULL) {
        if (GoldenEye.targetedPlayer != NULL) {
            _VECTOR pos;
            Vec_Copy(&GoldenEye.targetedPlayer->position, &pos);
            pos.y -= 1.0f;
            GoldenEye.deathRay->transformFlags |= TRANSFORM_MOVED;
            SP_SetPos(GoldenEye.deathRay, &pos);
        }

        if (SP_GetState(GoldenEye.deathRay) == 2) {
            obj_tag *target = GoldenEye.targetedPlayer;
            if (target != NULL) {
                if (!MP_playerIsDead(target)) {
                    if (target->objectType == OBJECTTYPE_PLAYER) {
                        Player_Kill(target);
                    } else {
                        Drone_tag *drone = (Drone_tag *)target->extraObjectData;
                        drone->health = -1.0f;
                        Drone_SM_SetState(&drone->sm, DSTATE_BotDeathAnim, 0);
                    }
                }
                short idx = Control_Plr2Ind(GoldenEye.targetedPlayer);
                if (idx != -1) {
                    MPGame.teamScore[MPSettings.Player[idx].TeamId == PHOENIX ? MI6 : PHOENIX] += 1.0f;
                    MPGame.players[idx].maybeIdxOfLastInjurer = -2;
                }
            }
            GoldenEye.deathRay = NULL;
            GoldenEye.targetedPlayer = NULL;

            // The key's flags for this part's holder and the crystal's for the other's, whichever this part is
            MP_GoldenEyeLetGo(mpObj->holder, MPPLAYER_GOLDENEYE_KEY, 1, 0x37);
            MP_GoldenEyeLetGo(other->holder, MPPLAYER_GOLDENEYE_CRYSTAL, 2, 0x38);
            GoldenEye.keys[1].gameObj->effectFlags &= ~FLAG_HIDDEN;
            GoldenEye.keys[0].gameObj->effectFlags &= ~FLAG_HIDDEN;
            MP_GoldeneyeResetObject(mpObj, gameObj, 99, 0);
            MP_GoldeneyeResetObject(other, GoldenEye.keys[gameObj->subState == 0].gameObj, 99, 0);
        }
    }

    View_SetDrawInAllViews(gameObj);

    switch (gameObj->curState) {
    case 0:
        mpObj->holder = _MP_HitBy(gameObj, NO_TEAM, NULL, &mpObj->num);
        if (mpObj->holder == NULL)
            return;
        MP_GoldenEyeTaken(mpObj, gameObj);
        return;

    case 1:
        if (!dropped) {
            MP_SetUpPlayerSomehow(gameObj, mpObj->holder, gameObj, gameObj->subState != 0 ? 0x23 : 0x15);

            // Both parts held by one team, and no ray out yet: fire it at an agent of the other team
            if (GoldenEye.keys[gameObj->subState == 0].gameObj->curState != 1)
                return;
            if (mpObj->num != other->num)
                return;
            if (GoldenEye.deathRay != NULL)
                return;
            GoldenEye.targetedPlayer = MP_GetTarget(mpObj->num == PHOENIX ? MI6 : PHOENIX, NULL, true);
            if (GoldenEye.targetedPlayer == NULL)
                return;

            short idx = Control_Plr2Ind(mpObj->holder);
            if (idx != -1)
                MPGame.players[idx].points += 1.0f;
            idx = Control_Plr2Ind(other->holder);
            if (idx != -1)
                MPGame.players[idx].points += 1.0f;

            MPSound_Play(SFX_BOND_MOMENT_BM_TENSE);
            GoldenEye.keys[1].gameObj->curState = 3;
            GoldenEye.keys[1].gameObj->effectFlags |= FLAG_HIDDEN;
            GoldenEye.keys[0].gameObj->curState = 3;
            GoldenEye.keys[0].gameObj->effectFlags |= FLAG_HIDDEN;
            GoldenEye.deathRay = SP_Create(&gameObj->position, NULL, HT_Script_GoldenEyeRay, (HASHCODE)0, 1, 1, NULL,
                                           NULL, NULL, 0);

            char *str = Txt_GetStringFromHeap(0);
            const char *team = Txt_BindLabel(mpObj->num == PHOENIX ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0);
            const char *format = Txt_BindLabel(NOTIF_X_ACTIVATED_GOLDENEYE, 0);
            sprintf(str, format, team);
            Text_AddMsg(-1, 0, 4, str, 0, MP_GoldenEyeMsgFrames());

            if (GoldenEye.targetedPlayer->objectType == OBJECTTYPE_PLAYER)
                Player_SetCamMode((BLData *)GoldenEye.targetedPlayer->extraObjectData, 1);
            return;
        }

        // Dropped (MP_PlayerKilled, when its holder dies): onto the floor below it
        if (mpObj->type == GOLDENEYE_KEY)
            MP_GoldenEyeLetGo(gameObj->maybeParent, MPPLAYER_GOLDENEYE_KEY, 1, 0x37);
        else
            MP_GoldenEyeLetGo(gameObj->maybeParent, MPPLAYER_GOLDENEYE_CRYSTAL, 2, 0x38);
        {
            _VECTOR *pos = Mat_Position(gameObj->transformMatrix);
            build_PointOnFloor(gameObj->inCel, gameObj, pos, gameObj->objGraphics->extentMin.y, NULL);
            Vec_Copy(pos, &gameObj->position);
        }
        MP_GoldeneyeResetObject(mpObj, gameObj, 2, 1);
        MP_GoldenEyeMsg(mpObj, NOTIF_X_DROPPED_GOLDENEYE_KEY, NOTIF_X_DROPPED_GOLDENEYE_CRYSTAL);
        gameObj->curState = 2;
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
        return;

    case 2: {
        mpObj->holder = _MP_HitBy(gameObj, NO_TEAM, NULL, &mpObj->num);
        if (mpObj->holder != NULL) {
            MP_GoldenEyeTaken(mpObj, gameObj);
            return;
        }

        // Left on the floor for 30 seconds: back to a place
        ushort frames = mpObj->unknown04;
        mpObj->unknown04 = frames + 1;
        if (frames > FRAME_RATE_INT * 30) {
            mpObj->unknown04 = 0;
            MP_GoldeneyeResetObject(mpObj, gameObj, 0, 0);
            char *str = Txt_GetStringFromHeap(0);
            const char *format = Txt_BindLabel(mpObj->type == GOLDENEYE_KEY ? NOTIF_GOLDENEYE_KEY_RETURNED
                                                                            : NOTIF_GOLDENEYE_CRYSTAL_RETURNED, 0);
            sprintf(str, format);
            Text_AddMsg(-1, 0, 4, str, 0, MP_GoldenEyeMsgFrames());
            MPSound_Play(SFX_BOND_MOMENT_BM_CAS_BEAM);
        }
        break;
    }

    case 99:
        gameObj->curState = 0;
        break;
    }
}

// AUTOLTCG
void __declspec(naked) MP_GoldenEyeUpdate(MPOBJECT *mpObj, bool dropped, obj_tag *gameObj) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 12]   // dropped
        push dword ptr [esp + 12]   // mpObj
        call _MP_GoldenEyeUpdate
        add esp, 12
        ret
    }
}
