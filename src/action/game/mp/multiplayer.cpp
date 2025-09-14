#include "multiplayer.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include <stdio.h>
#include <string.h>

#define CurrentAssassinObjId (((obj_tag *)0x0026178c))
#define AssassinTarget (((obj_tag *)0x00261788))
// AUTOGEN
unsigned int Control_Plr2Ind(obj_tag* a);

#define NUM_SKINS 29 // unique characters
#define MP_skins ((MP_skin*)0x001637c0)

#define mpbots (*(MPBOTS*)0x00245280)

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

#define SpawnPntCount U32_AT(0x00262f38)
#define SpawnPntTeamCount (*(uint32_t(*)[3])0x00262968)

// AUTOGEN
cel_tag* build_FindCel(_VECTOR *position, world_tag *world);



// AUTOINJECT
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection) {
  
  _VECTOR defaultDirection = {0.0f, -1.0f, 0.0f};
  if(searchDirection == NULL)
    searchDirection = &defaultDirection;

  _VECTOR endPosition = {
    .x = position->x + searchDirection->x * distance,
    .y = position->y + searchDirection->y * distance,
    .z = position->z + searchDirection->z * distance,
  };

  HITDATA_tag* hitList = NULL;

  bool intersects = Collide_RayIntersect(position, &endPosition, cel, obj, NULL, &hitList, 0, 0x70c, 0);

  if(intersects) {
    Vec_Copy(&(hitList->hitPosition), position);
    Collide_FreeHitList(&hitList);
  }

  return intersects;
}

// AUTOINJECT
void MP_RegisterSpawnPoint(_VECTOR *position, _VECTOR *facingDirection, ushort teamId) {

  if(teamId == MPTeam::NO_TEAM)
    return;

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

void MP_CleanupMPObjExt(MP_OBJ_EXT *mp_obj) {
  if(mp_obj == NULL)
    return;
    
  if(mpbots.NumBots && (mp_obj->aiEmitter).someDataPtr != NULL) {
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
#define GoldenEye (*(MP_OBJ_EXT*)0x00261678)
#define GoldenEye_Crystal (*(MP_OBJ_EXT*)0x002616bc)
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
      if(GoldenEye.gameObj == obj) {
        MP_CleanupMPObjExt(&GoldenEye);
        dispatchBotMessage = true;
      }
      if(GoldenEye_Crystal.gameObj == obj) {
        MP_CleanupMPObjExt(&GoldenEye_Crystal);
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
      MPObjects[i] == NULL;
      dispatchBotMessage = false;
    }
  }

  // If required, dispatch a bot message to inform them of the deletion
  if(dispatchBotMessage) {
    MsgObject msg;
    msg.createdFrame = GameState.NumFramesUnpaused;
    msg.handleOnFrame = GameState.NumFramesUnpaused;
    msg.msgType = 0x3d;
    msg.param_a = 0xc5;
    msg.param_b = 0;
    msg.param_c = 0;
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





// AUTOGEN
void MP_Update(void);