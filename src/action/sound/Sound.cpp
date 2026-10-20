#include "Sound.h"
#include <string.h>
#include <stdio.h>

#include "SFX.h"
#include "music.h"
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

// XBE_GLOBAL(0x0018bcb8, 0xf20) readonly relocate
SND2LBL_tag Snd2Lbl[NUM_SND2LBL] = {
    {(Action_TranslatedText)0x06000007, (Action_SFX)683},
    {(Action_TranslatedText)0x86000008, (Action_SFX)710},
    {(Action_TranslatedText)0x06000009, (Action_SFX)685},
    {(Action_TranslatedText)0x0600000a, (Action_SFX)686},
    {(Action_TranslatedText)0x0600000b, (Action_SFX)687},
    {(Action_TranslatedText)0x0600000c, (Action_SFX)689},
    {(Action_TranslatedText)0x0600000d, (Action_SFX)688},
    {(Action_TranslatedText)0x0600000e, (Action_SFX)723},
    {(Action_TranslatedText)0x0600000f, (Action_SFX)1521},
    {(Action_TranslatedText)0x06000010, (Action_SFX)702},
    {(Action_TranslatedText)0x06000011, (Action_SFX)691},
    {(Action_TranslatedText)0x06000012, (Action_SFX)695},
    {(Action_TranslatedText)0x06000013, (Action_SFX)696},
    {(Action_TranslatedText)0x06000014, (Action_SFX)697},
    {(Action_TranslatedText)0x06000015, (Action_SFX)682},
    {(Action_TranslatedText)0x06000016, (Action_SFX)660},
    {(Action_TranslatedText)0x06000017, (Action_SFX)661},
    {(Action_TranslatedText)0x06000018, (Action_SFX)662},
    {(Action_TranslatedText)0x06000019, (Action_SFX)663},
    {(Action_TranslatedText)0x0600001a, (Action_SFX)664},
    {(Action_TranslatedText)0x0600001b, (Action_SFX)665},
    {(Action_TranslatedText)0x0600001c, (Action_SFX)666},
    {(Action_TranslatedText)0x0600001d, (Action_SFX)667},
    {(Action_TranslatedText)0x0600001e, (Action_SFX)668},
    {(Action_TranslatedText)0x0600001f, (Action_SFX)669},
    {(Action_TranslatedText)0x06000020, (Action_SFX)670},
    {(Action_TranslatedText)0x06000021, (Action_SFX)671},
    {(Action_TranslatedText)0x06000022, (Action_SFX)672},
    {(Action_TranslatedText)0x06000023, (Action_SFX)673},
    {(Action_TranslatedText)0x06000024, (Action_SFX)674},
    {(Action_TranslatedText)0x06000025, (Action_SFX)675},
    {(Action_TranslatedText)0x06000026, (Action_SFX)676},
    {(Action_TranslatedText)0x06000027, (Action_SFX)677},
    {(Action_TranslatedText)0x06000028, (Action_SFX)678},
    {(Action_TranslatedText)0x06000029, (Action_SFX)679},
    {(Action_TranslatedText)0x0600002a, (Action_SFX)680},
    {(Action_TranslatedText)0x0600002b, (Action_SFX)681},
    {(Action_TranslatedText)0x0600002c, (Action_SFX)659},
    {(Action_TranslatedText)0x0600002d, (Action_SFX)603},
    {(Action_TranslatedText)0x0600002e, (Action_SFX)604},
    {(Action_TranslatedText)0x0600002f, (Action_SFX)605},
    {(Action_TranslatedText)0x06000030, (Action_SFX)606},
    {(Action_TranslatedText)0x06000031, (Action_SFX)607},
    {(Action_TranslatedText)0x06000032, (Action_SFX)608},
    {(Action_TranslatedText)0x06000033, (Action_SFX)611},
    {(Action_TranslatedText)0x06000034, (Action_SFX)612},
    {(Action_TranslatedText)0x0600003d, (Action_SFX)632},
    {(Action_TranslatedText)0x8600003e, (Action_SFX)599},
    {(Action_TranslatedText)0x0600003f, (Action_SFX)625},
    {(Action_TranslatedText)0x06000040, (Action_SFX)629},
    {(Action_TranslatedText)0x06000047, (Action_SFX)489},
    {(Action_TranslatedText)0x06000048, (Action_SFX)488},
    {(Action_TranslatedText)0x06000049, (Action_SFX)490},
    {(Action_TranslatedText)0x0600004a, (Action_SFX)491},
    {(Action_TranslatedText)0x0600004b, (Action_SFX)524},
    {(Action_TranslatedText)0x0600004c, (Action_SFX)526},
    {(Action_TranslatedText)0x0600004d, (Action_SFX)525},
    {(Action_TranslatedText)0x0600004e, (Action_SFX)522},
    {(Action_TranslatedText)0x0600004f, (Action_SFX)521},
    {(Action_TranslatedText)0x06000050, (Action_SFX)523},
    {(Action_TranslatedText)0x06000051, (Action_SFX)922},
    {(Action_TranslatedText)0x06000052, (Action_SFX)155},
    {(Action_TranslatedText)0x06000053, (Action_SFX)156},
    {(Action_TranslatedText)0x06000054, (Action_SFX)154},
    {(Action_TranslatedText)0x06000055, (Action_SFX)719},
    {(Action_TranslatedText)0x06000056, (Action_SFX)720},
    {(Action_TranslatedText)0x06000057, (Action_SFX)718},
    {(Action_TranslatedText)0x06000058, (Action_SFX)721},
    {(Action_TranslatedText)0x86000059, (Action_SFX)587},
    {(Action_TranslatedText)0x0600005a, (Action_SFX)592},
    {(Action_TranslatedText)0x0600005b, (Action_SFX)595},
    {(Action_TranslatedText)0x8600005c, (Action_SFX)658},
    {(Action_TranslatedText)0x8600005d, (Action_SFX)657},
    {(Action_TranslatedText)0x0600005e, (Action_SFX)593},
    {(Action_TranslatedText)0x0600005f, (Action_SFX)591},
    {(Action_TranslatedText)0x06000060, (Action_SFX)594},
    {(Action_TranslatedText)0x8600006c, (Action_SFX)228},
    {(Action_TranslatedText)0x8600006d, (Action_SFX)252},
    {(Action_TranslatedText)0x8600006e, (Action_SFX)172},
    {(Action_TranslatedText)0x8600006f, (Action_SFX)174},
    {(Action_TranslatedText)0x86000070, (Action_SFX)220},
    {(Action_TranslatedText)0x86000071, (Action_SFX)225},
    {(Action_TranslatedText)0x86000072, (Action_SFX)221},
    {(Action_TranslatedText)0x06000073, (Action_SFX)223},
    {(Action_TranslatedText)0x06000074, (Action_SFX)227},
    {(Action_TranslatedText)0x06000075, (Action_SFX)224},
    {(Action_TranslatedText)0x06000076, (Action_SFX)171},
    {(Action_TranslatedText)0x86000077, (Action_SFX)219},
    {(Action_TranslatedText)0x06000078, (Action_SFX)328},
    {(Action_TranslatedText)0x06000079, (Action_SFX)281},
    {(Action_TranslatedText)0x0600007a, (Action_SFX)705},
    {(Action_TranslatedText)0x0600007b, (Action_SFX)366},
    {(Action_TranslatedText)0x0600007c, (Action_SFX)338},
    {(Action_TranslatedText)0x0600007d, (Action_SFX)337},
    {(Action_TranslatedText)0x0600007e, (Action_SFX)339},
    {(Action_TranslatedText)0x0600007f, (Action_SFX)341},
    {(Action_TranslatedText)0x06000080, (Action_SFX)346},
    {(Action_TranslatedText)0x06000081, (Action_SFX)342},
    {(Action_TranslatedText)0x06000082, (Action_SFX)340},
    {(Action_TranslatedText)0x06000083, (Action_SFX)343},
    {(Action_TranslatedText)0x06000084, (Action_SFX)345},
    {(Action_TranslatedText)0x06000085, (Action_SFX)344},
    {(Action_TranslatedText)0x06000086, (Action_SFX)336},
    {(Action_TranslatedText)0x06000087, (Action_SFX)326},
    {(Action_TranslatedText)0x06000088, (Action_SFX)365},
    {(Action_TranslatedText)0x06000089, (Action_SFX)350},
    {(Action_TranslatedText)0x0600008a, (Action_SFX)348},
    {(Action_TranslatedText)0x0600008b, (Action_SFX)351},
    {(Action_TranslatedText)0x0600008c, (Action_SFX)349},
    {(Action_TranslatedText)0x0600008d, (Action_SFX)352},
    {(Action_TranslatedText)0x0600008e, (Action_SFX)353},
    {(Action_TranslatedText)0x0600008f, (Action_SFX)325},
    {(Action_TranslatedText)0x06000090, (Action_SFX)323},
    {(Action_TranslatedText)0x06000098, (Action_SFX)1046},
    {(Action_TranslatedText)0x06000099, (Action_SFX)443},
    {(Action_TranslatedText)0x0600009a, (Action_SFX)445},
    {(Action_TranslatedText)0x0600009b, (Action_SFX)447},
    {(Action_TranslatedText)0x0600009c, (Action_SFX)449},
    {(Action_TranslatedText)0x0600009e, (Action_SFX)367},
    {(Action_TranslatedText)0x0600009f, (Action_SFX)327},
    {(Action_TranslatedText)0x060000a0, (Action_SFX)376},
    {(Action_TranslatedText)0x060000a1, (Action_SFX)416},
    {(Action_TranslatedText)0x060000a2, (Action_SFX)380},
    {(Action_TranslatedText)0x060000a3, (Action_SFX)384},
    {(Action_TranslatedText)0x060000a4, (Action_SFX)388},
    {(Action_TranslatedText)0x060000a5, (Action_SFX)392},
    {(Action_TranslatedText)0x060000a6, (Action_SFX)396},
    {(Action_TranslatedText)0x060000a7, (Action_SFX)400},
    {(Action_TranslatedText)0x060000a8, (Action_SFX)404},
    {(Action_TranslatedText)0x060000a9, (Action_SFX)408},
    {(Action_TranslatedText)0x060000aa, (Action_SFX)412},
    {(Action_TranslatedText)0x060000ab, (Action_SFX)417},
    {(Action_TranslatedText)0x060000ac, (Action_SFX)419},
    {(Action_TranslatedText)0x060000ad, (Action_SFX)420},
    {(Action_TranslatedText)0x060000ae, (Action_SFX)421},
    {(Action_TranslatedText)0x060000af, (Action_SFX)422},
    {(Action_TranslatedText)0x060000b0, (Action_SFX)423},
    {(Action_TranslatedText)0x060000b1, (Action_SFX)424},
    {(Action_TranslatedText)0x060000b2, (Action_SFX)425},
    {(Action_TranslatedText)0x060000b3, (Action_SFX)433},
    {(Action_TranslatedText)0x060000b4, (Action_SFX)435},
    {(Action_TranslatedText)0x060000b5, (Action_SFX)437},
    {(Action_TranslatedText)0x060000b6, (Action_SFX)428},
    {(Action_TranslatedText)0x060000b7, (Action_SFX)430},
    {(Action_TranslatedText)0x060000b8, (Action_SFX)440},
    {(Action_TranslatedText)0x060000b9, (Action_SFX)441},
    {(Action_TranslatedText)0x060000ba, (Action_SFX)442},
    {(Action_TranslatedText)0x060000bb, (Action_SFX)444},
    {(Action_TranslatedText)0x060000bc, (Action_SFX)446},
    {(Action_TranslatedText)0x060000bd, (Action_SFX)448},
    {(Action_TranslatedText)0x060000be, (Action_SFX)454},
    {(Action_TranslatedText)0x060000bf, (Action_SFX)455},
    {(Action_TranslatedText)0x060000c0, (Action_SFX)456},
    {(Action_TranslatedText)0x060000c1, (Action_SFX)928},
    {(Action_TranslatedText)0x060000c2, (Action_SFX)459},
    {(Action_TranslatedText)0x060000c3, (Action_SFX)462},
    {(Action_TranslatedText)0x060000c4, (Action_SFX)706},
    {(Action_TranslatedText)0x060000c5, (Action_SFX)373},
    {(Action_TranslatedText)0x060000c6, (Action_SFX)590},
    {(Action_TranslatedText)0x060000c7, (Action_SFX)589},
    {(Action_TranslatedText)0x060000c8, (Action_SFX)588},
    {(Action_TranslatedText)0x060000cd, (Action_SFX)942},
    {(Action_TranslatedText)0x060000ce, (Action_SFX)931},
    {(Action_TranslatedText)0x060000cf, (Action_SFX)932},
    {(Action_TranslatedText)0x060000d3, (Action_SFX)934},
    {(Action_TranslatedText)0x060000d6, (Action_SFX)935},
    {(Action_TranslatedText)0x060000d7, (Action_SFX)936},
    {(Action_TranslatedText)0x060000d8, (Action_SFX)937},
    {(Action_TranslatedText)0x060000d9, (Action_SFX)938},
    {(Action_TranslatedText)0x060000da, (Action_SFX)933},
    {(Action_TranslatedText)0x060000db, (Action_SFX)1024},
    {(Action_TranslatedText)0x060000dc, (Action_SFX)1045},
    {(Action_TranslatedText)0x060000dd, (Action_SFX)1044},
    {(Action_TranslatedText)0x060000de, (Action_SFX)939},
    {(Action_TranslatedText)0x060000df, (Action_SFX)940},
    {(Action_TranslatedText)0x060000e0, (Action_SFX)941},
    {(Action_TranslatedText)0x060000e1, (Action_SFX)929},
    {(Action_TranslatedText)0x060000e2, (Action_SFX)799},
    {(Action_TranslatedText)0x060000ed, (Action_SFX)804},
    {(Action_TranslatedText)0x060000ee, (Action_SFX)796},
    {(Action_TranslatedText)0x060000ef, (Action_SFX)684},
    {(Action_TranslatedText)0x060000f5, (Action_SFX)839},
    {(Action_TranslatedText)0x060000f6, (Action_SFX)809},
    {(Action_TranslatedText)0x06000101, (Action_SFX)810},
    {(Action_TranslatedText)0x06000102, (Action_SFX)811},
    {(Action_TranslatedText)0x06000104, (Action_SFX)812},
    {(Action_TranslatedText)0x06000105, (Action_SFX)813},
    {(Action_TranslatedText)0x06000106, (Action_SFX)814},
    {(Action_TranslatedText)0x06000107, (Action_SFX)815},
    {(Action_TranslatedText)0x06000108, (Action_SFX)816},
    {(Action_TranslatedText)0x06000109, (Action_SFX)830},
    {(Action_TranslatedText)0x0600010a, (Action_SFX)831},
    {(Action_TranslatedText)0x0600010b, (Action_SFX)832},
    {(Action_TranslatedText)0x0600010c, (Action_SFX)833},
    {(Action_TranslatedText)0x0600010d, (Action_SFX)1026},
    {(Action_TranslatedText)0x0600010e, (Action_SFX)818},
    {(Action_TranslatedText)0x0600010f, (Action_SFX)819},
    {(Action_TranslatedText)0x06000110, (Action_SFX)820},
    {(Action_TranslatedText)0x06000111, (Action_SFX)829},
    {(Action_TranslatedText)0x06000112, (Action_SFX)834},
    {(Action_TranslatedText)0x06000113, (Action_SFX)835},
    {(Action_TranslatedText)0x06000114, (Action_SFX)836},
    {(Action_TranslatedText)0x06000115, (Action_SFX)837},
    {(Action_TranslatedText)0x06000117, (Action_SFX)808},
    {(Action_TranslatedText)0x06000118, (Action_SFX)378},
    {(Action_TranslatedText)0x06000119, (Action_SFX)382},
    {(Action_TranslatedText)0x0600011a, (Action_SFX)386},
    {(Action_TranslatedText)0x0600011b, (Action_SFX)390},
    {(Action_TranslatedText)0x0600011c, (Action_SFX)394},
    {(Action_TranslatedText)0x0600011d, (Action_SFX)398},
    {(Action_TranslatedText)0x0600011e, (Action_SFX)402},
    {(Action_TranslatedText)0x0600011f, (Action_SFX)406},
    {(Action_TranslatedText)0x06000120, (Action_SFX)410},
    {(Action_TranslatedText)0x06000121, (Action_SFX)463},
    {(Action_TranslatedText)0x06000122, (Action_SFX)413},
    {(Action_TranslatedText)0x06000123, (Action_SFX)377},
    {(Action_TranslatedText)0x06000124, (Action_SFX)381},
    {(Action_TranslatedText)0x06000125, (Action_SFX)385},
    {(Action_TranslatedText)0x06000126, (Action_SFX)389},
    {(Action_TranslatedText)0x06000127, (Action_SFX)393},
    {(Action_TranslatedText)0x06000128, (Action_SFX)397},
    {(Action_TranslatedText)0x06000129, (Action_SFX)401},
    {(Action_TranslatedText)0x0600012a, (Action_SFX)405},
    {(Action_TranslatedText)0x0600012b, (Action_SFX)409},
    {(Action_TranslatedText)0x0600012c, (Action_SFX)375},
    {(Action_TranslatedText)0x0600012d, (Action_SFX)415},
    {(Action_TranslatedText)0x0600012e, (Action_SFX)379},
    {(Action_TranslatedText)0x0600012f, (Action_SFX)383},
    {(Action_TranslatedText)0x06000130, (Action_SFX)387},
    {(Action_TranslatedText)0x06000131, (Action_SFX)391},
    {(Action_TranslatedText)0x06000132, (Action_SFX)395},
    {(Action_TranslatedText)0x06000133, (Action_SFX)399},
    {(Action_TranslatedText)0x06000134, (Action_SFX)403},
    {(Action_TranslatedText)0x06000135, (Action_SFX)407},
    {(Action_TranslatedText)0x06000136, (Action_SFX)411},
    {(Action_TranslatedText)0x06000138, (Action_SFX)432},
    {(Action_TranslatedText)0x06000139, (Action_SFX)434},
    {(Action_TranslatedText)0x0600013a, (Action_SFX)436},
    {(Action_TranslatedText)0x0600013b, (Action_SFX)438},
    {(Action_TranslatedText)0x0600013c, (Action_SFX)439},
    {(Action_TranslatedText)0x0600013d, (Action_SFX)427},
    {(Action_TranslatedText)0x0600013e, (Action_SFX)429},
    {(Action_TranslatedText)0x0600013f, (Action_SFX)431},
    {(Action_TranslatedText)0x06000140, (Action_SFX)457},
    {(Action_TranslatedText)0x06000141, (Action_SFX)324},
    {(Action_TranslatedText)0x06000142, (Action_SFX)879},
    {(Action_TranslatedText)0x06000143, (Action_SFX)844},
    {(Action_TranslatedText)0x06000144, (Action_SFX)845},
    {(Action_TranslatedText)0x06000145, (Action_SFX)846},
    {(Action_TranslatedText)0x06000146, (Action_SFX)847},
    {(Action_TranslatedText)0x06000147, (Action_SFX)848},
    {(Action_TranslatedText)0x06000148, (Action_SFX)849},
    {(Action_TranslatedText)0x06000149, (Action_SFX)850},
    {(Action_TranslatedText)0x0600014a, (Action_SFX)851},
    {(Action_TranslatedText)0x0600014b, (Action_SFX)852},
    {(Action_TranslatedText)0x0600014c, (Action_SFX)853},
    {(Action_TranslatedText)0x0600014d, (Action_SFX)854},
    {(Action_TranslatedText)0x0600014e, (Action_SFX)855},
    {(Action_TranslatedText)0x0600014f, (Action_SFX)856},
    {(Action_TranslatedText)0x06000150, (Action_SFX)857},
    {(Action_TranslatedText)0x06000151, (Action_SFX)858},
    {(Action_TranslatedText)0x06000152, (Action_SFX)859},
    {(Action_TranslatedText)0x06000153, (Action_SFX)860},
    {(Action_TranslatedText)0x06000154, (Action_SFX)861},
    {(Action_TranslatedText)0x06000155, (Action_SFX)862},
    {(Action_TranslatedText)0x06000156, (Action_SFX)863},
    {(Action_TranslatedText)0x06000157, (Action_SFX)864},
    {(Action_TranslatedText)0x06000158, (Action_SFX)865},
    {(Action_TranslatedText)0x06000159, (Action_SFX)866},
    {(Action_TranslatedText)0x0600015a, (Action_SFX)867},
    {(Action_TranslatedText)0x0600015b, (Action_SFX)868},
    {(Action_TranslatedText)0x0600015c, (Action_SFX)869},
    {(Action_TranslatedText)0x0600015d, (Action_SFX)870},
    {(Action_TranslatedText)0x0600015e, (Action_SFX)871},
    {(Action_TranslatedText)0x0600015f, (Action_SFX)872},
    {(Action_TranslatedText)0x06000160, (Action_SFX)873},
    {(Action_TranslatedText)0x06000161, (Action_SFX)874},
    {(Action_TranslatedText)0x06000162, (Action_SFX)875},
    {(Action_TranslatedText)0x06000163, (Action_SFX)876},
    {(Action_TranslatedText)0x06000164, (Action_SFX)877},
    {(Action_TranslatedText)0x06000165, (Action_SFX)878},
    {(Action_TranslatedText)0x06000166, (Action_SFX)843},
    {(Action_TranslatedText)0x06000168, (Action_SFX)1014},
    {(Action_TranslatedText)0x0600016b, (Action_SFX)921},
    {(Action_TranslatedText)0x0600016c, (Action_SFX)881},
    {(Action_TranslatedText)0x0600016d, (Action_SFX)882},
    {(Action_TranslatedText)0x0600016e, (Action_SFX)883},
    {(Action_TranslatedText)0x0600016f, (Action_SFX)884},
    {(Action_TranslatedText)0x06000170, (Action_SFX)885},
    {(Action_TranslatedText)0x06000171, (Action_SFX)886},
    {(Action_TranslatedText)0x06000172, (Action_SFX)887},
    {(Action_TranslatedText)0x06000173, (Action_SFX)888},
    {(Action_TranslatedText)0x06000174, (Action_SFX)889},
    {(Action_TranslatedText)0x06000175, (Action_SFX)890},
    {(Action_TranslatedText)0x06000176, (Action_SFX)891},
    {(Action_TranslatedText)0x06000177, (Action_SFX)892},
    {(Action_TranslatedText)0x06000178, (Action_SFX)893},
    {(Action_TranslatedText)0x06000179, (Action_SFX)894},
    {(Action_TranslatedText)0x0600017a, (Action_SFX)895},
    {(Action_TranslatedText)0x0600017b, (Action_SFX)896},
    {(Action_TranslatedText)0x0600017c, (Action_SFX)897},
    {(Action_TranslatedText)0x0600017d, (Action_SFX)898},
    {(Action_TranslatedText)0x0600017e, (Action_SFX)899},
    {(Action_TranslatedText)0x06000180, (Action_SFX)900},
    {(Action_TranslatedText)0x06000181, (Action_SFX)901},
    {(Action_TranslatedText)0x06000182, (Action_SFX)902},
    {(Action_TranslatedText)0x06000183, (Action_SFX)903},
    {(Action_TranslatedText)0x06000184, (Action_SFX)904},
    {(Action_TranslatedText)0x06000185, (Action_SFX)905},
    {(Action_TranslatedText)0x06000186, (Action_SFX)906},
    {(Action_TranslatedText)0x06000187, (Action_SFX)907},
    {(Action_TranslatedText)0x06000188, (Action_SFX)908},
    {(Action_TranslatedText)0x06000189, (Action_SFX)909},
    {(Action_TranslatedText)0x0600018a, (Action_SFX)910},
    {(Action_TranslatedText)0x0600018b, (Action_SFX)911},
    {(Action_TranslatedText)0x0600018c, (Action_SFX)912},
    {(Action_TranslatedText)0x0600018d, (Action_SFX)913},
    {(Action_TranslatedText)0x0600018e, (Action_SFX)914},
    {(Action_TranslatedText)0x0600018f, (Action_SFX)915},
    {(Action_TranslatedText)0x06000190, (Action_SFX)916},
    {(Action_TranslatedText)0x06000191, (Action_SFX)917},
    {(Action_TranslatedText)0x06000192, (Action_SFX)918},
    {(Action_TranslatedText)0x06000193, (Action_SFX)919},
    {(Action_TranslatedText)0x06000194, (Action_SFX)920},
    {(Action_TranslatedText)0x06000195, (Action_SFX)880},
    {(Action_TranslatedText)0x06000196, (Action_SFX)1013},
    {(Action_TranslatedText)0x06000197, (Action_SFX)1012},
    {(Action_TranslatedText)0x06000198, (Action_SFX)965},
    {(Action_TranslatedText)0x06000199, (Action_SFX)956},
    {(Action_TranslatedText)0x0600019a, (Action_SFX)957},
    {(Action_TranslatedText)0x0600019b, (Action_SFX)958},
    {(Action_TranslatedText)0x0600019c, (Action_SFX)959},
    {(Action_TranslatedText)0x0600019d, (Action_SFX)960},
    {(Action_TranslatedText)0x0600019e, (Action_SFX)961},
    {(Action_TranslatedText)0x0600019f, (Action_SFX)962},
    {(Action_TranslatedText)0x060001a0, (Action_SFX)963},
    {(Action_TranslatedText)0x060001a1, (Action_SFX)964},
    {(Action_TranslatedText)0x060001a2, (Action_SFX)955},
    {(Action_TranslatedText)0x060001a3, (Action_SFX)976},
    {(Action_TranslatedText)0x060001a4, (Action_SFX)967},
    {(Action_TranslatedText)0x060001a5, (Action_SFX)968},
    {(Action_TranslatedText)0x060001a6, (Action_SFX)969},
    {(Action_TranslatedText)0x060001a7, (Action_SFX)970},
    {(Action_TranslatedText)0x060001a8, (Action_SFX)971},
    {(Action_TranslatedText)0x060001a9, (Action_SFX)972},
    {(Action_TranslatedText)0x060001aa, (Action_SFX)973},
    {(Action_TranslatedText)0x060001ab, (Action_SFX)974},
    {(Action_TranslatedText)0x060001ac, (Action_SFX)975},
    {(Action_TranslatedText)0x060001ad, (Action_SFX)966},
    {(Action_TranslatedText)0x060001ae, (Action_SFX)987},
    {(Action_TranslatedText)0x060001af, (Action_SFX)978},
    {(Action_TranslatedText)0x060001b0, (Action_SFX)979},
    {(Action_TranslatedText)0x060001b1, (Action_SFX)980},
    {(Action_TranslatedText)0x060001b2, (Action_SFX)981},
    {(Action_TranslatedText)0x060001b3, (Action_SFX)982},
    {(Action_TranslatedText)0x060001b4, (Action_SFX)983},
    {(Action_TranslatedText)0x060001b5, (Action_SFX)984},
    {(Action_TranslatedText)0x060001b6, (Action_SFX)985},
    {(Action_TranslatedText)0x060001b7, (Action_SFX)986},
    {(Action_TranslatedText)0x060001b8, (Action_SFX)977},
    {(Action_TranslatedText)0x060001b9, (Action_SFX)1011},
    {(Action_TranslatedText)0x060001ba, (Action_SFX)989},
    {(Action_TranslatedText)0x060001bb, (Action_SFX)990},
    {(Action_TranslatedText)0x060001bc, (Action_SFX)991},
    {(Action_TranslatedText)0x060001bd, (Action_SFX)992},
    {(Action_TranslatedText)0x060001be, (Action_SFX)993},
    {(Action_TranslatedText)0x060001bf, (Action_SFX)994},
    {(Action_TranslatedText)0x060001c0, (Action_SFX)995},
    {(Action_TranslatedText)0x060001c1, (Action_SFX)1007},
    {(Action_TranslatedText)0x060001c2, (Action_SFX)1008},
    {(Action_TranslatedText)0x060001c3, (Action_SFX)1009},
    {(Action_TranslatedText)0x060001c4, (Action_SFX)1010},
    {(Action_TranslatedText)0x060001c5, (Action_SFX)988},
    {(Action_TranslatedText)0x060001c6, (Action_SFX)371},
    {(Action_TranslatedText)0x060001c7, (Action_SFX)1124},
    {(Action_TranslatedText)0x060001c8, (Action_SFX)1114},
    {(Action_TranslatedText)0x060001c9, (Action_SFX)1115},
    {(Action_TranslatedText)0x060001ca, (Action_SFX)1116},
    {(Action_TranslatedText)0x060001cb, (Action_SFX)1117},
    {(Action_TranslatedText)0x060001cc, (Action_SFX)1118},
    {(Action_TranslatedText)0x060001cd, (Action_SFX)1119},
    {(Action_TranslatedText)0x060001ce, (Action_SFX)1120},
    {(Action_TranslatedText)0x060001cf, (Action_SFX)1121},
    {(Action_TranslatedText)0x060001d0, (Action_SFX)1122},
    {(Action_TranslatedText)0x060001d1, (Action_SFX)1123},
    {(Action_TranslatedText)0x060001d2, (Action_SFX)1113},
    {(Action_TranslatedText)0x060001d3, (Action_SFX)1506},
    {(Action_TranslatedText)0x060001d4, (Action_SFX)1507},
    {(Action_TranslatedText)0x060001d6, (Action_SFX)1086},
    {(Action_TranslatedText)0x060001d7, (Action_SFX)1074},
    {(Action_TranslatedText)0x060001d8, (Action_SFX)1075},
    {(Action_TranslatedText)0x060001d9, (Action_SFX)1076},
    {(Action_TranslatedText)0x060001da, (Action_SFX)1077},
    {(Action_TranslatedText)0x060001db, (Action_SFX)1078},
    {(Action_TranslatedText)0x060001dc, (Action_SFX)1079},
    {(Action_TranslatedText)0x060001dd, (Action_SFX)1080},
    {(Action_TranslatedText)0x060001de, (Action_SFX)1081},
    {(Action_TranslatedText)0x060001df, (Action_SFX)1082},
    {(Action_TranslatedText)0x060001e0, (Action_SFX)1083},
    {(Action_TranslatedText)0x060001e1, (Action_SFX)1084},
    {(Action_TranslatedText)0x060001e2, (Action_SFX)1085},
    {(Action_TranslatedText)0x060001e3, (Action_SFX)1073},
    {(Action_TranslatedText)0x060001e6, (Action_SFX)1088},
    {(Action_TranslatedText)0x060001e7, (Action_SFX)1518},
    {(Action_TranslatedText)0x060001e8, (Action_SFX)1087},
    {(Action_TranslatedText)0x060001e9, (Action_SFX)1512},
    {(Action_TranslatedText)0x060001ea, (Action_SFX)1514},
    {(Action_TranslatedText)0x060001eb, (Action_SFX)1515},
    {(Action_TranslatedText)0x060001ee, (Action_SFX)1141},
    {(Action_TranslatedText)0x060001ef, (Action_SFX)1142},
    {(Action_TranslatedText)0x060001f0, (Action_SFX)1143},
    {(Action_TranslatedText)0x060001f1, (Action_SFX)1144},
    {(Action_TranslatedText)0x060001f2, (Action_SFX)1145},
    {(Action_TranslatedText)0x060001f3, (Action_SFX)1146},
    {(Action_TranslatedText)0x060001f4, (Action_SFX)1147},
    {(Action_TranslatedText)0x060001f5, (Action_SFX)1298},
    {(Action_TranslatedText)0x060001f6, (Action_SFX)1406},
    {(Action_TranslatedText)0x060001f7, (Action_SFX)1463},
    {(Action_TranslatedText)0x060001f8, (Action_SFX)1520},
    {(Action_TranslatedText)0x060001f9, (Action_SFX)1516},
    {(Action_TranslatedText)0x060001fa, (Action_SFX)1517},
    {(Action_TranslatedText)0x060001fb, (Action_SFX)1511},
    {(Action_TranslatedText)0x060001fc, (Action_SFX)1148},
    {(Action_TranslatedText)0x060001fd, (Action_SFX)1149},
    {(Action_TranslatedText)0x060001fe, (Action_SFX)1150},
    {(Action_TranslatedText)0x060001ff, (Action_SFX)1151},
    {(Action_TranslatedText)0x06000200, (Action_SFX)1152},
    {(Action_TranslatedText)0x06000201, (Action_SFX)1153},
    {(Action_TranslatedText)0x06000202, (Action_SFX)1154},
    {(Action_TranslatedText)0x06000203, (Action_SFX)1155},
    {(Action_TranslatedText)0x06000204, (Action_SFX)1156},
    {(Action_TranslatedText)0x06000205, (Action_SFX)1157},
    {(Action_TranslatedText)0x06000206, (Action_SFX)1159},
    {(Action_TranslatedText)0x06000207, (Action_SFX)1160},
    {(Action_TranslatedText)0x06000208, (Action_SFX)1161},
    {(Action_TranslatedText)0x06000209, (Action_SFX)1162},
    {(Action_TranslatedText)0x0600020a, (Action_SFX)1163},
    {(Action_TranslatedText)0x0600020b, (Action_SFX)1140},
    {(Action_TranslatedText)0x0600020c, (Action_SFX)1353},
    {(Action_TranslatedText)0x0600020d, (Action_SFX)1310},
    {(Action_TranslatedText)0x0600020f, (Action_SFX)1311},
    {(Action_TranslatedText)0x06000210, (Action_SFX)1312},
    {(Action_TranslatedText)0x06000211, (Action_SFX)1313},
    {(Action_TranslatedText)0x06000212, (Action_SFX)1314},
    {(Action_TranslatedText)0x06000213, (Action_SFX)1315},
    {(Action_TranslatedText)0x06000214, (Action_SFX)1316},
    {(Action_TranslatedText)0x06000215, (Action_SFX)1317},
    {(Action_TranslatedText)0x06000216, (Action_SFX)1318},
    {(Action_TranslatedText)0x06000217, (Action_SFX)1319},
    {(Action_TranslatedText)0x06000218, (Action_SFX)1320},
    {(Action_TranslatedText)0x06000219, (Action_SFX)1321},
    {(Action_TranslatedText)0x0600021a, (Action_SFX)1322},
    {(Action_TranslatedText)0x0600021b, (Action_SFX)1323},
    {(Action_TranslatedText)0x0600021c, (Action_SFX)1324},
    {(Action_TranslatedText)0x0600021d, (Action_SFX)1325},
    {(Action_TranslatedText)0x0600021e, (Action_SFX)1326},
    {(Action_TranslatedText)0x0600021f, (Action_SFX)1327},
    {(Action_TranslatedText)0x06000220, (Action_SFX)1328},
    {(Action_TranslatedText)0x06000221, (Action_SFX)1329},
    {(Action_TranslatedText)0x06000222, (Action_SFX)1330},
    {(Action_TranslatedText)0x06000223, (Action_SFX)1331},
    {(Action_TranslatedText)0x06000224, (Action_SFX)1332},
    {(Action_TranslatedText)0x06000225, (Action_SFX)1333},
    {(Action_TranslatedText)0x06000226, (Action_SFX)1334},
    {(Action_TranslatedText)0x06000227, (Action_SFX)1335},
    {(Action_TranslatedText)0x06000228, (Action_SFX)1336},
    {(Action_TranslatedText)0x06000229, (Action_SFX)1337},
    {(Action_TranslatedText)0x0600022a, (Action_SFX)1338},
    {(Action_TranslatedText)0x0600022b, (Action_SFX)1339},
    {(Action_TranslatedText)0x0600022c, (Action_SFX)1340},
    {(Action_TranslatedText)0x0600022d, (Action_SFX)1341},
    {(Action_TranslatedText)0x0600022e, (Action_SFX)1342},
    {(Action_TranslatedText)0x0600022f, (Action_SFX)1343},
    {(Action_TranslatedText)0x06000230, (Action_SFX)1344},
    {(Action_TranslatedText)0x06000231, (Action_SFX)1345},
    {(Action_TranslatedText)0x06000232, (Action_SFX)1346},
    {(Action_TranslatedText)0x06000233, (Action_SFX)1347},
    {(Action_TranslatedText)0x06000234, (Action_SFX)1348},
    {(Action_TranslatedText)0x06000235, (Action_SFX)1349},
    {(Action_TranslatedText)0x06000236, (Action_SFX)1350},
    {(Action_TranslatedText)0x06000237, (Action_SFX)1351},
    {(Action_TranslatedText)0x06000238, (Action_SFX)1352},
    {(Action_TranslatedText)0x06000239, (Action_SFX)1309},
    {(Action_TranslatedText)0x06000269, (Action_SFX)817},
};
#define SUBTITLE_ALWAYS 0x80000000u
// The subtitles' frames per second of their sound (PS2: g_fSubtitleDurationModification)
// XBE_GLOBAL(0x0018cbd8, 0x4) readonly relocate
float SubtitleDurationModification = 0.92f;
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
