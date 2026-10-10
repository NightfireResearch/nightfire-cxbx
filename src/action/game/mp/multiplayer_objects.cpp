// The scenario objects' update (type 53, MPOBJECT), the demolition and protection objective, and a blueprint
// brought to an espionage base.

#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Text.h"
#include "../../gfx/Sprite.h"
#include "../../sound/Sound.h"
#include "../obj/Explode.h"
#include "../obj/Player.h"
#include "../obj/ScriptPlayer.h"
#include "../drone/Drone.h"
#include "../drone/BOT.h"
#include "../weapon_stats.h"
#include "../../util/Random.h"
#include "../../../driving/platform/X87.h" // Ftol
#include <stdio.h>

#pragma fp_contract(off)

// As multiplayer.cpp (not in the header: windows.h has parameters named Protection)
#define Demolition (*(MP_OBJ_EXT*)0x00261af8)
#define Protection (*(MP_OBJ_EXT*)0x00261b40)

// The objects that damaged the objective, from SP_GetHitDamage
#define HitByList (*(obj_tag*(*)[64])0x00261c58)

// Five weapons per weapon set (MPSettings.weaponSet): the first is the one every agent starts with
#define PickupMatrix ((short(*)[5])0x0017e5d8)

#define MP_getObjExtObj ((obj_tag *(__cdecl *)(MP_OBJ_EXT *ext))0x0009e740)
#define MP_sendTeamBotMessage ((void (__cdecl *)(MPTeam team, uint msg, obj_tag *obj, uint param4, uint senderId))0x0009e430)

// Defined in multiplayer.cpp's AUTOGEN
short Control_Plr2Ind(obj_tag* a);
// Defined in multiplayer_modes.cpp's AUTOGEN
void MP_sendBotMessage(obj_tag *obj, uint msg, uint param3, uint param4);
// Defined in Player.cpp's AUTOGEN
undefined4 Player_EquipWeapon(BLData *param_1, short weaponNum, short param_3);

// AUTOGEN
bool __cdecl BOTWEAP_EquipWeapon(Drone_tag *param_1, short param_2, short param_3);
// AUTOGEN
ushort __cdecl BOTSTATE_pickupWeaponChangeChoice(Drone_tag *param_1, short param_2, char param_3, char param_4);

constexpr Action_TranslatedText MP_SUPPOSED_TO_PROTECT_TARGET = (Action_TranslatedText)0x0200002b;
constexpr Action_TranslatedText MP_MI6_FAILED_TO_DESTROY = (Action_TranslatedText)0x0200002c;
constexpr Action_TranslatedText MP_PHOENIX_FAILED_TO_DESTROY = (Action_TranslatedText)0x0200002d;
constexpr Action_TranslatedText MP_MI6_DESTROYED = (Action_TranslatedText)0x0200002e;
constexpr Action_TranslatedText MP_PHOENIX_DESTROYED = (Action_TranslatedText)0x0200002f;
constexpr Action_TranslatedText NOTIF_X_GOT_BLUEPRINT_TECH = (Action_TranslatedText)0x02000047;

constexpr HASHCODE EXPLOSION_OBJECTIVE = (HASHCODE)0x0600004f;    // our name
constexpr short EQUIP_FULL_AMMO = 999;                              // as Player.cpp

// The update of every scenario object; control_funcs[OBJECTTYPE_MPOBJECT] points at it. Flags and bases share
// CTF_FLAG's update and espionage bases have none.
// AUTOINJECT
void MP_ObjectUpdate(obj_tag *gameObj) {
    MPOBJECT *mpObj = (MPOBJECT *)gameObj->extraObjectData;
    switch (mpObj->type) {
    case CTF_FLAG:
        _MP_FlagUpdate(gameObj, mpObj, false);
        break;
    case UPLINK:
        _MP_UplinkUpdate(mpObj, gameObj);
        break;
    case DEMOLITION:
        _MP_DemolitionProtectionUpdate(mpObj, PHOENIX, gameObj);
        break;
    case BLUEPRINT:
        _MP_BluePrintUpdate(mpObj, false, gameObj);
        break;
    case GOLDENEYE_KEY:
    case GOLDENEYE_CRYSTAL:
        _MP_GoldenEyeUpdate(mpObj, false, gameObj);
        break;
    case PROTECTION:
        _MP_DemolitionProtectionUpdate(mpObj, MI6, gameObj);
        break;
    case KOH:
        MP_KOHUpdate(gameObj);
        break;
    }

    if (mpObj->scriptPlayer != NULL && (gameObj->transformFlags & TRANSFORM_MOVED)) {
        SP_SetPos(mpObj->scriptPlayer, &gameObj->position);
        if (SP_GetState(mpObj->scriptPlayer) == 2)
            mpObj->scriptPlayer = NULL;
    }
}

// The demolition or protection objective: its script player takes the damage and curState is its health.
// defenders (our name) is the team that must keep it whole; it scores if the time runs out, the other team if it
// is destroyed. subState 1 once either has happened.
void _MP_DemolitionProtectionUpdate(MPOBJECT *mpObj, ushort defenders, obj_tag *gameObj) {
    MP_OBJ_EXT *ext = &Demolition;
    if (mpObj->type != DEMOLITION)
        ext = &Protection;

    if (gameObj->subState != 0)
        return;

    MP_getObjExtObj(ext);
    ushort hitCount;
    float damage = SP_GetHitDamage(mpObj->scriptPlayer, HitByList, ARRAY_SIZE(HitByList), &hitCount, 1, NULL);
    gameObj->curState = Ftol((double)(short)gameObj->curState - damage);
    if (damage > 0.0f) {
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
        if (hitCount != 0 && HitByList[0] != NULL)
            mpObj->attackerIdx = Control_Plr2Ind(HitByList[0]);
    }

    // A defender who hits it is told to protect it
    short attacker = mpObj->attackerIdx;
    for (ushort i = 0; i < hitCount; i++) {
        short idx = Control_Plr2Ind(HitByList[i]);
        if (idx < 0 || MPSettings.Player[idx].TeamId != defenders)
            continue;
        MPGamePlayer &player = MPGame.players[idx];
        if (player.friendlyFireProtectionLabelTimer == 0) {
            short frames = Ftol(FRAME_RATE_INT * 1.5);
            Text_AddMsg((char)idx, 0, 1, (char *)Txt_BindLabel(MP_SUPPOSED_TO_PROTECT_TARGET, 0), 0, frames);
        }
        player.friendlyFireProtectionLabelTimer = Ftol(FRAME_RATE_INT * 1.5);
    }

    if (MPGame.TimeUnpaused > MPGame.TimeLimit) {
        MPGame.teamScore[defenders] += 1.0f;
        gameObj->subState = 1;
        SP_SwitchToScript(mpObj->scriptPlayer, 1, NULL);
        Action_TranslatedText label = defenders == MI6 ? MP_PHOENIX_FAILED_TO_DESTROY : MP_MI6_FAILED_TO_DESTROY;
        sprintf(StatusSpr->text, Txt_BindLabel(label, 0));
        Sprite_SetText(StatusSpr, StatusSpr->text);
        return;
    }

    if ((short)gameObj->curState > 0)
        return;

    Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);
    MPTeam attackers = defenders == PHOENIX ? MI6 : PHOENIX;
    MPGame.teamScore[attackers] += 1.0f;
    if (attacker != -1) {
        // destroyed by a defender: a point off the defenders and off the agent
        float points = 1.0f;
        if (MPSettings.Player[attacker].TeamId == defenders) {
            points = -1.0f;
            MPGame.teamScore[defenders] -= 1.0f;
        }
        MPGame.players[attacker].points += points;
    }

    gameObj->subState = 1;
    gameObj->creationTimeFrames = GameState.NumFramesUnpaused;
    SP_UnPause(mpObj->scriptPlayer);
    Explode_Create(gameObj, &gameObj->centrePoint, &CONST_UP_VECTOR, 5.0f, 5.0f, EXPLOSION_OBJECTIVE, 500.0f,
                   0xff, 0xff, 100, NULL, 0);
    if (attacker >= 0) {
        Action_TranslatedText label = MPSettings.Player[attacker].TeamId == PHOENIX ? MP_PHOENIX_DESTROYED : MP_MI6_DESTROYED;
        sprintf(StatusSpr->text, Txt_BindLabel(label, 0));
    }
    Sprite_SetText(StatusSpr, StatusSpr->text);
    gameObj->subState = 1;
    MPGame.EndGameFlowState = 6;

    if (ext != NULL) {
        obj_tag *obj = ext->gameObj;
        ext->gameObj = NULL;
        MP_sendTeamBotMessage(attackers, 0x41, obj, 0, 0);
    }
}

// AUTOLTCG
void __declspec(naked) MP_DemolitionProtectionUpdate(MPOBJECT *mpObj, ushort defenders) {
    _asm {
        push ebx                    // gameObj
        movzx eax, ax               // defenders
        push eax
        push dword ptr [esp + 12]   // mpObj
        call _MP_DemolitionProtectionUpdate
        add esp, 12
        ret
    }
}

// The weapon a blueprint brings its holder: the first of the set's weapons[1..4] it does not hold, after the slot
// of the one it gives up. With the professional option it gives up the first weapon it holds from id 2 up, other
// than the starting one. (Our helpers; the original has both inline.)
static void BluePrintRewardPlayer(obj_tag *holder, const short *weapons, short startWeapon) {
    BLData *bl = (BLData *)holder->extraObjectData;
    short dropped = 0;
    short slot = 0;
    if (MPSettings.TripleDamageModifierProfessionalMode) {
        for (short w = 2; w < 0x48; w++) {
            if (w == startWeapon || !bl->weaponStats[w].enabled)
                continue;
            dropped = w;
            for (short s = 1; s < 5; s++) {
                if (w == weapons[s]) {
                    slot = s;
                    break;
                }
            }
            break;
        }
    }

    for (short s = slot + 1; s < 5; s++) {
        if (bl->weaponStats[(ushort)weapon_data[weapons[s]].weaponBaseNum].enabled)
            continue;
        short weapon = weapons[s];
        // the original tests AL only
        if (weapon != 0 && (char)Player_EquipWeapon(bl, weapon, EQUIP_FULL_AMMO) && dropped != 0) {
            bl->weaponStats[dropped].enabled = false;
            bl->weaponStats[dropped].clipOrCooldown = 0;
            if (holder->animState->currentWeaponId == dropped)
                holder->animState->switchingToWeaponId = (char)weapon;
        }
        break;
    }
}

static void BluePrintRewardBot(Drone_tag *drone, const short *weapons, short startWeapon) {
    short dropped = 0;
    short slot = 0;
    if (MPSettings.TripleDamageModifierProfessionalMode) {
        for (short w = 2; w < 0x53; w++) {
            if (w == startWeapon || !drone->botVars->weapons[w].held)
                continue;
            dropped = w;
            for (short s = 1; s < 5; s++) {
                if (w == weapons[s]) {
                    slot = s;
                    break;
                }
            }
            break;
        }
    }

    for (short s = slot + 1; s < 5; s++) {
        if (drone->botVars->weapons[(ushort)weapon_data[weapons[s]].weaponBaseNum].held)
            continue;
        short weapon = weapons[s];
        if (weapon != 0) {
            if (BOTWEAP_EquipWeapon(drone, weapon, EQUIP_FULL_AMMO) && dropped != 0) {
                drone->botVars->weapons[dropped].held = 0;
                drone->botVars->weapons[dropped].rounds = 0;
            }
            BOTSTATE_pickupWeaponChangeChoice(drone, weapon, 0, 0);
        }
        break;
    }
}

// The blueprint brought to an espionage base: its team scores, the holder gets a point and a weapon, and the
// blueprint goes back to a random place.
// AUTOINJECT
void MP_BluePrintReachedBase(obj_tag *gameObj, MPOBJECT *mpObj) {
    Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_TRUCK, 100.0f, 0, 0);

    obj_tag *carrier = gameObj->maybeParent;
    if (carrier != NULL) {
        short idx = Control_Plr2Ind(carrier);
        if (idx != -1) {
            MPGame.players[idx].flags &= ~MPPLAYER_FLAG_2;
            MPTeam team = MPSettings.Player[idx].TeamId;
            if (carrier->objectType == OBJECTTYPE_DRONE) {
                DCVars_tag dcVars;
                Drone_DCVfromOBJ(carrier, &dcVars);
            }
            MPGame.teamObjectiveFlags[team] &= ~3;
            MP_sendBotMessage(carrier, 0x3b, 1, 0);
            MPGame.unknown_9 = 0;
            MP_sendBotMessage(carrier, 0x34, 0, 0);
        }
    }

    MPGame.teamScore[mpObj->num] += 1.0f;
    short holderIdx = Control_Plr2Ind(mpObj->holder);
    if (holderIdx != -1)
        MPGame.players[holderIdx].points += 1.0f;

    Mat_Copy(&BluePrints[(ushort)Rand_Rand(BluePrintCount)].mtx, &mpObj->resetMtx);
    MP_ResetMPObject(mpObj, 0, gameObj, false);

    const short *weapons = PickupMatrix[MPSettings.weaponSet];
    obj_tag *holder = mpObj->holder;
    if (holder != NULL) {
        short startWeapon = weapon_data[weapons[0]].weaponBaseNum;
        if (holder->objectType == OBJECTTYPE_PLAYER) {
            if (holder->curState == 1)
                BluePrintRewardPlayer(holder, weapons, startWeapon);
        }
        else if (holder->objectType == OBJECTTYPE_DRONE) {
            if (!MPDrone_MaybeIsDyingOrDead(holder))
                BluePrintRewardBot((Drone_tag *)holder->extraObjectData, weapons, startWeapon);
        }
    }

    char *str = Txt_GetStringFromHeap(0);
    const char *teamName = Txt_BindLabel(mpObj->num == PHOENIX ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0);
    const char *format = Txt_BindLabel(NOTIF_X_GOT_BLUEPRINT_TECH, 0);
    sprintf(str, format, teamName);
    Text_AddMsg(-1, 0, 4, str, 0, Ftol(FRAME_RATE_INT * 1.5));
}
