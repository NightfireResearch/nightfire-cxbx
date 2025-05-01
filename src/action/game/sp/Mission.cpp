#include "Mission.h"
#include "../mp/multiplayer.h"
#include "../../sound/music.h"

#include <stdio.h>

#define FailLabel U32_AT(0x0017e548)
#define BaseMap U32_AT(0x0025fe20)
#define ThisOrderNum U32_AT(0x0025fe24)
#define MissionState U32_AT(0x0017e544)

#define MissionData (*(MissionData(*)[24])0x0017e180)

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

    for(int i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we reach a later mission then we are done
            return count;

        for(int j = 0; j < MissionData[i].numObjectives; j++) {
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

    for(int i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we haven't found it then the index is bad somehow
            return;

        for(int j = 0; j < MissionData[i].numObjectives; j++) {
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

#define MissionFailConditionHit U8_AT(0x001df19b)
#define MissionWinConditionHit U8_AT(0x001df19c)
#define MissionFailTime U32_AT(0x001df5b4)
#define MissionWinTime U32_AT(0x001df5b8)

#define switch_channels ((char*)0x001df138)

#define FailedDueToAlarm U8_AT(0x001df199)
#define FailedDueToKilledCivilian U8_AT(0x001df198)
#define FailedDueToKikoEscape U8_AT(0x001df1b3)
#define FailedDueToMissileLaunch U8_AT(0x001df1ff)

// AUTOINJECT
void Mission_MonitorObjectives(void) {

    bool allObjectivesCompleted = true;

    // Evaluate objectives and send notifications
    for(int i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        bool fromPreviousPart = (ThisOrderNum <= MissionData[i].idxInOrder);

        for(int j = 0; j < MissionData[i].numObjectives; j++) {

            Objective *o = &MissionData[i].objectives[j];
            char* str = Txt_GetStringFromHeap(0);

            bool isFailCondition = (o->someFlags & 0x2); // If set, the objective is a fail condition, otherwise it's a precondition for success

            bool thisObjectiveMet = switch_channels[o->completedChannel] == (isFailCondition ? false : true);
            allObjectivesCompleted = allObjectivesCompleted && thisObjectiveMet;

            switch(o->status) {
                case 0:
                    // On entering a new level, show ones from the previous hashcode? Mayhew part 1->2 reveals that escorting is complete?
                    if(fromPreviousPart) { //??? Guessing at intent
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 300);
                        o->status = ((byte)~o->someFlags & 2 | 4) >> 1;
                    }
                    break;
                
                case 1:
                    // New objective revealed 
                    if (((o->revealedChannel != 0) && (o->revealedChannel != 0xff)) && (switch_channels[o->revealedChannel] != '\0')) {
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 300);
                        o->status = ~(uint)((byte)o->someFlags >> 1) & 1 | 2; //?!
                    }
                    break;
                case 2: 
                    if (!thisObjectiveMet) {
                        MissionFailConditionHit = true;
                        MissionFailTime = GameState.NumFramesUnpaused;
                        sprintf(str,"%s: %s",Txt_BindLabel((Action_TranslatedText)0x170, 0), Txt_BindLabel(o->name, 0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 0xb4);
                        o->status = 5;
                    }
                    break;


                case 3:
                    if(thisObjectiveMet) {
                        sprintf(str, "%s", Txt_BindLabel(TXT_NOTIF_OBJECTIVE_COMPLETE, 0));
                        Text_AddMsg(0, fromPreviousPart, 2, str, 0, 300);
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
        return;
    }


}