#include "Script.h"
#include "Anim.h"
#include "Loader.h"
#include "Text.h"
#include "Camera.h"
#include "viewer.h"
#include "../memory.h"
#include "../math/math.h"
#include "../sound/music.h"
#include "../sound/Sound.h"
#include "../game/drone/Drone.h"
#include "../game/obj/Player.h"
#include "../game/obj/Light.h"
#include "../game/sp/PlayerStats.h"
#include "../gfx/Sprite.h"
#include "../input.h"

// Two persistent letterbox-bar sprites, disabled (0xff) by Script_KillStream when a camera stream ends
#define Borders (*(sprite*(*)[2])0x00279350)

typedef enum ScriptCmd {
    ScriptCmd_EndScript=4,
    ScriptCmd_SetSomeFloat=5,
    ScriptCmd_DoSomeThingFromAWord=6,
    ScriptCmd_EntityStart=7,
    ScriptCmd_EntityEnd=9,
    ScriptCmd_AnimStart=10,
    ScriptCmd_AnimEnd=12,
    ScriptCmd_CameraStart=13,
    ScriptCmd_CameraEnd=15,
    ScriptCmd_EventHandler=18,
    ScriptCmd_FadeStart=19,
    ScriptCmd_SpriteStart=20,
    ScriptCmd_SpriteEnd=21,
    ScriptCmd_SoundStart=22,
    ScriptCmd_SoundEnd=23,
    ScriptCmd_LightStart=24,
    ScriptCmd_LightEnd=26,
    ScriptCmd_SubScriptStart=27,
    ScriptCmd_SubScriptEnd=29,
    ScriptCmd_TextStart=30
} ScriptCmd;

// The real bytecode interpreter core (250+ instructions, dispatches into Script_EntityStart/Script_AnimStart/
// Script_CameraStart/Script_LightStart/Script_SoundStart/Script_SpriteStart/Script_SubScriptStart/
// Script_EventHandler and more, via Script_Interp). Left un-reimplemented for now. Can't autogenerate a wrapper
// for it either - custom calling convention (scriptInfo is expected in EBX by the original function, located
// at 0x000c3330) - so it gets the same hand-written trampoline treatment as SP_LoadScript.
//
// Unlike SP_LoadScript, this one gets CALLed (not tail-jmp'd) with EBX saved/restored around it: the original
// function freely clobbers EBX (it's used as the scriptInfo pointer throughout, never pushed/popped), but our
// C++ callers are ordinary __cdecl and MSVC is entitled to assume EBX survives an ordinary-looking call to us -
// Script_Update calls this in a tight loop over every stream, so a plain tail-jmp here was letting the
// interpreter silently stomp whatever the compiler kept cached in EBX across loop iterations.
void __declspec(naked) Script_Run(SCRIPTINFO *scriptInfo) {
    _asm {
        push ebx
        mov ebx, [esp + 8]
        mov eax, 0x000C3330
        call eax
        pop ebx
        ret
    }
}

// Cleans up whatever a single active stream slot is currently linked to, depending on its type, then clears the
// slot. "Restoring" (ScriptFlag_RestoreOnKillStream) leaves persistent things (the object itself, a light) alone
// and just detaches the script's control over them; otherwise they're deleted/freed outright.
// AUTOINJECT
void Script_KillStream(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    switch (stream->whatKindOfInterpolation) {
    case SStream_Entity: {
        obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj != NULL) {
            linkedObj->scriptPlayer = NULL;
            if (scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream) {
                Vec_Copy2(Mat_Position(linkedObj->transformMatrix), &linkedObj->position, &linkedObj->lastPosition);
                linkedObj->renderType |= 0x20;
            } else {
                linkedObj->flags |= 1; // mark for deletion
            }
        }
        break;
    }

    // Anim/Sprite/Camera/Light all deliberately leave the slot untouched (return rather than falling through to
    // the clear below) when there's nothing linked - matching the original exactly, unlike Entity above
    case SStream_Anim: {
        obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj == NULL)
            return;

        if (scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream) {
            linkedObj->extraObjectData = NULL;
        } else {
            linkedObj->flags |= 1; // mark for deletion
        }
        break;
    }

    case SStream_Sprite:
        if (stream->linkedGameOrSoundObject == NULL)
            return;
        Sprite_Delete((sprite*)stream->linkedGameOrSoundObject);
        break;

    case SStream_Camera: {
        void* cameraState = stream->linkedGameOrSoundObject;
        if (cameraState == NULL)
            return;

        Camera_PopStates();
        Camera_Enable(4, 0, 0, NULL);
        Mem_Free(&cameraState);
        Music_Event(0xc, scriptInfo->scriptHashcode);

        if (Borders[0] != NULL) {
            Borders[0]->maybeEnabled = 0xff;
        }
        if (Borders[1] != NULL) {
            Borders[1]->maybeEnabled = 0xff;
        }
        break;
    }

    case SStream_Sound:
        Sound_Stop((DYNAMICSOUNDS*)stream->linkedGameOrSoundObject);
        break;

    case SStream_Light:
        if (stream->linkedGameOrSoundObject == NULL)
            return;
        if (!(scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream)) {
            Light_Delete((light_tag*)stream->linkedGameOrSoundObject);
        }
        break;

    case SStream_SubScript:
        if (stream->linkedGameOrSoundObject != NULL) {
            Script_Free((SCRIPTINFO*)stream->linkedGameOrSoundObject);
        }
        break;
    }

    // Every case above either returned early (nothing linked, for the types where that means "leave it alone")
    // or falls through to here to detach the (possibly just freed/deleted) link
    stream->linkedGameOrSoundObject = NULL;
    stream->whatKindOfInterpolation = 0;
}

// The per-stream evaluator (363 instructions) - given a stream's current playback position, computes and applies
// whatever it drives (entity transform, camera cut, sound volume/position, sprite fade, ...). Left un-reimplemented
// for now.
// AUTOGEN
void Script_Interp(SCRIPTINFO *scriptInfo, SSTREAM *stream);

// AUTOINJECT
void Script_SetPosRot(SCRIPTINFO *scriptInfo, _MATRIX *mtx) {
    Mat_Copy(mtx, &scriptInfo->maybeMatrix);

    if (scriptInfo->maybeSomeSleepFrames != 0) {
        for (ushort i = 0; i < scriptInfo->numElements; i++) {
            Script_Interp(scriptInfo, scriptInfo->streamDataBuffer + i);
        }
    }
}

// AUTOGEN - the binary script-asset loader/parser
SCRIPTINFO* Script_Load(HASHCODE hashcode, _VECTOR *transform, _VECTOR *rotation, uint *fileBuf, void *maybeScriptPlayer, void *callback, void *callbackContext);

// AUTOINJECT
void Script_Update(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return;

    if (!(scriptInfo->playbackFlags & ScriptPlayback_Playing))
        return;

    if (scriptInfo->maybeSomeSleepFrames != 0) {
        if (scriptInfo->maybeSomeSleepFrames > 0) {
            scriptInfo->maybeSomeSleepFrames--;
        }
        return;
    }

    // This can genuinely loop more than once per call: if Script_Run's own bookkeeping flips the script to
    // "no longer playing" partway through (eg. it just reached its last command), we fall through to finalize
    // below in the same tick rather than waiting for the next Script_Update call.
    for (;;) {
        if (Script_IsPlaying(scriptInfo)) {
            scriptInfo->streamNum = 0;
            while ((ushort)scriptInfo->streamNum < scriptInfo->numElements) {
                Script_Run(scriptInfo);
                scriptInfo->streamNum++;
            }

            scriptInfo->maybeProgressFrames += FRAME_RATE_MUL;

            scriptInfo->streamNum = 0;
            while ((ushort)scriptInfo->streamNum < scriptInfo->numElements) {
                scriptInfo->streamDataBuffer[scriptInfo->streamNum].elapsedFrames += FRAME_RATE_MUL;
                scriptInfo->streamNum++;
            }

            // Pre-emptively start fading out a scripted camera NIS once it's within 20 frames of its end
            if (ScriptCam != 0 && (scriptInfo->scriptFlags & ScriptFlag_IsCameraNIS) &&
                (float)(int)(scriptInfo->maybeNumFrames - 20) <= scriptInfo->maybeProgressFrames &&
                glb_viewer[4]->fadeActive != 1) {

                if (Script_IsDeathNIS(scriptInfo->scriptHashcode)) {
                    goto finalize;
                }

                // A handful of specific scripts (hashcode-identified) either skip this automatic fade entirely,
                // or opt out of it - exact reason for each isn't confirmed
                if (scriptInfo->scriptHashcode != 0x60008c8 && scriptInfo->scriptHashcode != 0x6000080) {
                    if (scriptInfo->scriptHashcode != 0x60004fa && scriptInfo->scriptHashcode != 0x6000994) {
                        Camera_SetFade(4, 40.0f, 0xff, 0);
                    }
                }
            }
        }

        // Advance the fade-out timer (started by Script_FFwd) once it's running
        if (scriptInfo->fadeElapsedFrames != 0.0f) {
            if ((float)scriptInfo->fadeFrames < scriptInfo->fadeElapsedFrames) {
                scriptInfo->fadeFlags |= ScriptFade_ThresholdHit;
            } else {
                scriptInfo->fadeElapsedFrames += FRAME_RATE_MUL;
            }
        }

        if (scriptInfo->fadeElapsedFrames <= (float)scriptInfo->fadeFrames || !Script_IsPlaying(scriptInfo)) {
            break;
        }
    }

finalize:
    if (scriptInfo->fadeElapsedFrames <= (float)scriptInfo->fadeFrames)
        return;

    // Still playing (only reachable via the DeathNIS goto above) - not done fading yet
    if (Script_IsPlaying(scriptInfo))
        return;

    Camera_SetFade(0, -22.0f, 0xff, 1);
    scriptInfo->playbackFlags &= ~ScriptPlayback_FFwdFading;
    scriptInfo->fadeFlags &= ~ScriptFade_FFwdRequested;
    scriptInfo->fadeElapsedFrames = 0.0f;
}

// Kicks off (or cancels) the "skip cutscene" fade-out. doFFwd==false always cancels any in-progress fade;
// doFFwd==true only starts one if the script permits it (ScriptFade_FFwdAllowed) and one isn't already running.
// AUTOINJECT
void Script_FFwd(SCRIPTINFO *scriptInfo, char doFFwd) {
    if (scriptInfo == NULL)
        return;

    if (doFFwd == 0 || !(scriptInfo->fadeFlags & ScriptFade_FFwdAllowed) || (scriptInfo->fadeFlags & ScriptFade_FFwdRequested)) {
        scriptInfo->playbackFlags &= ~ScriptPlayback_FFwdFading;
        return;
    }

    if (ScriptCam != 0) {
        Input_ClearAllActions(-1);
        Text_FlushAllSubtitles();
    }

    if (scriptInfo->scriptFlags & ScriptFlag_FFwdKillsCamera) {
        // Skip straight to the end by killing the active camera stream outright, rather than fading out
        for (ushort i = 0; i < scriptInfo->numElements; i++) {
            if (scriptInfo->streamDataBuffer[i].whatKindOfInterpolation == SStream_Camera) {
                Script_KillStream(scriptInfo, scriptInfo->streamDataBuffer + i);
                return;
            }
        }
        return;
    }

    // Fade out over whatever's left of the script, capped at 45 frames
    float framesRemaining = (float)scriptInfo->maybeNumFrames - scriptInfo->maybeProgressFrames;
    if (framesRemaining > 45.0f)
        framesRemaining = 45.0f;
    scriptInfo->fadeFrames = (char)(int)framesRemaining;
    scriptInfo->fadeElapsedFrames = FRAME_RATE_MUL;
    scriptInfo->playbackFlags |= ScriptPlayback_FFwdFading;
    Music_Event(5, scriptInfo->scriptHashcode);
    scriptInfo->fadeFlags |= ScriptFade_FFwdRequested;
    Camera_SetFade(4, (float)scriptInfo->fadeFrames, 0xff, 1);

    // Also nudge every currently-linked Anim stream's object: sets bit 0x40000 at AnimState::animObj+0x50 (not
    // otherwise mapped yet) for anything with an AnimState. The original checks this via "animState + 0x58 == 0"
    // (ie. animState == -0x58) rather than a plain null check - that's just the compiler reusing the flags from
    // computing &animState->animObj (animState+0x58) instead of emitting a separate TEST; it's a null check.
    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM *stream = scriptInfo->streamDataBuffer + i;
        if (stream->whatKindOfInterpolation != SStream_Anim || stream->linkedGameOrSoundObject == NULL)
            continue;

        obj_tag *linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj->animState != NULL) {
            uint *flagsField = (uint*)((char*)&linkedObj->animState->animObj + 0x50);
            *flagsField |= 0x40000;
        }
    }
}

// AUTOINJECT
bool Script_IsDeathNIS(HASHCODE hashcode) {
    return hashcode == 0x6000952 || hashcode == 0x600098a || hashcode == 0x6000991;
}

// AUTOINJECT
bool Script_IsPlaying(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return false;

    // AtEnd set without Reverse means "not playing"
    if (!(scriptInfo->playbackFlags & ScriptPlayback_Reverse) && (scriptInfo->playbackFlags & ScriptPlayback_AtEnd))
        return false;

    return scriptInfo->playbackFlags & ScriptPlayback_Playing;
}

// The original always returns false - every path through this function (including the null check below) ends in
// "return false", and every caller we've checked (SP_LoadScript, Explode_Create, C_NIS_Handler, ...) discards the
// result anyway. Kept as bool/false rather than void to match the original faithfully, in case some not-yet-found
// caller does care - but as far as we can tell, this return value is dead.
// AUTOINJECT
bool Script_Play(SCRIPTINFO *scriptInfo, char reverse) {
    if (scriptInfo == NULL)
        return false;

    if (reverse) {
        scriptInfo->playbackFlags |= ScriptPlayback_Reverse;
    } else {
        scriptInfo->playbackFlags &= ~ScriptPlayback_Reverse;     
    }
    
    scriptInfo->playbackFlags |= ScriptPlayback_Playing;   

    if (scriptInfo->scriptFlags & ScriptFlag_IsCameraNIS) {
        ScriptCam = scriptInfo->scriptHashcode;

        if (Script_IsDeathNIS(scriptInfo->scriptHashcode)) {
            Music_Event(6, 1);
        }

        Text_FlushAllSubtitles();
    }

    return false;
}

// AUTOINJECT
obj_tag* Script_GetObj(SCRIPTINFO *scriptInfo, ushort streamIdx) {
    if (scriptInfo == NULL || streamIdx >= scriptInfo->numElements)
        return NULL;

    uchar kind = scriptInfo->streamDataBuffer[streamIdx].whatKindOfInterpolation;
    if (kind == 0 || kind >= SStream_Sprite)
        return NULL;

    return (obj_tag*)scriptInfo->streamDataBuffer[streamIdx].linkedGameOrSoundObject;
}

// AUTOINJECT
void Script_HideObj(SCRIPTINFO *scriptInfo, char hide) {
    if (scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM* stream = &scriptInfo->streamDataBuffer[i];
        uchar kind = stream->whatKindOfInterpolation;

        // Only entity/anim streams have a hideable linked object
        if (kind != 0 && kind < SStream_Sprite && stream->linkedGameOrSoundObject != NULL) {
            obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
            if (hide == 0) {
                linkedObj->effectFlags &= ~FLAG_HIDDEN;
            } else {
                linkedObj->effectFlags |= FLAG_HIDDEN;
            }
        }

        // Hiding also kills any active sound stream outright
        if (kind == SStream_Sound) {
            Script_KillStream(scriptInfo, stream);
        }
    }
}

// AUTOINJECT
void Script_RemoveObj(obj_tag *obj, SCRIPTINFO *scriptInfo) {
    if (obj == NULL || scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM* stream = &scriptInfo->streamDataBuffer[i];
        if (stream->whatKindOfInterpolation == SStream_Entity && stream->linkedGameOrSoundObject == obj) {
            Script_KillStream(scriptInfo, stream);
            return;
        }
    }
}

// AUTOINJECT
void Script_SetColour(SCRIPTINFO *scriptInfo, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b) {
    if (scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        uchar kind = scriptInfo->streamDataBuffer[i].whatKindOfInterpolation;
        if (kind == 0 || kind >= SStream_Sprite)
            continue;

        obj_tag* linkedObj = (obj_tag*)scriptInfo->streamDataBuffer[i].linkedGameOrSoundObject;
        if (linkedObj == NULL)
            continue;

        linkedObj->tweakR = clr_r;
        linkedObj->tweakG = clr_g;
        linkedObj->tweakB = clr_b;
    }
}

// AUTOINJECT
void Script_Free(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return;

    // Script_KillStream mutates streamNum/streamDataBuffer as it goes, so this mirrors the original's use of
    // streamNum (rather than a fresh loop counter) as the iteration index
    scriptInfo->streamNum = 0;
    while ((ushort)scriptInfo->streamNum < scriptInfo->numElements) {
        Script_KillStream(scriptInfo, scriptInfo->streamDataBuffer + scriptInfo->streamNum);
        scriptInfo->streamNum++;
    }

    HASHCODE hashcode = scriptInfo->scriptHashcode;
    if (isLoadable(hashcode)) {
        SFXSuspendFileAccess();
        LoadableReload(hashcode);
        SFXUnSuspendFileAccess();
    }

    if (scriptInfo->scriptFlags & ScriptFlag_ReEnableDronesOnFree) {
        Drone_EnableAll(1, hashcode);
    }

    if (scriptInfo->pausesPlayer & 1) {
        Player_Enable(glb_players[0], &scriptInfo->otherMatrix, (int)(byte)scriptInfo->playerEnableFlags);
        PlarStat_LogTimerUnpause(0);
    }

    if (ScriptCam == hashcode) {
        ScriptCam = (HASHCODE)0;
    }

    Mem_Free((void**)&scriptInfo);
}