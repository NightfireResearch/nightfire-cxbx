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
#include "../drone/NDrone2.h"  // DRONE_MSG
#include "../drone/BOT.h"
#include "../drone/BOTSTATE.h"
#include "../drone/BOTWEAP.h"
#include "../weapon_stats.h"
#include "../../util/Random.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "../obj/control.h"
#include "../obj/PlayerWeapon.h"
#include "../obj/Pickup.h"   // PickupMatrix: five weapons per set, the first the one every agent starts with
#include "../../math/X87Ops.h"

#pragma fp_contract(off)

// As multiplayer.cpp (not in the header: windows.h has parameters named Protection)
extern MP_OBJ_EXT Demolition;
extern MP_OBJ_EXT Protection;

// The objects that damaged the objective, from SP_GetHitDamage
extern obj_tag *HitByList[64];

constexpr short EQUIP_FULL_AMMO = 999;                              // as Player.cpp

// The update of every scenario object; control_funcs[OBJECTTYPE_MPOBJECT] points at it. Flags and bases share
// CTF_FLAG's update and espionage bases have none.
// AUTOINJECT
void MP_ObjectUpdate(obj_tag *gameObj) {
    MPOBJECT *mpObj = (MPOBJECT *)gameObj->extraObjectData;
    switch (mpObj->type) {
    case CTF_FLAG:
        MP_FlagUpdate(gameObj, mpObj, false);
        break;
    case UPLINK:
        MP_UplinkUpdate(mpObj, gameObj);
        break;
    case DEMOLITION:
        MP_DemolitionProtectionUpdate(mpObj, PHOENIX, gameObj);
        break;
    case BLUEPRINT:
        MP_BluePrintUpdate(mpObj, false, gameObj);
        break;
    case GOLDENEYE_KEY:
    case GOLDENEYE_CRYSTAL:
        MP_GoldenEyeUpdate(mpObj, false, gameObj);
        break;
    case PROTECTION:
        MP_DemolitionProtectionUpdate(mpObj, MI6, gameObj);
        break;
    case KOH:
        MP_KOHUpdate(gameObj);
        break;
    }

    if (mpObj->scriptPlayer != NULL && (gameObj->transformFlags & TRANSFORM_MOVED)) {
        SP_SetPos(mpObj->scriptPlayer, &gameObj->position);
        if (SP_GetState(mpObj->scriptPlayer) == SP_STATUS_DONE)
            mpObj->scriptPlayer = NULL;
    }
}

// The demolition or protection objective: its script player takes the damage and curState is its health.
// defenders (our name) is the team that must keep it whole; it scores if the time runs out, the other team if it
// is destroyed. subState 1 once either has happened.
void MP_DemolitionProtectionUpdate(MPOBJECT *mpObj, ushort defenders, obj_tag *gameObj) {
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
            Text_AddMsg((char)idx, 0, TXTMSG_INFO, (char *)Txt_BindLabel(NOTIF_SUPPOSED_TO_PROTECT_TARGET, 0), 0,
                        frames);
        }
        player.friendlyFireProtectionLabelTimer = Ftol(FRAME_RATE_INT * 1.5);
    }

    if (MPGame.TimeUnpaused > MPGame.TimeLimit) {
        MPGame.teamScore[defenders] += 1.0f;
        gameObj->subState = 1;
        SP_SwitchToScript(mpObj->scriptPlayer, SP_PLAY_NOW, NULL);
        Action_TranslatedText label = defenders == MI6 ? NOTIF_PHOENIX_FAILED_TO_DESTROY : NOTIF_MI6_FAILED_TO_DESTROY;
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
    Explode_Create(gameObj, &gameObj->centrePoint, &CONST_UP_VECTOR, 5.0f, 5.0f, HT_Script_BlastFrames, 500.0f,
                   0xff, 0xff, 100, NULL, MATERIAL_NONE);
    if (attacker >= 0) {
        Action_TranslatedText label = MPSettings.Player[attacker].TeamId == PHOENIX ? NOTIF_PHOENIX_DESTROYED
                                                                                    : NOTIF_MI6_DESTROYED;
        sprintf(StatusSpr->text, Txt_BindLabel(label, 0));
    }
    Sprite_SetText(StatusSpr, StatusSpr->text);
    gameObj->subState = 1;
    MPGame.EndGameFlowState = MPEND_RESTART_ROUND;

    if (ext != NULL) {
        obj_tag *obj = ext->gameObj;
        ext->gameObj = NULL;
        MP_sendTeamBotMessage(attackers, DRONE_MSG_BotObjectiveDestroyed, obj, 0, 0);
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
        if (weapon != 0 && Player_EquipWeapon(bl, weapon, EQUIP_FULL_AMMO) && dropped != 0) {
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

    obj_tag *carrier = gameObj->parent;
    if (carrier != NULL) {
        short idx = Control_Plr2Ind(carrier);
        if (idx != -1) {
            MPGame.players[idx].flags &= ~MPPLAYER_HAS_BLUEPRINT;
            MPTeam team = MPSettings.Player[idx].TeamId;
            if (carrier->objectType == OBJECTTYPE_DRONE) {
                DCVars_tag dcVars;
                Drone_DCVfromOBJ(carrier, &dcVars);
            }
            MPGame.teamObjectiveFlags[team] &= ~3;
            MP_sendBotMessage(carrier, DRONE_MSG_BotGoalComplete, 1, 0);
            MPGame.unknown_9 = 0;
            MP_sendBotMessage(carrier, DRONE_MSG_BotBlueprintDelivered, 0, 0);
        }
    }

    MPGame.teamScore[mpObj->num] += 1.0f;
    short holderIdx = Control_Plr2Ind(mpObj->holder);
    if (holderIdx != -1)
        MPGame.players[holderIdx].points += 1.0f;

    Mat_Copy(&BluePrints[(ushort)Rand_Rand(BluePrintCount)].mtx, &mpObj->resetMtx);
    MP_ResetMPObject(mpObj, MPOBJ_HOME, gameObj, false);

    const short *weapons = PickupMatrix[MPSettings.weaponSet];
    obj_tag *holder = mpObj->holder;
    if (holder != NULL) {
        short startWeapon = weapon_data[weapons[0]].weaponBaseNum;
        if (holder->objectType == OBJECTTYPE_PLAYER) {
            if (holder->curState == 1)
                BluePrintRewardPlayer(holder, weapons, startWeapon);
        }
        else if (holder->objectType == OBJECTTYPE_DRONE) {
            if (!MPDrone_IsDyingOrDead(holder))
                BluePrintRewardBot((Drone_tag *)holder->extraObjectData, weapons, startWeapon);
        }
    }

    char *str = Txt_GetStringFromHeap(0);
    const char *teamName = Txt_BindLabel(mpObj->num == PHOENIX ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0);
    const char *format = Txt_BindLabel(NOTIF_X_GOT_BLUEPRINT_TECH, 0);
    sprintf(str, format, teamName);
    Text_AddMsg(-1, 0, TXTMSG_SUBTITLE, str, 0, Ftol(FRAME_RATE_INT * 1.5));
}
