// Game-mode updates and the death of an agent: King of the Hill, Uplink, and MP_PlayerKilled's scoring.

#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../sound/Sound.h"
#include "../obj/bullet.h"
#include "../obj/car.h"
#include "../obj/GT.h"
#include "../obj/GunImp.h"
#include "../obj/Player.h"
#include "../obj/ScriptPlayer.h"
#include "../drone/Drone.h"
#include "../drone/BOT.h"
#include "../drone/NDrone2.h"
#include <stdio.h>
#include <bit>

#pragma fp_contract(off)

// The objects that damaged the dying agent, from its hit list
#define HitByList (*(obj_tag*(*)[64])0x00261c58)

// Defined in multiplayer.cpp's AUTOGEN
short Control_Plr2Ind(obj_tag* a);

// AUTOGEN
void MP_sendBotMessage(obj_tag *obj, uint msg, uint param3, uint param4);
// AUTOGEN
bool Intersect_SphereBox(_VECTOR *spherePosition, float radius, _VECTOR *bboxMin, _VECTOR *bboxMax);
// AUTOGEN
ushort Hurt_GetType(obj_tag *obj);

#define BOT_SetHealth ((void (__cdecl *)(Drone_tag *drone, float health))0x0001b120)
#define MP_sendTeamBotMessage ((void (__cdecl *)(MPTeam team, uint msg, obj_tag *obj, uint param4, uint senderId))0x0009e430)

// Bot messages sent here (docs/drone/bots-and-navigation, DSTATE_BotGlobal): 0x3a objective changed, 0x3b goal
// complete, 0x3d cancel goals on an object, 0x43 a player died. 0x39, 0x3e and 0x3f are not in its table.

// A team's score in the team game: the whole points of its agents. (Our helper; the original repeats it inline.)
static void MP_SumTeamScore(int team) {
    MPGame.teamScore[team] = 0;
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (MPSettings.Player[i].TeamId == team)
            MPGame.teamScore[team] += (int)MPGame.players[i].points;
    }
}

// AUTOINJECT
void MP_KOHUpdate(obj_tag *hill) {
    for (int i = 0; i < NUM_AGENTS; i++) {
        MPGamePlayer &player = MPGame.players[i];
        obj_tag *obj = player.playerObj;
        if (obj == NULL)
            continue;

        char type = obj->objectType;
        if (type == OBJECTTYPE_DRONE || type == OBJECTTYPE_DEAD_DRONE) {
            Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
            if (drone->flags & 0x600)
                continue;
            if (!(drone->flags & 0x100))
                continue;
            if (drone->health <= 0.0f)
                continue;
            if (type == OBJECTTYPE_DEAD_DRONE)
                continue;
            if (obj->flags & 1)
                continue;
        }
        if (type == OBJECTTYPE_DEAD_PLAYER)
            continue;
        if (type == OBJECTTYPE_PLAYER && (obj->curState == 2 || obj->curState == 3))
            continue;

        _VECTOR offset;
        Vec_Subtract(&obj->centrePoint, Mat_Position(hill->transformMatrix), &offset);
        bool inside = Intersect_SphereBox(&offset, 0.0f, &hill->objGraphics->extentMin, &hill->objGraphics->extentMax);
        ushort flags = player.flags;

        if (inside) {
            if (!(flags & MPPLAYER_IN_HILL) &&
                (player.hillSoundTime == 0.0f || (double)player.hillSoundTime + 5.0 < MPGame.TimeIncPaused)) {
                Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
                player.hillSoundTime = MPGame.TimeIncPaused;
            }

            short idx = Control_Plr2Ind(obj);
            if (idx != -1) {
                ushort was = MPGame.players[idx].flags;
                MPGame.players[idx].flags = was | MPPLAYER_IN_HILL;
                if (obj->objectType == OBJECTTYPE_DRONE) {
                    DCVars_tag dcVars;
                    Drone_DCVfromOBJ(obj, &dcVars);
                }
                bool entered = !(was & MPPLAYER_IN_HILL);
                if (entered)
                    MP_sendBotMessage(obj, 0x3b, 1, 0);
                MPGame.unknown_9 = 0;
                if (entered)
                    MP_sendBotMessage(obj, 0x39, 0, 0);
            }

            int before = (int)player.points;
            player.points = (float)((double)REC_FRAME_RATE * 0.2f + player.points);
            if (MPSettings.maybeIsTeamGame)
                MP_SumTeamScore(MPSettings.Player[i].TeamId);
            int after = (int)player.points;
            if ((uint)after % 5 == 0 && after != before)
                Sound_PlayExt(SFX_GENERIC_BEEP_HITECHBEEP09, 100.0f, 0, 0);
        }
        else if (flags & MPPLAYER_IN_HILL) {
            short idx = Control_Plr2Ind(obj);
            if (idx != -1) {
                MPGame.players[idx].flags &= ~MPPLAYER_IN_HILL;
                if (obj->objectType == OBJECTTYPE_DRONE) {
                    DCVars_tag dcVars;
                    Drone_DCVfromOBJ(obj, &dcVars);
                }
                MPGame.unknown_9 = 0;
                MP_sendBotMessage(obj, 0x3a, 0, 0);
            }
        }
    }
}

// Our helper for the colour change the original repeats inline.
static void MP_UplinkSetColour(MPOBJECT *mpObj, obj_tag *gameObj) {
    if (gameObj->curState == MI6) {
        gameObj->tweakR = 0x2d;
        gameObj->tweakG = 0x61;
        gameObj->tweakB = 0xd2;
    } else {
        gameObj->tweakR = 0xd2;
        gameObj->tweakG = 0x2d;
        gameObj->tweakB = 0x35;
    }
    SP_UnPause(mpObj->scriptPlayer);
    SP_SetColour(mpObj->scriptPlayer, gameObj->tweakR, gameObj->tweakG, gameObj->tweakB);
}

// A neutral uplink taken by an agent of team. (Our helper; the original repeats it for each team.)
static void MP_UplinkTaken(MPOBJECT *mpObj, obj_tag *gameObj, obj_tag *taker, MPTeam team) {
    gameObj->curState = team;
    MP_UplinkSetColour(mpObj, gameObj);
    short idx = Control_Plr2Ind(taker);
    if (idx != -1) {
        MPTeam takerTeam = MPSettings.Player[idx].TeamId;
        uint senderId = 0;
        if (taker->objectType == OBJECTTYPE_DRONE) {
            DCVars_tag dcVars;
            Drone_DCVfromOBJ(taker, &dcVars);
            senderId = dcVars.aiStateMachine->id;
        }
        MP_sendBotMessage(taker, 0x3b, 1, 0);
        MP_sendTeamBotMessage(takerTeam, 0x3d, gameObj, 0, senderId);
        MPGame.unknown_9 = 0;
        MP_sendBotMessage(taker, 0x3e, 0, 0);
    }
    mpObj->holderIdx = Control_Plr2Ind(taker);
    Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
}

// gameObj->curState is the team holding the uplink, or NO_TEAM.
void _MP_UplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj) {
    short state = gameObj->curState;
    if (state < 0)
        return;

    if (state <= MI6) {
        if (_MP_HitBy(gameObj, gameObj->curState, NULL, NULL) == NULL) {
            obj_tag *taker = _MP_HitBy(gameObj, gameObj->curState == PHOENIX, NULL, NULL);
            if (taker != NULL) {
                gameObj->subState = 0;
                gameObj->curState = gameObj->curState == PHOENIX;
                MP_UplinkSetColour(mpObj, gameObj);
                if (Control_Plr2Ind(taker) != -1) {
                    if (taker->objectType == OBJECTTYPE_DRONE) {
                        DCVars_tag dcVars;
                        Drone_DCVfromOBJ(taker, &dcVars);
                    }
                    MPGame.unknown_9 = 0;
                    MP_sendBotMessage(taker, 0x3f, 0, 0);
                }
                mpObj->holderIdx = Control_Plr2Ind(taker);
                Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            }
        }

        // x87: the rate stays unrounded for both sums
        static_assert(std::bit_cast<uint32_t>(1.0f / 60.0f) == 0x3c888889, "the original's 1/60");
        double rate = (double)REC_FRAME_RATE * (1.0f / 60.0f) * 60.0f * 0.2f;
        short team = gameObj->curState;
        MPGame.teamScore[team] = (float)(rate + MPGame.teamScore[team]);
        if (mpObj->holderIdx != -1) {
            MPGamePlayer &holder = MPGame.players[mpObj->holderIdx];
            holder.points = (float)(rate + holder.points);
            if (MPSettings.maybeIsTeamGame)
                MP_SumTeamScore((short)gameObj->curState);
        }
    }
    else if (state == NO_TEAM) {
        mpObj->holderIdx = -1;
        obj_tag *taker = _MP_HitBy(gameObj, PHOENIX, NULL, NULL);
        if (taker != NULL)
            MP_UplinkTaken(mpObj, gameObj, taker, PHOENIX);
        taker = _MP_HitBy(gameObj, MI6, NULL, NULL);
        if (taker != NULL)
            MP_UplinkTaken(mpObj, gameObj, taker, MI6);
    }
}

// AUTOLTCG
void __declspec(naked) MP_UplinkUpdate(MPOBJECT *mpObj, obj_tag *gameObj) {
    _asm {
        push eax                    // gameObj
        push dword ptr [esp + 8]    // mpObj
        call _MP_UplinkUpdate
        add esp, 8
        ret
    }
}

// AUTOINJECT
void MP_PlayerKilled(obj_tag *obj) {
    short killer = -1;
    int killKind = 0;           // 2 by an agent of the same team (or a suicide), 3 by the other team
    float points = 0.0f;
    bool hurtByHazard = false;

    if (!MPSettings.isMultiplayer)
        return;

    short victim = Control_Plr2Ind(obj);
    if (victim == -1)
        return;

    MPGamePlayer &victimRec = MPGame.players[victim];
    victimRec.deaths++;
    if (MPSettings.GameMode == GM_TOPAGENT) {
        victimRec.points -= 1.0f;
        if ((int)victimRec.deaths >= (int)MPSettings.MaxPoints)
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
    }

    short injurer = victimRec.maybeIdxOfLastInjurer;
    if (injurer != victim && injurer >= 0) {
        char *str = Txt_GetStringFromHeap(0);
        const char *format = Txt_BindLabel(NOTIF_KILLED_X, 0);
        sprintf(str, format, MPSettings.Player[victim].Name);
        Text_AddMsg((char)injurer, 0, 1, str, 0, 0xb4);
    }

    if (obj->objectType == OBJECTTYPE_PLAYER)
        Player_SetHealth((BLData *)obj->extraObjectData, 0.0f);
    else if (obj->objectType == OBJECTTYPE_DRONE)
        BOT_SetHealth((Drone_tag *)obj->extraObjectData, 0.0f);

    ushort hitCount;
    Collide_GetDamageNObjects(obj->hitList, HitByList, &hitCount, ARRAY_SIZE(HitByList));

    // The most recent damage first, until an agent is credited. A bullet leads to whoever fired it, a vehicle or a
    // gun to whoever is on it.
    for (short i = hitCount - 1; i >= 0 && killKind == 0; i--) {
        obj_tag *hitBy = HitByList[i];
        if (hitBy == NULL)
            continue;
        if (hitBy->objectType == OBJECTTYPE_BULLET) {
            hitBy = ((BU_tag *)hitBy->extraObjectData)->firedByObj;
            if (hitBy == NULL)
                continue;
        }

        uint senderId = 0xffffffff;
        while (hitBy != NULL) {
            switch (hitBy->objectType) {
            case OBJECTTYPE_CAR: {
                CAR_INFO *car = (CAR_INFO *)hitBy->extraObjectData;
                hitBy = car->playerController != NULL ? car->playerController : car->lastController;
                continue;
            }
            case OBJECTTYPE_GUNTURRET:
                hitBy = ((GUNTURRET *)hitBy->extraObjectData)->playerController;
                continue;
            case OBJECTTYPE_GUNTURRET2:
                hitBy = ((GUNIMP *)hitBy->extraObjectData)->user;
                continue;

            case OBJECTTYPE_HURT: {
                ushort hurtType = Hurt_GetType(hitBy);
                if (hurtType >= 5 && hurtType <= 7)
                    hurtByHazard = true;
                break;
            }

            case OBJECTTYPE_DRONE:
            case OBJECTTYPE_DEAD_DRONE: {
                DCVars_tag dcVars;
                Drone_DCVfromOBJ(hitBy, &dcVars);
                senderId = dcVars.aiStateMachine->id;
            }
                // fall through
            case OBJECTTYPE_PLAYER:
            case OBJECTTYPE_DEAD_PLAYER: {
                killer = Control_Plr2Ind(hitBy);
                if (killer > -1) {
                    MPGamePlayer &killerRec = MPGame.players[killer];
                    if (killer == victim) {
                        victimRec.unknown10 = 0;
                        victimRec.victories--;
                        victimRec.maybeIdxOfMyAssassin = -1;
                    } else {
                        if (!MPSettings.maybeIsTeamGame ||
                            MPSettings.Player[victim].TeamId != MPSettings.Player[killer].TeamId) {
                            killerRec.victories++;
                            killerRec.unknown10++;
                        }
                        victimRec.maybeIdxOfMyAssassin = killer;
                    }

                    points = 1.0f;
                    if (killer == victim) {
                        points = -1.0f;
                    } else if (killerRec.playerObj != NULL && killer >= NUM_PLAYERS) {
                        BOT_vars_t *bot = ((Drone_tag *)killerRec.playerObj->extraObjectData)->botVars;
                        // 7: Vengeful, against its preferred opponent
                        if (bot->stats.personality == 7 && victim == bot->preferredOpponent)
                            points = 2.0f;
                    }

                    if (!MPSettings.field53_0x190 &&
                        (MPSettings.GameMode == GM_ARENA || MPSettings.GameMode == GM_TEAMARENA ||
                         MPSettings.GameMode == GM_KOTH || MPSettings.GameMode == GM_TEAMKOTH))
                        killerRec.points += points;

                    killKind = (MPSettings.Player[victim].TeamId != MPSettings.Player[killer].TeamId) + 2;
                }

                MsgObject msg;
                msg.createdFrame = GameState.NumFramesUnpaused;
                msg.handleOnFrame = GameState.NumFramesUnpaused;
                msg.msgType = 0x43;
                msg.scope = DSTATE_BotGlobal;
                msg.sender = senderId;
                msg.receiver = 0;
                msg.extraData = obj;
                Drone_SM_RouteMsg(&msg);
                break;
            }
            }
            break;
        }
    }

    if (MPSettings.GameMode == GM_ASSASSIN) {
        obj_tag *assassin = CurrentAssassinObjId;
        if (obj == AssassinTarget && killer != -1 && MPGame.players[killer].playerObj == assassin) {
            MPGame.players[killer].points += 5.0f;
            Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_TRUCK, 100.0f, 0, 0);
        }
        if (obj == assassin) {
            if (killer != -1 && MPGame.players[killer].playerObj == AssassinTarget) {
                MPGame.players[killer].points += 3.0f;
                Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
            } else {
                Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_BEAM, 100.0f, 0, 0);
            }
            MP_assassinReset(true);
        }
    }

    if (MPSettings.GameMode == GM_TEAMARENA) {
        ushort team = MPSettings.Player[victim].TeamId;
        switch (killKind) {
        case 0:
        case 2:
            if (points < 0.0f)
                points = -points;
            MPGame.teamScore[team] -= points;
            break;
        case 3:
            if (points < 0.0f)
                points = -points;
            MPGame.teamScore[team == PHOENIX ? MI6 : PHOENIX] += points;
            break;
        }
    }

    // Whatever the victim held goes back
    for (int i = 0; i < ARRAY_SIZE(MPObjects); i++) {
        if (MPObjects[i] == NULL)
            continue;
        MPOBJECT *mpObj = (MPOBJECT *)MPObjects[i]->extraObjectData;
        if (mpObj == NULL || mpObj->holder != obj)
            continue;

        Collide_FreeHitList(&MPObjects[i]->hitList);
        if (hurtByHazard)
            mpObj->unknown04 = FRAME_RATE_INT * 30;
        switch (mpObj->type) {
        case CTF_FLAG:
            _MP_FlagUpdate(MPObjects[i], mpObj, true);
            break;
        case BLUEPRINT:
            _MP_BluePrintUpdate(mpObj, true, MPObjects[i]);
            break;
        case GOLDENEYE_KEY:
        case GOLDENEYE_CRYSTAL:
            _MP_GoldenEyeUpdate(mpObj, true, MPObjects[i]);
            break;
        }
    }
}
