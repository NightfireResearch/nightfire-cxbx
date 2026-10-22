#ifndef SCRIPTPLAYER_H_
#define SCRIPTPLAYER_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

// Bits of SCRIPTPLAYER::triggerMask, and the values SP_Hit computes/returns each frame
enum SP_TriggerFlags {
    SP_Trigger_SwitchChannel = 0x1,  // someSwitchChannel is currently on
    SP_Trigger_BulletHit     = 0x2,  // A bullet matching requiredBulletType hit a linked entity
    SP_Trigger_Unpause       = 0x4,  // unpauseRequested was set, or the countdown/damage timer elapsed
    SP_Trigger_PlayerTouch   = 0x8,  // A player touched a linked entity
    SP_Trigger_DroneTouch    = 0x10, // A drone touched a linked entity
};

#pragma pack(push, 1)

// Cross-referenced against Ghidra's own (partially reverse-engineered) SCRIPTPLAYER struct, which is
// authoritative for the offsets/sizes here. Field purposes were then pinned down by reading every other
// SP_ function Ghidra knows about (SP_Update, SP_Hit, SP_Activate, SP_GetState, SP_UnPause, SP_HideObj,
// SP_SwitchToScript, SP_GetHitDamage) even though most of those aren't reimplemented yet - see their
// decompiles for the full state machine. Only unknown_0x39 remains genuinely unidentified.
typedef struct SCRIPTPLAYER {
    obj_tag* ownerObj;                  // 0x00 - object that created this script player (eg. a switch/fusebox);
                                         //        also the object SP_Activate calls GT_Activate/Switch_Activate on
    ScriptPlayerCallback* callback;     // 0x04 - given to Script_Load by SP_LoadScript (script event 15 calls it), then cleared
    void* callbackContext;              // 0x08 - opaque context, passed through to Script_Load and the callback
    SCRIPTINFO* scriptInfos[2];         // 0x0c - up to two scripts; nthScript selects which one is "active"
    float damageThreshold;              // 0x14 - see accumulatedDamage. Set from an unsigned placement dword,
                                         //        never scaled. A value of 0.0 disables the damage-trigger check
    float accumulatedDamage;            // 0x18 - SP_Update adds SP_GetHitDamage's result here each frame while
                                         //        the active script is asleep; once this reaches damageThreshold,
                                         //        SP_UnPause is called and the threshold is subtracted back off
                                         //        (so leftover damage carries over)
    HASHCODE scriptHashcodes[2];        // 0x1c - hashcode of each of the two scripts (SP_LoadScript reads
                                         //        scriptHashcodes[nthScript] via "scriptInfos[nthScript + 4]"
                                         //        pointer-arithmetic trickery from the compiler)
    short state;                        // 0x24 - playback state machine driven by SP_Update - see SP_State
    short scriptPlayModes[2];           // 0x26 - per-script (indexed like scriptHashcodes) initial play mode,
                                         //        consumed by SP_LoadScript: 1=start playing immediately
                                         //        (SP_State_PlayingForward), 8=start armed, paused
                                         //        (SP_State_Armed), other=load then pause at the first frame,
                                         //        awaiting a trigger (SP_State_WaitingForward). scriptPlayModes[0]
                                         //        ==1 also makes SP_CreateScriptPlayer defer the load to
                                         //        SP_State_DeferredLoad
    ushort nthScript;                   // 0x2a - selects scriptInfos[nthScript] / scriptHashcodes[nthScript]
    short unpauseRequested;             // 0x2c - one-shot "please unpause" flag; set by SP_UnPause and by
                                         //        SP_Activate (conditionally, gated on triggerMask bit 4), folded
                                         //        into the trigger bitmask by SP_Update once per frame then cleared
    ushort triggerMask;                 // 0x2e - bitmask of which trigger sources SP_Update should react to - see
                                         //        SP_TriggerFlags. The conditions actually satisfied this frame
                                         //        (from SP_Hit, plus SP_Trigger_Unpause for unpauseRequested/
                                         //        countdown-elapsed) are AND-ed against this
    ushort someSwitchChannel;           // 0x30 - reset to 0 (off) whenever the script player is (re)created;
                                         //        also toggled by SP_Update to reflect the current play state
    ushort disableSwitchChannel;        // 0x32 - if set and this channel is off, SP_Update skips entirely (same
                                         //        pattern as Sensor/Searchlight's disableSwitchChannel)
    char someCountdown;                 // 0x34 - frame counter set to 5 whenever playback (re)starts; suppresses
                                         //        SP_Hit checks and the unpause bit while counting down
    char playOnce;                      // 0x35 - 0 = loop/toggle between the two scripts forever; nonzero = finish
                                         //        (state=5, delete) after the active script finishes once
    char maybeHideState;                // 0x36 - set by SP_HideObj; nonzero makes SP_Update skip entirely
    char requiredBulletType;            // 0x37 - bullet-hit filter used by SP_Hit: 0 = any bullet counts,
                                         //        otherwise only bullets whose type matches this value count
    char loadingScreenShown;            // 0x38 - always zeroed on creation, not sourced from placement data;
                                         //        one-shot latch so SP_Update only pushes the "loading" GameFlow
                                         //        state once while waiting for the script asset to load
    char unknown_0x39[3];               // 0x39 - trailing bytes, never referenced by any function seen so far
} SCRIPTPLAYER;

XBOX_SIZE(SCRIPTPLAYER, 60);
LAYOUT_32(offsetof(SCRIPTPLAYER, scriptInfos) == 0xc, "Bad offset of scriptInfos");
LAYOUT_32(offsetof(SCRIPTPLAYER, scriptHashcodes) == 0x1c, "Bad offset of scriptHashcodes");
LAYOUT_32(offsetof(SCRIPTPLAYER, scriptPlayModes) == 0x26, "Bad offset of scriptPlayModes");
LAYOUT_32(offsetof(SCRIPTPLAYER, nthScript) == 0x2a, "Bad offset of nthScript");
LAYOUT_32(offsetof(SCRIPTPLAYER, triggerMask) == 0x2e, "Bad offset of triggerMask");
LAYOUT_32(offsetof(SCRIPTPLAYER, someSwitchChannel) == 0x30, "Bad offset of someSwitchChannel");
LAYOUT_32(offsetof(SCRIPTPLAYER, someCountdown) == 0x34, "Bad offset of someCountdown");

#pragma pack(pop)

// The register-convention entries (ScriptPlayer.cpp)
void SP_LoadScript(void);
void SP_Hit(void);
ushort _SP_Hit(SCRIPTPLAYER *scriptPlayer);
void SP_RemoveObj(obj_tag* obj, void* scriptPlayer);
void SP_Delete(obj_tag *obj);
void SP_Update(obj_tag *obj);
SCRIPTINFO * SP_getScriptInfo(obj_tag *gameObj);
void SP_SetColour(obj_tag *gameObj, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
void SP_SetPos(obj_tag *gameObj, _VECTOR *newPos);
void SP_SetPosRot(obj_tag *gameObj, _MATRIX *newMtx);
obj_tag* SP_CreateScriptPlayer(_VECTOR *pos, _VECTOR *rot, level_tag *placementData, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext);
obj_tag* SP_Create(_VECTOR *pos, _VECTOR *rot, HASHCODE scriptHashcode0, HASHCODE scriptHashcode1, short playMode, char playOnce, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext, short triggerMask);
ushort SP_GetState(obj_tag *obj);
void SP_HideObj(obj_tag *obj, char hide);
void SP_UnPause(obj_tag *obj);
void SP_Activate(obj_tag *activatorObj, obj_tag *param_2);
void SP_SwitchToScript(obj_tag *obj, short newScriptPlayMode, ScriptPlayerCallback *callback);
float SP_GetHitDamage(obj_tag *obj, obj_tag **outHitObjects, ushort maxHitObjects, ushort *outHitObjectCount, uint bulletFilterFlags, bool *outPlayerWasHit);

#endif // SCRIPTPLAYER_H_