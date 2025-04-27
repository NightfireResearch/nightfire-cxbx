#include "Mission.h"
#include "../mp/multiplayer.h"

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
  if(MPSettings.isMultiplayer) {
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