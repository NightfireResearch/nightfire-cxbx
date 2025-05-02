#ifndef MISSION_H
#define MISSION_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    Action_TranslatedText name;
    Action_TranslatedText failLabel;
    Action_TranslatedText description;
    char completedChannel;
    char markCompletionTimeAtInit;
    char unknown1; // ??
    char revealedChannel;
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
    char pad[20]; // There's some non-zero data here but it's not obvious where it's used, if anywhere
} Mission;

static_assert(sizeof(Mission) == 0x28, "Size of Mission not correct");

typedef struct {
    undefined4 status;
    undefined4 statusText;
    undefined4 name;
    undefined4 description;
} OBJ_STATE;


#pragma pack(pop)


void Mission_SetFailLabel(Action_TranslatedText text);
void Mission_SetMapHCode(HASHCODE param_1);
HASHCODE Mission_BaseMapHCode(void);
void Mission_SetStatus(undefined4 param_1);
undefined4 Mission_Status(void);
int Mission_NumVisObjectives(void);
void Mission_ObjectiveState(OBJ_STATE *state, short objectiveNum);
void Mission_MonitorObjectives(void);
void Mission_Update(void);
void Mission_Init(HASHCODE hashcode, short warmReset);

#endif // MISSION_H