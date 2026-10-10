#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../obj/car.h"
#include "../obj/GT.h"
#include "../obj/GunImp.h"
#include "../drone/BOT.h"
#include <stdio.h>
#include <string.h>
#include "../../../driving/platform/X87.h" // Ftol

// AUTOGEN
short Control_Plr2Ind(obj_tag* a);
// AUTOGEN
bool MPDrone_MaybeIsDyingOrDead(obj_tag *obj);
// AUTOGEN
char* Timer_Seconds2String(int timeInHundredths, undefined4 timeFormat);
// The original's, through Player.cpp's declaration
obj_tag* Player_Init(ushort playerNum, _VECTOR *pos, _VECTOR *rot, level_tag *spawnPointData);
#define BOT_init ((obj_tag* (__cdecl *)(short, _VECTOR *, _VECTOR *, obj_tag *, MPBOT *, char))0x0001b170)

#define NUM_SKINS 29 // unique characters


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

            float distance = Vec_SqDist3D(&SpawnPoints[i].spawnPos, &player->position);
            if (distance < nearestPlayerDistance)
                nearestPlayerDistance = distance;
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

// AUTOGEN
void __stdcall Pickup_MakeRandomWeaponSet(void);

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

#define switch_MP4EVER U32_AT(0x001dfa14)

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
    MP_RADAR_OBJECT &blip = MPRadarObjects[*count];
    Vec_Copy(pos, &blip.pos);
    blip.type = type;
    blip.colour = colour;
    (*count)++;
}

// The radar's blips for viewer: every other agent still in play, then the game mode's objects. None when the
// viewer's per-player setting at +0x28 is 0.
// AUTOINJECT
ushort MP_GetRadarObjects(obj_tag *viewer, MP_RADAR_OBJECT **objects) {
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

#define AssassinSpriteInfo (*(SpriteInfo *)0x0017e5ac)
#define MPTimeInfo (*(SpriteInfo *)0x0017e554)
#define MPStatusInfo (*(SpriteInfo *)0x0017e580)

// The assassin sprites' textures (names ours)
#define TEX_MP_ASSASSIN_180 ((HASHCODE)0x03000180)
#define TEX_MP_ASSASSIN_189 ((HASHCODE)0x03000189)

static void CreateAt(SpawnPlace *place) {
    MP_CreateObject(&place->mtx, &place->maybePlacementData, place->celgl);
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

    memset(MPGame.radar_related, 0, sizeof(MPGame.radar_related));
    if (MPSettings.GameMode == GM_ASSASSIN) {
        MP_assassinReset(false);
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (MPGame.players[i].playerObj == NULL)
                continue;
            AssassinSpriteInfo.linkedViewer = 5;
            AssassinSpriteInfo.textureHashcode = TEX_MP_ASSASSIN_180;
            MPGame.radar_related[2 * i + 1] = Sprite_Create2(&AssassinSpriteInfo);
            AssassinSpriteInfo.textureHashcode = TEX_MP_ASSASSIN_189;
            MPGame.radar_related[2 * i] = Sprite_Create2(&AssassinSpriteInfo);
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
