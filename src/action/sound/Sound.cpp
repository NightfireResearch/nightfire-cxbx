#include "Sound.h"
#include <string.h>
#include <stdio.h>

#include "SFX.h"
#include "../util/DList.h"
#include "../math/math.h"
#include "../engine/Text.h"
#include "../engine/Script.h"
#include "../engine/viewer.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"

#include <math.h>

// XBE_GLOBAL(0x0029a128, 0x18)
#define DynamicSoundList ((DLISTINFO_tag*)(0x0029a128))

// AUTOINJECT
bool Sound_SetPosition(DYNAMICSOUNDS *handle, _VECTOR *position) {

    if(handle == NULL || position == NULL)
        return false;

    Vec_Copy(position, &handle->location);
    handle->needsUpdate = true;

    if((handle->sfxId < ARRAY_SIZE(SFXOutputData)) && SFXOutputData[handle->sfxId].loopAlways)
        return handle->playbackState == 1;

    return SFXIsSFXPlaying(handle, -1);
}

// AUTOINJECT
bool Sound_SetVolume(DYNAMICSOUNDS *handle, float volume) {

    if (handle == NULL)
        return false;

    handle->volume = volume;
    handle->needsUpdate = true;

    if((handle->sfxId < ARRAY_SIZE(SFXOutputData)) && SFXOutputData[handle->sfxId].loopAlways)
        return handle->playbackState == 1;

    return SFXIsSFXPlaying(handle, -1);

}

// AUTOINJECT
void Sound_Stop(DYNAMICSOUNDS *handle) {
    if(handle == NULL)
        return;

    handle->playbackState = 2;
}

// AUTOINJECT
void Sound_SetAlertness(DYNAMICSOUNDS *handle, float alert) {

    if (handle == NULL)
        return;

    handle->alertness = alert;

}

// AUTOINJECT
void Sound_ModAlertness(DYNAMICSOUNDS *handle, float multiplier) {

    if (handle == NULL)
        return;

    handle->alertness *= multiplier;

}

// AUTOINJECT
bool Sound_IsLooping(DYNAMICSOUNDS *handle) {

    if (handle == NULL)
        return true;

    if(handle->sfxId >= ARRAY_SIZE(SFXOutputData))
        return false;
    
    return SFXOutputData[handle->sfxId].loopAlways;

}

// The originals, called by address
#define SFXInitialise ((void (__cdecl *)(char *bankName))0x000c9d60)
#define SFXReset ((void (__stdcall *)(void))0x000c9c30)

// Set by Sound_Ready once the level's bank is loaded
#define SoundInit U32_AT(0x0029a280)
#define MusicEventList (*(uint32_t(*)[0x40])0x0029a180)

// Written by Sound_UpdateListeners, once per frame
#define GlobalListenerPos (*(_VECTOR*)0x0029a140)

// The sound ids' low 20 bits index SFXOutputData
#define SFX_ID_MASK 0xfffff
#define NUM_DYNAMIC_SOUNDS 0x100
// The view a cut scene's camera uses
#define SCRIPT_VIEWER 4

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// AUTOINJECT
void Sound_Init(void) {
    DList_Init(DynamicSoundList, sizeof(DYNAMICSOUNDS), NUM_DYNAMIC_SOUNDS);
    SFXInitialise(NULL);
    memset(MusicEventList, 0, sizeof(MusicEventList));
}

// AUTOINJECT
void Sound_Kill(void) {
    if (SoundInit)
        SFXReset();
    SoundInit = 0;
}

// A node of DynamicSoundList, to start next frame (Sound_UpdateSounds). A radius above zero is taken (and the
// sound's distance limited), any other the sound's own (10 and 20 past the table).
DYNAMICSOUNDS* _Sound_Play(Action_SFX sfxId, _VECTOR *position, float volume, float radiusOuter, float radiusInner, short pitch, char is3d, undefined4 ref) {

    DYNAMICSOUNDS* snd = (DYNAMICSOUNDS *)DList_MoveFromFree2InUse(DynamicSoundList);
    if (snd == NULL) {
        NF_WARN("Summut hasnt freed there sound handle/or samples not finnished\n"); // GC check (0x800652cc)
        return NULL;
    }

    uint id = sfxId & SFX_ID_MASK;
    uint limitedDistance = 0;

    if (radiusInner <= 0.0f)
        radiusInner = id < ARRAY_SIZE(SFXOutputData) ? SFXOutputData[id].defaultRadiusInner : 10.0f;
    else
        limitedDistance = 1;

    if (radiusOuter <= 0.0f)
        radiusOuter = id < ARRAY_SIZE(SFXOutputData) ? SFXOutputData[id].defaultRadiusOuter : 20.0f;
    else
        limitedDistance = 1;

    snd->radiusOuter = radiusOuter;
    snd->limitedDistance = limitedDistance;
    snd->radiusInner = radiusInner;
    snd->sfxId = sfxId;
    snd->volume = volume;
    snd->playbackState = 0;
    snd->is3d = is3d;
    snd->maybePitchBend = pitch;
    snd->needsUpdate = true;
    snd->age = 0;
    snd->alertness = id < ARRAY_SIZE(SFXOutputData) ? SFXOutputData[id].alertnessRelated : 0.0f;
    snd->audible = 0;
    snd->isLimitedRadius = 0;
    snd->unknown44 = 0;
    snd->ref = ref;

    Vec_Copy(position, &snd->location);
    Vec_Copy(&CONST_ZERO_VECTOR, &snd->velocity);

    return snd;
}

// The original takes the sound in EAX, the rest on the stack
// AUTOLTCG
__declspec(naked) DYNAMICSOUNDS* Sound_Play(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 36]   // ref
        push dword ptr [esp + 36]   // is3d
        push dword ptr [esp + 36]   // pitch
        push dword ptr [esp + 36]   // radiusInner
        push dword ptr [esp + 36]   // radiusOuter
        push dword ptr [esp + 36]   // volume
        push dword ptr [esp + 36]   // position
        push eax
        call _Sound_Play
        add esp, 32
        pop edx
        pop ecx
        ret
    }
}

// Starts a sound. A 2D one always gives its subtitle. A 3D one past 1.1 times its outer radius does not start if
// there is one listener and the table has it truly 3D and not looping; one within half its outer radius, in a cut
// scene, or not truly 3D, gives its subtitle.
void _Sound_ReqestPlaySfx(DYNAMICSOUNDS *snd) {
    if (!snd->is3d) {
        SFXStart(snd->sfxId & SFX_ID_MASK, snd);
        snd->audible = 1;
        return;
    }

    double dist = Vec_Dist3D(&GlobalListenerPos, &snd->location);
    uint listeners = MPSettings.isMultiplayer ? MPSettings.numPlayers : 1;
    uint id = snd->sfxId;
    bool inTable = id < ARRAY_SIZE(SFXOutputData);
    if (!(inTable && SFXOutputData[id].loopAlways) && listeners == 1 && inTable && SFXOutputData[id].maybeTracked3d
        && dist > (double)snd->radiusOuter * 1.1f)
        return;

    _VECTOR pos = snd->location;
    _VECTOR vel = snd->velocity;
    snd->audible = dist < 0.5f * (double)snd->radiusOuter;
    if (ScriptCam != HASHCODE_NONE || !inTable || !SFXOutputData[id].maybeTracked3d)
        snd->audible = 1;

    if (snd->limitedDistance)
        SFXStart3D(id & SFX_ID_MASK, &pos, &vel, snd, Ftol(snd->volume), Ftol(snd->radiusInner),
                   Ftol(snd->radiusOuter));
    else
        SFXStart3D(id & SFX_ID_MASK, &pos, &vel, snd, Ftol(snd->volume), -1, -1);
}

// The original takes the sound in EDI
// AUTOLTCG
__declspec(naked) void Sound_ReqestPlaySfx(void) {
    _asm {
        push ecx
        push edx
        push edi
        call _Sound_ReqestPlaySfx
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// Every sound in use, once a frame: a new one started, a playing one stopped once it ends (a looping one only when
// stopped), a stopped one freed; a playing 3D one moved. The listener is not read.
// AUTOINJECT
void Sound_UpdateSounds(_VECTOR *listener) {
    DYNAMICSOUNDS *next;
    for (DYNAMICSOUNDS *snd = (DYNAMICSOUNDS *)DynamicSoundList->activeList.head; snd != NULL; snd = next) {
        next = snd->next;
        snd->age++;

        switch (snd->playbackState) {
        case 0:
            _Sound_ReqestPlaySfx(snd);
            snd->playbackState = 1;
            if (snd->audible)
                Sound_DoSubtitle((Action_SFX)snd->sfxId);
            break;
        case 1: {
            bool playing;
            if (snd->sfxId < ARRAY_SIZE(SFXOutputData) && SFXOutputData[snd->sfxId].loopAlways)
                playing = snd->playbackState == 1;
            else
                playing = SFXIsSFXPlaying(snd, -1);
            if (!playing)
                snd->playbackState = 2;
            break;
        }
        case 2:
            SFXRemove(snd, -1);
            DList_RemoveFromInUse2Free(DynamicSoundList, (LLNODE_tag *)snd);
            snd->playbackState = 3;
            snd->ref = 0;
            break;
        }

        if (snd->is3d && snd->playbackState == 1) {
            _VECTOR pos = snd->location;
            _VECTOR vel = snd->velocity;
            SFXSetPositionAndVelocity(&pos, &vel, snd, -1);
        }
        snd->needsUpdate = false;
    }
}

// Each player's view is a listener, and a cut scene's camera the first; the last of them places the map sounds and
// sets the environment, from its cel.
// AUTOINJECT
void Sound_UpdateListeners(void) {
    ushort listeners = MPSettings.isMultiplayer ? MPSettings.numPlayers : 1;
    viewer_tag *view = glb_viewer[0];
    // The cut scene's listener moves as the last player's did: uninitialised stack in the original with none
    _VECTOR move = {0.0f, 0.0f, 0.0f};
    _VECTOR up, dir, norm;

    for (uint i = 0; i < listeners; i++) {
        view = glb_viewer[i];
        if (view == NULL)
            continue;
        Vec_Subtract(&view->pos, &view->prevPos, &move);
        Mat_GetUp(&up, &view->viewMatrix);
        Mat_GetDir(&dir, &view->viewMatrix);
        Mat_GetNorm(&norm, &view->viewMatrix);
        _VECTOR pos = *(_VECTOR *)&view->viewMatrix.m[12];
        _VECTOR vel = move, listenerDir = dir, listenerUp = up, listenerNorm = norm;
        SFXSetupListener(&pos, &vel, &listenerDir, &listenerUp, &listenerNorm, i);
    }

    viewer_tag *script = glb_viewer[SCRIPT_VIEWER];
    if (script != NULL && script->enabled) {
        view = script;
        Mat_GetUp(&up, &view->viewMatrix);
        Mat_GetDir(&dir, &view->viewMatrix);
        Mat_GetNorm(&norm, &view->viewMatrix);
        _VECTOR pos = *(_VECTOR *)&view->viewMatrix.m[12];
        _VECTOR vel = move, listenerDir = dir, listenerUp = up, listenerNorm = norm;
        SFXSetupListener(&pos, &vel, &listenerDir, &listenerUp, &listenerNorm, 0);
    }

    if (view != NULL)
        GlobalListenerPos = *(_VECTOR *)&view->viewMatrix.m[12];

    if (view != NULL) {
        cel_tag *cel = view->someCel;
        if (cel != NULL) {
            bool outdoors = (cel->bitsFromPlacementTag & CEL_NO_SKY) || cel->soundEnvironment >= 0x14;
            SFXSetEnvironment(cel->soundEnvironment, outdoors);
            HandleMapSoundAllocation();
        }
        Sound_UpdateSounds((_VECTOR *)&view->viewMatrix.m[12]);
    }
    SFXUpdate();
}

// AUTOINJECT
bool Sound_IsFinnished(DYNAMICSOUNDS *handle) {
    if (handle == NULL)
        return true;
    return handle->playbackState == 2;
}

// The alertness heard at pos: each sound playing with any, not looping, within its outer radius, adds it, all of it
// within its inner radius and falling to none at the outer. level gets the highest the sum reached, and *loudest the
// sound that took it there. Sounds 0x11c and 0x11d are not heard in TowerA and TowerB; sound 0x248 becomes as loud
// to its outer radius, with alertness 10.
// AUTOINJECT
float Sound_Alertness(_VECTOR *pos, float *level, DYNAMICSOUNDS **loudest) {
    float sum = 0.0f;
    float best = 0.0f;
    if (loudest != NULL)
        *loudest = NULL;

    DYNAMICSOUNDS *next;
    for (DYNAMICSOUNDS *snd = (DYNAMICSOUNDS *)DynamicSoundList->activeList.head; snd != NULL; snd = next) {
        next = snd->next;
        if (snd->playbackState == 2 || snd->playbackState == 3)
            continue;
        if (snd->sfxId < ARRAY_SIZE(SFXOutputData) && SFXOutputData[snd->sfxId].loopAlways)
            continue;
        if (snd->alertness == 0.0f)
            continue;

        float distSq = (float)Vec_SqDist3D(pos, &snd->location);
        if (distSq > (double)snd->radiusOuter * snd->radiusOuter)
            continue;

        uint id = snd->sfxId & SFX_ID_MASK;
        if (id >= 0x11c) {
            if (id <= 0x11d) {
                if (GameState.CurrentLevelHashcode == HT_Level_TowerA || GameState.CurrentLevelHashcode == HT_Level_TowerB)
                    continue;
            } else if (id == 0x248) {
                snd->radiusInner = snd->radiusOuter;
                snd->alertness = 10.0f;
            }
        }

        double dist = sqrt((double)distSq);
        double loudness;
        if (dist < snd->radiusInner)
            loudness = 1.0;
        else
            loudness = (snd->radiusOuter - dist) / ((double)snd->radiusOuter - snd->radiusInner);
        double total = loudness * snd->alertness + sum;
        sum = (float)total;
        if (total > best) {
            if (loudest != NULL)
                *loudest = snd;
            best = sum;
        }
    }

    *level = best;
    return sum < 0.0f ? 0.0f : sum;
}

// AUTOINJECT
void Sound_Stop_Ref(DYNAMICSOUNDS *handle, undefined4 ref) {
    if (handle != NULL && handle->ref == ref && handle->playbackState != 3)
        handle->playbackState = 2;
}

// AUTOINJECT
bool Sound_Playing_Ref(DYNAMICSOUNDS *handle, undefined4 ref) {
    return handle != NULL && handle->ref == ref && (handle->playbackState == 1 || handle->playbackState == 0);
}

// XBE_GLOBAL(0x0029a288, 0x4)
static uint32_t Sound_FrameDelay;

// AUTOINJECT
DYNAMICSOUNDS* Sound_Play3D(Action_SFX param_1,_VECTOR *position,float volume,float radiusOuter,float radiusInner, undefined2 maybePitchBend,undefined4 param_7,int isLimitedRadius) {
  
    undefined1 tmpFrameDelay = (undefined1)Sound_FrameDelay;
    Sound_FrameDelay = 0;
    
    if ((param_1 & 0xfffff) == 0xffff)
        return NULL;

    if (isLimitedRadius == 0) {
        radiusInner = -1.0;
        radiusOuter = -1.0;
    }
    DYNAMICSOUNDS *snd = _Sound_Play(param_1,position,volume,radiusOuter,radiusInner,maybePitchBend,1,param_7);
    if (snd != NULL) {
        snd->isLimitedRadius = (char)isLimitedRadius;
        snd->frameDelay = tmpFrameDelay;
    }
    
    return snd;
}

// AUTOINJECT
DYNAMICSOUNDS* Sound_PlayExt(Action_SFX param_1, float volume, undefined2 maybePitchBend, undefined4 param_4) {
      
    undefined1 tmpFrameDelay = (undefined1)Sound_FrameDelay;
    Sound_FrameDelay = 0;
    
    if ((param_1 & 0xfffff) == 0xffff)
        return NULL;

    DYNAMICSOUNDS *snd = _Sound_Play(param_1,&CONST_ZERO_VECTOR,volume,-1.0,-1.0,maybePitchBend,0,param_4);
    if (snd != NULL) {
        snd->frameDelay = tmpFrameDelay;
    }
    
    return snd;
}

// AUTOINJECT
void Sound_StopAllWithId(Action_SFX sfx) {

    // Iterate over the playback list
    for(DYNAMICSOUNDS* ds = (DYNAMICSOUNDS*)DynamicSoundList->activeList.head; ds != NULL; ds = ds->next) {
        // If it matches, set its status to 2
        if(ds->sfxId == sfx)
            ds->playbackState = 2;
    }

}

// AUTOINJECT
void Sound_ZeroAlertness(void) {

    for(DYNAMICSOUNDS* ds = (DYNAMICSOUNDS*)DynamicSoundList->activeList.head; ds != NULL; ds = ds->next) {
        if ((ds->playbackState != 2) && // Not stopped
            (ds->playbackState != 3) && // Not in some other state
            ( (ds->sfxId >= ARRAY_SIZE(SFXOutputData)) || (!SFXOutputData[ds->sfxId].loopAlways) ) ) // Either an invalid SFX ID, or a non-loopAlways SFX
        {
            ds->alertness = 0.0f;
        }
    }
}

#define MAX_MAP_SOUNDS 50

// One ambient sound placed in the level, copied verbatim (8 dwords each) out of the map data by
// Sound_LoadMapSounds. Only the fields HandleMapSoundAllocation reads are named; +0x00 is copied but never
// read by anything in the Xbox build. The PS2 build's glbMapSoundConfigs is laid out differently.
#pragma pack(push, 1)
typedef struct {
    undefined4 unknown0;
    uint sfxId;           // 0x04 an Action_SFX; 0x?ffff in the low 20 bits means "none"
    _VECTOR position;     // 0x08
    float pitch;          // 0x14 truncated to a short for Sound_Play's maybePitchBend
    float radiusOuter;    // 0x18 also the distance at which the sound starts (and 1.2x it, stops)
    float radiusInner;    // 0x1c
} MapSound;
#pragma pack(pop)
static_assert(sizeof(MapSound) == 0x20, "Bad size for MapSound");
static_assert(offsetof(MapSound, position) == 0x08, "Bad offset of MapSound.position");
static_assert(offsetof(MapSound, radiusOuter) == 0x18, "Bad offset of MapSound.radiusOuter");

static MapSound GMapSounds[MAX_MAP_SOUNDS];
// XBE_GLOBAL(0x0029a14c, 0x32)
static char GMapSoundActive[MAX_MAP_SOUNDS];
// XBE_GLOBAL(0x0029a284, 0x4)
static int GNumMapSounds;

// Takes the level's map-sound block (parsemap_handle_block_id passes it): a count, then that many MapSounds.
// Every sound starts inactive, so HandleMapSoundAllocation starts each one afresh once the listener is near.
// GMapSoundHandle is not cleared, as in the original: a handle is only read while its sound is active.
//
// AUTOINJECT
void Sound_LoadMapSounds(int *mapSoundBlock) {
    int count = mapSoundBlock[0];

    memset(GMapSoundActive, 0, sizeof(GMapSoundActive));

    // OUR GUARD, not the original's. Neither the Xbox nor the PS2 build checks the count (no bounds check or
    // getter survives in either): more than MAX_MAP_SOUNDS overran the array, in the original into
    // DynamicSoundList, which follows it in memory - and would now overrun whatever the linker puts after ours.
    // Stop there rather than corrupt memory. A count of zero or less copies nothing but is still stored, as in
    // the original.
    NF_ASSERT(count <= MAX_MAP_SOUNDS, "the level has more map sounds than GMapSounds has room for");
    GNumMapSounds = count;
    if (count > 0)
        memcpy(GMapSounds, &mapSoundBlock[1], count * sizeof(MapSound));
}


// The handle of each playing map sound. HandleMapSoundAllocation is the only function that touches it (Ghidra
// xrefs: one read, one write, both here), so it is ours. Not cleared at level load in the original either:
// GMapSoundActive is, and a handle is only read while its sound is active.
// XBE_GLOBAL(0x00299a20, 0xc8)
DYNAMICSOUNDS *GMapSoundHandle[MAX_MAP_SOUNDS];

// The factor on the squared start radius past which a playing map sound is stopped: a 20% band (about 9.5%
// in distance) so a listener standing on the edge does not start and stop it every frame.
#define MAP_SOUND_STOP_HYSTERESIS 1.2f

// Called every frame: starts each map sound when the listener comes within its outer radius and stops it
// once they are beyond 1.2x that radius squared.
//
// AUTOINJECT
void HandleMapSoundAllocation(void) {

    // GNumMapSounds is re-read on every iteration, as the original does (it cannot change meanwhile)
    for (int i = 0; i < GNumMapSounds; i++) {
        MapSound *snd = &GMapSounds[i];

        _VECTOR pos;
        Vec_Copy(&snd->position, &pos);
        double distSq = Vec_SqDist3D(&GlobalListenerPos, &pos);

        // The original squares the radius on the x87 stack and never rounds it back to a float. A product of
        // two floats is exact in a double, so doing it in double reproduces the original's comparison exactly.
        double radiusSq = (double)snd->radiusOuter * (double)snd->radiusOuter;

        if (GMapSoundActive[i]) {
            if (radiusSq * MAP_SOUND_STOP_HYSTERESIS < distSq) {
                Sound_Stop(GMapSoundHandle[i]); // NULL-safe, as the original's inline test is
                GMapSoundActive[i] = 0;
            }
        } else if (distSq < radiusSq) {
            // The original inlines Sound_Play3D(..., isLimitedRadius = 1) exactly, including consuming
            // Sound_FrameDelay even when the sfxId is "none". The handle is stored, and the sound marked
            // active, even when there was no sound to play or no free voice - so a failed start is not
            // retried until the listener has left the 1.2x band and come back.
            GMapSoundHandle[i] = Sound_Play3D((Action_SFX)snd->sfxId, &pos, 100.0f, snd->radiusOuter,
                                              snd->radiusInner, (short)(int)snd->pitch, 0, 1);
            GMapSoundActive[i] = 1;
        }
    }
}

#pragma pack(push, 1)
// A sound with a subtitle, and its label; the label's top bit shows it even with subtitles off
typedef struct SND2LBL_tag {
    Action_TranslatedText label;
    Action_SFX sound;
} SND2LBL_tag;
#pragma pack(pop)
static_assert(sizeof(SND2LBL_tag) == 8, "SND2LBL_tag is 8 bytes");

#define NUM_SND2LBL 0x1e4
#define Snd2Lbl (*(SND2LBL_tag (*)[NUM_SND2LBL])0x0018bcb8)
#define SUBTITLE_ALWAYS 0x80000000u
// The subtitles' frames per second of their sound (PS2: g_fSubtitleDurationModification)
#define SubtitleDurationModification FLOAT_AT(0x0018cbd8)
#define SubtitlesEnabled U32_AT(0x001f6614)

// The subtitle of a sound that has one, for its length (3 seconds for a sound past the table)
// AUTOINJECT
void __fastcall Sound_DoSubtitle(Action_SFX sound) {
    uint i = 0;
    while (Snd2Lbl[i].sound != sound) {
        i++;
        if (i >= NUM_SND2LBL)
            return;
    }
    uint label = Snd2Lbl[i].label;
    if (!SubtitlesEnabled && !(label & SUBTITLE_ALWAYS))
        return;
    float seconds = (uint)sound > 0x60c ? 3.0f : SFXOutputData[sound].durationForSubtitles;
    int frames = Ftol((double)SubtitleDurationModification * seconds * 60.0f);
    Text_AddMsg(-1, (char)0xff, TXTMSG_SUBTITLE,
                (char *)Txt_BindLabel((Action_TranslatedText)(label & ~SUBTITLE_ALWAYS), 0), 0, (short)frames);
}
