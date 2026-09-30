#include "Mission.h"
#include "SwitchChannels.h"
#include "../mp/multiplayer.h"
#include "../../sound/music.h"
#include "../../ui/Menu.h"
#include "../../ui/MenuManager.h"
#include "../../game/sp/PlayerStats.h"

#include <stdio.h>

// XBE_GLOBAL(0x0017e548, 0x4)
static uint32_t FailLabel = 0xffffffff;
// XBE_GLOBAL(0x0025fe20, 0x4)
static uint32_t BaseMap;
// XBE_GLOBAL(0x0025fe24, 0x4)
static uint32_t ThisOrderNum;
// XBE_GLOBAL(0x0017e544, 0x4)
static uint32_t MissionState = 0x5;

// The objectives each mission part points at: still the game's own arrays (their status is written at run time).
// XBE_GLOBAL(0x0017dc58, 0x18)
#define Objectives_1_CastleExterior ((Objective*)0x0017dc58) // 1 objective(s)
// XBE_GLOBAL(0x0017dc70, 0x18)
#define Objectives_1_CastleCourtyard ((Objective*)0x0017dc70) // 1 objective(s)
// XBE_GLOBAL(0x0017dc88, 0x30)
#define Objectives_2_CastleIndoors ((Objective*)0x0017dc88) // 2 objective(s)
// XBE_GLOBAL(0x0017dcb8, 0x30)
#define Objectives_2_CastleIndoors2 ((Objective*)0x0017dcb8) // 2 objective(s)
// XBE_GLOBAL(0x0017dce8, 0x18)
#define Objectives_1_HendersonA ((Objective*)0x0017dce8) // 1 objective(s)
// XBE_GLOBAL(0x0017dd00, 0x48)
#define Objectives_3_HendersonB ((Objective*)0x0017dd00) // 3 objective(s)
// XBE_GLOBAL(0x0017dd48, 0x48)
#define Objectives_3_HendersonC ((Objective*)0x0017dd48) // 3 objective(s)
// XBE_GLOBAL(0x0017dd90, 0x18)
#define Objectives_1_HendersonD ((Objective*)0x0017dd90) // 1 objective(s)
// XBE_GLOBAL(0x0017dda8, 0x48)
#define Objectives_3_TowerA ((Objective*)0x0017dda8) // 3 objective(s)
// XBE_GLOBAL(0x0017ddf0, 0x48)
#define Objectives_3_TowerB ((Objective*)0x0017ddf0) // 3 objective(s)
// XBE_GLOBAL(0x0017de38, 0x78)
#define Objectives_5_TowerC ((Objective*)0x0017de38) // 5 objective(s)
// XBE_GLOBAL(0x0017deb0, 0x48)
#define Objectives_3_PowerStationA1 ((Objective*)0x0017deb0) // 3 objective(s)
// XBE_GLOBAL(0x0017def8, 0x48)
#define Objectives_3_PowerStationA2 ((Objective*)0x0017def8) // 3 objective(s)
// XBE_GLOBAL(0x0017df40, 0x60)
#define PowerStation_B1 ((Objective*)0x0017df40) // 4 objective(s)
// XBE_GLOBAL(0x0017dfa0, 0x18)
#define PowerStation_B2 ((Objective*)0x0017dfa0) // 1 objective(s)
// XBE_GLOBAL(0x0017dfb8, 0x30)
#define Tower_2A ((Objective*)0x0017dfb8) // 2 objective(s)
// XBE_GLOBAL(0x0017dfe8, 0x30)
#define Tower_2B ((Objective*)0x0017dfe8) // 2 objective(s)
// XBE_GLOBAL(0x0017e018, 0x18)
#define Tower_2Elevator ((Objective*)0x0017e018) // 1 objective(s)
// XBE_GLOBAL(0x0017e030, 0x18)
#define Tower_2C ((Objective*)0x0017e030) // 1 objective(s)
// XBE_GLOBAL(0x0017e048, 0x48)
#define Caves ((Objective*)0x0017e048) // 3 objective(s)
// XBE_GLOBAL(0x0017e090, 0x78)
#define Evil_Base ((Objective*)0x0017e090) // 5 objective(s)
// XBE_GLOBAL(0x0017e108, 0x18)
#define Evil_Silo ((Objective*)0x0017e108) // 1 objective(s)
// XBE_GLOBAL(0x0017e120, 0x30)
#define EvilBase_ShuttleBay ((Objective*)0x0017e120) // 2 objective(s)
// XBE_GLOBAL(0x0017e150, 0x30)
#define Space_Station_D ((Objective*)0x0017e150) // 2 objective(s)

// XBE_GLOBAL(0x0017e180, 0x3c0)
static Mission MissionData[24] = {
    {HT_Level_CastleExterior, HT_Level_CastleExterior, 0, Objectives_1_CastleExterior, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_CastleCourtyard, HT_Level_CastleExterior, 1, Objectives_1_CastleCourtyard, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_CastleIndoors1, HT_Level_CastleExterior, 2, Objectives_2_CastleIndoors, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_CastleIndoors2, HT_Level_CastleExterior, 3, Objectives_2_CastleIndoors2, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonA, HT_Level_HendersonA, 0, Objectives_1_HendersonA, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonB, HT_Level_HendersonA, 1, Objectives_3_HendersonB, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonC, HT_Level_HendersonA, 2, Objectives_3_HendersonC, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonD, HT_Level_HendersonA, 3, Objectives_1_HendersonD, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_TowerA, HT_Level_TowerA, 0, Objectives_3_TowerA, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_TowerB, HT_Level_TowerA, 1, Objectives_3_TowerB, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_TowerC, HT_Level_TowerA, 2, Objectives_5_TowerC, 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}},
    {HT_Level_PowerStationA1, HT_Level_PowerStationA1, 0, Objectives_3_PowerStationA1, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_PowerStationA2, HT_Level_PowerStationA1, 1, Objectives_3_PowerStationA2, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level1, HT_Level_Cut_Level1, 0, PowerStation_B1, 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level2, HT_Level_Cut_Level1, 1, PowerStation_B2, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2A, HT_Level_Tower2A, 0, Tower_2A, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2B, HT_Level_Tower2A, 1, Tower_2B, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2Elevator, HT_Level_Tower2A, 2, Tower_2Elevator, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2C, HT_Level_Tower2A, 3, Tower_2C, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level3, HT_Level_Cut_Level3, 0, Caves, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_EvilBase, HT_Level_EvilBase, 0, Evil_Base, 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_EvilSilo, HT_Level_EvilBase, 1, Evil_Silo, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_EvilBaseC, HT_Level_EvilBase, 2, EvilBase_ShuttleBay, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_SpaceStationD, HT_Level_SpaceStationD, 0, Space_Station_D, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};


// AUTOINJECT
Action_TranslatedText Mission_FailLabel(void) {
    return (Action_TranslatedText)FailLabel;
}

// AUTOINJECT
void Mission_SetFailLabel(Action_TranslatedText text) {
    FailLabel = text;
}

// AUTOINJECT
void Mission_SetMapHCode(HASHCODE param_1) {
  BaseMap = param_1;
}

// AUTOINJECT
HASHCODE Mission_BaseMapHCode(void) {
  if(!MPSettings.isMultiplayer) {
    return (HASHCODE)BaseMap;
  }
  return GameState.CurrentLevelHashcode;
}

// AUTOINJECT
void Mission_SetStatus(undefined4 param_1) {
  MissionState = param_1;
}

// AUTOINJECT
undefined4 Mission_Status(void) {
  return MissionState;
}

// AUTOINJECT
int Mission_NumVisObjectives(void) {

    int count = 0;

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we reach a later mission then we are done
            return count;

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            int objState = MissionData[i].objectives[j].status;

            if(objState != 0 && objState != 1)
                count++;
        }

        
    }

    return count;
}


// AUTOINJECT
void Mission_ObjectiveState(OBJ_STATE *state, short objectiveNum) {

    int objWithinLevel = 0;

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we haven't found it then the index is bad somehow
            return;

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            Objective *o = &MissionData[i].objectives[j];

            if(o->status != 0 && o->status != 1) {
                if(objWithinLevel == objectiveNum) {
                    state->statusText = (
                        (o->status == 2) ? 0x2000001 :
                        (o->status == 3) ? 0x2000000 :
                        (o->status == 4) ? 0x16f :
                        0x170 );
                    state->name = o->name;
                    state->description = o->description;
                    state->status = o->status;
                    return;
                }
                objWithinLevel++;
            }
        }


    }

}

// Hard-coded switch channels (docs/switch-channels.md)
#define MissionFailConditionHit switch_channels[0x63]
#define MissionWinConditionHit switch_channels[0x64]
#define MissionFailTime switch_channels_time[0x63]
#define MissionWinTime switch_channels_time[0x64]

#define FailedDueToAlarm switch_channels[0x61]
#define FailedDueToKilledCivilian switch_channels[0x60]
#define FailedDueToKikoEscape switch_channels[0x7b]
#define FailedDueToMissileLaunch switch_channels[0xc7]

// AUTOINJECT
void Mission_MonitorObjectives(void) {

    bool allObjectivesCompleted = true;

    // Evaluate objectives and send notifications
    for(int i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        bool fromPreviousPart = (ThisOrderNum <= MissionData[i].idxInOrder);

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {

            Objective *o = &MissionData[i].objectives[j];
            char* str = Txt_GetStringFromHeap(0);

            bool isFailCondition = (o->someFlags & 0x2); // If set, the objective is a fail condition, otherwise it's a precondition for success

            bool thisObjectiveMet = switch_channels[o->completedChannel] == (isFailCondition ? false : true);
            allObjectivesCompleted = allObjectivesCompleted && thisObjectiveMet;

            switch(o->status) {
                case 0:
                    // On entering a new level, show ones from the previous hashcode? Mayhew part 1->2 reveals that escorting is complete?
                    if(MissionData[i].level <= GameState.CurrentLevelHashcode) { //??? Guessing at intent
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 300);
                        o->status = isFailCondition ? 2 : 3;
                    }
                    break;
                case 1:
                    // New objective revealed 
                    if (((o->revealedChannel != 0) && (o->revealedChannel != 0xff)) && (switch_channels[o->revealedChannel] != '\0')) {
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 300);
                        o->status = isFailCondition ? 2 : 3;
                    }
                    break;
                case 2:
                    // Triggered a failure
                    if (!thisObjectiveMet) {
                        MissionFailConditionHit = true;
                        MissionFailTime = GameState.NumFramesUnpaused;
                        sprintf(str,"%s: %s",Txt_BindLabel((Action_TranslatedText)0x170, 0), Txt_BindLabel(o->name, 0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 180);
                        o->status = 5;
                    }
                    break;
                case 3:
                    // Completed objective within this level of the mission
                    if(thisObjectiveMet) {
                        sprintf(str, "%s", Txt_BindLabel(TXT_NOTIF_OBJECTIVE_COMPLETE, 0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 180);
                        o->status = 4;
                    }
                    break;

                case 5:
                    if(o->failLabel != 0xFFFFFFFF)
                        FailLabel = o->failLabel;
                    break;

                case 4:
                default:
                    // Label has been created, no need to do anything more
                    break;

                
            }

        }
        
    }

    // Check conditions for mission failure or success

    // Mission failure - Alarm triggered
    if(FailedDueToAlarm) {
        FailLabel = TXT_MISSION_FAIL_ALARM_TRIGGERED;
        MissionFailConditionHit = true;
        MissionFailTime = GameState.NumFramesUnpaused;
        return;
    }

    // Mission failure - Civilian was killed
    if(FailedDueToKilledCivilian) {
        FailLabel = TXT_MISSION_FAIL_KILLED_CIVILIAN;
        MissionFailConditionHit = true;
        MissionFailTime = GameState.NumFramesUnpaused;
        return;
    }

    // Mission failure - Kiko escaped in Evil Base C
    if(FailedDueToKikoEscape && GameState.CurrentLevelHashcode == HT_Level_EvilBaseC) {
        FailLabel = TXT_MISSION_FAIL_KIKO_ESCAPE;
        MissionFailConditionHit = true;
        MissionFailTime = GameState.NumFramesUnpaused;
        return;
    }

    // Mission failure - Missile launched in Space Station D
    if(FailedDueToMissileLaunch && GameState.CurrentLevelHashcode == HT_Level_SpaceStationD) {
        FailLabel = TXT_MISSION_FAIL_MISSILE_LAUNCHED;
        MissionFailConditionHit = true;
        MissionFailTime = GameState.NumFramesUnpaused;
        return;
    }

    // Mission success
    if(allObjectivesCompleted) {
        MissionWinConditionHit = 1;
        MissionWinTime = GameState.NumFramesUnpaused;
        Music_Event(8,1);
        printf("--- Completed mission 0x%08x in %i frames\n", GameState.CurrentLevelHashcode, GameState.NumFramesUnpaused);
        return;
    }

}

// Helper function, inlined or didn't exist in the original code but definitely more readable with this pulled out
HASHCODE Mission_GetEndTo(HASHCODE level) {
    switch(level) {
        default:
            return HT_Level_Menu_Pre;

        case HT_Level_HendersonD:
            return FMV_OUTRO_MAYHEW_DEAD;

        case HT_Level_CastleIndoors2:
            return FMV_OUTRO_HELICOPTER_CRASH;

        case HT_Level_TowerC:
            return FMV_OUTRO_PARACHUTE_OFF_TOWER;

        case HT_Level_PowerStationA2:
            return FMV_OUTRO_THROUGH_VENT_KIKO;

        case HT_Level_Tower2C:
            return FMV_OUTRO_LOBBY_ESCAPE;
        
        case HT_Level_EvilBaseC:
            return FMV_OUTRO_KIKO_ROCKETLAUNCH;

        case HT_Level_SpaceStationD:
            return FMV_OUTRO_ESCAPE_POD_END_GAME;

        case 0x700000f: // Removed level?
            return (HASHCODE)0x730000f;
    }
}

// XBE_GLOBAL(0x0017e540, 0x4)
static uint32_t InternalState = 0x5;
// XBE_GLOBAL(0x0017e550, 0x4)
static uint32_t FadeClr_147 = 0xff;
// XBE_GLOBAL(0x0025fe28, 0x4)
static float TimeOut_148;
#define PlayerHasFinishedDying switch_channels[0x62]
// XBE_GLOBAL(0x0017e54c, 0x4)
static HASHCODE LevelToEndTo = HT_Level_Menu_Pre;

// AUTOINJECT
void Mission_Update(void) {
    
    if(GameState.CurrentLevelHashcode == HT_Level_Menu_Pre)
        return;

    if(MPSettings.isMultiplayer)
        return;

    switch(InternalState) {
        case 1: // Normal running

            if(PlayerHasFinishedDying) {

                // Stop music and show fail screen
                Music_Event(6,1);
                InternalState = 3;
                FadeClr_147 = 0xff0000ff;

                if (glb_players[0] != NULL)
                    Player_SetHealth((BLData*)glb_players[0]->extraObjectData, 0.0f);

                return;

            } else {

                // Check for mission failure or success
                Mission_MonitorObjectives();

                if (MissionFailConditionHit) {
                    Music_Event(7,1);
                    InternalState = 2;
                    return;
                }
                
                if (MissionWinConditionHit) {
                    Music_Event(8,1);
                    InternalState = 6;
                    FadeClr_147 = 0xff;
                    return;
                }
                

            }

            break;

        case 2: // Fail screen
            GameState.maybePaused = true;
            MissionState = 2;
            LevelToEndTo = HT_Level_Menu_Pre;
            InternalState = 7;
            Text_AddMsg(0, 0, 3, (char*)Txt_BindLabel(TXT_MISSION_FAIL, 0), 0, 4 * VIDEO_FRAME_RATE); // 4 seconds
            TimeOut_148 = 5 * VIDEO_FRAME_RATE; // ?

            break;

        case 3: // Quit mission
            TimeOut_148 = 1.0f;
            GameState.maybePaused = true;
            MissionState = 3;
            InternalState = 7;
            LevelToEndTo = HT_Level_Menu_Pre;
            break;

        case 4: // TimeOut expired - either quit to main menu, show results screen, or the "try/quit" screens?
            if(MissionState == 6) {
                int lVar3 = Menu_GetLevelIndex(Mission_BaseMapHCode());
                int lVar4 = Menu_GetLevelIndex((HASHCODE)GameState.BaseMapHashCode);
                if (lVar4 < lVar3) {
                  GameState.BaseMapHashCode = GameState.CurrentLevelHashcode;
                }
                GameState.ReloadMenupage = P_NFRESULTS;
                ResetMap_LevelToLoad(LevelToEndTo, 0, 0);
                GameFlow_PushState(7, 60.0, FadeClr_147);
                InternalState = 5;
            } else { // Show retry/quit menu
                MenuManager_Create(0x80000004, 0x40000042, 0, '\0', 40, 0, 4);
                GS_PauseGame(true);
                InternalState = 5;
            }

        break;

        case 5: // Awaiting game flow to change state / menu actions, don't need to do anything
            break;

        case 6: // Mission complete, exit to FMV / main menu
            MissionState = 6;
            InternalState = 7;
            GameState.maybePaused = true;
            LevelToEndTo = Mission_GetEndTo(GameState.CurrentLevelHashcode);
            Text_AddMsg(0, 0, 3, (char*)Txt_BindLabel(TXT_MISSION_COMPLETE, 0), 0, 4 * (short)VIDEO_FRAME_RATE); // 4 seconds
            TimeOut_148 = 4 * VIDEO_FRAME_RATE;
            break;
        
        case 7: // Await timeout
            GS_PausePlayer(1, 0);
            TimeOut_148 -= FRAME_RATE_MUL;
            if (TimeOut_148 <= 0) {
                GS_PauseGame(true);
                InternalState = 4;
            }
            break;

        default:
            InternalState = 1;
            break;
    }


}



// AUTOINJECT
void Mission_Init(HASHCODE hashcode, short warmReset) {

    LevelToEndTo = HT_Level_Menu_Pre;

    if(hashcode == HT_Level_Menu_Pre)
        return;

    if(!warmReset)
        BaseMap = 0xFFFFFFFF;
    
    FailLabel = 0xFFFFFFFF;
    ThisOrderNum = 0xFFFFFFFF;
    InternalState = 1;
    Mission_SetStatus(1);
    GS_PauseGame(false);

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].level == hashcode) {
            BaseMap = MissionData[i].baseLevel;
            ThisOrderNum = MissionData[i].idxInOrder;
            break;
        }
    }

    if(BaseMap == 0xFFFFFFFF) {
        InternalState = 5;
        Mission_SetStatus(5);
        return;
    }

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].level == BaseMap) {
            if(MissionData[i].level == hashcode) { // If we're (re)starting from the start of the mission, reset all objectives
                PlrStat_ResetForMission();
            }
            break; // Otherwise, we don't need to do anything
        }
    }


    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        int someStateThing;
        if(MissionData[i].idxInOrder < ThisOrderNum) {
            someStateThing = warmReset ? 4 : 1;
        } else if (MissionData[i].idxInOrder == ThisOrderNum) {
            someStateThing = 1;
        } else {
            someStateThing = 0;
        }

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            Objective* o = &MissionData[i].objectives[j];
            if((someStateThing == 0) || (someStateThing == 2)) {
                
                o->status = (o->revealedChannel ? 1 : 0);

                if(o->completedChannel) {
                    switch_channels[o->completedChannel] = o->markCompletionTimeAtInit;

                    if(o->markCompletionTimeAtInit) {
                        switch_channels_time[o->completedChannel] = GameState.NumFramesUnpaused;
                    }
                }

            }
        }
    }
    


}