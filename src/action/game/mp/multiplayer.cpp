#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../obj/car.h"
#include "../obj/GT.h"
#include "../obj/GunImp.h"
#include "../drone/BOT.h"
#include <stdio.h>
#include <string.h>

#define CurrentAssassinObjId (*(obj_tag **)0x0026178c)
#define AssassinTarget (*(obj_tag **)0x00261788)
// AUTOGEN
short Control_Plr2Ind(obj_tag* a);

#define NUM_SKINS 29 // unique characters
// XBE_GLOBAL(0x001637c0, 0x1d0)
#define MP_skins ((MP_skin*)0x001637c0)


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

// AUTOGEN
obj_tag* MP_RegisterMPObject(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

void MP_CleanupMPObjExt(MP_OBJ_EXT *mp_obj) {
  if(mp_obj == NULL)
    return;
    
  if(mpbots.NumBots && (mp_obj->aiEmitter).data != NULL) {
    AINetwork_FreeEmitter(&mp_obj->aiEmitter);
  }

  memset(mp_obj, 0, sizeof(MP_OBJ_EXT));

}

#define UplinkCount U16_AT(0x002637c8)
#define Bases (*(MP_OBJ_EXT(*)[2])0x00261bd0)
#define Uplinks (*(MP_OBJ_EXT(*)[8])0x002633d8)
#define Flags (*(MP_OBJ_EXT(*)[2])0x00263740)
#define Hill (*(MP_OBJ_EXT*)0x00261b88)
#define EsponageBase (*(MP_OBJ_EXT(*)[2])0x00261a70)
#define GoldenEye (*(GoldenEyeStruct(*))0x00261678)
#define Demolition (*(MP_OBJ_EXT*)0x00261af8)
#define Protection (*(MP_OBJ_EXT*)0x00261b40)
#define BluePrint (*(MP_OBJ_EXT*)0x002635f8)

#define MPObjects (*(obj_tag*(*)[64])0x00263640)

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
obj_tag* MP_getFlagObj(uint i) {
  if(i >= ARRAY_SIZE(Flags))
    return NULL;
  return Flags[i].gameObj;
}

// AUTOINJECT
obj_tag* MP_getBaseObj(uint i) {
  if(i >= ARRAY_SIZE(Bases))
    return NULL;
  return Bases[i].gameObj;
}

// AUTOINJECT
obj_tag* MP_getDemolitionObj(void) {
  return Demolition.gameObj;
}

// AUTOINJECT
obj_tag* MP_getProtectionObj(void) {
  return Protection.gameObj;
}

// AUTOINJECT
obj_tag* MP_getHillObj(void) {
  return Hill.gameObj;
}

// UNINJECTABLE - custom calling convention
MP_OBJ_EXT* MP_getObjExtFromMPOBJECT(MPOBJECT *mpObj) {

  switch(mpObj->type) {
    case CTF_FLAG:
      return &Flags[mpObj->num];
    case CTF_BASE:
      return &Bases[mpObj->num];
    case UPLINK:
      for(int i = 0; i < ARRAY_SIZE(Uplinks); i++) {
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

// AUTOINJECT
short MP_PlayerOrBotInd(obj_tag *obj) {
  
  if(obj == NULL)
    return -1;

  if(!MPSettings.maybeDroneAIEnabled) // Unclear why this is needed
    return -1; 

  // Search through MPGame player list
  for(int i = 0; i < 10; i++) {
    if(MPGame.players[i].playerObj == obj)
      return i;
  }

  return -1;
}

#define StatusSpr (*(sprite **)0x002637dc)

static void ShowTopAgentResult(sprite *status) {
    int fewestDeaths = 30000;
    int eligibleCountSinceTie = 0;

    for (int slot = 0; slot < ARRAY_SIZE(MPGame.players); ++slot) {
        const MPGamePlayer &player = MPGame.players[slot];
        int deaths = static_cast<int32_t>(player.deaths);
        int eliminationLimit = static_cast<int32_t>(MPSettings.MaxPoints);

        if (player.playerObj == nullptr || deaths >= eliminationLimit)
            continue;

        ++eligibleCountSinceTie;
        if (deaths < fewestDeaths) {
            const char *winnerFormat = Txt_BindLabel(MP_RESULT_TOP_AGENT, 0);
            sprintf(status->text, winnerFormat, MPSettings.Player[slot].Name);
            fewestDeaths = deaths;
        } else if (deaths == fewestDeaths) {
            // The original resets on a tie, but counts later, worse survivors again.
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
    // The original compares Phoenix's score to itself: numbers draw; NaN selects MI6.
    bool scoreIsNaN = phoenixScore != phoenixScore;

    (void)Txt_BindLabel(MP_PRESS_START, 0); // Original fetches it, but the format never consumes it.
    const char *result = Txt_BindLabel(scoreIsNaN ? MP_RESULT_MI6 : MP_RESULT_DRAW, 0);
    const char *heading = Txt_BindLabel(MP_RESULT_HEADING, 0);
    sprintf(status->text, "%s%s", heading, result);
}

static void ShowScoreLimitResult(sprite *status) {
    int winnerSlot = -1;
    int playersAtLimit = 0;
    // Match x87's exact signed-integer load; float would round large limits.
    double scoreLimit = static_cast<int32_t>(MPSettings.MaxPoints);

    for (int slot = 0; slot < ARRAY_SIZE(MPGame.players); ++slot) {
        if (static_cast<double>(MPGame.players[slot].points) >= scoreLimit) {
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
    sprite *status = StatusSpr;
    MPGame.EndGameFlowState = 3;

    if (MPSettings.GameMode == GM_TOPAGENT) {
        ShowTopAgentResult(status);
    } else if (MPSettings.maybeIsTeamGame) {
        ShowTeamResult(status);
        return; // Original team path skips the sprite refresh.
    } else {
        ShowScoreLimitResult(status);
    }

    Sprite_SetText(status, status->text);
}
// AUTOGEN
void MP_Pickup_Process(void);
// AUTOGEN
void MP_CheckForEndCondition(void);
// AUTOGEN
obj_tag* MP_CreateObject(_MATRIX *mtx, uint* data, celglist_tag *celgl);
// AUTOGEN
bool MP_ReSpawn(obj_tag* obj, ushort idx);

#define DemolitionPlaces (*(SpawnPlace(*)[8])(0x00262f40))
#define DemolitionCount U16_AT(0x002637cc)
#define ProtectionPlaces (*(SpawnPlace(*)[8])(0x00261790))
#define ProtectionCount U16_AT(0x002637d0)

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
    MP_CreateObject(&DemolitionPlaces[idx].mtx, &DemolitionPlaces[idx].maybePlacementData, DemolitionPlaces[idx].celgl);
    MPGame.TimeUnpaused = 0;
  }

  if(MPSettings.GameMode == GM_PROTECTION) {
    int idx = Rand_Rand(ProtectionCount);
    MP_CreateObject(&ProtectionPlaces[idx].mtx, &ProtectionPlaces[idx].maybePlacementData, ProtectionPlaces[idx].celgl);
    MPGame.TimeUnpaused = 0;
  }

  for(int i = 0; i < MPSettings.numPlayers; i++) {
    obj_tag* plyObj = MPGame.players[i].playerObj;
    if(plyObj != NULL)
      MP_ReSpawn(plyObj, i);
  }

  for(int i = 4; i < 10; i++) { // TODO: Hardcoded bounds
    obj_tag *botObj = MPGame.players[i].playerObj;
    if(botObj != NULL)
      BOT_respawn(botObj, i, false);
  }
  
  MPGame.unknown_maybe_capture_state = 0;
  MPGame.unknown_maybe_unused = 0;
  
  for(int i = 0; i < 10; i++) {
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
      for(int i = 0; i < 4; i++) { // TODO: make a define for the number of human players?
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

// AUTOGEN
void __stdcall Pickup_MakeRandomWeaponSet(void);

// One multiplayer weapon/ammo pickup slot (Ghidra's MP_PICKUP; 64 of them, 0x1600 bytes, from this clear)
#pragma pack(push, 1)
typedef struct {
    obj_tag *gameObj;
    CelPos_tag celPos;                  // 0x04
    AIEmitter_tag aiEmitter;            // 0x14 so bots can path to it
    float maybeBotPickupVisitTimes[6];  // 0x3c one per bot: MP_Pickup_Process ages all 6, MP_ResetBotPickupTimes
                                        //      and BOTSTATE_pickGoal index it by bot
    uint32_t unknown_0x54;              // 0x54 no reference in the XBE (MP_Init's clear aside)
} MP_PICKUP;
#pragma pack(pop)
static_assert(sizeof(MP_PICKUP) == 0x58, "MP_PICKUP is wrong size");
static_assert(offsetof(MP_PICKUP, aiEmitter) == 0x14, "Bad offset of MP_PICKUP.aiEmitter");

#define MPpickups (*(MP_PICKUP(*)[64])0x00260078)
#define PickupNextAddIndex U32_AT(0x0025fe30)
#define PickupLastDeletedIdx (*(int*)0x00261b84) // -1 = none
#define BluePrints (*(SpawnPlace(*)[8])0x00262458)
#define BluePrintCount U16_AT(0x002637d4)
#define GoldenEyeSpawns (*(SpawnPlace(*)[16])0x00262978)
#define GoldenEyeKeyCount U16_AT(0x00262970)
#define GoldenEyeNonKeyCount U16_AT(0x00262972)

// Game mode bits (see MultiplayerGameMode): bit 29 is set by every team mode, bit 30 by KOTH, team KOTH and
// uplink.
#define GM_BIT_TEAMGAME 29
#define GM_BIT_30 30

// The time limit demolition and protection fall back to when MaxDuration is negative, in seconds
#define MP_DEFAULT_OBJECTIVE_TIME_LIMIT 60.0f

// Player slots in MPGame.players[] (10 on Xbox)
#define MP_GAME_PLAYER_SLOTS 10

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

    for (int i = 0; i < MP_GAME_PLAYER_SLOTS; i++) {
        MPGame.players[i].maybeIdxOfLastInjurer = MP_INJURER_NONE;
        MPGame.players[i].maybeIdxOfMyAssassin = MP_ASSASSIN_NONE;
        // Top Agent starts everyone on the points limit
        if (gameMode == GM_TOPAGENT)
            MPGame.players[i].points = (float)(int)MPSettings.MaxPoints;
    }
}
