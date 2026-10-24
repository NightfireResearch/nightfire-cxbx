#ifndef DRONEANIM_H_
#define DRONEANIM_H_

#include "Drone.h"
#include "../../../common/xbeClass.h"
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
    uint htFlags;               // 0x00 - DroneAnimFlags, for DroneAnim_SetHTAnim (-> Drone_tag.animFlags)
    float moveSpeed;            // 0x04 - -> Drone_tag.animMoveSpeed
    short animState;            // 0x08 - the anim state it belongs to
    ushort nextRow;             // 0x0a - played when it ends
    uchar mode;                 // 0x0c - DroneAnimMode
    uchar unknown0d;
    uchar blendFrames;          // 0x0e - into nextRow
    uchar aimMode;              // 0x0f - DroneAimMode (-> Drone_tag.aimMode)
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
LAYOUT_32(sizeof(DroneDataFunc) == 8, "DroneDataFunc is 8 bytes");
LAYOUT_32(sizeof(DroneAnimState) == 12, "DroneAnimState is 12 bytes");
static_assert(sizeof(DroneAnimInfo) == 24, "DroneAnimInfo is 24 bytes");
static_assert(sizeof(DroneAnimScript) == 4, "DroneAnimScript is 4 bytes");

#define NUM_DRONE_ANIM_STATES 119
#define NUM_DRONE_ANIM_ROWS 436
#define NUM_DRONE_ANIM_SETS 27

extern const DroneAnimState DroneAnimStates[NUM_DRONE_ANIM_STATES];             // DroneTables.cpp
extern const DroneAnimInfo Drone_AnimInfo[NUM_DRONE_ANIM_ROWS];                 // DroneAnimInfo.cpp
extern const DroneAnimScript Drone_AnimTables[NUM_DRONE_ANIM_ROWS][NUM_DRONE_ANIM_SETS];   // DroneTables.cpp

extern const DroneDataFunc DroneData_AnimFunc[2];
extern const DroneDataFunc DroneData_DAStateFunc[2];

// DroneAnimInfo.htFlags (Drone_tag.animFlags)
typedef enum {
    DANIMFLAG_LOOP = 0x1,
    DANIMFLAG_NO_COLLISION = 0x2,       // NDrone2_DoCollision: the drone does not collide while it plays
    DANIMFLAG_SKIP_UNSEEN = 0x4,        // not animated, and in some modes skipped, while the drone is not drawn
    DANIMFLAG_FACING_90 = 0x8,          // the drone faces this far round from its rotation while it plays
    DANIMFLAG_FACING_180 = 0x10,        //   (the opponent's bearing, DroneMove_NoBunching)
    DANIMFLAG_FACING_MINUS_90 = 0x20,
    DANIMFLAG_TURN_90 = 0x80,           // DroneAnim_OnEndAnim: the drone turns by this at the row's end
    DANIMFLAG_TURN_180 = 0x100,         //   (also: a blend out of it keeps its root turn, DroneAnim_SetHTAnim)
    DANIMFLAG_TURN_MINUS_90 = 0x200,

    DANIMFLAG_TURNS = DANIMFLAG_TURN_90 | DANIMFLAG_TURN_180 | DANIMFLAG_TURN_MINUS_90,
} DroneAnimFlags;

// DroneAnimInfo.mode, Drone_tag.animMode: when a row's call is complete (docs/drone/behaviours 6.1)
typedef enum {
    DANIMMODE_0 = 0,
    DANIMMODE_LOOP_UNTIL_CALLED = 1,    // complete once it has ended
    DANIMMODE_COMPLETE = 2,             // complete at once
    DANIMMODE_3 = 3,
    DANIMMODE_4 = 4,                    // DroneWeap_Ready2Fire: ready to fire at once
    DANIMMODE_INTERRUPTIBLE = 5,        // left only for a call made while it played
    DANIMMODE_SHORT = 6,                // at most 15 frames, unless the last hit was heavy (DroneAnim_OnStartAnim)
    DANIMMODE_7 = 7,
    DANIMMODE_8 = 8,
    DANIMMODE_COMPLETE_9 = 9,           // complete at once, as DANIMMODE_COMPLETE
} DroneAnimMode;

// DroneAnimInfo.aimMode, Drone_tag.aimMode (our names)
typedef enum {
    DRONE_AIM_NONE = 0,                 // the head does not follow anyone
    DRONE_AIM_ALWAYS = 2,
    DRONE_AIM_FROM_HALF_WAY = 3,        // or as the anim's events say (aimByEvent)
    DRONE_AIM_NO_LOOK = 0xd,            // as DRONE_AIM_NONE for the head
} DroneAimMode;

// Drone_tag.headMode: looks further and holds on to its choice
#define DRONE_HEAD_MODE_WIDE 2

// Drone_tag.animSet, the Drone_AnimTables column: set by the skin, then by the weapon held (NDrone2_DefaultInit,
// DroneWeap_ChangeWeapon). The weapon sets' names are from docs/drone/behaviours 6.1; ours.
typedef enum {
    DANIMSET_DEFAULT = 0,               // the column used when a set has no script for a row
    DANIMSET_HOSTAGE = 1,               // the servants' skins; DMODE_Hostage and DMODE_HostageTied
    DANIMSET_WOMAN = 2,                 // the business women's skins, Dominique's jacket
    DANIMSET_MAYHEW = 3,
    DANIMSET_PISTOL = 4,
    DANIMSET_RIFLE = 5,
    DANIMSET_SMG = 6,
    DANIMSET_NINJA = 7,                 // strikes with weapon 99 (DroneWeap_CloseCombatImpact)
    DANIMSET_PISTOL_ZOE = 8,            // DANIMSET_PISTOL for SKINCLASS_ZOE
    DANIMSET_SNIPER = 9,
    DANIMSET_CASTLE_GUARD = 0xa,
    DANIMSET_GUARD = 0xb,
    DANIMSET_UNARMED = 0xc,             // any other skin, and the empty-handed weapons
    DANIMSET_KIKO_COMBAT = 0xd,
    DANIMSET_CIVILIAN = 0xe,            // Zoe's, the party girls' and Kiko's skins
    DANIMSET_ABSEIL = 0xf,              // the abseilers (savedAnimSet keeps the one before)
    DANIMSET_ASTRONAUT = 0x10,
    DANIMSET_RIFLE_SNIPER = 0x12,       // DANIMSET_RIFLE for the sniper DTYPEs
    DANIMSET_13 = 0x13,                 // weapons 0x1e, 0x1f, 0x24 and 0x25
    DANIMSET_BOT_RIFLE = 0x14,          // the bots' (BOTWEAP_getAnimSet), named for their scripts
    DANIMSET_BOT_SMG = 0x15,            //   (HT_Script_PlayerRifle_Idle ...)
    DANIMSET_BOT_HANDGUN = 0x16,
    DANIMSET_BOT_TWO_HANDED_HANDGUN = 0x17,
    DANIMSET_BOT_UNARMED = 0x18,
    DANIMSET_BOT_LAUNCHER = 0x19,
    DANIMSET_BOT_TWIN_HANDGUNS = 0x1a,
} DroneAnimSet;

// Anim states (DroneAnimStates; DroneAnim_CallAnim, Drone_tag.animState, DroneAnimCall.state), named after the
// states and functions that ask for them (our names); a number where nothing says more.
typedef enum {
    DANIM_ANY = 0,                      // CanDoAnimState: always; a transition to any state
    DANIM_ABSEIL_HANG = 2,              // DSTATE_AbseilHang
    DANIM_ABSEIL_SLIDE = 3,             // DSTATE_AbseilSlide
    DANIM_BACKOFF = 4,                  // NDrone2_CanBackoff
    DANIM_AIM_CROUCH = 5,               // DroneAnim_ValidateCoverAnim gives way to 7, or else 10
    DANIM_AIM_CROUCH_ALT = 7,
    DANIM_WALK_AIMED = 8,               // DRONE_PROP_WALK_AIMED
    DANIM_09 = 9,
    DANIM_AIM_STAND = 0xa,              // what most states fall back on
    DANIM_SEARCH_LOOK = 0xb,
    DANIM_STRAFE_LEFT = 0xc,
    DANIM_STRAFE_RIGHT = 0xd,
    DANIM_SNIPER_IDLE = 0xe,
    DANIM_CROUCH_WALK = 0xf,            // DRONE_PROP_CROUCH_WALK
    DANIM_CORNER_CROUCH = 0x10,         // the corner cover lists: crouched (CoverCornerAnimLists)
    DANIM_CROUCH_COVER = 0x11,          // and low cover
    DANIM_CROUCH_LEAN = 0x13,           // DroneAnim_CanCrouchLean; the side 1 list's
    DANIM_CROUCH_LEAN_SIDE2 = 0x14,
    DANIM_CROUCH_STEP_OUT = 0x15,       // DroneAnim_CanCrouchStepOut
    DANIM_CROUCH_STEP_OUT_SIDE2 = 0x16,
    DANIM_STAND_LEAN = 0x17,            // DroneAnim_CanStandLean
    DANIM_STAND_LEAN_SIDE2 = 0x18,
    DANIM_HIDE = 0x19,                  // the hiding states, where DANIM_CROUCH_COVER is missing
    DANIM_CORNER_STAND = 0x1a,          // the corner cover lists: standing
    DANIM_STAND_STEP_OUT = 0x1b,        // DroneAnim_CanStandStepOut
    DANIM_STAND_STEP_OUT_SIDE2 = 0x1c,
    DANIM_PRONE = 0x1d,                 // DSTATE_Prone
    DANIM_RUN = 0x1e,
    DANIM_1F = 0x1f,                    // stands in for DANIM_RUN
    DANIM_SMOKED_OUT = 0x20,
    DANIM_STAND_ALERT = 0x21,           // what DroneAnim_CallAnim falls back on
    DANIM_LOOK_AROUND = 0x22,
    DANIM_IDLE_FIDDLE = 0x23,           // DSTATE_StandFiddle
    DANIM_STAND_IDLE = 0x24,            // DroneAnim_SetStandIdleAnim, with 0x25-0x27; DroneAnim_CallAnim's last resort
    DANIM_STRAFE_DODGE = 0x29,          // DRONE_PROP_STRAFE_DODGE
    DANIM_STRAFE_DODGE_RIGHT = 0x2a,    //   and to the right
    DANIM_STUN_DART_A = 0x2b,           // DSTATE_StunDartImpact, one or the other
    DANIM_STUN_DART_B = 0x2c,
    DANIM_STUN_GRENADE_A = 0x2d,        // DSTATE_StunGrenadeImpact, one or the other
    DANIM_STUN_GRENADE_B = 0x2e,
    DANIM_TASER_DOWN_A = 0x2f,          // DSTATE_Stunned, after DANIM_TASER_FALL_A
    DANIM_TASER_DOWN_B = 0x30,          //   and after DANIM_TASER_FALL_B
    DANIM_TASER_FALL_A = 0x31,          // DSTATE_Taser, one or the other
    DANIM_TASER_FALL_B = 0x32,
    DANIM_34 = 0x34,
    DANIM_ALERT_WALK = 0x35,
    DANIM_PATROL_ALERT_LOOK = 0x36,
    DANIM_WALK_IDLE = 0x37,             // DroneAnim_SetWalkIdleAnim, with 0x38 and 0x39
    DANIM_PRESS_ALARM = 0x3c,           // DSTATE_PressAlarm
    DANIM_ALT_ATTACK = 0x3d,            // DSTATE_AltAttack, NDrone2_CanAltAttack, BOTSTATE_chooseUnarmedAttackAnim
    DANIM_3E = 0x3e,
    DANIM_DEATH = 0x40,                 // DroneAnim_LocationDeathAnim
    DANIM_41 = 0x41,                    // played as DANIM_DEATH (DroneAnim_BaseCallState)
    DANIM_DEATH_SHOT = 0x42,            // DroneAnim_LocationDeathAnim: shot in the body while the player aims
    DANIM_43 = 0x43,
    DANIM_DEATH_EXPLOSION = 0x44,
    DANIM_DISCARD = 0x45,               // DSTATE_AimStandDiscard
    DANIM_DRAW_WEAPON = 0x46,           // DSTATE_DrawWeapon
    DANIM_IMPACT_EXPLOSION = 0x47,      // NDrone2_ExplosiveImpact
    DANIM_THROW_GRENADE = 0x49,
    DANIM_RELOAD_B = 0x4a,
    DANIM_IDLE_FIDGET = 0x4b,           // DSTATE_Idle and DSTATE_Patrol, three variants
    DANIM_IMPACT = 0x4c,                // DroneAnim_LocationImpactAnim
    DANIM_USE_DOOR = 0x4e,
    DANIM_4F = 0x4f,
    DANIM_KNOCKED_OUT = 0x50,           // DSTATE_KnockedOut_Anim
    DANIM_IMPACT_PUNCH = 0x51,          // NDrone2_PunchImpact
    DANIM_RELOAD = 0x52,
    DANIM_ROLL_LEFT = 0x53,
    DANIM_ROLL_RIGHT = 0x54,
    DANIM_55 = 0x55,
    DANIM_56 = 0x56,
    DANIM_57 = 0x57,                    // gives way to DANIM_CROUCH_COVER, DANIM_HIDE, DANIM_STAND_ALERT
    DANIM_COVER_FIRE = 0x58,            // the cover lists' firing step
    DANIM_STEP_LEFT = 0x5a,
    DANIM_STEP_RIGHT = 0x5b,
    DANIM_SURRENDER = 0x5c,             // DSTATE_Surrender_Anim
    DANIM_5D = 0x5d,
    DANIM_5E = 0x5e,
    DANIM_TELL_TO_STOP = 0x5f,          // DSTATE_CivilianDoorGuard
    DANIM_60 = 0x60,
    DANIM_LOOK_BEHIND = 0x61,
    DANIM_62 = 0x62,
    DANIM_NINJA_BACKFLIP = 0x66,        // DSTATE_NinjaBackflip
    DANIM_NINJA_RUN = 0x67,
    DANIM_NINJA_SIDEFLIP_LEFT = 0x68,   // DSTATE_NinjaSideflipLeft
    DANIM_NINJA_SIDEFLIP_RIGHT = 0x69,  // DSTATE_NinjaSideflipRight
    DANIM_NINJA_SOMERSAULT = 0x6a,      // DSTATE_NinjaSomersault
    DANIM_NINJA_STAND = 0x6b,           // DSTATE_NinjaStand, NinjaStandFire, NinjaNoRoute
    DANIM_NINJA_SWORD = 0x6d,           // DSTATE_NinjaSword
    DANIM_ASTRONAUT_DEATH = 0x6f,       // DSTATE_AstronautDeath
    DANIM_ASTRONAUT_COMBAT = 0x70,      // DSTATE_AstronautCombat
    DANIM_FLOAT_BACK = 0x71,            // DroneMove_AstronautAnim: moveTarget beyond 135 degrees
    DANIM_FLOAT_AHEAD = 0x72,           //   within 45 degrees of ahead; DSTATE_AstronautLaunch
    DANIM_FLOAT_TURN_POSITIVE = 0x73,   //   otherwise, by the turn's sign
    DANIM_FLOAT_TURN_NEGATIVE = 0x74,
    DANIM_GET_UP = 0x76,
} DroneAnimStateId;

// Drone_AnimInfo rows (DroneAnim_SetDAnimNow, Drone_tag.animRow): each the first row of the anim state it is named
// after, unless said
typedef enum {
    DROW_DEATH_LAND = 0xa,              // a DANIM_DEATH row going on to DROW_DEAD (DSTATE_ElevatorJumper)
    DROW_ABSEIL_HANG = 0xb,
    DROW_AIM_STAND = 0x32,
    DROW_COVER_FIRE = 0x5c,             // a shot fired through the anim (DroneAnim_PlayFiringAnim)
    DROW_SNIPER_IDLE = 0x7d,
    DROW_ASTRONAUT_COMBAT = 0x82,
    DROW_CORNER_CROUCH = 0x87,
    DROW_CORNER_STAND = 0xab,
    DROW_DEAD = 0xbf,                   // anim state 1's, which goes on to itself: after a death script
    DROW_STAND_ALERT = 0xf4,
    DROW_STAND_IDLE = 0x11a,
    DROW_NINJA_STAND = 0x1a5,
} DroneAnimRow;

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

bool DroneAnim_CanSetAnimCall(Drone_tag *drone);
void DroneAnim_CallFullyComplete(Drone_tag *drone);
ushort DroneAnim_BaseCallState(ushort state);
bool DroneAnim_SkipAnim(Drone_tag *drone);
bool DroneAnim_TakeCall(Drone_tag *drone);
short DroneAnim_GetDAnimForAnimState(ushort state, ushort call, ushort variant, ushort *blend, uchar *callFlag);
bool DroneAnim_GetDAnimForCall(Drone_tag *drone, ushort call, ushort variant, ushort *row, ushort *blend,
                               uchar *callFlag);
void DroneAnim_SetDAnimVeryInternal(Drone_tag *drone, ushort row, ushort animSet);

#endif // DRONEANIM_H_
