#include "ScriptPlayer.h"
#include "../../math/math.h"
#include "../../engine/Script.h"
#include "../../engine/Collide.h"
#include "../../engine/Loader.h"
#include "../../game.h"
#include "../../input.h"
#include "../../sound/music.h"
#include "../sp/Mission.h"
#include "../sp/PlayerStats.h"
#include "../sp/SwitchChannels.h"
#include "GT.h"
#include "Switch.h"
#include "bullet.h"
#include "../mp/multiplayer.h"

// A fixed-size scratch buffer the original code reuses across calls to Collide_GetDamageNObjects - see SP_GetHitDamage
#define HitObjList (*(obj_tag*(*)[64])0x00279410)

// Playback state machine driven by SP_Update. The forward/reverse cycle is:
//   NotStarted -> PlayingForward -> WaitingReverse -> PlayingReverse -> WaitingForward -> PlayingForward -> ...
// looping forever, unless playOnce is set - in which case PlayingForward goes straight to Finished instead of
// WaitingReverse. Armed/DeferredLoad are only ever entered once, right after creation.
enum SP_State {
    SP_State_NotStarted = 0,     // Waiting for the first trigger before loading/playing anything
    SP_State_WaitingForward = 1, // Finished playing reverse; waiting for a trigger to play forward again
    SP_State_PlayingForward = 2,
    SP_State_WaitingReverse = 3, // Finished playing forward; waiting for a trigger to play reverse
    SP_State_PlayingReverse = 4,
    SP_State_Finished = 5,       // Done for good - marks the object for deletion
    SP_State_Armed = 6,          // Loaded but waiting for the first trigger before actually playing
    SP_State_DeferredLoad = 7,   // One-shot: load the script on the next update, then fall into the normal flow
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
    ScriptPlayerCallback* callback;     // 0x04 - called by SP_LoadScript once the script is loaded, then cleared
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

static_assert(sizeof(SCRIPTPLAYER) == 60, "SCRIPTPLAYER should be 60 bytes (confirmed via Ghidra)");
static_assert(offsetof(SCRIPTPLAYER, scriptInfos) == 0xc, "Bad offset of scriptInfos");
static_assert(offsetof(SCRIPTPLAYER, scriptHashcodes) == 0x1c, "Bad offset of scriptHashcodes");
static_assert(offsetof(SCRIPTPLAYER, scriptPlayModes) == 0x26, "Bad offset of scriptPlayModes");
static_assert(offsetof(SCRIPTPLAYER, nthScript) == 0x2a, "Bad offset of nthScript");
static_assert(offsetof(SCRIPTPLAYER, triggerMask) == 0x2e, "Bad offset of triggerMask");
static_assert(offsetof(SCRIPTPLAYER, someSwitchChannel) == 0x30, "Bad offset of someSwitchChannel");
static_assert(offsetof(SCRIPTPLAYER, someCountdown) == 0x34, "Bad offset of someCountdown");

#pragma pack(pop)

// Forward declaration - defined further down, next to Create_ScriptPlayer_Params
void SP_LoadScript(obj_tag *obj, SCRIPTPLAYER *scriptPlayer);

// AUTOINJECT
void SP_RemoveObj(obj_tag* obj, void* scriptPlayerPtr) {
    if (obj == NULL || scriptPlayerPtr == NULL)
        return;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)scriptPlayerPtr;
    Script_RemoveObj(obj, scriptPlayer->scriptInfos[scriptPlayer->nthScript]);
}

// AUTOINJECT
ushort SP_GetState(obj_tag *obj) {
    if (obj == NULL)
        return 0;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;

    // "finished" states both read as busy/blocked to callers
    if (scriptPlayer->state == SP_State_Finished || scriptPlayer->state == SP_State_WaitingReverse)
        return 2;

    if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] != NULL)
        return (scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeSomeSleepFrames == 0);

    return 0;
}

// AUTOINJECT
void SP_HideObj(obj_tag *obj, char hide) {
    if (obj == NULL)
        return;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;
    scriptPlayer->maybeHideState = hide;
    Script_HideObj(scriptPlayer->scriptInfos[scriptPlayer->nthScript], hide);
}

// AUTOINJECT
void SP_UnPause(obj_tag *obj) {
    if (obj == NULL)
        return;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;
    scriptPlayer->unpauseRequested = 1;
    if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] != NULL) {
        scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeSomeSleepFrames = 0;
    }
}

// Computes the bits of triggerMask that are satisfied this frame by hits against the active script's linked
// objects. Not part of the public API - only ever called from within SP_Update.
// Cannot autoinject - custom calling convention (the original takes its one parameter in EBX). Only used from
// within SP_Update, so not a problem - see View_CaptureSceneSub for another example of this pattern.
// UNINJECTABLE
ushort SP_Hit(SCRIPTPLAYER *scriptPlayer) {
    SCRIPTINFO* scriptInfo = scriptPlayer->scriptInfos[scriptPlayer->nthScript];

    ushort hitFlags = 0;
    if (scriptPlayer->someSwitchChannel != 0 && switch_channels[scriptPlayer->someSwitchChannel] != 0) {
        hitFlags = SP_Trigger_SwitchChannel;
    }

    if (scriptInfo == NULL)
        return hitFlags;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        obj_tag* linkedObj = Script_GetObj(scriptInfo, i);
        if (linkedObj == NULL || linkedObj->hitList == NULL)
            continue;

        // When only bullet hits matter and there's no type filter, pre-filter the hit list up front
        if (scriptPlayer->requiredBulletType == 0 && scriptPlayer->triggerMask == SP_Trigger_BulletHit) {
            Collide_FilterBullets(&linkedObj->hitList, 0x106);
        }

        for (HITDATA_tag* hit = linkedObj->hitList; hit != NULL; hit = hit->next) {
            obj_tag* hitObj = hit->hitObj;
            if (hitObj == NULL)
                continue;

            if (hitObj->objectType == OBJECTTYPE_DRONE) {
                hitFlags |= SP_Trigger_DroneTouch;
            } else if (hitObj->objectType == OBJECTTYPE_PLAYER) {
                hitFlags |= SP_Trigger_PlayerTouch;
            } else if (hitObj->objectType == OBJECTTYPE_BULLET) {
                BU_tag* bullet = (BU_tag*)hitObj->extraObjectData;
                if (scriptPlayer->requiredBulletType == 0 ||
                    bullet->wpnDef->weaponVariantNum == (ushort)(byte)scriptPlayer->requiredBulletType) {
                    hitFlags |= SP_Trigger_BulletHit;
                }
            }
        }
    }

    return hitFlags;
}

// AUTOINJECT
void SP_Activate(obj_tag *activatorObj, obj_tag *param_2) {
    if (activatorObj == NULL || activatorObj->scriptPlayer == NULL)
        return;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)activatorObj->scriptPlayer;
    obj_tag* targetObj = scriptPlayer->ownerObj;

    if (targetObj != NULL) {
        if (targetObj->objectType == OBJECTTYPE_GUNTURRET) {
            GT_Activate(targetObj);
        } else if (targetObj->objectType == OBJECTTYPE_SWITCH) {
            _VECTOR activatorDir, toTarget;
            Mat_GetDir(&activatorDir, &activatorObj->transformMatrix);
            Vec_Subtract(Mat_Position(param_2->transformMatrix), Mat_Position(activatorObj->transformMatrix), &toTarget);

            // Only activate the switch if it's roughly in front of the activator
            if (Vec_Dot(&toTarget, &activatorDir) < 0.0f)
                return;

            Switch_Activate(targetObj, param_2);
            return;
        }
    }

    if ((scriptPlayer->triggerMask & SP_Trigger_Unpause) &&
        (!(scriptPlayer->triggerMask & SP_Trigger_SwitchChannel) || scriptPlayer->someSwitchChannel == 0 ||
         switch_channels[scriptPlayer->someSwitchChannel] != 0)) {
        scriptPlayer->unpauseRequested = 1;
        if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] != NULL) {
            scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeSomeSleepFrames = 0;
        }
    }
}

// AUTOINJECT
void SP_SwitchToScript(obj_tag *obj, short newScriptPlayMode, ScriptPlayerCallback *callback) {
    if (obj == NULL)
        return;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;
    ushort altIndex = (scriptPlayer->nthScript == 0) ? 1 : 0;

    // Only the alternate script's hashcode needs to be set - it doesn't need to already be loaded
    if (scriptPlayer->scriptHashcodes[altIndex] == 0)
        return;

    // This permanently discards the currently active script (unlike the usual forward/reverse toggle in
    // SP_Update, which keeps both scripts around)
    Script_Free(scriptPlayer->scriptInfos[scriptPlayer->nthScript]);
    scriptPlayer->scriptInfos[scriptPlayer->nthScript] = NULL;
    scriptPlayer->scriptHashcodes[scriptPlayer->nthScript] = (HASHCODE)0;

    scriptPlayer->nthScript = altIndex;
    scriptPlayer->scriptPlayModes[scriptPlayer->nthScript] = newScriptPlayMode;
    scriptPlayer->callback = callback;

    SP_LoadScript(obj, scriptPlayer);

    if (scriptPlayer->playOnce == 0) {
        scriptPlayer->state = (scriptPlayer->scriptPlayModes[scriptPlayer->nthScript] != 1) ? SP_State_WaitingReverse : SP_State_PlayingForward;
    }
}

// AUTOINJECT
float SP_GetHitDamage(obj_tag *obj, obj_tag **outHitObjects, ushort maxHitObjects, ushort *outHitObjectCount, uint bulletFilterFlags, bool *outPlayerWasHit) {
    float totalDamage = 0.0f;

    if (outHitObjectCount != NULL) {
        *outHitObjectCount = 0;
    }
    if (outPlayerWasHit != NULL) {
        *outPlayerWasHit = false;
    }

    if (obj == NULL)
        return 0.0f;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;
    SCRIPTINFO* scriptInfo = scriptPlayer->scriptInfos[scriptPlayer->nthScript];
    if (scriptInfo == NULL)
        return 0.0f;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        obj_tag* linkedObj = Script_GetObj(scriptInfo, i);
        if (linkedObj == NULL)
            continue;

        Collide_FilterBullets(&linkedObj->hitList, (ushort)bulletFilterFlags);

        ushort hitCount = 0;
        totalDamage += Collide_GetDamageNObjects(linkedObj->hitList, HitObjList, &hitCount, (ushort)ARRAY_SIZE(HitObjList));

        for (ushort j = 0; j < hitCount; j++) {
            if (HitObjList[j]->objectType != OBJECTTYPE_BULLET)
                continue;

            BU_tag* bullet = (BU_tag*)HitObjList[j]->extraObjectData;
            obj_tag* firedByObj = bullet->firedByObj;

            if (outHitObjects != NULL && outHitObjectCount != NULL && *outHitObjectCount < maxHitObjects) {
                outHitObjects[*outHitObjectCount] = firedByObj;
                (*outHitObjectCount)++;
            }

            if (firedByObj != NULL && firedByObj->objectType == OBJECTTYPE_PLAYER && outPlayerWasHit != NULL) {
                *outPlayerWasHit = true;
            }
        }
    }

    return totalDamage;
}

// AUTOINJECT
void SP_Update(obj_tag* obj) {
    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;

    if (scriptPlayer->maybeHideState != 0)
        return;

    if (scriptPlayer->disableSwitchChannel != 0 && switch_channels[scriptPlayer->disableSwitchChannel] == 0)
        return;

    Script_Update(scriptPlayer->scriptInfos[scriptPlayer->nthScript]);

    ushort hitFlags = (scriptPlayer->someCountdown == 0) ? SP_Hit(scriptPlayer) : 0;

    char switchState = (scriptPlayer->someSwitchChannel == 0) ? 0 : switch_channels[scriptPlayer->someSwitchChannel];

    if (scriptPlayer->someCountdown != 0) {
        scriptPlayer->someCountdown--;
    }

    if (scriptPlayer->unpauseRequested != 0 && scriptPlayer->someCountdown == 0) {
        hitFlags |= SP_Trigger_Unpause;
    }

    bool triggered = scriptPlayer->triggerMask & hitFlags;
    scriptPlayer->unpauseRequested = 0;

    switch (scriptPlayer->state) {
    case SP_State_NotStarted:
        if (triggered) {
            bool okToStart = true;

            if (!MPSettings.isMultiplayer) {
                okToStart = glb_blokes[0]->health > 0.0f;

                int missionStatus = Mission_Status();
                if (missionStatus != 0 && Mission_Status() != 1) {
                    okToStart = false;
                }

                if ((scriptPlayer->scriptHashcodes[0] == 0x6000994 && Get_Music_Event(2) != 0) || !okToStart) {
                    scriptPlayer->state = SP_State_Finished;
                    break;
                }
            }

            if (isLoadable(scriptPlayer->scriptHashcodes[0]) && scriptPlayer->loadingScreenShown == 0) {
                scriptPlayer->loadingScreenShown = 1;
                GameFlow_PushState(0xd, 20.0f, 0xff);
                return;
            }

            SP_LoadScript(obj, scriptPlayer);
            Script_Play(scriptPlayer->scriptInfos[scriptPlayer->nthScript], 0);
            scriptPlayer->state = SP_State_PlayingForward;
        }
        break;

    case SP_State_WaitingForward:
        if (!triggered)
            break;
        scriptPlayer->state = SP_State_PlayingForward;
        switchState = 1;
        scriptPlayer->someCountdown = 5;
        Script_Play(scriptPlayer->scriptInfos[scriptPlayer->nthScript], scriptPlayer->scriptPlayModes[scriptPlayer->nthScript] == 8);
        break;

    case SP_State_PlayingForward: {
        if ((scriptPlayer->triggerMask & SP_Trigger_SwitchChannel) && scriptPlayer->someSwitchChannel != 0 &&
            switch_channels[scriptPlayer->someSwitchChannel] != 0) {
            SP_UnPause(obj);
        }

        if (Input_Action(-1, ACTION_SKIP_CUTSCENE, 1) != 0 &&
            !Script_IsDeathNIS(scriptPlayer->scriptHashcodes[0])) {
            Script_FFwd(scriptPlayer->scriptInfos[scriptPlayer->nthScript], 1);
        }

        if (!Script_IsPlaying(scriptPlayer->scriptInfos[scriptPlayer->nthScript])) {
            if (scriptPlayer->playOnce == 0) {
                ushort altIndex = (scriptPlayer->nthScript == 0) ? 1 : 0;
                if (scriptPlayer->scriptHashcodes[altIndex] != 0) {
                    scriptPlayer->nthScript = altIndex;
                    SP_LoadScript(obj, scriptPlayer);
                }
                scriptPlayer->state = SP_State_WaitingReverse;
            } else {
                scriptPlayer->state = SP_State_Finished;
            }
        }
        break;
    }

    case SP_State_WaitingReverse:
        if (!triggered)
            break;
        scriptPlayer->state = SP_State_PlayingReverse;
        switchState = 0;
        scriptPlayer->someCountdown = 5;
        Script_Play(scriptPlayer->scriptInfos[scriptPlayer->nthScript], scriptPlayer->scriptPlayModes[scriptPlayer->nthScript] == 8);
        break;

    case SP_State_PlayingReverse: {
        if (!Script_IsPlaying(scriptPlayer->scriptInfos[scriptPlayer->nthScript])) {
            ushort altIndex = (scriptPlayer->nthScript == 0) ? 1 : 0;
            if (scriptPlayer->scriptHashcodes[altIndex] != 0) {
                scriptPlayer->nthScript = altIndex;
                SP_LoadScript(obj, scriptPlayer);
            }
            scriptPlayer->state = SP_State_WaitingForward;
        }
        break;
    }

    case SP_State_Finished:
        obj->flags |= 1;
        break;

    case SP_State_Armed: {
        if (triggered) {
            if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] != NULL) {
                scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeSomeSleepFrames = 0;
            }

            // nthScript is always still 0 here - this state is only ever reached right after creation
            ushort altIndex = (scriptPlayer->nthScript == 0) ? 1 : 0;
            if (scriptPlayer->scriptHashcodes[altIndex] != 0) {
                Script_Free(scriptPlayer->scriptInfos[0]);
                scriptPlayer->scriptInfos[0] = NULL;
                scriptPlayer->nthScript = altIndex;
                SP_LoadScript(obj, scriptPlayer);
                scriptPlayer->state = SP_State_PlayingForward;
            }
        }
        break;
    }

    case SP_State_DeferredLoad:
        SP_LoadScript(obj, scriptPlayer);
        break;
    }

    if (scriptPlayer->someSwitchChannel != 0 && triggered) {
        switch_channels[scriptPlayer->someSwitchChannel] = (switchState != 0);
    }

    if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] != NULL &&
        scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeSomeSleepFrames != 0 &&
        scriptPlayer->damageThreshold != 0.0f) {

        bool wasPlayerHit = false;
        float damage = SP_GetHitDamage(obj, NULL, 0, NULL, 0, &wasPlayerHit);
        if (damage != 0.0f) {
            scriptPlayer->accumulatedDamage += damage;

            HASHCODE primaryHashcode = scriptPlayer->scriptHashcodes[0];
            if (wasPlayerHit && (primaryHashcode == 0x60007e2 ||
                (primaryHashcode > 0x60007e3 && primaryHashcode < 0x60007eb))) {
                PlrStat_LogShotHitScenery(0);
            }

            if (scriptPlayer->damageThreshold <= scriptPlayer->accumulatedDamage) {
                SP_UnPause(obj);
                scriptPlayer->accumulatedDamage -= scriptPlayer->damageThreshold;
            }
        }
    }
}

// AUTOINJECT
void SP_Delete(obj_tag *obj) {
    SCRIPTPLAYER *sp = (SCRIPTPLAYER*)obj->extraObjectData;
    Script_Free(sp->scriptInfos[0]);
    sp->scriptInfos[0] = NULL;
    Script_Free(sp->scriptInfos[1]);
    sp->scriptInfos[1] = NULL;
    obj->flags |= 1;
}

// AUTOINJECT
SCRIPTINFO * SP_getScriptInfo(obj_tag *gameObj) {

  if(gameObj == NULL)
    return NULL;
  
  SCRIPTPLAYER* sp = (SCRIPTPLAYER *)gameObj->extraObjectData;
  
  if(sp == NULL)
    return NULL;

  return sp->scriptInfos[sp->nthScript];

}

// AUTOINJECT
void SP_SetColour(obj_tag *gameObj, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Script_SetColour(scriptPlayer->scriptInfos[scriptPlayer->nthScript], clr_r, clr_g, clr_b);
    
}
// AUTOINJECT
void SP_SetPos(obj_tag *gameObj, _VECTOR *newPos) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Vec_Copy(newPos, Mat_Position(scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix));

  Script_SetPosRot(scriptPlayer->scriptInfos[scriptPlayer->nthScript], &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);
    
}

// AUTOINJECT
void SP_SetPosRot(obj_tag *gameObj, _MATRIX *newMtx) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Mat_Copy(newMtx, &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);

  Script_SetPosRot(scriptPlayer->scriptInfos[scriptPlayer->nthScript], &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);
    
}

#pragma pack(push, 1)

// The raw placement-data payload for Place_ScriptPlayer entries (see parsemap_create_dynamic_objects), and also
// synthesised on the stack by SP_Create for script players spawned at runtime rather than from map data.
typedef struct {
    ObjectCreationData_Basic basicCreation; // 0x00-0x2b
    HASHCODE scriptHashcode0;    // 0x2c - hashcode of script #0 -> SCRIPTPLAYER::scriptHashcodes[0]
    HASHCODE scriptHashcode1;   // 0x30 - hashcode of script #1 -> SCRIPTPLAYER::scriptHashcodes[1]
    short scriptPlayMode0;      // 0x34 -> SCRIPTPLAYER::scriptPlayModes[0]
    char pad_36[2];
    short scriptPlayMode1;      // 0x38 -> SCRIPTPLAYER::scriptPlayModes[1]
    char pad_3a[2];
    char playOnce;              // 0x3c -> SCRIPTPLAYER::playOnce
    char pad_3d[3];
    short triggerMask;          // 0x40 -> SCRIPTPLAYER::triggerMask
    char pad_42[2];
    ushort switchChannel;       // 0x44 -> SCRIPTPLAYER::someSwitchChannel
    char pad_46[2];
    int damageThresholdRaw;     // 0x48 -> SCRIPTPLAYER::damageThreshold (unsigned dword, converted to float as-is)
    char requiredBulletType;    // 0x4c -> SCRIPTPLAYER::requiredBulletType
    char pad_4d[3];
    short disableSwitchChannel; // 0x50 -> SCRIPTPLAYER::disableSwitchChannel
    char pad_52[2];
    int scalePercent;           // 0x54 - object scale * 100 (0 => scale defaults to 1.0)
} Create_ScriptPlayer_Params;

static_assert(offsetof(Create_ScriptPlayer_Params, scriptHashcode0) == 0x2c, "Bad offset of scriptHashcode");
static_assert(offsetof(Create_ScriptPlayer_Params, switchChannel) == 0x44, "Bad offset of switchChannel");
static_assert(offsetof(Create_ScriptPlayer_Params, scalePercent) == 0x54, "Bad offset of scalePercent");

#pragma pack(pop)

// Can't generate automatically - custom calling convention. scriptPlayer is expected in ESI by the original
// function (located at 0x000c3e20), while obj stays a normal stack argument - see View_AddCels for another
// example of this pattern.
void __declspec(naked) SP_LoadScript(obj_tag *obj, SCRIPTPLAYER *scriptPlayer) {
    _asm {
        mov esi, [esp + 8]
        mov eax, 0x000C3E20
        jmp eax
    }
}

// AUTOINJECT
obj_tag* SP_CreateScriptPlayer(_VECTOR *pos, _VECTOR *rot, level_tag *placementData, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext) {

    Create_ScriptPlayer_Params* params = (Create_ScriptPlayer_Params*)placementData;

    // Nothing to do if neither script is set
    if (params->scriptHashcode0 == 0 && params->scriptHashcode1 == 0)
        return NULL;

    obj_tag* obj = control_create_object(sizeof(SCRIPTPLAYER), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER*)obj->extraObjectData;
    scriptPlayer->scriptHashcodes[0] = params->scriptHashcode0;
    scriptPlayer->scriptHashcodes[1] = params->scriptHashcode1;
    scriptPlayer->scriptPlayModes[0] = params->scriptPlayMode0;
    scriptPlayer->scriptPlayModes[1] = params->scriptPlayMode1;
    scriptPlayer->playOnce = params->playOnce;
    scriptPlayer->triggerMask = params->triggerMask;
    scriptPlayer->someSwitchChannel = params->switchChannel;

    // params->damageThresholdRaw must be treated as unsigned when converting to float
    scriptPlayer->damageThreshold = (float)(unsigned int)params->damageThresholdRaw;

    scriptPlayer->requiredBulletType = params->requiredBulletType;
    scriptPlayer->disableSwitchChannel = params->disableSwitchChannel;
    scriptPlayer->ownerObj = ownerObj;
    scriptPlayer->state = SP_State_NotStarted;
    scriptPlayer->someCountdown = 0;
    scriptPlayer->nthScript = 0;
    scriptPlayer->scriptInfos[0] = NULL;
    scriptPlayer->scriptInfos[1] = NULL;
    scriptPlayer->callback = funcPtr;
    scriptPlayer->callbackContext = callbackContext;
    obj->objectType = OBJECTTYPE_SCRIPTPLAYER;
    switch_channels[scriptPlayer->someSwitchChannel] = 0;
    scriptPlayer->loadingScreenShown = 0;

    if (scriptPlayer->triggerMask == 0) {
        scriptPlayer->state = SP_State_Armed;
    }

    if (scriptPlayer->scriptPlayModes[0] == 1) {
        scriptPlayer->state = SP_State_DeferredLoad;
    } else if (scriptPlayer->scriptPlayModes[0] != 0) {
        SP_LoadScript(obj, scriptPlayer);
    }

    if (scriptPlayer->scriptInfos[0] != NULL) {
        float scale = (params->scalePercent == 0) ? 1.0f : ((float)(unsigned int)params->scalePercent * 0.01f);
        scriptPlayer->scriptInfos[0]->scale = scale;
        if (scriptPlayer->scriptInfos[1] != NULL) {
            scriptPlayer->scriptInfos[1]->scale = scale;
        }
    }

    return obj;
}

// Convenience wrapper around SP_CreateScriptPlayer for script players spawned at runtime (eg. Switch_Create's
// flip-animation player) rather than from map placement data - synthesises the placement struct on the stack.
// Note playMode is applied to BOTH scriptPlayModes[0] and [1] (matches the original exactly).
// AUTOINJECT
obj_tag* SP_Create(_VECTOR *pos, _VECTOR *rot, HASHCODE scriptHashcode0, HASHCODE scriptHashcode1, short playMode, char playOnce, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext, short triggerMask) {
    Create_ScriptPlayer_Params params = {};
    params.scriptHashcode0 = scriptHashcode0;
    params.scriptHashcode1 = scriptHashcode1;
    params.scriptPlayMode0 = playMode;
    params.scriptPlayMode1 = playMode;
    params.playOnce = playOnce;
    params.triggerMask = triggerMask;

    if (rot == NULL)
        rot = &CONST_ZERO_VECTOR;

    return SP_CreateScriptPlayer(pos, rot, (level_tag*)&params, ownerObj, funcPtr, callbackContext);
}
