#ifndef DRONEANIM_H_
#define DRONEANIM_H_

#include "Drone.h"
#include "../../engine/Anim.h"   // AnimObj, sAnimScript_tag

#pragma pack(push, 1)

// The drone animation tables (docs/drone/behaviours/README.md 6.1). States ask for an anim state; an anim state is
// a run of rows (its variants); a row has a script per anim set (Drone_tag.animSet).

// A transition out of an anim state: what to play for a call (DroneAnim_GetDAnimForAnimState). A list ends with
// toState 0xff.
typedef struct DroneAnimTransition {
    uchar toState;              // 0x0 - the anim state called for, 0 for any
    uchar variants;             // 0x1 - how many rows after row the variants take
    short row;                  // 0x2 - the Drone_AnimInfo row; 0 plays nothing, -1 is not allowed
    uchar blendFrames;          // 0x4
    uchar callFlag;             // 0x5 - -> Drone_tag.animCallFlag
} DroneAnimTransition;

// DroneAnimStates, one per anim state
typedef struct DroneAnimState {
    ushort firstRow;            // 0x0 - its first Drone_AnimInfo row; the variants are the rows after it
    uchar kind;                 // 0x2 - -> DroneAnimCall.kind
    uchar priority;             // 0x3 - -> DroneAnimCall.priority
    uchar unknown4;
    uchar unknown5;
    ushort unknown6;
    const DroneAnimTransition *transitions; // 0x8 - DroneAnim_GetDAnimForAnimState
} DroneAnimState;

// Drone_AnimInfo, one per row
typedef struct DroneAnimInfo {
    uint htFlags;               // 0x00 - for DroneAnim_SetHTAnim (-> Drone_tag.unknown46c)
    float moveSpeed;            // 0x04 - -> Drone_tag.animMoveSpeed
    short animState;            // 0x08 - the anim state it belongs to
    ushort nextRow;             // 0x0a - played when it ends
    uchar mode;                 // 0x0c - completion mode
    uchar unknown0d;
    uchar blendFrames;          // 0x0e - into nextRow
    uchar aimMode;              // 0x0f - -> Drone_tag.aimMode
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

// DroneData_AnimFunc and DroneData_DAStateFunc: a function per row and per anim state, ended by id 0xffff
typedef struct DroneDataFunc {
    ushort id;
    ushort _pad2;
    DroneAnimFunc func;
} DroneDataFunc;

#pragma pack(pop)

static_assert(sizeof(DroneAnimTransition) == 6, "DroneAnimTransition is 6 bytes");
static_assert(sizeof(DroneDataFunc) == 8, "DroneDataFunc is 8 bytes");
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

// XBE_GLOBAL(0x001640fc, 0x10)
#define DroneData_AnimFunc (*(const DroneDataFunc(*)[2])0x001640fc)
// XBE_GLOBAL(0x0016410c, 0x10)
#define DroneData_DAStateFunc (*(const DroneDataFunc(*)[2])0x0016410c)

// DroneAnimInfo.htFlags (Drone_tag.unknown46c)
#define DANIMFLAG_LOOP 0x1
#define DANIMFLAG_SKIP_UNSEEN 0x4       // DroneAnim_SkipAnim: skipped, in some modes, while the drone is not drawn
#define DANIMFLAG_TURN_90 0x80          // DroneAnim_OnEndAnim: the drone turns by this at the row's end
#define DANIMFLAG_TURN_180 0x100        //   (also: a blend out of it keeps its root turn, DroneAnim_SetHTAnim)
#define DANIMFLAG_TURN_MINUS_90 0x200

// Anim states named after the functions that ask for them (our names)
#define DANIM_STAND_IDLE 0x24       // DroneAnim_SetStandIdleAnim, with 0x25-0x27; DroneAnim_CallAnim's last resort
#define DANIM_WALK_IDLE 0x37        // DroneAnim_SetWalkIdleAnim, with 0x38 and 0x39
#define DANIM_DEATH 0x40            // DroneAnim_LocationDeathAnim
#define DANIM_DEATH_SHOT 0x42       // DroneAnim_LocationDeathAnim: shot in the body while the player aims
#define DANIM_IMPACT_EXPLOSION 0x47 // NDrone2_ExplosiveImpact
#define DANIM_IMPACT 0x4c           // DroneAnim_LocationImpactAnim
#define DANIM_IMPACT_PUNCH 0x51     // NDrone2_PunchImpact

bool DroneAnim_CanDoAnimState(DCVars_tag *dcv, short state, short variant);
bool DroneAnim_CallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                        DCVars_tag *dcv);
uchar DroneAnim_CurrentMoveState(Drone_tag *drone);
bool DroneAnim_SetStandIdleAnim(DCVars_tag *dcv, char wait);
bool DroneAnim_SetWalkIdleAnim(DCVars_tag *dcv, char wait);
bool DroneAnim_LocationDeathAnim(DCVars_tag *dcv, short endState);
bool DroneAnim_LocationImpactAnim(DCVars_tag *dcv);
bool DroneAnim_PlayFiringAnim(DCVars_tag *dcv);
void DroneAnim_FreeScript(DCVars_tag *dcv);
bool DroneAnim_SetDAnimNow(ushort row, ushort blend, uchar callFlag, DCVars_tag *dcv);
void DroneAnim_SetAnimD(ushort row, ushort blend, Drone_tag *drone);
bool DroneAnim_SetDAnimInternal(ushort row, ushort blend, uchar callFlag, Drone_tag *drone);
void DroneAnim_SetHTAnim(HASHCODE hashcode, uint flags, ushort frame, Drone_tag *drone, ushort blend, float speed);
bool DroneAnim_SetEndAnim(Drone_tag *drone, uchar blend);
uchar DroneAnim_OnEndAnim(Drone_tag *drone, uint flags, uchar blend);
void DroneAnim_OnStartAnim(Drone_tag *drone);
void DroneAnim_SnapRotate(Drone_tag *drone);
void DroneAnim_CallHandler(DCVars_tag *dcv);
void DroneAnim_EventFunc(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, ushort event);
void DroneAnim_AnimEndCallback(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script);
void DroneAnim_PreTransCallback(obj_tag *obj, AnimObj *animObj, _VECTOR *delta);
const DroneDataFunc* DroneData_GetAnimFuncInfo(ushort row);
const DroneDataFunc* DroneData_GetDAStateFuncInfo(ushort state);
void DAnimFunc_Run(Drone_tag *drone, ushort phase);

// Register-argument originals: the adaptors patched over them (never call these from C), and their C bodies
void DroneAnim_CanSetAnimCall(void);            // EAX drone
void DroneAnim_CallFullyComplete(void);         // ESI drone
void DroneAnim_BaseCallState(void);             // AX state, the result in AX
void DroneAnim_SkipAnim(void);                  // EAX drone
void DroneAnim_TakeCall(void);                  // ESI drone
void DroneAnim_GetDAnimForAnimState(void);      // AX state, DX call, ESI blend, EDI flag, the variant on the stack
void DroneAnim_GetDAnimForCall(void);           // EBX drone, DX call, AX variant, ECX flag, row and blend on the stack
void DroneAnim_SetDAnimVeryInternal(void);      // EAX &drone->animMode, DX row, the anim set on the stack
bool _DroneAnim_CanSetAnimCall(Drone_tag *drone);
void _DroneAnim_CallFullyComplete(Drone_tag *drone);
ushort _DroneAnim_BaseCallState(ushort state);
bool _DroneAnim_SkipAnim(Drone_tag *drone);
bool _DroneAnim_TakeCall(Drone_tag *drone);
short _DroneAnim_GetDAnimForAnimState(ushort state, ushort call, ushort variant, ushort *blend, uchar *callFlag);
bool _DroneAnim_GetDAnimForCall(Drone_tag *drone, ushort call, ushort variant, ushort *row, ushort *blend,
                                uchar *callFlag);
void _DroneAnim_SetDAnimVeryInternal(Drone_tag *drone, ushort row, ushort animSet);

#endif // DRONEANIM_H_
