#ifndef MISSION_H
#define MISSION_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    Action_TranslatedText name;
    uint unknown;
    Action_TranslatedText description;
    char someSwitchChannel;
    char unknown1[2];
    char someOtherSwitchChannel;
    char someFlags; // ??
    char unknown2[3];
    int status;
} Objective;

static_assert(sizeof(Objective) == 0x18, "Size of Objective not correct");

typedef struct {
    HASHCODE level;
    HASHCODE baseLevel;
    uint idxInOrder;
    Objective* objectives;
    uint numObjectives;
    char pad[20];
} MissionData;

static_assert(sizeof(MissionData) == 0x28, "Size of MissionData not correct");
#pragma pack(pop)


void Mission_SetFailLabel(Action_TranslatedText text);
void Mission_SetMapHCode(HASHCODE param_1);
HASHCODE Mission_BaseMapHCode(void);
void Mission_SetStatus(undefined4 param_1);
undefined4 Mission_Status(void);
int Mission_NumVisObjectives(void);



#endif // MISSION_H