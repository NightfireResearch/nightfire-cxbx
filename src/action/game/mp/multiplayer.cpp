#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../obj/car.h"
#include "../obj/GT.h"
#include "../obj/GunImp.h"
#include "../obj/Pickup.h"
#include "../drone/BOT.h"
#include "../obj/bullet.h"
#include "../obj/control.h"
#include "../view.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "../../engine/AINetwork.h"
#include "../../engine/Script.h"        // Script_GetObj
#include "../../engine/Text.h"          // Txt_GetStringFromHeap
#include "../../math/X87Ops.h"
#include "../obj/build.h"               // build_FindCel
#include "../obj/ScriptPlayer.h"        // SP_getScriptInfo
#include "../drone/BOTWEAP.h"           // BOTWEAP_isValidWeapon
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"           // Drone_SM_SendMsgSelf
#include "../../engine/Loader.h"        // IsLevelLoaded
#include "../../sound/Sound.h"
#include <bit>
#include "../../assets.h"                // HT_Level_Menu_Pre
#include "../../engine/Anim.h"          // AnimObjectUpdate
#include "../../engine/Camera.h"        // Camera_Set
#include "../../ui/HUD.h"               // HUD_Enable
#include "../obj/Player.h"
#include "../obj/PlayerWeapon.h"        // Player_EquipWeapon
#include "../Upgrade.h"                 // Upgrade_MPWeapon
#include "../weapon_stats.h"
#include "../../gfx/Font.h"

// XBE_GLOBAL(0x0025fe38, 0x23c)
MPSettings_t MPSettings;

// XBE_GLOBAL(0x00261678, 0x110)
GoldenEyeStruct GoldenEye;

// XBE_GLOBAL(0x00245240, 0x40)
MPJoinSlot mp_join_slots[NUM_PLAYERS];

// XBE_GLOBAL(0x001637c0, 0x1d0)
MP_skin MP_skins[29] = {
    {.skinHashcode = (HASHCODE)0x5000089, .fileHashcode = (HASHCODE)0x10001b4, .handType = 4},
    {.skinHashcode = (HASHCODE)0x50000a5, .fileHashcode = (HASHCODE)0x10001ec, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500008e, .fileHashcode = (HASHCODE)0x10001bc, .handType = 5},
    {.skinHashcode = (HASHCODE)0x5000092, .fileHashcode = (HASHCODE)0x10001c5, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000095, .fileHashcode = (HASHCODE)0x10001c9, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000096, .fileHashcode = (HASHCODE)0x10001d5, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000097, .fileHashcode = (HASHCODE)0x10001dd, .handType = 4},
    {.skinHashcode = (HASHCODE)0x5000098, .fileHashcode = (HASHCODE)0x10001de, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500009a, .fileHashcode = (HASHCODE)0x10001e1, .handType = 4},
    {.skinHashcode = (HASHCODE)0x50000c6, .fileHashcode = (HASHCODE)0x100021b, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500009e, .fileHashcode = (HASHCODE)0x10001e5, .handType = 5},
    {.skinHashcode = (HASHCODE)0x50000a0, .fileHashcode = (HASHCODE)0x10001e7, .handType = 4},
    {.skinHashcode = (HASHCODE)0x50000a4, .fileHashcode = (HASHCODE)0x10001eb, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500008b, .fileHashcode = (HASHCODE)0x10001b7, .handType = 4},
    {.skinHashcode = (HASHCODE)0x50000bb, .fileHashcode = (HASHCODE)0x100020b, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500001c, .fileHashcode = (HASHCODE)0x100012f, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500001d, .fileHashcode = (HASHCODE)0x100012a, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500001f, .fileHashcode = (HASHCODE)0x100012d, .handType = 4},
    {.skinHashcode = (HASHCODE)0x5000056, .fileHashcode = (HASHCODE)0x100015a, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000060, .fileHashcode = (HASHCODE)0x100016a, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000063, .fileHashcode = (HASHCODE)0x1000170, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000086, .fileHashcode = (HASHCODE)0x10001af, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000081, .fileHashcode = (HASHCODE)0x10001aa, .handType = 7},
    {.skinHashcode = (HASHCODE)0x5000082, .fileHashcode = (HASHCODE)0x10001ab, .handType = 6},
    {.skinHashcode = (HASHCODE)0x5000084, .fileHashcode = (HASHCODE)0x10001ad, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500006b, .fileHashcode = (HASHCODE)0x100017e, .handType = 5},
    {.skinHashcode = (HASHCODE)0x5000085, .fileHashcode = (HASHCODE)0x10001ae, .handType = 4},
    {.skinHashcode = (HASHCODE)0x500007b, .fileHashcode = (HASHCODE)0x10001a1, .handType = 4},
    {.skinHashcode = (HASHCODE)0x50000b9, .fileHashcode = (HASHCODE)0x1000208, .handType = 4},
};

// XBE_GLOBAL(0x001f660c, 0x4)
uint32_t MultiplayerLayout_LeftRightOrTopBtm;
// XBE_GLOBAL(0x00245280, 0x6e)
MPBOTS mpbots;
// XBE_GLOBAL(0x00260078, 0x1600)
MP_PICKUP MPpickups[64];
// XBE_GLOBAL(0x00261a70, 0x88)
MP_OBJ_EXT EsponageBase[2];
// XBE_GLOBAL(0x00261bd0, 0x88)
MP_OBJ_EXT Bases[2];
// XBE_GLOBAL(0x00261d58, 0x700)
MPSpawnPoint SpawnPoints[64];
// XBE_GLOBAL(0x00262458, 0x2e0)
SpawnPlace BluePrints[8];
// XBE_GLOBAL(0x00262970, 0x2)
uint16_t GoldenEyeKeyCount;
// XBE_GLOBAL(0x00262972, 0x2)
uint16_t GoldenEyeNonKeyCount;
// XBE_GLOBAL(0x00262978, 0x5c0)
SpawnPlace GoldenEyeSpawns[16];
// XBE_GLOBAL(0x00263220, 0x1b8)
RADAROBJ MPRadarObjects[22];
// XBE_GLOBAL(0x00263640, 0x100)
obj_tag *MPObjects[64];
// XBE_GLOBAL(0x002637d4, 0x2)
uint16_t BluePrintCount;
// XBE_GLOBAL(0x002637d8, 0x4)
sprite *TimeSpr;
// XBE_GLOBAL(0x002637dc, 0x4)
sprite *StatusSpr;

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

// XBE_GLOBAL(0x0026178c, 0x4)
obj_tag *CurrentAssassinObjId;
// XBE_GLOBAL(0x00261788, 0x4)
obj_tag *AssassinTarget;

#define NUM_SKINS 29 // unique characters

#define WEAPON_FISTS 1


// Bot messages (docs/drone/bots-and-navigation): 0x3b goal complete, 0x3d cancel goals on an object, 0x40 the
// demolition or protection object has gone (MP_getObjExtObj)
#define BOT_MSG_GOAL_COMPLETE 0x3b
#define BOT_MSG_3C 0x3c
#define BOT_MSG_CANCEL_GOALS 0x3d
#define BOT_MSG_OBJECTIVE_GONE 0x40

constexpr float HalfPi = 1.5707964f;
static_assert(std::bit_cast<uint32_t>(HalfPi) == 0x3fc90fdb, "not the original's pi / 2");


// AUTOINJECT
void MP_setLoadingSkins(void) {

  // Set default value to unused
  for(int  i = 0; i < NUM_SKINS; i++) {
    MP_skins[i].isInThisMpGame = false;
  }

  // Discover player skins
  for(int i = 0; i < MPSettings.numPlayers; i++) {
    int skinNum = MPSettings.Player[i].SkinNum;
    MP_skins[skinNum].isInThisMpGame = true;
    printf("Player %i uses skin %i\n", i, skinNum);
  }

  // Discover bot skins
  for(int i = 0; i < mpbots.NumBots; i++) {
    int skinNum = mpbots.bot[i].SkinNum;
    MP_skins[skinNum].isInThisMpGame = true;
    printf("Bot %i uses skin %i\n", i, skinNum);
  }

}

// Used on PS2 as part of AnimLoadFile, but seems not to be used on Xbox.
// This would also be the only usage of "isInThisMpGame".
// Included here anyway for reference.
bool MP_NeedSkin(HASHCODE hc) {
  if(!GameState.isMultiplayerLevel)
    return true;

  for(int i = 0; i < NUM_SKINS; i++) {
    if(MP_skins[i].skinHashcode == hc) {
      return MP_skins[i].isInThisMpGame;
    }
  }

  return true;
}

// AUTOINJECT
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b) {
   
    if ((!MPSettings.maybeDroneAIEnabled) && (MPSettings.GameMode != GM_ASSASSIN))
        return false;
    
    short idx_a = Control_Plr2Ind(a);
    short idx_b = Control_Plr2Ind(b);

    if ((idx_a > -1) && (idx_b > -1)) {
        
        MPTeam team_a = MPSettings.Player[idx_a].TeamId;
        MPTeam team_b = MPSettings.Player[idx_b].TeamId;
        
        if ((team_a != NO_TEAM) && (team_b != NO_TEAM))
            return team_a == team_b;
        
    }

    return false;
}

// AUTOINJECT
bool MP_isObjectOnTeam(obj_tag *param_1,uint teamId) {
  
  if ((MPSettings.maybeDroneAIEnabled) || (MPSettings.GameMode == GM_ASSASSIN)) {
    short idx = Control_Plr2Ind(param_1);
    MPTeam uVar1 = MPSettings.Player[idx].TeamId;
    if (uVar1 != NO_TEAM) {
      return uVar1 == teamId;
    }
  }
  
  return false;
}

// AUTOINJECT
MPTeam MP_getObjectTeam(obj_tag* param_1) {
  
  if ((!MPSettings.maybeDroneAIEnabled) && (MPSettings.GameMode != GM_ASSASSIN)) {
    return NO_TEAM;
  }

  short idx = Control_Plr2Ind(param_1);
  return MPSettings.Player[idx].TeamId;
}

// AUTOINJECT
bool MP_IsAssasin(obj_tag *param_1) {
  return ((CurrentAssassinObjId != NULL) && (CurrentAssassinObjId == param_1));
}

// AUTOINJECT
bool MP_IsTarget(obj_tag *param_1) {
  return ((AssassinTarget != NULL) && (AssassinTarget == param_1));
}

// The friendly-fire messages: to the one who fired, and to the one hit, each with the other's name
#define TXT_MP_FRIENDLY_FIRE_BY ((Action_TranslatedText)0x02000029)
#define TXT_MP_FRIENDLY_FIRE_ON ((Action_TranslatedText)0x0200002a)

// Who hit player playerIndex (a drone, player or gun turret, or the one who fired a bullet) becomes his last injurer.
// A teammate's hit does not count (false) without friendly fire, and with it both are told, at most every three
// seconds. Always true outside multiplayer; false when there is no one to blame. The health is not read.
// AUTOINJECT
bool MP_RegisterBulletHit(ushort playerIndex, obj_tag *obj, float health) {
  if (MPSettings.isMultiplayer == 0)
    return true;
  if (obj == NULL)
    return false;

  obj_tag *shooter;
  switch (obj->objectType) {
    case OBJECTTYPE_DRONE:
    case OBJECTTYPE_PLAYER:
    case OBJECTTYPE_GUNTURRET2:
      shooter = obj;
      break;
    case OBJECTTYPE_BULLET:
      shooter = ((BU_tag *)obj->extraObjectData)->firedByObj;
      break;
    default:
      return false;
  }
  short shooterIndex = Control_Plr2Ind(shooter);
  if (shooterIndex < 0)
    return false;

  MPGame.players[playerIndex].maybeIdxOfLastInjurer = shooterIndex;
  if (MPSettings.maybeIsTeamGame != 0 &&
      MPSettings.Player[playerIndex].TeamId == MPSettings.Player[shooterIndex].TeamId &&
      shooterIndex != playerIndex) {
    if (MPSettings.FriendlyFire == 0)
      return false;
    if (MPGame.players[playerIndex].friendlyFireLabelTimer == 0) {
      char *text = Txt_GetStringFromHeap(0);
      sprintf(text, Txt_BindLabel(TXT_MP_FRIENDLY_FIRE_BY, 0), MPSettings.Player[shooterIndex].Name);
      Text_AddMsg((char)playerIndex, 0, 1, text, 0, 0xb4);
      sprintf(text, Txt_BindLabel(TXT_MP_FRIENDLY_FIRE_ON, 0), MPSettings.Player[playerIndex].Name);
      Text_AddMsg((char)shooterIndex, 0, 1, text, 0, 0xb4);
      MPGame.players[playerIndex].friendlyFireLabelTimer = (short)(FRAME_RATE_INT * 3);
    }
  }
  return true;
}

// XBE_GLOBAL(0x00262f38, 0x4)
static uint32_t SpawnPntCount;
// By MPTeam; NO_TEAM spawn points are rejected. (0x00262970 after it is GoldenEyeKeyCount.)
// XBE_GLOBAL(0x00262968, 0x8)
static uint32_t SpawnPntTeamCount[2];

// AUTOINJECT
void MP_RegisterSpawnPoint(_VECTOR *position, _VECTOR *facingDirection, ushort teamId) {

  if(teamId == MPTeam::NO_TEAM) {
    NF_WARN("Spawn points MUST have team assoc. with them!\n"); // GC check (0x800d32a0)
    return;
  }

  int startIdx = 0;
  int endIdx = 64;

  if ((MPSettings.maybeIsTeamGame) || (MPSettings.GameMode == GM_ASSASSIN)) {
    // Spawn points are arranged in a list but for convenience (?) we don't interleave by team.
    // Values 0-31 are for team PHOENIX, values 32-63 are for MI6 
    switch(teamId) {
      case MPTeam::PHOENIX:
        startIdx = 0;
        endIdx = 32;
        break;
      case MPTeam::MI6:
        startIdx = 32;
        endIdx = 64;
        break;
    }
  }

  for(int i = startIdx; i < endIdx; i++) {

    // Find the first uninitialised spawn point
    if(SpawnPoints[i].initialised)
      continue;
    
    // Locate the point on the floor
    _VECTOR spawnPos;
    Vec_Copy(position, &spawnPos);
    cel_tag* cel = build_FindCel(position, glb_world);
    build_PointOnFloor(cel, NULL, &spawnPos, 3.0f, NULL);
    spawnPos.y += 1.6f;

    // Set up the spawn point
    Vec_Copy(&spawnPos, &SpawnPoints[i].spawnPos);
    Vec_Copy(facingDirection, &SpawnPoints[i].facingDir);

    SpawnPoints[i].initialised = true;

    // Maintain counts
    SpawnPntTeamCount[teamId]++;
    SpawnPntCount++;
    
    // We've found a spawn point and initialised it, so stop searching
    break;

  }

}

// AUTOINJECT
uint MP_GetSpawnPoint(short teamId, obj_tag *respawningPlayer) {
    int startIdx = 0;
    int endIdx = ARRAY_SIZE(SpawnPoints);
    if (MPSettings.maybeIsTeamGame || MPSettings.GameMode == GM_ASSASSIN) {
        if (teamId == PHOENIX) {
            endIdx = 32;
        } else if (teamId == MI6) {
            startIdx = 32;
        }
    }

    int closestSpawn = -1;
    int farthestSpawn = -1;
    float closestDistance = 9999.0f;
    float farthestDistance = -9999.0f;
    uint eligibleSpawns[ARRAY_SIZE(SpawnPoints)];
    int eligibleCount = 0;

    for (int i = startIdx; i < endIdx; i++) {
        if (!SpawnPoints[i].initialised)
            continue;

        float nearestPlayerDistance = 9999.0f;
        for (int playerIdx = 0; playerIdx < ARRAY_SIZE(MPGame.players); playerIdx++) {
            obj_tag *player = MPGame.players[playerIdx].playerObj;
            if (player == NULL || player == respawningPlayer)
                continue;

            double distance = Vec_SqDist3D(&SpawnPoints[i].spawnPos, &player->position);
            if (distance < nearestPlayerDistance)
                nearestPlayerDistance = (float)distance;
        }

        // Prevent spawning on top of another player
        // This condition could in extreme cases lead to all spawn points being ineligible, causing undefined behaviour below.
        if (nearestPlayerDistance <= 2.0f)
            continue;

        if (nearestPlayerDistance < closestDistance) {
            closestDistance = nearestPlayerDistance;
            closestSpawn = i;
        }
        if (nearestPlayerDistance > farthestDistance) {
            farthestDistance = nearestPlayerDistance;
            farthestSpawn = i;
        }
        eligibleSpawns[eligibleCount++] = i;
    }

    switch (MPSettings.RespawnSelectionMode) {
        case RESPAWN_NEAR:
            return closestSpawn >= 0 ? closestSpawn : startIdx;
        case RESPAWN_FAR:
            return farthestSpawn >= 0 ? farthestSpawn : startIdx;
        case RESPAWN_RANDOM:
            // If there are no eligible spawns, this returns the first (uninitialized) array entry (undefined behavior).
            return eligibleSpawns[Rand_Rand(eligibleCount)];
        default:
            return startIdx;
    }
}


void MP_CleanupMPObjExt(MP_OBJ_EXT *mp_obj) {
  if(mp_obj == NULL)
    return;
    
  if(mpbots.NumBots && (mp_obj->aiEmitter).data != NULL) {
    AINetwork_FreeEmitter(&mp_obj->aiEmitter);
  }

  memset(mp_obj, 0, sizeof(MP_OBJ_EXT));

}

// XBE_GLOBAL(0x002637c8, 0x2)
uint16_t UplinkCount;
// XBE_GLOBAL(0x002633d8, 0x220)
MP_OBJ_EXT Uplinks[8];
// XBE_GLOBAL(0x00263740, 0x88)
MP_OBJ_EXT Flags[2];
// XBE_GLOBAL(0x00261b88, 0x44)
MP_OBJ_EXT Hill;
// XBE_GLOBAL(0x00261af8, 0x44)
MP_OBJ_EXT Demolition;
// XBE_GLOBAL(0x00261b40, 0x44)
MP_OBJ_EXT Protection;
// XBE_GLOBAL(0x002635f8, 0x44)
MP_OBJ_EXT BluePrint;


// AUTOINJECT
void MP_objectBeingDeleted(obj_tag* obj) {

  if(!MPSettings.maybeDroneAIEnabled)
    return;

  if(obj == NULL)
    return;
  
  bool dispatchBotMessage = false;

  for(int i = 0; i < ARRAY_SIZE(MPGame.players); i++) {
    if(MPGame.players[i].playerObj == obj) {
      MPGame.players[i].playerObj = NULL;
    }
  }

  // Search through the special gamemode specific lists
  switch(MPSettings.GameMode) {
    case GM_UPLINK:
      for(int i = 0; i < UplinkCount; i++) {
        if(Uplinks[i].gameObj == obj) {
          MP_CleanupMPObjExt(&Uplinks[i]);
          dispatchBotMessage = true;
        }
      }
      break;

    case GM_KOTH:
    case GM_TEAMKOTH:
      if(Hill.gameObj == obj) {
        MP_CleanupMPObjExt(&Hill);
        dispatchBotMessage = true;
      }
      break;

    case GM_GOLDENEYE:
      if(GoldenEye.keys[0].gameObj == obj) {
        MP_CleanupMPObjExt(&GoldenEye.keys[0]);
        dispatchBotMessage = true;
      }
      if(GoldenEye.keys[1].gameObj == obj) {
        MP_CleanupMPObjExt(&GoldenEye.keys[1]);
        dispatchBotMessage = true;
      }
      break;

    case GM_CTF:
      for(int i = 0; i < ARRAY_SIZE(Flags); i++) {
        if(Flags[i].gameObj == obj) {
          MP_CleanupMPObjExt(&Flags[i]);
          dispatchBotMessage = true;
        }
      }
      for(int i = 0; i < ARRAY_SIZE(Bases); i++) {
        if(Bases[i].gameObj == obj) {
          MP_CleanupMPObjExt(&Bases[i]);
          dispatchBotMessage = true;
        }
      }
      break;
    
    case GM_DEMOLITION:
      if(Demolition.gameObj != NULL && Demolition.gameObj == obj) {
        MP_CleanupMPObjExt(&Demolition);
        dispatchBotMessage = true;
      }
      break;

    case GM_PROTECTION:
      if(Protection.gameObj != NULL && Protection.gameObj == obj) {
        MP_CleanupMPObjExt(&Protection);
        dispatchBotMessage = true;
      }
      break;

    case GM_BLUEPRINT:
      if(BluePrint.gameObj == obj) {
        MP_CleanupMPObjExt(&BluePrint);
        dispatchBotMessage = true;
      }
      for(int i = 0; i < ARRAY_SIZE(EsponageBase); i++) {
        if(EsponageBase[i].gameObj == obj) {
          MP_CleanupMPObjExt(&EsponageBase[i]);
          dispatchBotMessage = true;
        }
      }
      break;
        
  }

  // Search through general MPObjects list too
  for(int i = 0; i < ARRAY_SIZE(MPObjects); i++) {
    if(MPObjects[i] == obj) {
      MPObjects[i] = NULL;
      dispatchBotMessage = false;
    }
  }

  // If required, dispatch a bot message to inform them of the deletion
  if(dispatchBotMessage) {
    MsgObject msg;
    msg.createdFrame = GameState.NumFramesUnpaused;
    msg.handleOnFrame = GameState.NumFramesUnpaused;
    msg.msgType = 0x3d;
    msg.scope = 0xc5;          // DSTATE_BotGlobal
    msg.sender = 0;
    msg.receiver = 0;          // broadcast
    msg.extraData = obj;
    Drone_SM_RouteMsg(&msg);
  }
}

// AUTOINJECT
MP_OBJ_EXT* MP_getFlagObj(uint i) {
  if(i >= ARRAY_SIZE(Flags))
    return NULL;
  return &Flags[i];
}

// AUTOINJECT
MP_OBJ_EXT* MP_getBaseObj(uint i) {
  if(i >= ARRAY_SIZE(Bases))
    return NULL;
  return &Bases[i];
}

// AUTOINJECT
MP_OBJ_EXT* MP_getDemolitionObj(void) {
  return &Demolition;
}

// AUTOINJECT
MP_OBJ_EXT* MP_getProtectionObj(void) {
  return &Protection;
}

// AUTOINJECT
MP_OBJ_EXT* MP_getHillObj(void) {
  return &Hill;
}

// The scenario object's MP_OBJ_EXT, by its type: an uplink's is looked up by its object
MP_OBJ_EXT* _MP_getObjExtFromMPOBJECT(MPOBJECT *mpObj) {

  switch(mpObj->type) {
    case CTF_FLAG:
      return &Flags[mpObj->num];
    case CTF_BASE:
      return &Bases[mpObj->num];
    case UPLINK:
      for(int i = 0; i < (int)ARRAY_SIZE(Uplinks); i++) {
        if(Uplinks[i].gameObj != NULL && Uplinks[i].gameObj->extraObjectData == mpObj)
          return &Uplinks[i];
      }
      return NULL;
    case DEMOLITION:
      return &Demolition;
    case ESPIONAGEBASE:
      return &EsponageBase[mpObj->num];
    case BLUEPRINT:
      return &BluePrint;
    case GOLDENEYE_KEY:
      return &GoldenEye.keys[0];
    case GOLDENEYE_CRYSTAL:
      return &GoldenEye.keys[1];
    case PROTECTION:
      return &Protection;
    case KOH:
      return &Hill;
  }

  return NULL;
}


#ifndef NIGHTFIRE_STANDALONE
// The original takes mpObj in EDI
// AUTOLTCG
__declspec(naked) MP_OBJ_EXT* MP_getObjExtFromMPOBJECT(void) {
    _asm {
        push edi
        call _MP_getObjExtFromMPOBJECT
        add esp, 4
        ret
    }
}
#endif

// Rebuilds a scenario object's AI emitter, which the bots path to: at the object's matrix and cel unless keepPos,
// the cel looked up if it has none. Emitters exist only in a game with bots.
void _MP_recalcObjExtPaths(MP_OBJ_EXT *ext, bool keepPos) {
    if (ext == NULL)
        return;
    if (mpbots.NumBots && ext->aiEmitter.data != NULL)
        AINetwork_FreeEmitter(&ext->aiEmitter);
    if (!keepPos) {
        obj_tag *obj = ext->gameObj;
        ext->celPos.pos.x = obj->transformMatrix.m[12];
        ext->celPos.pos.y = obj->transformMatrix.m[13];
        ext->celPos.pos.z = obj->transformMatrix.m[14];
        ext->celPos.cel = obj->inCel;
    }
    if (ext->celPos.cel == NULL)
        ext->celPos.cel = build_FindCel(&ext->celPos.pos, glb_world);
    if (ext->celPos.cel != NULL && mpbots.NumBots) {
        AINetwork_InitEmitter(NULL, &ext->celPos.pos, ext->celPos.cel, &ext->aiEmitter, NULL);
        if (ext->aiEmitter.path != NULL)
            AINetwork_EmitPath(&ext->aiEmitter, 0.0f);
    }
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes ext in ESI and keepPos on the stack, removed by the caller (MP_getEsponageBaseObj).
// AUTOLTCG
__declspec(naked) void MP_recalcObjExtPaths(bool keepPos) {
    _asm {
        push dword ptr [esp + 4]    // keepPos
        push esi                    // ext
        call _MP_recalcObjExtPaths
        add esp, 8
        ret
    }
}
#endif

// Puts a scenario object back at rest: at mpObj->resetMtx unless keepPlace, in state, relinked into the world,
// shown in every view, with its AI emitter rebuilt. The original (0x9ecb0) takes mpObj in ECX, gameObj in EBX and
// keepPlace in AL; every caller is ours, so it is not patched over and goes by our name.
void _MP_ResetMPObject(MPOBJECT *mpObj, ushort state, obj_tag *gameObj, bool keepPlace) {
    if (!keepPlace)
        Mat_Copy(&mpObj->resetMtx, &gameObj->transformMatrix);
    gameObj->transformFlags |= TRANSFORM_MOVED;
    gameObj->curState = state;
    control_unlink_object(gameObj);
    Control_BuildWorldSph(gameObj);
    build_LinkToRoom(gameObj, 0, glb_world);
    gameObj->maybeParent = NULL;
    MP_OBJ_EXT *ext = _MP_getObjExtFromMPOBJECT(mpObj);
    if (ext != NULL)
        _MP_recalcObjExtPaths(ext, false);
    gameObj->effectFlags &= ~0x21;
    gameObj->tweakA = 0xff;
    View_SetDrawInAllViews(gameObj);
}

// Puts a carried object on its holder, at the holder's attach point (an AnimGetBoneWorldTrans one), facing along
// it; a player's own view does not draw it. The original (0x9c7e0) takes attach in EAX, holder in EBX and the
// object in ESI and on the stack; every caller is ours and passes the same object, so it is not patched over and
// goes by our name.
void _MP_SetUpPlayerSomehow(obj_tag *gameObj, obj_tag *holder, uint attach) {
    _VECTOR dir, pos;
    _MATRIX mtx;
    AnimGetBoneWorldTrans(holder, attach | 0x80000000, 0, &pos, &mtx);
    Mat_GetDir(&dir, &mtx);
    Mat_Align2Dir(&gameObj->transformMatrix, &dir, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    Matrix_SetTrans(&pos, &gameObj->transformMatrix);
    gameObj->transformFlags |= TRANSFORM_MOVED;
    gameObj->effectFlags |= 0x20;
    if (holder->objectType == OBJECTTYPE_PLAYER)
        View_SetDrawInOtherViewsOnly(gameObj, ((BLData *)holder->extraObjectData)->playerNum);
}


// AUTOINJECT
short MP_PlayerOrBotInd(obj_tag *obj) {
  
  if(obj == NULL)
    return -1;

  if(!MPSettings.maybeDroneAIEnabled) // Unclear why this is needed
    return -1; 

  // Search through MPGame player list
  for(int i = 0; i < NUM_AGENTS; i++) {
    if(MPGame.players[i].playerObj == obj)
      return i;
  }

  return -1;
}


static void ShowTopAgentResult(sprite *status) {
    int fewestDeaths = 30000;
    int eligibleCountSinceTie = 0;

    for (int slot = 0; slot < ARRAY_SIZE(MPGame.players); ++slot) {
        const MPGamePlayer &player = MPGame.players[slot];
        int deaths = player.deaths;
        int eliminationLimit = MPSettings.MaxPoints;

        if (player.playerObj == NULL || deaths >= eliminationLimit)
            continue;

        ++eligibleCountSinceTie;
        if (deaths < fewestDeaths) {
            const char *winnerFormat = Txt_BindLabel(MP_RESULT_TOP_AGENT, 0);
            sprintf(status->text, winnerFormat, MPSettings.Player[slot].Name);
            fewestDeaths = deaths;
        } else if (deaths == fewestDeaths) {
            // Original behavior with three eligible players (deaths in slot order):
            // [1, 1, 2] displays the first player as the winner;
            // [1, 2, 1] displays a draw.
            eligibleCountSinceTie = 0;
        }
    }

    if (eligibleCountSinceTie == 0) {
        const char *drawText = Txt_BindLabel(MP_RESULT_DRAW, 0);
        sprintf(status->text, drawText);
    }
}

static void ShowTeamResult(sprite *status) {
    float phoenixScore = MPGame.teamScore[PHOENIX];
    // The original compares Phoenix's score to itself.
    // It displays a draw for any non-NaN score, or an MI6 win for NaN.
    bool scoreIsNaN = phoenixScore != phoenixScore;

    // The original looks up "Press START" even though it isn't displayed.
    (void)Txt_BindLabel(MP_PRESS_START, 0);
    const char *result = Txt_BindLabel(scoreIsNaN ? MP_RESULT_MI6 : MP_RESULT_DRAW, 0);
    const char *heading = Txt_BindLabel(MP_RESULT_HEADING, 0);
    sprintf(status->text, "%s%s", heading, result);
}

static void ShowScoreLimitResult(sprite *status) {
    int winnerSlot = -1;
    int playersAtLimit = 0;
    double scoreLimit = MPSettings.MaxPoints;

    for (int slot = 0; slot < ARRAY_SIZE(MPGame.players); ++slot) {
        if (MPGame.players[slot].points >= scoreLimit) {
            winnerSlot = slot;
            ++playersAtLimit;
        }
    }

    (void)Txt_BindLabel(MP_PRESS_START, 0);
    if (playersAtLimit == 1) {
        const char *won = Txt_BindLabel(MP_RESULT_WON, 0);
        const char *heading = Txt_BindLabel(MP_RESULT_HEADING, 0);
        sprintf(status->text, "%s%s %s", heading, MPSettings.Player[winnerSlot].Name, won);
    } else {
        const char *drawText = Txt_BindLabel(MP_RESULT_DRAW, 0);
        const char *heading = Txt_BindLabel(MP_RESULT_HEADING, 0);
        sprintf(status->text, "%s%s", heading, drawText);
    }
}

// AUTOINJECT
void MP_SortOutWhoWon() {
    MPGame.EndGameFlowState = 3;

    if (MPSettings.GameMode == GM_TOPAGENT) {
        ShowTopAgentResult(StatusSpr);
    } else if (MPSettings.maybeIsTeamGame) {
        ShowTeamResult(StatusSpr);
        return; // The original returns here without calling Sprite_SetText.
    } else {
        ShowScoreLimitResult(StatusSpr);
    }

    Sprite_SetText(StatusSpr, StatusSpr->text);
}

// XBE_GLOBAL(0x00262f40, 0x2e0)
SpawnPlace DemolitionPlaces[8];
// XBE_GLOBAL(0x002637cc, 0x2)
uint16_t DemolitionCount;
// XBE_GLOBAL(0x00261790, 0x2e0)
SpawnPlace ProtectionPlaces[8];
// XBE_GLOBAL(0x002637d0, 0x2)
uint16_t ProtectionCount;

// AUTOINJECT
void MP_RestartScenario(void) { 

  MPGame.restartScenarioTimeout = 0.0f;
  MPGame.TimeIncPaused = 0;

  Car_Reset();
  Control_DeleteAllObjectsOfType(OBJECTTYPE_BULLET);
  Control_DeleteAllObjectsOfType(OBJECTTYPE_MPOBJECT);
  Control_DeleteAllObjectsOfType(OBJECTTYPE_EFFECT);
  Control_DeleteAllObjectsOfType(OBJECTTYPE_GAS);

  for(obj_tag* o = Control_ReturnNextObjectOfType(OBJECTTYPE_GUNTURRET2, NULL); o != NULL; o = Control_ReturnNextObjectOfType(OBJECTTYPE_GUNTURRET2, o)) {
    if(o->curState == 1) {
      GunImp_Deactivate(o);
    }
  }

  for(obj_tag* o = Control_ReturnNextObjectOfType(OBJECTTYPE_GUNTURRET, NULL); o != NULL; o = Control_ReturnNextObjectOfType(OBJECTTYPE_GUNTURRET, o)) {
    if(o->curState == 7) {
      GT_LoseControl(o);
    }
  }

  for(int i = 0; i < ARRAY_SIZE(MPObjects); i++) {
    if(MPObjects[i] == NULL || MPObjects[i]->extraObjectData == NULL)
      continue;
    MPOBJECT *mpObj = (MPOBJECT*)MPObjects[i]->extraObjectData;
    if(mpObj->scriptPlayer == NULL)
      continue;
    mpObj->scriptPlayer->flags |= 1; 
  }

  if(MPSettings.GameMode == GM_DEMOLITION) {
    int idx = Rand_Rand(DemolitionCount);
    MP_CreateObject(&DemolitionPlaces[idx].mtx, DemolitionPlaces[idx].keys, DemolitionPlaces[idx].celgl);
    MPGame.TimeUnpaused = 0;
  }

  if(MPSettings.GameMode == GM_PROTECTION) {
    int idx = Rand_Rand(ProtectionCount);
    MP_CreateObject(&ProtectionPlaces[idx].mtx, ProtectionPlaces[idx].keys, ProtectionPlaces[idx].celgl);
    MPGame.TimeUnpaused = 0;
  }

  for(int i = 0; i < MPSettings.numPlayers; i++) {
    obj_tag* plyObj = MPGame.players[i].playerObj;
    if(plyObj != NULL)
      MP_ReSpawn(plyObj, i);
  }

  for(int i = NUM_PLAYERS; i < NUM_AGENTS; i++) {
    obj_tag *botObj = MPGame.players[i].playerObj;
    if(botObj != NULL)
      BOT_respawn(botObj, i, false);
  }
  
  MPGame.unknown_maybe_capture_state = 0;
  MPGame.unknown_maybe_unused = 0;
  
  for(int i = 0; i < NUM_AGENTS; i++) {
    MPGame.players[i].maybeIdxOfLastInjurer = -1;
    MPGame.players[i].maybeIdxOfMyAssassin = -1;
  }

}


// AUTOINJECT
void MP_Update(void) {

  if(GameState.CurrentLevelHashcode == HT_Level_Menu_Pre)
    return;
  
  if(!MPSettings.isMultiplayer)
    return;

  if(GameFlow_GetState() != 2)
    return;

  // Handle paused state
  if(GS_IsPaused(-1)) {
    MPGame.lastTimePaused = (float)(int)psiGetTimeIn100ths();
    return;
  }

  // Countdown on friendly-fire warning popups
  for(int i = 0; i < ARRAY_SIZE(MPGame.players); i++) {
    if(MPGame.players[i].friendlyFireLabelTimer != 0) {
      MPGame.players[i].friendlyFireLabelTimer--;
    }
    if(MPGame.players[i].friendlyFireProtectionLabelTimer != 0) {
      MPGame.players[i].friendlyFireProtectionLabelTimer--;
    }
  }

  switch(MPGame.EndGameFlowState) {
    case 0:
      MP_Pickup_Process(); 
      MP_CheckForEndCondition();
      // some thing which got optimised out?
      break;

    case 1:
      MP_SortOutWhoWon();
      break;

    case 2:
      if(MPSettings.GameMode != GM_TOPAGENT) {
        // TODO: The function also seems to call Txt_BindLabel(PRESS_START, 0) but never uses the result?
        sprintf(StatusSpr->text, "%s", Txt_BindLabel(MP_TIME_UP, 0));
        Sprite_SetText(StatusSpr, StatusSpr->text);
        MPGame.EndGameFlowState = 3;
      } else {
        // In the Xbox code this happens due to fallthrough
        MP_SortOutWhoWon();
      }
      break;

    case 3: // Some kind of temporary delay state, showing the winners?

      if(MPSettings.GameMode == GM_TOPAGENT)
        break;
      
      MPGame.winStateTimeout += REC_FRAME_RATE;
      
      if(MPGame.winStateTimeout >= 5.0f) {
        MPGame.EndGameFlowState = 4;
        GameState.maybePaused = true;
      }

      break;

    case 4:
      MPGame.EndGameFlowState = 5;
      GameState.ReloadMenupage = P_MPDEBRIEFING;
      ResetMap_LevelToLoad(HT_Level_Menu_Pre, false, false);
      GameFlow_PushState(7, 60.0f, 0xFF);
      for(int i = 0; i < NUM_PLAYERS; i++) {
        obj_tag* plyObj = MPGame.players[i].playerObj;
        if(plyObj != NULL) {
          Player_SetCamMode((BLData*)plyObj->extraObjectData, 1);
          GS_PausePlayer(1, i);
        }
      }
      break;

    case 6:
      MPGame.restartScenarioTimeout += REC_FRAME_RATE;
      if(MPGame.restartScenarioTimeout >= 2.5f) {
        Sprite_SetText(StatusSpr, (char*)Txt_BindLabel(NOTIF_RESTARTING, 0));
      }
      if(MPGame.restartScenarioTimeout > 5.0f) {
        MPGame.EndGameFlowState = 0;
        MP_RestartScenario();
        sprintf(StatusSpr->text, "");
        Sprite_SetText(StatusSpr, StatusSpr->text);
      }
      break;

  }


}


// XBE_GLOBAL(0x0025fe30, 0x4)
uint32_t PickupNextAddIndex;
// XBE_GLOBAL(0x00261b84, 0x4)
int PickupLastDeletedIdx;  // -1 = none

// Game mode bits (see MultiplayerGameMode): bit 29 is set by every team mode, bit 30 by KOTH, team KOTH and
// uplink.
#define GM_BIT_TEAMGAME 29
#define GM_BIT_30 30

// The time limit demolition and protection fall back to when MaxDuration is negative, in seconds
#define MP_DEFAULT_OBJECTIVE_TIME_LIMIT 60.0f

// Reset values of the two per-player indices (MPGamePlayer notes -2 as possibly "the environment")
#define MP_INJURER_NONE ((short)-2)
#define MP_ASSASSIN_NONE ((short)-1)

// Called at every level load. Clears all the per-match multiplayer state (spawn points, scenario objects,
// pickups, scores) and sets up the timers from MPSettings - except when the level being loaded is the
// front-end menu, so that the menus (the debriefing's scores, for one) still see the finished match.
//
// AUTOINJECT
void MP_Init(void) {

    if (GameState.NextLevelHashcode == HT_Level_Menu_Pre)
        return;

    Pickup_MakeRandomWeaponSet();

    memset(&SpawnPoints, 0, sizeof(SpawnPoints));
    SpawnPntTeamCount[PHOENIX] = 0;
    memset(&MPObjects, 0, sizeof(MPObjects));
    memset(&Flags, 0, sizeof(Flags));
    memset(&Bases, 0, sizeof(Bases));
    memset(&Uplinks, 0, sizeof(Uplinks));
    memset(&DemolitionPlaces, 0, sizeof(DemolitionPlaces));
    memset(&Demolition, 0, sizeof(Demolition));
    memset(&ProtectionPlaces, 0, sizeof(ProtectionPlaces));
    memset(&Protection, 0, sizeof(Protection));
    memset(&GoldenEye, 0, sizeof(GoldenEye));
    memset(&GoldenEyeSpawns, 0, sizeof(GoldenEyeSpawns));
    memset(&BluePrints, 0, sizeof(BluePrints));
    memset(&EsponageBase, 0, sizeof(EsponageBase));
    AssassinTarget = NULL;
    CurrentAssassinObjId = NULL;
    memset(&Hill, 0, sizeof(Hill));
    memset(&MPGame, 0, sizeof(MPGame));
    // The original then clears MPGame.players[] (0x1e0 bytes) a second time; it is already zero.
    SpawnPntTeamCount[MI6] = 0;
    memset(&MPpickups, 0, sizeof(MPpickups));

    uint gameMode = MPSettings.GameMode;
    MPSettings.maybeIsTeamGame = (gameMode >> GM_BIT_TEAMGAME) & 1;
    MPSettings.field53_0x190 = (gameMode >> GM_BIT_30) & 1;

    SpawnPntCount = 0;
    UplinkCount = 0;
    DemolitionCount = 0;
    ProtectionCount = 0;
    GoldenEyeKeyCount = 0;
    GoldenEyeNonKeyCount = 0;
    BluePrintCount = 0;
    PickupLastDeletedIdx = -1;
    PickupNextAddIndex = 0;
    MPSettings.numActivePickups = 0;

    MPGame.restartScenarioTimeout = 0.0f;
    MPGame.winStateTimeout = 0.0f;
    MPGame.TimeLimit = (float)(int)MPSettings.MaxDuration; // FILD: a signed conversion
    MPGame.TimeUnpaused = 0;
    MPGame.TimeIncPaused = 0;
    MPGame.unknown_maybe_capture_state = 0;
    MPGame.unknown_maybe_unused = 0;

    if ((gameMode == GM_DEMOLITION || gameMode == GM_PROTECTION) && MPGame.TimeLimit < 0.0f)
        MPGame.TimeLimit = MP_DEFAULT_OBJECTIVE_TIME_LIMIT;

    for (int i = 0; i < NUM_AGENTS; i++) {
        MPGame.players[i].maybeIdxOfLastInjurer = MP_INJURER_NONE;
        MPGame.players[i].maybeIdxOfMyAssassin = MP_ASSASSIN_NONE;
        // Top Agent starts everyone on the points limit
        if (gameMode == GM_TOPAGENT)
            MPGame.players[i].points = (float)(int)MPSettings.MaxPoints;
    }
}

// Drone_tag.flags bits (docs/drone/bots-and-navigation/README.md 4.3)
#define DRONE_AI_RUNNING 0x100
#define DRONE_DEAD 0x200
#define DRONE_DYING 0x400

// The first agent on the team (NO_TEAM: either) whose object is in obj's hit list, unless that agent is dead,
// dying or spawned within the last 4 frames - then none. The agent's team goes to teamOut.
obj_tag* _MP_HitBy(obj_tag *obj, ushort team, obj_tag *exclude, ushort *teamOut) {

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        obj_tag *hitObj = hit->hitObj;
        if (hitObj == NULL || hitObj == exclude)
            continue;
        if (hitObj->objectType != OBJECTTYPE_PLAYER && hitObj->objectType != OBJECTTYPE_DRONE)
            continue;

        for (ushort i = 0; i < NUM_AGENTS; i++) {
            obj_tag *agent = MPGame.players[i].playerObj;
            if (agent == NULL)
                continue;
            if (team != NO_TEAM && MPSettings.Player[i].TeamId != team)
                continue;
            if (agent != hitObj)
                continue;

            int age = GameState.NumFramesUnpaused - agent->creationTimeFrames;
            if (age < 0)
                age = -age;
            if (age < 4)
                return NULL;

            char type = agent->objectType;
            if (type == OBJECTTYPE_DRONE || type == OBJECTTYPE_DEAD_DRONE) {
                if (MPDrone_MaybeIsDyingOrDead(agent))
                    return NULL;
                if (type == OBJECTTYPE_DEAD_DRONE)
                    return NULL;
            }
            if (type == OBJECTTYPE_DEAD_PLAYER)
                return NULL;
            if (type == OBJECTTYPE_PLAYER && (agent->curState == 2 || agent->curState == 3))
                return NULL;

            if (teamOut != NULL)
                *teamOut = MPSettings.Player[i].TeamId;
            return MPGame.players[i].playerObj;
        }
    }

    return NULL;
}

#ifndef NIGHTFIRE_STANDALONE
// The original (0x0009c880) takes obj in EAX and team in CX, exclude and teamOut on the stack; its callers clean
// the stack.
// AUTOLTCG
__declspec(naked) obj_tag* MP_HitBy(obj_tag *obj, ushort team, obj_tag *exclude, ushort *teamOut) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        movzx ecx, cx
        push ecx
        push eax
        call _MP_HitBy
        add esp, 16
        ret
    }
}
#endif

// A random agent on the team (NO_TEAM: either) other than exclude; with aliveOnly, only a drone not dying or dead or a player in
// state 1. NULL when there is none.
// AUTOINJECT
obj_tag* MP_GetTarget(ushort team, obj_tag *exclude, bool aliveOnly) {
    obj_tag *candidates[NUM_AGENTS];
    ushort count = 0;

    for (int i = 0; i < NUM_AGENTS; i++) {
        obj_tag *agent = MPGame.players[i].playerObj;
        if (agent == NULL)
            continue;
        if (team != NO_TEAM && MPSettings.Player[i].TeamId != team)
            continue;
        if (agent == exclude)
            continue;

        if (aliveOnly) {
            switch (agent->objectType) {
                case OBJECTTYPE_DRONE:
                    if (MPDrone_MaybeIsDyingOrDead(agent))
                        continue;
                    break;
                case OBJECTTYPE_PLAYER:
                    if (agent->curState != 1)
                        continue;
                    break;
                case OBJECTTYPE_DEAD_DRONE:
                case OBJECTTYPE_DEAD_PLAYER:
                    continue;
            }
        }

        candidates[count++] = agent;
    }

    if (count == 0)
        return NULL;
    return candidates[Rand_Rand(count)];
}

// Switch channels MP_CheckForEndCondition sets
#define SW_MP_SCORE_REACHED 0xfd
#define SW_MP_TIME_UP 0xfe

// XBE_GLOBAL(0x001dfa14, 0x4)
uint32_t switch_MP4EVER;

static void ShowTimeLeft(void) {
    int hundredths = Ftol(((double)MPGame.TimeLimit - MPGame.TimeUnpaused) * 100.0f);
    sprintf(TimeSpr->text, "%s", Timer_Seconds2String(hundredths, 2));
    Sprite_SetText(TimeSpr, TimeSpr->text);
}

// The higher team score and, with players, any agent's points above it; kept in MPGame.unknown_3
static int HighestScore(bool players) {
    int best = Ftol(MPGame.teamScore[MI6]);
    if (MPGame.teamScore[PHOENIX] > (double)best)
        best = Ftol(MPGame.teamScore[PHOENIX]);

    if (players) {
        for (int i = 0; i < NUM_AGENTS; i++) {
            int points = Ftol(MPGame.players[i].points);
            if (points >= best)
                best = points;
        }
    }

    MPGame.unknown_3 = best;
    return best;
}

// Top Agent: agents out of lives (deaths at the points limit). Ends the match when all but one are out or every
// human is; with every human out and more than one bot left, turns the first live drone into a dead one.
static void CheckTopAgentEliminations(void) {
    int maxPoints = MPSettings.MaxPoints;
    ushort playersOut = 0;
    ushort botsOut = 0;

    for (int i = 0; i < NUM_AGENTS; i++) {
        obj_tag *agent = MPGame.players[i].playerObj;
        if (agent == NULL)
            continue;
        int deaths = MPGame.players[i].deaths;
        char type = agent->objectType;
        if (type == OBJECTTYPE_DEAD_PLAYER && deaths >= maxPoints)
            playersOut++;
        if ((type == OBJECTTYPE_DEAD_DRONE || (type == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(agent))) &&
            deaths >= maxPoints)
            botsOut++;
    }

    if (playersOut + botsOut >= (ushort)(MPSettings.numPlayersAndBots - 1) ||
        playersOut == (ushort)MPSettings.numPlayers)
        MPGame.EndGameFlowState = 1;

    if (playersOut >= MPSettings.numPlayers && MPSettings.numBots != 0 && botsOut < MPSettings.numBots - 1) {
        for (ushort i = 0; i < NUM_AGENTS; i++) {
            obj_tag *agent = MPGame.players[i].playerObj;
            if (agent != NULL && agent->objectType == OBJECTTYPE_DRONE) {
                agent->objectType = OBJECTTYPE_DEAD_DRONE;
                // Always true here, so only the first drone is changed
                if (botsOut < MPSettings.numBots)
                    break;
            }
        }
    }
}

// Once a frame while the match runs (MP_Update): the clocks, the time display, and whether the match is over -
// EndGameFlowState 1 on the points limit, 2 on the time limit, 6 when a demolition or protection round runs out.
// AUTOINJECT
void MP_CheckForEndCondition(void) {
    double now = (uint32_t)psiGetTimeIn100ths();
    if (MPGame.lastTimePaused < 0.0f)
        MPGame.lastTimePaused = (float)now;
    double elapsed = now - MPGame.lastTimePaused;
    MPGame.lastTimePaused = (float)now;
    float dt = (float)(elapsed * 0.01f);

    if (!GS_IsPaused(-1))
        MPGame.TimeIncPaused += dt;

    if (switch_MP4EVER)
        return;

    if (MPSettings.GameMode == GM_TOPAGENT)
        CheckTopAgentEliminations();

    bool roundMode = MPSettings.GameMode == GM_DEMOLITION || MPSettings.GameMode == GM_PROTECTION;

    if (!GS_IsPaused(-1))
        MPGame.TimeUnpaused += dt;

    if (roundMode) {
        if (MPGame.TimeLimit > 0.0f && !switch_channels[SW_MP_TIME_UP]) {
            if (MPGame.TimeUnpaused >= MPGame.TimeLimit)
                MPGame.EndGameFlowState = 6;
            ShowTimeLeft();
        }
    } else if (MPSettings.MaxDuration != -1 && !switch_channels[SW_MP_TIME_UP]) {
        switch_channels[SW_MP_TIME_UP] = MPGame.TimeUnpaused >= MPGame.TimeLimit;
        ShowTimeLeft();
    }

    int best = HighestScore(roundMode || !MPSettings.maybeIsTeamGame);

    int maxPoints = MPSettings.MaxPoints;
    if (MPSettings.GameMode != GM_TOPAGENT && maxPoints != -1)
        switch_channels[SW_MP_SCORE_REACHED] = best >= maxPoints;

    if (switch_channels[SW_MP_SCORE_REACHED])
        MPGame.EndGameFlowState = 1;
    else if (switch_channels[SW_MP_TIME_UP])
        MPGame.EndGameFlowState = 2;
}

// Whether obj is out of play: not an agent at all, dead or dying, or a drone whose AI is not running
// AUTOINJECT
bool MP_playerIsDead(obj_tag *obj) {
    bool isAgent = false;
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (MPGame.players[i].playerObj == obj) {
            isAgent = true;
            break;
        }
    }
    if (!isAgent)
        return true;

    char type = obj->objectType;
    if (type == OBJECTTYPE_DRONE || type == OBJECTTYPE_DEAD_DRONE) {
        Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
        if (drone->flags & (DRONE_DEAD | DRONE_DYING))
            return true;
        if (!(drone->flags & DRONE_AI_RUNNING))
            return true;
        if (drone->health <= 0.0f)
            return true;
        if (type == OBJECTTYPE_DEAD_DRONE)
            return true;
        return obj->flags & 1;
    }

    if (type == OBJECTTYPE_DEAD_PLAYER)
        return true;
    if (type == OBJECTTYPE_PLAYER && ((BLData *)obj->extraObjectData)->health <= 0.0f)
        return true;
    return type == OBJECTTYPE_GFX;
}

// Clears the visit times of the live pickups that have passed
// AUTOINJECT
void MP_Pickup_Process(void) {
    int found = 0;
    for (MP_PICKUP *pickup = MPpickups; found < MPSettings.numActivePickups; pickup++) {
        if (pickup->gameObj == NULL)
            continue;
        found++;
        for (int bot = 0; bot < NUM_BOTS; bot++) {
            float &visit = pickup->maybeBotPickupVisitTimes[bot];
            if (visit != 0.0f && visit < MPGame.TimeIncPaused)
                visit = 0.0f;
        }
    }
}

// AUTOINJECT
void MP_ResetBotPickupTimes(ushort botNum) {
    for (int i = 0; i < ARRAY_SIZE(MPpickups); i++)
        MPpickups[i].maybeBotPickupVisitTimes[botNum] = 0.0f;
}

// Blip colours, 0xRRGGBBAA
#define RADAR_COLOUR_PHOENIX 0xd22d35ff
#define RADAR_COLOUR_MI6 0x2d61d2ff
#define RADAR_COLOUR_NEUTRAL 0x7f7f7fff

static void AddRadarObject(ushort *count, _VECTOR *pos, ushort type, uint colour) {
    RADAROBJ &blip = MPRadarObjects[*count];
    Vec_Copy(pos, &blip.pos);
    blip.icon = type;
    blip.colour = colour;
    (*count)++;
}

// The radar's blips for viewer: every other agent still in play, then the game mode's objects. None when the
// viewer's per-player setting at +0x28 is 0.
// AUTOINJECT
ushort MP_GetRadarObjects(obj_tag *viewer, RADAROBJ **objects) {
    *objects = MPRadarObjects;
    // Control_Plr2Ind's -1 for a non-agent reads the entry before Player[0]
    if (!MPSettings.Player[Control_Plr2Ind(viewer)].SomeField2)
        return 0;

    ushort count = 0;

    for (int i = 0; i < NUM_AGENTS; i++) {
        obj_tag *agent = MPGame.players[i].playerObj;
        if (agent == NULL || agent == viewer)
            continue;

        switch (agent->objectType) {
            case OBJECTTYPE_DRONE:
            case 4: // no name in ObjectType
                if (MPDrone_MaybeIsDyingOrDead(agent))
                    continue;
                break;
            case OBJECTTYPE_PLAYER:
                if (((BLData *)agent->extraObjectData)->health <= 0.0f)
                    continue;
                break;
            default:
                continue;
        }

        uint colour = RADAR_COLOUR_NEUTRAL;
        if (MPSettings.maybeIsTeamGame)
            colour = MPSettings.Player[i].TeamId != PHOENIX ? RADAR_COLOUR_MI6 : RADAR_COLOUR_PHOENIX;
        AddRadarObject(&count, Mat_Position(agent->transformMatrix), MP_RADAR_AGENT, colour);
    }

    if (MPSettings.GameMode == GM_CTF) {
        for (ushort i = 0; i < ARRAY_SIZE(Flags); i++)
            AddRadarObject(&count, Mat_Position(Flags[i].gameObj->transformMatrix), MP_RADAR_FLAG,
                           i != 0 ? RADAR_COLOUR_MI6 : RADAR_COLOUR_PHOENIX);
    }

    if (MPSettings.GameMode == GM_UPLINK) {
        for (ushort i = 0; i < ARRAY_SIZE(Uplinks); i++) {
            obj_tag *uplink = Uplinks[i].gameObj;
            if (uplink != NULL)
                AddRadarObject(&count, &uplink->centrePoint, MP_RADAR_UPLINK,
                               (uint)uplink->tweakR << 24 | uplink->tweakG << 16 | uplink->tweakB << 8 | 0xff);
        }
    }

    if (MPSettings.GameMode == GM_DEMOLITION && Demolition.gameObj != NULL)
        AddRadarObject(&count, &Demolition.gameObj->centrePoint, MP_RADAR_OBJECTIVE, RADAR_COLOUR_NEUTRAL);
    if (MPSettings.GameMode == GM_PROTECTION && Protection.gameObj != NULL)
        AddRadarObject(&count, &Protection.gameObj->centrePoint, MP_RADAR_OBJECTIVE, RADAR_COLOUR_NEUTRAL);

    if (MPSettings.GameMode == GM_GOLDENEYE) {
        for (ushort i = 0; i < ARRAY_SIZE(GoldenEye.keys); i++) {
            obj_tag *key = GoldenEye.keys[i].gameObj;
            if (!(key->effectFlags & FLAG_HIDDEN))
                AddRadarObject(&count, Mat_Position(key->transformMatrix), MP_RADAR_GOLDENEYE_KEY,
                               RADAR_COLOUR_NEUTRAL);
        }
    }

    if (BluePrint.gameObj != NULL && MPSettings.GameMode == GM_BLUEPRINT) {
        AddRadarObject(&count, Mat_Position(BluePrint.gameObj->transformMatrix), MP_RADAR_BLUEPRINT,
                       RADAR_COLOUR_NEUTRAL);
        AddRadarObject(&count, Mat_Position(EsponageBase[0].gameObj->transformMatrix), MP_RADAR_BASE,
                       RADAR_COLOUR_PHOENIX);
        AddRadarObject(&count, Mat_Position(EsponageBase[1].gameObj->transformMatrix), MP_RADAR_BASE,
                       RADAR_COLOUR_MI6);
    }

    return count;
}

// Assassin: a new assassin, the agent with team MI6, every other agent PHOENIX. With handOver, the old assassin's
// maybeIdxOfMyAssassin takes over; otherwise, or when there is none, a random agent. The target is a random agent
// other than the assassin (or, with no assassin yet, than the old target).
// AUTOINJECT
void MP_assassinReset(bool handOver) {
    for (int i = 0; i < NUM_AGENTS; i++)
        MPSettings.Player[i].TeamId = PHOENIX;

    obj_tag *oldAssassin = CurrentAssassinObjId;
    CurrentAssassinObjId = NULL;
    if (oldAssassin != NULL)
        Sound_PlayExt(SFX_BOND_MOMENT_BM_CAS_WALL, 100.0f, 0, 0);

    if (handOver && oldAssassin != NULL) {
        short idx = Control_Plr2Ind(oldAssassin);
        if (idx >= 0) {
            int next = MPGame.players[idx].maybeIdxOfMyAssassin;
            if (next >= 0)
                CurrentAssassinObjId = MPGame.players[next].playerObj;
        }
    }

    AssassinTarget = MP_GetTarget(PHOENIX, CurrentAssassinObjId != NULL ? CurrentAssassinObjId : AssassinTarget, false);
    if (CurrentAssassinObjId == NULL)
        CurrentAssassinObjId = MP_GetTarget(PHOENIX, AssassinTarget, false);

    short idx = Control_Plr2Ind(CurrentAssassinObjId);
    if (idx >= 0)
        MPSettings.Player[idx].TeamId = MI6;
}

// XBE_GLOBAL(0x0017e5ac, 0x2c)
SpriteInfo AssassinSpriteInfo = {
    .colour = 0xffffffff,
    .unknown1 = 0xffffffff,
    .maybeEnabled = 1,
    .maybeFlags = 8192,
    .posX = 320,
    .posY = 72,
    .textRef = (Action_TranslatedText)0xffffffff,
    .linkedViewer = 5,
};
// XBE_GLOBAL(0x0017e554, 0x2c)
SpriteInfo MPTimeInfo = {
    .colour = 0xffffffff,
    .unknown1 = 255,
    .maybeEnabled = 1,
    .maybeFlags = 8192,
    .posX = 320,
    .posY = 134,
    .maybeClippedText = FontFormat1Centred,
    .linkedViewer = 5,
};
// XBE_GLOBAL(0x0017e580, 0x2c)
SpriteInfo MPStatusInfo = {
    .colour = 0xffffffff,
    .unknown1 = 255,
    .maybeEnabled = 1,
    .maybeFlags = 8192,
    .posX = 320,
    .posY = 72,
    .maybeClippedText = FontFormat2Centred,
    .linkedViewer = 5,
};

// The assassin sprites' textures (names ours)
#define TEX_MP_ASSASSIN_180 ((HASHCODE)0x03000180)
#define TEX_MP_ASSASSIN_189 ((HASHCODE)0x03000189)

static void CreateAt(SpawnPlace *place) {
    MP_CreateObject(&place->mtx, place->keys, place->celgl);
}

// Sets the match going once the level is loaded: the game mode's objective objects, the players and bots at their
// spawn points, the Assassin sprites, and the time and status sprites.
// AUTOINJECT
void MP_Start(void) {
    MPSettings.maybeDroneAIEnabled = MPSettings.isMultiplayer;
    MPSettings.Started = false;
    // The original calls the empty debug hook (0x000e0ec0) here
    if (!MPSettings.isMultiplayer)
        return;

    level_tag_PlayerStartPosition spawnData = {};

    if (MPSettings.GameMode == GM_DEMOLITION && DemolitionCount != 0)
        CreateAt(&DemolitionPlaces[Rand_Rand(DemolitionCount)]);
    if (MPSettings.GameMode == GM_PROTECTION && ProtectionCount != 0)
        CreateAt(&ProtectionPlaces[Rand_Rand(ProtectionCount)]);
    if (MPSettings.GameMode == GM_BLUEPRINT && BluePrintCount != 0)
        CreateAt(&BluePrints[Rand_Rand(BluePrintCount)]);
    if (MPSettings.GameMode == GM_GOLDENEYE) {
        // Places 0-7 are the key's, 8-15 the others'
        CreateAt(&GoldenEyeSpawns[Rand_Rand(GoldenEyeKeyCount)]);
        CreateAt(&GoldenEyeSpawns[Rand_Rand(GoldenEyeNonKeyCount) + 8]);
    }

    for (ushort i = 0; i < MPSettings.numPlayers; i++) {
        ushort spawn = MP_GetSpawnPoint(MPSettings.Player[i].TeamId, NULL);
        MPGame.players[i].playerObj = Player_Init(i, &SpawnPoints[spawn].spawnPos, &SpawnPoints[spawn].facingDir,
                                                  (level_tag *)&spawnData);
    }

    if (mpbots.Enabled)
        MPSettings.numBots = (uchar)mpbots.NumBots;
    if (MPSettings.numBots > NUM_BOTS)
        MPSettings.numBots = NUM_BOTS;

    for (ushort i = 0; i < MPSettings.numBots; i++) {
        MPSettings_PerPlayer &settings = MPSettings.Player[NUM_PLAYERS + i];
        if (mpbots.Enabled)
            settings.TeamId = (MPTeam)(uchar)mpbots.bot[i].isGood;
        ushort spawn = MP_GetSpawnPoint(settings.TeamId, NULL);
        MPBOT *bot = mpbots.Enabled ? &mpbots.bot[i] : NULL;
        MPGame.players[NUM_PLAYERS + i].playerObj = BOT_init(NUM_PLAYERS + i, &SpawnPoints[spawn].spawnPos,
                                                             &SpawnPoints[spawn].facingDir, NULL, bot, 0);
    }

    MPSettings.numPlayersAndBots = MPSettings.numBots + MPSettings.numPlayers;

    memset(MPGame.radarMarks, 0, sizeof(MPGame.radarMarks));
    if (MPSettings.GameMode == GM_ASSASSIN) {
        MP_assassinReset(false);
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (MPGame.players[i].playerObj == NULL)
                continue;
            AssassinSpriteInfo.linkedViewer = 5;
            AssassinSpriteInfo.textureHashcode = TEX_MP_ASSASSIN_180;
            MPGame.radarMarks[i].assassin = Sprite_Create2(&AssassinSpriteInfo);
            AssassinSpriteInfo.textureHashcode = TEX_MP_ASSASSIN_189;
            MPGame.radarMarks[i].target = Sprite_Create2(&AssassinSpriteInfo);
        }
    }

    TimeSpr = Sprite_Create2(&MPTimeInfo);
    StatusSpr = Sprite_Create2(&MPStatusInfo);
    switch (MPSettings.numPlayers) {
        case 1:
            TimeSpr->positionY = 56;
            break;
        case 2:
            if (!MultiplayerLayout_LeftRightOrTopBtm)
                TimeSpr->positionY = 56;
            break;
        case 3:
            TimeSpr->positionX = 480;
            TimeSpr->positionY = 360;
            break;
    }
    sprintf(TimeSpr->text, "");
    Sprite_SetText(TimeSpr, TimeSpr->text);
    sprintf(StatusSpr->text, "");
    Sprite_SetText(StatusSpr, StatusSpr->text);

    MPSettings.Started = true;
    MPGame.lastTimePaused = -1.0f;
}

// ---- the scenario objects, as the HUD and the bots ask for them

// A team's score line for the HUD: its score and the other team's. NULL outside a team game.
// AUTOINJECT
char* MP_GetTeamScore(ushort team) {
    if (!MPSettings.maybeIsTeamGame)
        return NULL;
    char *text = Txt_GetStringFromHeap(0);
    // both settings of field53_0x190 print the same
    int theirs = Ftol(MPGame.teamScore[team == PHOENIX]);
    int ours = Ftol(MPGame.teamScore[team]);
    sprintf(text, "%d - %d", ours, theirs);
    return text;
}

// An agent's score lines for the HUD: victories and deaths, after its points in Assassin and King of the Hill
// AUTOINJECT
char* MP_GetPlayerScore(ushort player) {
    char *text = Txt_GetStringFromHeap(0);
    MPGamePlayer *agent = &MPGame.players[player];
    if (MPSettings.GameMode == GM_ASSASSIN || MPSettings.GameMode == GM_KOTH)
        sprintf(text, "%d\n%d\n%d", Ftol(agent->points), agent->victories, agent->deaths);
    else if (MPSettings.maybeIsTeamGame)
        sprintf(text, "%d\n%d", agent->victories, agent->deaths);
    else
        sprintf(text, "\n%d\n%d", agent->victories, agent->deaths);
    return text;
}

// Whether obj carries the team's flag
// AUTOINJECT
bool MP_IsFlagParent(obj_tag *obj, ushort team) {
    obj_tag *flag = Flags[team].gameObj;
    return flag != NULL && flag->maybeParent == obj;
}

// AUTOINJECT
bool MP_IsBluePrintParent(obj_tag *obj) {
    obj_tag *blueprint = BluePrint.gameObj;
    return blueprint != NULL && blueprint->maybeParent == obj;
}

// Whether the GoldenEye key or crystal (part 0 or 1) is carried by someone on obj's team
// AUTOINJECT
bool MP_ObjTeamHasGEObj(obj_tag *obj, int part) {
    MP_OBJ_EXT *ext = &GoldenEye.keys[part];
    if (ext->gameObj == NULL || ext->gameObj->maybeParent == NULL)
        return false;
    MPTeam team = MPSettings.Player[Control_Plr2Ind(obj)].TeamId;
    return team == MPSettings.Player[Control_Plr2Ind(ext->gameObj->maybeParent)].TeamId;
}

// The bots' path to a scenario object, once the level is loaded
void _MP_PostLoadInitObjExt(MP_OBJ_EXT *ext) {
    if (ext != NULL && mpbots.NumBots && ext->aiEmitter.path != NULL)
        AINetwork_EmitPath(&ext->aiEmitter, 0);
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes ext in EAX
// AUTOLTCG
__declspec(naked) void MP_PostLoadInitObjExt(void) {
    _asm {
        push eax
        call _MP_PostLoadInitObjExt
        add esp, 4
        ret
    }
}
#endif

// An uplink: by index (NULL past the last, or with no object), or for a negative idx the first from -1 - idx whose
// object is not in state filterState
// AUTOINJECT
MP_OBJ_EXT* MP_getUplinkObj(int idx, int filterState) {
    if (idx < 0) {
        for (int i = -1 - idx; i < UplinkCount; i++) {
            MP_OBJ_EXT *ext = &Uplinks[i];
            if (ext->gameObj != NULL && (short)ext->gameObj->curState != filterState)
                return ext;
        }
        return NULL;
    }
    if (idx >= UplinkCount)
        return NULL;
    MP_OBJ_EXT *ext = &Uplinks[idx];
    return ext->gameObj != NULL ? ext : NULL;
}

// An uplink's state (its object's), 2 with none
// AUTOINJECT
ushort MP_getUplinkStatus(short uplink) {
    obj_tag *obj = Uplinks[uplink].gameObj;
    if (obj == NULL)
        return 2;
    return obj->curState;
}

// AUTOINJECT
MP_OBJ_EXT* MP_getBlueprintObj(void) {
    return BluePrint.gameObj != NULL ? &BluePrint : NULL;
}

// A team's espionage base, with its bot goal moved to the side of the base given by quadrant (0-3, a quarter turn
// each from its facing) and its path rebuilt; another quadrant leaves it where it was
// AUTOINJECT
MP_OBJ_EXT* MP_getEsponageBaseObj(uint team, int quadrant) {
    if (team >= 2)
        return NULL;
    if (quadrant < 0 || quadrant > 3)
        return &EsponageBase[team];

    MP_OBJ_EXT *ext = &EsponageBase[team];
    obj_tag *base = ext->gameObj;
    if (base == NULL)
        return NULL;
    Extended angle = Extended((double)quadrant) * Extended(HalfPi) + Extended(base->rotation.y);
    Extended reach = Extended(base->radius) + Extended(0.1f);
    ext->celPos.pos.x = (float)(FSin(angle) * reach + Extended(base->centrePoint.x));
    ext->celPos.pos.y = base->centrePoint.y;
    ext->celPos.pos.z = (float)(FCos(angle) * reach + Extended(base->centrePoint.z));
    ext->celPos.cel = build_FindCel(&ext->celPos.pos, glb_world);
    build_PointOnFloor(ext->celPos.cel, NULL, &ext->celPos.pos, 1.25f, NULL);
    _MP_recalcObjExtPaths(ext, true);
    return ext;
}

// The GoldenEye key or crystal while it is in state 0 or 2, else NULL
// AUTOINJECT
MP_OBJ_EXT* MP_getGoldenEyeObj(uint key) {
    if (key > 1)
        return NULL;
    MP_OBJ_EXT *ext = &GoldenEye.keys[key];
    if (ext->gameObj == NULL)
        return NULL;
    short state = ext->gameObj->curState;
    return state == 0 || state == 2 ? ext : NULL;
}

// Whether pos is inside the King of the Hill object's box
// AUTOINJECT
bool MP_isPosOnHill(_VECTOR *pos) {
    obj_tag *hill = Hill.gameObj;
    _VECTOR offset;
    Vec_Subtract(pos, (_VECTOR *)&hill->transformMatrix.m[12], &offset);
    return Intersect_SphereBox(&offset, 0.0f, &hill->objGraphics->extentMin, &hill->objGraphics->extentMax);
}

// A scenario object's object. The demolition and protection objects are their script's: when that changes, the
// side's bots are told the old one has gone (bot message 0x40).
// AUTOINJECT
obj_tag* MP_getObjExtObj(MP_OBJ_EXT *ext) {
    MPTeam told;    // the side told when the object goes
    if (ext == &Protection)
        told = PHOENIX;
    else if (ext == &Demolition)
        told = MI6;
    else
        return ext->gameObj;

    if (ext->mpObj != NULL) {
        obj_tag *obj = Script_GetObj(SP_getScriptInfo(ext->mpObj->scriptPlayer), 0);
        obj_tag *old = ext->gameObj;
        if (obj != old) {
            ext->gameObj = obj;
            if (old != NULL)
                MP_sendTeamBotMessage(told, BOT_MSG_OBJECTIVE_GONE, old, 0, 0);
        }
    }
    return ext->gameObj;
}

// AUTOINJECT
obj_tag* MP_getAssassinTarget(void) {
    return AssassinTarget;
}

// The multiplayer pickup slot of a pickup, or NULL
// AUTOINJECT
MP_PICKUP* MP_getMPpickup(PICKUPINFO *info) {
    for (int i = 0; i < 64; i++) {
        obj_tag *obj = MPpickups[i].gameObj;
        if (obj != NULL && obj->extraObjectData == info)
            return &MPpickups[i];
    }
    return NULL;
}

// AUTOINJECT
obj_tag* MP_getProtectionObj_NotExt(void) {
    return MP_getObjExtObj(&Protection);
}

// AUTOINJECT
obj_tag* MP_getDemolitionObj_NotExt(void) {
    return MP_getObjExtObj(&Demolition);
}

// ---- messages to the bots, the agents' status and the pickups

// A bot message (bot messages are DRONE_MSGs for the BotGlobal state) to one drone, after delay frames
// AUTOINJECT
void MP_sendBotMessage(obj_tag *obj, uint msg, uint extra, uint delay) {
    if (obj->objectType != OBJECTTYPE_DRONE)
        return;
    DCVars_tag dc;
    if (Drone_DCVfromOBJ(obj, &dc))
        Drone_SM_SendMsgSelf(msg, (void *)(uintptr_t)extra, delay, DSTATE_BotGlobal, &dc);
}

// A bot message to every bot of a team (any bot for NO_TEAM), after delay frames
// AUTOINJECT
void MP_sendTeamBotMessage(MPTeam team, uint msg, void *extraData, uint delay, uint senderId) {
    MsgObject message;
    message.msgType = msg;
    message.scope = DSTATE_BotGlobal;
    message.sender = senderId;
    message.receiver = team == NO_TEAM ? 0 : -1 - (int)team;
    message.createdFrame = GameState.NumFramesUnpaused;
    message.handleOnFrame = GameState.NumFramesUnpaused + delay;
    message.extraData = extraData;
    Drone_SM_RouteMsg(&message);
}

// Sets (or with clear, clears) an agent's MPGame flags, and tells the bots what happened: msg is a bot message
// for the agent itself, and some also change its team's objective flags and tell the team's bots of target.
// AUTOINJECT
void MP_setPlayerStatus(obj_tag *obj, ushort status, uint msg, uint unused, obj_tag *target, bool clear) {
    if (obj == NULL)
        return;
    int playerIndex = Control_Plr2Ind(obj);
    if (playerIndex == -1)
        return;

    ushort flags = MPGame.players[playerIndex].flags;
    MPTeam team = MPSettings.Player[playerIndex].TeamId;
    uint sender = 0;
    if (clear)
        MPGame.players[playerIndex].flags = ~status & flags;
    else
        MPGame.players[playerIndex].flags = flags | status;
    if (msg == 0)
        return;

    if (obj->objectType == OBJECTTYPE_DRONE) {
        DCVars_tag dc;
        Drone_DCVfromOBJ(obj, &dc);
        sender = dc.aiStateMachine->id;
    }

    ushort *teamFlags = &MPGame.teamObjectiveFlags[team];
    switch (msg) {
        case 0x2f: case 0x32: case 0x35:
            *teamFlags |= 1;
            MP_sendBotMessage(obj, BOT_MSG_GOAL_COMPLETE, 1, 0);
            MP_sendTeamBotMessage(team, BOT_MSG_CANCEL_GOALS, target, 0, sender);
            break;
        case 0x36:
            *teamFlags |= 2;
            MP_sendBotMessage(obj, BOT_MSG_GOAL_COMPLETE, 1, 0);
            MP_sendTeamBotMessage(team, BOT_MSG_CANCEL_GOALS, target, 0, sender);
            break;
        case 0x30: case 0x33: case 0x37:
            *teamFlags &= ~1;
            MP_sendBotMessage(obj, BOT_MSG_3C, 1, 0);
            break;
        case 0x38:
            *teamFlags &= ~2;
            MP_sendBotMessage(obj, BOT_MSG_3C, 1, 0);
            break;
        case 0x31: case 0x34:
            *teamFlags &= ~3;
            MP_sendBotMessage(obj, BOT_MSG_GOAL_COMPLETE, 1, 0);
            break;
        case 0x39:
            // nothing for an agent already in the hill
            if (flags & MPPLAYER_IN_HILL) {
                msg = 0;
                break;
            }
            MP_sendBotMessage(obj, BOT_MSG_GOAL_COMPLETE, 1, 0);
            break;
        case 0x3e:
            MP_sendBotMessage(obj, BOT_MSG_GOAL_COMPLETE, 1, 0);
            MP_sendTeamBotMessage(team, BOT_MSG_CANCEL_GOALS, target, 0, sender);
            break;
    }
    MPGame.teamObjectiveFlags[NO_TEAM] = 0;
    if (msg != 0)
        MP_sendBotMessage(obj, msg, 0, 0);
}

// In Assassin, a respawned agent takes over its old object's part: oldObj's player goes back to Phoenix, and newObj
// becomes the target or the assassin (on MI6) in its place. False when it was neither.
// AUTOINJECT
bool MP_changeAssassinOrTarget(obj_tag *oldObj, obj_tag *newObj) {
    if (MPSettings.GameMode != GM_ASSASSIN)
        return false;
    short oldIndex = Control_Plr2Ind(oldObj);
    if (oldIndex >= 0)
        MPSettings.Player[oldIndex].TeamId = PHOENIX;
    short newIndex = Control_Plr2Ind(newObj);
    if (newIndex < 0)
        return false;
    if (AssassinTarget == oldObj) {
        AssassinTarget = newObj;
        return true;
    }
    if (CurrentAssassinObjId == oldObj) {
        CurrentAssassinObjId = newObj;
        MPSettings.Player[newIndex].TeamId = MI6;
        return true;
    }
    return false;
}

// A sound for the whole match, at full volume
// AUTOINJECT
void MPSound_Play(Action_SFX sfx) {
    Sound_PlayExt(sfx, 100.0f, 0, 0);
}

// Damage to the demolition or protection object: its state counts its strength down, and the attacker is noted
// AUTOINJECT
void MP_ApplyDamage(obj_tag *obj, float damage, obj_tag *from) {
    MPOBJECT *mpObj = (MPOBJECT *)obj->extraObjectData;
    if (mpObj->type != DEMOLITION && mpObj->type != PROTECTION)
        return;
    obj->curState = (ushort)Ftol((double)(short)obj->curState - damage);
    if (damage > 0.0f)
        mpObj->attackerIdx = MP_PlayerOrBotInd(from);
}

// Takes a pickup out of the multiplayer slots: the slot and the pickup's own data are cleared
// AUTOINJECT
void MP_UnregisterPickup(obj_tag *pickup) {
    if (!MPSettings.isMultiplayer)
        return;
    if (pickup == NULL || pickup->objectType != OBJECTTYPE_PICKUP)
        return;
    PICKUPINFO *info = (PICKUPINFO *)pickup->extraObjectData;
    int slot = info->mpIndex;
    if (slot == -1)
        return;
    MP_PICKUP *mpPickup = &MPpickups[slot];
    if (mpPickup->gameObj == NULL || mpPickup->gameObj != pickup)
        return;
    if (mpbots.NumBots && mpPickup->aiEmitter.data != NULL)
        AINetwork_FreeEmitter(&mpPickup->aiEmitter);
    memset(mpPickup, 0, sizeof(*mpPickup));
    memset(pickup->extraObjectData, 0, sizeof(PICKUPINFO));
    info->mpIndex = -1;
    MPSettings.numActivePickups--;
    PickupLastDeletedIdx = slot;
}

// Gives a pickup a multiplayer slot, with a path for the bots: the slot last freed, else the next free one from
// where the last was added, else the one with the shortest lifetime left is taken over. Returns the slot, -1 for
// none.
// AUTOINJECT
int MP_RegisterPickup(obj_tag *pickup) {
    int slot = -1;
    if (!MPSettings.isMultiplayer)
        return -1;
    if (pickup == NULL || pickup->objectType != OBJECTTYPE_PICKUP)
        return -1;
    PICKUPINFO *info = (PICKUPINFO *)pickup->extraObjectData;
    if (info->kind == PICKUP_KIND_WEAPON && mpbots.NumBots)
        BOTWEAP_isValidWeapon(info->item);

    if (PickupLastDeletedIdx != -1 && MPpickups[PickupLastDeletedIdx].gameObj == NULL) {
        slot = PickupLastDeletedIdx;
        PickupLastDeletedIdx = -1;
    } else {
        int next = PickupNextAddIndex;
        for (int i = next; i < 64 && slot == -1; i++) {
            if (MPpickups[i].gameObj == NULL)
                slot = i;
        }
        for (int i = 0; i < next && slot == -1; i++) {
            if (MPpickups[i].gameObj == NULL)
                slot = i;
        }
    }

    if (slot == -1) {
        // Kept as the original has it: the slot's pickup is taken over, but the mpIndex set below is that of the
        // last pickup looked at here
        ushort shortest = 0xffff;
        int left = MPSettings.numActivePickups;
        if (left == 0)
            return -1;
        for (int i = 0; left != 0; i++) {
            if (MPpickups[i].gameObj == NULL)
                continue;
            info = (PICKUPINFO *)MPpickups[i].gameObj->extraObjectData;
            if (info->lifetime != 0 && info->lifetime < shortest) {
                shortest = info->lifetime;
                slot = i;
            }
            left--;
        }
        if (slot == -1)
            return -1;
        MP_UnregisterPickup(MPpickups[slot].gameObj);
    }

    MP_PICKUP *mpPickup = &MPpickups[slot];
    mpPickup->gameObj = pickup;
    cel_tag *cel = pickup->inCel;
    if (cel == NULL)
        cel = build_FindCel((_VECTOR *)&pickup->transformMatrix.m[12], glb_world);
    mpPickup->celPos.pos = pickup->position;
    mpPickup->celPos.cel = cel;
    if (cel != NULL && mpbots.NumBots)
        AINetwork_InitEmitter(NULL, (_VECTOR *)&pickup->transformMatrix.m[12], cel, &mpPickup->aiEmitter, NULL);
    info->mpIndex = (short)slot;
    if (IsLevelLoaded() && slot < 64 && mpPickup->gameObj != NULL && IsLevelLoaded() && mpbots.NumBots &&
        mpPickup->aiEmitter.path != NULL)
        AINetwork_EmitPath(&mpPickup->aiEmitter, 0.0f);

    MPSettings.numActivePickups++;
    PickupNextAddIndex = slot < 63 ? slot + 1 : 0;
    return slot;
}

// A scenario object's MP_OBJ_EXT: the object, where it stands, and its path for the bots
void _MP_initObjExt(MP_OBJ_EXT *ext, obj_tag *obj) {
    if (ext == NULL)
        return;
    ext->gameObj = obj;
    ext->celPos.pos = obj->position;
    ext->celPos.cel = obj->inCel;
    if (mpbots.NumBots) {
        if (ext->celPos.cel == NULL)
            ext->celPos.cel = build_FindCel((_VECTOR *)&obj->transformMatrix.m[12], glb_world);
        if (ext->celPos.cel != NULL)
            AINetwork_InitEmitter(NULL, (_VECTOR *)&obj->transformMatrix.m[12], ext->celPos.cel, &ext->aiEmitter, NULL);
    }
    if (IsLevelLoaded() && mpbots.NumBots && ext->aiEmitter.path != NULL)
        AINetwork_EmitPath(&ext->aiEmitter, 0.0f);
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes ext in ECX and obj in EAX
// AUTOLTCG
__declspec(naked) void MP_initObjExt(void) {
    _asm {
        push eax
        push ecx
        call _MP_initObjExt
        add esp, 8
        ret
    }
}
#endif

// ---- the match's agents and objects coming into being

// The skin whose players throw Weap_OddjobHat
#define SKIN_ODDJOB_HAT ((HASHCODE)0x05000085)

// A player's weapons at the start of a match or after respawning: fists, the weapon set's first weapon (as the
// player's upgrades have it) with two clips, the grapple when the rules allow it, Weap_OddjobHat for the skin that
// throws it and a satchel for the side that plants it; then the hands are the weapon in use.
// AUTOINJECT
void MP_EquipPlayer(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    Player_InitAmmoWeapons(player);
    short weapon = (short)Upgrade_MPWeapon(blData->playerNum,
                                           weapon_data[PickupMatrix[MPSettings.weaponSet][0]].weaponBaseNum);
    short loaded = weapon_data[weapon].clipSize * 2;
    Player_EquipWeapon(blData, WEAPON_FISTS, 0);
    Player_EquipWeapon(blData, weapon, loaded);
    if (weapon == 0x1a)
        Player_EquipAmmo(blData, 0x1b, (ushort)weapon_data[0x1b].clipSize);
    if (MPSettings.GrappleEnabled)
        Player_EquipWeapon(blData, (short)Upgrade_MPWeapon(blData->playerNum, 0x50), 0);
    if (player->animState->animObj.skin->hashcode == SKIN_ODDJOB_HAT)
        Player_EquipWeapon(blData, Weap_OddjobHat, 1);

    MPTeam team = MPSettings.Player[blData->playerNum].TeamId;
    if ((MPSettings.GameMode == GM_PROTECTION && team == PHOENIX) ||
        (MPSettings.GameMode == GM_DEMOLITION && team == MI6))
        Player_EquipWeapon(blData, Weap_Satchel, 1);
    Player_CheckWeaponsLoaded(blData);

    player->animState->currentWeaponId = Weap_MaybeHandsOnlyOrLadder;
    player->animState->prevHeldWeaponId = Weap_MaybeHandsOnlyOrLadder;
    player->animState->switchingToWeaponId = (char)weapon;
    blData->weaponObject->curState = 0;
}

// The bots' paths to the pickups and to the scenario objects of the game mode, once the level is loaded. Not in
// the front end's level.
// AUTOINJECT
void MP_PostLoad_Init(void) {
    if (!MPSettings.isMultiplayer)
        return;
    if (GameState.NextLevelHashcode == HT_Level_Menu_Pre)
        return;

    for (int i = 0; i < 64; i++) {
        MP_PICKUP *pickup = &MPpickups[i];
        if (pickup->gameObj != NULL && i < 64 && IsLevelLoaded() && mpbots.NumBots && pickup->aiEmitter.path != NULL)
            AINetwork_EmitPath(&pickup->aiEmitter, 0.0f);
    }

    switch (MPSettings.GameMode) {
        case GM_CTF:
            for (int i = 0; i < 2; i++) {
                if (Flags[i].gameObj != NULL)
                    _MP_PostLoadInitObjExt(&Flags[i]);
            }
            for (int i = 0; i < 2; i++) {
                if (Bases[i].gameObj != NULL)
                    _MP_PostLoadInitObjExt(&Bases[i]);
            }
            break;

        case GM_DEMOLITION:
            if (Demolition.mpObj != NULL)
                Demolition.gameObj = Script_GetObj(SP_getScriptInfo(Demolition.mpObj->scriptPlayer), 0);
            _MP_PostLoadInitObjExt(&Demolition);
            break;

        case GM_PROTECTION:
            if (Protection.mpObj != NULL)
                Protection.gameObj = Script_GetObj(SP_getScriptInfo(Protection.mpObj->scriptPlayer), 0);
            _MP_PostLoadInitObjExt(&Protection);
            break;

        case GM_BLUEPRINT:
            if (BluePrint.gameObj != NULL)
                _MP_PostLoadInitObjExt(&BluePrint);
            for (int i = 0; i < 2; i++) {
                if (EsponageBase[i].gameObj != NULL)
                    _MP_PostLoadInitObjExt(&EsponageBase[i]);
            }
            break;

        case GM_GOLDENEYE:
            for (int i = 0; i < 2; i++) {
                if (GoldenEye.keys[i].gameObj != NULL && GoldenEye.keys[i].gameObj->curState == 0)
                    _MP_PostLoadInitObjExt(&GoldenEye.keys[i]);
            }
            break;

        case GM_KOTH:
        case GM_TEAMKOTH:
            if (Hill.gameObj != NULL)
                _MP_PostLoadInitObjExt(&Hill);
            break;

        case GM_UPLINK:
            for (uint i = 0; i < UplinkCount; i++) {
                if (Uplinks[i].gameObj != NULL)
                    _MP_PostLoadInitObjExt(&Uplinks[i]);
            }
            break;
    }
}

// An agent comes back at a spawn point for its team: a bot's drone is just moved there (BOT_respawn made it new);
// a player gets his health, weapons, camera and states back. False when the match is over, or in Top Agent once his
// deaths reach the points limit.
// AUTOINJECT
bool MP_ReSpawn(obj_tag *obj, ushort playerNum) {
    if (!MPSettings.isMultiplayer || MPGame.EndGameFlowState != 0)
        return false;
    // a signed comparison in the original
    if (MPSettings.GameMode == GM_TOPAGENT && (int)MPGame.players[playerNum].deaths >= (int)MPSettings.MaxPoints)
        return false;

    MPGamePlayer *agent = &MPGame.players[playerNum];
    uint spawn = MP_GetSpawnPoint((ushort)MPSettings.Player[playerNum].TeamId, obj);
    MPSpawnPoint *point = &SpawnPoints[(ushort)spawn];
    Vec_Copy(&point->spawnPos, &obj->position);
    Vec_Copy(&point->spawnPos, &obj->lastPosition);
    Vec_Copy(&point->spawnPos, (_VECTOR *)&obj->transformMatrix.m[12]);
    Vec_Copy(&point->facingDir, &obj->rotation);
    agent->unknown10 = 0;
    agent->flags = 0;
    agent->maybeIdxOfLastInjurer = -2;

    char type = obj->objectType;
    if (type == OBJECTTYPE_DRONE || type == OBJECTTYPE_DEAD_DRONE) {
        Control_GetObjMatrix(obj, &obj->transformMatrix);
        control_unlink_object(obj);
        build_LinkToRoom(obj, 1, glb_world);
        return true;
    }
    if (type != OBJECTTYPE_PLAYER && type != OBJECTTYPE_DEAD_PLAYER)
        return true;

    BLData *blData = (BLData *)obj->extraObjectData;
    HUD_Enable(blData->hudInfo, (HUD_PANE_IND)0xc, 0, 0);
    Player_SetCamMode(blData, 0);
    blData->armor = 0.0f;
    blData->field_0x844 = 0.0f;
    // the handicap is loaded as a signed int
    Player_SetHealth(blData, (float)((double)(int)MPSettings.Player[blData->playerNum].HealthModifier + 100.0f));
    blData->healthShown = blData->health;
    blData->movementDisabled = 0;
    blData->grappleState = 0;
    blData->pitchFromHorizontal = 0.0f;
    MP_EquipPlayer(obj);
    Camera_Set(glb_viewer[blData->playerNum], &obj->position, &obj->rotation);
    Player_ChangeState(obj, 0);
    Player_ChangeSubState(obj, 0);
    obj->effectFlags &= ~0x20;
    obj->objectType = OBJECTTYPE_PLAYER;
    agent->flags = 0;
    Control_GetObjMatrix(obj, &obj->transformMatrix);
    Player_StandAtNewPosition(obj, &obj->position, &obj->rotation, 0);
    AnimObjectUpdate(obj);
    Collide_FreeHitList(&obj->hitList);
    obj->creationTimeFrames = GameState.NumFramesUnpaused;
    return true;
}

// A scenario object from its placement keys (type, number, scripts): in the first free MPObjects slot (in a team
// game the first or second half by its number), with its MP_OBJ_EXT and, with a script, its script player. The
// uplinks start in state 2 and the demolition and protection objects with 2000 strength. NULL when there is no room.
// AUTOINJECT
obj_tag* MP_CreateObject(_MATRIX *mtx, uint *keys, celglist_tag *celgl) {
    ushort type = (ushort)keys[MP_KEY_TYPE];
    ushort num = (ushort)keys[MP_KEY_NUM];
    ushort first = 0, end = 64;
    if ((MPSettings.maybeIsTeamGame || MPSettings.GameMode == GM_ASSASSIN) && num < 2) {
        first = num * 32;
        end = first + 32;
    }
    ushort slot = first;
    while (slot < end && MPObjects[slot] != NULL)
        slot++;
    if (slot >= end)
        return NULL;

    _VECTOR *pos = (_VECTOR *)&mtx->m[12];
    obj_tag *obj = control_create_object(sizeof(MPOBJECT), pos, &CONST_ZERO_VECTOR, NULL);
    MPObjects[slot] = obj;
    if (obj == NULL)
        return NULL;

    Mat_Copy(mtx, &obj->transformMatrix);
    MPOBJECT *mpObj = (MPOBJECT *)MPObjects[slot]->extraObjectData;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl != NULL ? celgl->applyFlagsToObject : 0;
    obj->transformFlags |= 0x44;
    obj->objectType = OBJECTTYPE_MPOBJECT;
    build_LinkToRoom(obj, 0, glb_world);
    mpObj->type = type;
    mpObj->num = num;
    Mat_Copy(&obj->transformMatrix, &mpObj->resetMtx);
    mpObj->attackerIdx = -1;
    mpObj->holderIdx = -1;

    switch (mpObj->type) {
        case CTF_FLAG:
            _MP_initObjExt(&Flags[mpObj->num], obj);
            break;
        case CTF_BASE:
            _MP_initObjExt(&Bases[mpObj->num], obj);
            break;
        case UPLINK:
            _MP_initObjExt(&Uplinks[UplinkCount++], obj);
            obj->curState = 2;
            break;
        case DEMOLITION:
            _MP_initObjExt(&Demolition, obj);
            obj->curState = 2000;
            break;
        case PROTECTION:
            _MP_initObjExt(&Protection, obj);
            obj->curState = 2000;
            break;
        case ESPIONAGEBASE:
            _MP_initObjExt(&EsponageBase[mpObj->num], obj);
            break;
        case BLUEPRINT:
            _MP_initObjExt(&BluePrint, obj);
            break;
        case GOLDENEYE_KEY:
            _MP_initObjExt(&GoldenEye.keys[0], obj);
            obj->subState = 0;
            break;
        case GOLDENEYE_CRYSTAL:
            _MP_initObjExt(&GoldenEye.keys[1], obj);
            obj->subState = 1;
            break;
        case KOH:
            _MP_initObjExt(&Hill, obj);
            break;
    }
    MP_OBJ_EXT *ext = _MP_getObjExtFromMPOBJECT(mpObj);
    if (ext != NULL)
        ext->mpObj = mpObj;

    if (keys[MP_KEY_SCRIPT] != 0) {
        obj->effectFlags |= 0x10;
        _VECTOR rot;
        Mat_GetDir(&rot, mtx);
        vecutil_cartesian_to_spherical_acc(&rot, rot.x, rot.y, rot.z);
        mpObj->scriptPlayer = SP_Create(pos, &rot, (HASHCODE)keys[MP_KEY_SCRIPT], (HASHCODE)keys[MP_KEY_SCRIPT2],
                                        (short)keys[MP_KEY_PLAY_MODE], (char)keys[MP_KEY_PLAY_ONCE], NULL, NULL, NULL,
                                        0);
    }
    return obj;
}

// The places a mode puts its objects at later (MP_Start, MP_RestartScenario)
static void AddPlace(SpawnPlace *place, ushort *count, _VECTOR *pos, _VECTOR *rot, level_tag *lvl,
                     celglist_tag *celgl) {
    place = &place[*count];
    RotMatrix(rot, &place->mtx);
    Matrix_SetTrans(pos, &place->mtx);
    place->celgl = celgl;
    memcpy(place->keys, lvl->keys, sizeof(place->keys));
    (*count)++;
}

// A scenario object placed in the level, for the game mode that uses it: flags, bases, uplinks, the espionage
// bases and the hill are made now (MP_CreateObject); the demolition and protection objects, blueprints and GoldenEye
// parts only have their places noted, up to 8 of each. A protection object's two scripts are swapped in the
// placement. Returns the object made, else NULL.
// AUTOINJECT
obj_tag* MP_RegisterMPObject(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    switch (lvl->keys[MP_KEY_TYPE]) {
        case CTF_FLAG:
        case CTF_BASE:
            if (MPSettings.GameMode != GM_CTF)
                return NULL;
            break;
        case UPLINK:
            if (MPSettings.GameMode != GM_UPLINK || UplinkCount >= 8)
                return NULL;
            break;
        case DEMOLITION:
            if (MPSettings.GameMode != GM_DEMOLITION || DemolitionCount >= 8)
                return NULL;
            AddPlace(DemolitionPlaces, &DemolitionCount, pos, rot, lvl, celgl);
            return NULL;
        case PROTECTION: {
            if (MPSettings.GameMode != GM_PROTECTION || ProtectionCount >= 8)
                return NULL;
            uint script = lvl->keys[MP_KEY_SCRIPT];
            lvl->keys[MP_KEY_SCRIPT] = lvl->keys[MP_KEY_SCRIPT2];
            lvl->keys[MP_KEY_SCRIPT2] = script;
            AddPlace(ProtectionPlaces, &ProtectionCount, pos, rot, lvl, celgl);
            return NULL;
        }
        case ESPIONAGEBASE:
            if (MPSettings.GameMode != GM_BLUEPRINT)
                return NULL;
            break;
        case BLUEPRINT:
            if (MPSettings.GameMode != GM_BLUEPRINT || BluePrintCount >= 8)
                return NULL;
            AddPlace(BluePrints, &BluePrintCount, pos, rot, lvl, celgl);
            return NULL;
        case GOLDENEYE_KEY:
            if (MPSettings.GameMode != GM_GOLDENEYE || GoldenEyeKeyCount >= 8)
                return NULL;
            AddPlace(GoldenEyeSpawns, &GoldenEyeKeyCount, pos, rot, lvl, celgl);
            return NULL;
        case GOLDENEYE_CRYSTAL:
            if (MPSettings.GameMode != GM_GOLDENEYE || GoldenEyeNonKeyCount >= 8)
                return NULL;
            AddPlace(&GoldenEyeSpawns[8], &GoldenEyeNonKeyCount, pos, rot, lvl, celgl);
            return NULL;
        case KOH:
            if (MPSettings.GameMode != GM_KOTH && MPSettings.GameMode != GM_TEAMKOTH)
                return NULL;
            break;
        default:
            return NULL;
    }

    // its w column is uninitialised stack in the original
    _MATRIX mtx;
    memset(&mtx, 0, sizeof(mtx));
    RotMatrix(rot, &mtx);
    Matrix_SetTrans(pos, &mtx);
    return MP_CreateObject(&mtx, lvl->keys, celgl);
}
