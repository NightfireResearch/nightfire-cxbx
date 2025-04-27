#include "Mission.h"
#include "../mp/multiplayer.h"

#define FailLabel U32_AT(0x0017e548)
#define BaseMap U32_AT(0x0025fe20)
#define MissionState U32_AT(0x0017e544)

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
    return GameState.CurrentLevelHashcode;
  }
  return (HASHCODE)BaseMap;
}

// AUTOINJECT
void Mission_SetStatus(undefined4 param_1) {
  MissionState = param_1;
}

// AUTOINJECT
undefined4 Mission_Status(void) {
  return MissionState;
}