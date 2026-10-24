#ifndef MISSION_H
#define MISSION_H

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

typedef struct {
    Action_TranslatedText name;
    Action_TranslatedText failLabel;
    Action_TranslatedText description;
    uchar completedChannel;     // switch channels: indexes 0-255, as the original reads them
    char markCompletionTimeAtInit;
    char unknown1; // ??
    uchar revealedChannel;      // 0 and 0xff for none
    char flags;                 // ObjectiveFlags
    char unknown2[3];
    int status;                 // ObjectiveStatus
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

LAYOUT_32(sizeof(Mission) == 0x28, "Size of Mission not correct");

typedef struct {
    int status;                 // ObjectiveStatus
    Action_TranslatedText statusText;
    Action_TranslatedText name;
    Action_TranslatedText description;
} OBJ_STATE;

#pragma pack(pop)

// Objective::status, as Mission_MonitorObjectives moves it on; the in-progress and incomplete ones are named for
// their status texts (Mission_ObjectiveState)
enum ObjectiveStatus {
    OBJSTATUS_NOT_SHOWN = 0,    // until its part's level is reached
    OBJSTATUS_HIDDEN = 1,       // until its revealedChannel is on
    OBJSTATUS_IN_PROGRESS = 2,  // a fail condition, watched
    OBJSTATUS_INCOMPLETE = 3,
    OBJSTATUS_COMPLETE = 4,
    OBJSTATUS_FAILED = 5,
};

// Objective::flags
enum ObjectiveFlags {
    OBJECTIVE_FAIL_CONDITION = 0x2,     // the mission fails when its channel goes off; else it must come on
};

// Mission_Status: Mission_Init sets running, or none for a level without a mission; Mission_Update sets the end
enum MissionStatus : uint32_t {
    MISSION_STATUS_0 = 0,               // tested by SP_Update, never set
    MISSION_STATUS_RUNNING = 1,
    MISSION_STATUS_FAILED = 2,
    MISSION_STATUS_QUIT = 3,            // after the player's death, or a driving level quit
    MISSION_STATUS_NONE = 5,
    MISSION_STATUS_COMPLETE = 6,
};

Action_TranslatedText Mission_FailLabel(void);
void Mission_SetFailLabel(Action_TranslatedText text);
void Mission_SetMapHCode(HASHCODE hashcode);
HASHCODE Mission_BaseMapHCode(void);
void Mission_SetStatus(MissionStatus status);
MissionStatus Mission_Status(void);
int Mission_NumVisObjectives(void);
void Mission_ObjectiveState(OBJ_STATE *state, short objectiveNum);
void Mission_MonitorObjectives(void);
void Mission_Update(void);
void Mission_Init(HASHCODE hashcode, short warmReset);

#endif // MISSION_H