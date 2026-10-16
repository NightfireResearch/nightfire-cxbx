#ifndef DRONEANIM_H_
#define DRONEANIM_H_

#include "Drone.h"

#pragma pack(push, 1)

// The drone animation tables (docs/drone/behaviours/README.md 6.1). States ask for an anim state; an anim state is
// a run of rows (its variants); a row has a script per anim set (Drone_tag.animSet).

// DroneAnimStates, one per anim state
typedef struct DroneAnimState {
    ushort firstRow;            // 0x0 - its first Drone_AnimInfo row; the variants are the rows after it
    uchar kind;                 // 0x2 - -> DroneAnimCall.kind
    uchar priority;             // 0x3 - -> DroneAnimCall.priority
    uchar unknown4;
    uchar unknown5;
    ushort unknown6;
    const void *transitions;    // 0x8 - read by FUN_00033040
} DroneAnimState;

// Drone_AnimInfo, one per row
typedef struct DroneAnimInfo {
    uint htFlags;               // 0x00 - for DroneAnim_SetHTAnim
    uint unknown04;
    short animState;            // 0x08 - the anim state it belongs to
    ushort nextRow;             // 0x0a - played when it ends
    uchar mode;                 // 0x0c - completion mode
    uchar unknown0d;
    uchar blendFrames;          // 0x0e
    uchar unknown0f;
    uchar moveState;            // 0x10 - DroneAnim_CurrentMoveState: 3 and 5 crouched, 4 and 0x1a down (as the
                                //        eye-height code in NDrone2.cpp reads it)
    uchar unknown11[3];
    float speed;                // 0x14
} DroneAnimInfo;

// Drone_AnimTables, per row and anim set: the script is 0x06000000 | scriptLo, 0 for none
typedef struct DroneAnimScript {
    ushort scriptLo;
    ushort startFrame;          // 999 = the last frame
} DroneAnimScript;

#pragma pack(pop)

static_assert(sizeof(DroneAnimState) == 12, "DroneAnimState is 12 bytes");
static_assert(sizeof(DroneAnimInfo) == 24, "DroneAnimInfo is 24 bytes");
static_assert(sizeof(DroneAnimScript) == 4, "DroneAnimScript is 4 bytes");

#define NUM_DRONE_ANIM_STATES 119
#define NUM_DRONE_ANIM_ROWS 436
#define NUM_DRONE_ANIM_SETS 27

// XBE_GLOBAL(0x001664c0, 0x594)
#define DroneAnimStates (*(const DroneAnimState(*)[NUM_DRONE_ANIM_STATES])0x001664c0)
// XBE_GLOBAL(0x00166a58, 0x28e0)
#define Drone_AnimInfo (*(const DroneAnimInfo(*)[NUM_DRONE_ANIM_ROWS])0x00166a58)
// XBE_GLOBAL(0x00169338, 0xb7f0)
#define Drone_AnimTables (*(const DroneAnimScript(*)[NUM_DRONE_ANIM_ROWS][NUM_DRONE_ANIM_SETS])0x00169338)

// Anim states named after the functions that ask for them (our names)
#define DANIM_STAND_IDLE 0x24       // DroneAnim_SetStandIdleAnim, with 0x25-0x27; DroneAnim_CallAnim's last resort
#define DANIM_WALK_IDLE 0x37        // DroneAnim_SetWalkIdleAnim, with 0x38 and 0x39
#define DANIM_DEATH 0x40            // DroneAnim_LocationDeathAnim
#define DANIM_DEATH_SHOT 0x42       // DroneAnim_LocationDeathAnim: shot in the body while the player aims

bool DroneAnim_CanDoAnimState(DCVars_tag *dcv, short state, short variant);
bool DroneAnim_CallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                        DCVars_tag *dcv);
uchar DroneAnim_CurrentMoveState(Drone_tag *drone);
bool DroneAnim_SetStandIdleAnim(DCVars_tag *dcv, char wait);
bool DroneAnim_SetWalkIdleAnim(DCVars_tag *dcv, char wait);
bool DroneAnim_LocationDeathAnim(DCVars_tag *dcv, short endState);

#endif // DRONEANIM_H_
