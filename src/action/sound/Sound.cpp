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
#include "../math/X87Ops.h"

#include <math.h>

// XBE_GLOBAL(0x0029a280, 0x4)
uint32_t SoundInit;

// XBE_GLOBAL(0x0029a128, 0x18)
static DLISTINFO_tag DynamicSoundList;

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
// XBE_GLOBAL(0x0029a140, 0xc)
_VECTOR GlobalListenerPos;

#define NUM_DYNAMIC_SOUNDS 0x100

// AUTOINJECT
void Sound_Init(void) {
    DList_Init(&DynamicSoundList, sizeof(DYNAMICSOUNDS), NUM_DYNAMIC_SOUNDS);
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
DYNAMICSOUNDS* Sound_Play(Action_SFX sfxId, _VECTOR *position, float volume, float radiusOuter, float radiusInner, short pitch, char is3d, undefined4 ref) {

    DYNAMICSOUNDS* snd = (DYNAMICSOUNDS *)DList_MoveFromFree2InUse(&DynamicSoundList);
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
    snd->pitch = pitch;
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

// Starts a sound. A 2D one always gives its subtitle. A 3D one past 1.1 times its outer radius does not start if
// there is one listener and the table has it truly 3D and not looping; one within half its outer radius, in a cut
// scene, or not truly 3D, gives its subtitle.
void Sound_ReqestPlaySfx(DYNAMICSOUNDS *snd) {
    if (!snd->is3d) {
        SFXStart(snd->sfxId & SFX_ID_MASK, snd);
        snd->audible = 1;
        return;
    }

    double dist = Vec_Dist3D(&GlobalListenerPos, &snd->location);
    uint listeners = MPSettings.isMultiplayer ? MPSettings.numPlayers : 1;
    uint id = snd->sfxId;
    bool inTable = id < ARRAY_SIZE(SFXOutputData);
    if (!(inTable && SFXOutputData[id].loopAlways) && listeners == 1 && inTable && SFXOutputData[id].tracked3d
        && dist > (double)snd->radiusOuter * 1.1f)
        return;

    _VECTOR pos = snd->location;
    _VECTOR vel = snd->velocity;
    snd->audible = dist < 0.5f * (double)snd->radiusOuter;
    if (ScriptCam != HASHCODE_NONE || !inTable || !SFXOutputData[id].tracked3d)
        snd->audible = 1;

    if (snd->limitedDistance)
        SFXStart3D(id & SFX_ID_MASK, &pos, &vel, snd, Ftol(snd->volume), Ftol(snd->radiusInner),
                   Ftol(snd->radiusOuter));
    else
        SFXStart3D(id & SFX_ID_MASK, &pos, &vel, snd, Ftol(snd->volume), -1, -1);
}

// Every sound in use, once a frame: a new one started, a playing one stopped once it ends (a looping one only when
// stopped), a stopped one freed; a playing 3D one moved. The listener is not read.
// AUTOINJECT
void Sound_UpdateSounds(_VECTOR *listener) {
    DYNAMICSOUNDS *next;
    for (DYNAMICSOUNDS *snd = (DYNAMICSOUNDS *)DynamicSoundList.activeList.head; snd != NULL; snd = next) {
        next = snd->next;
        snd->age++;

        switch (snd->playbackState) {
        case 0:
            Sound_ReqestPlaySfx(snd);
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
            DList_RemoveFromInUse2Free(&DynamicSoundList, (LLNODE_tag *)snd);
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

    viewer_tag *script = glb_viewer[VIEWER_SCRIPT];
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
        cel_tag *cel = view->cel;
        if (cel != NULL) {
            bool outdoors = (cel->bitsFromPlacementTag & CEL_NO_SKY) || cel->reverb >= 0x14;
            SFXSetEnvironment(cel->reverb, outdoors);
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
// sound that took it there. The large wooden door's sounds are not heard in TowerA and TowerB; the gong's becomes as
// loud to its outer radius, with alertness 10.
// AUTOINJECT
float Sound_Alertness(_VECTOR *pos, float *level, DYNAMICSOUNDS **loudest) {
    float sum = 0.0f;
    float best = 0.0f;
    if (loudest != NULL)
        *loudest = NULL;

    DYNAMICSOUNDS *next;
    for (DYNAMICSOUNDS *snd = (DYNAMICSOUNDS *)DynamicSoundList.activeList.head; snd != NULL; snd = next) {
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
        if (id >= SFX_ENV_LARGE_WOODEN_DOOR_CLOSE) {
            if (id <= SFX_ENV_LARGE_WOODEN_DOOR_OPEN) {
                if (GameState.CurrentLevelHashcode == HT_Level_TowerA || GameState.CurrentLevelHashcode == HT_Level_TowerB)
                    continue;
            } else if (id == SFX_ENV_GONG_BULLET_IMPACT) {
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
DYNAMICSOUNDS* Sound_Play3D(Action_SFX sfxId,_VECTOR *position,float volume,float radiusOuter,float radiusInner, undefined2 pitch,undefined4 ref,int isLimitedRadius) {
  
    undefined1 tmpFrameDelay = (undefined1)Sound_FrameDelay;
    Sound_FrameDelay = 0;
    
    if ((sfxId & SFX_ID_MASK) == SFX_NONE)
        return NULL;

    if (isLimitedRadius == 0) {
        radiusInner = -1.0;
        radiusOuter = -1.0;
    }
    DYNAMICSOUNDS *snd = Sound_Play(sfxId,position,volume,radiusOuter,radiusInner,pitch,1,ref);
    if (snd != NULL) {
        snd->isLimitedRadius = (char)isLimitedRadius;
        snd->frameDelay = tmpFrameDelay;
    }
    
    return snd;
}

// AUTOINJECT
DYNAMICSOUNDS* Sound_PlayExt(Action_SFX sfxId, float volume, undefined2 pitch, undefined4 ref) {
      
    undefined1 tmpFrameDelay = (undefined1)Sound_FrameDelay;
    Sound_FrameDelay = 0;
    
    if ((sfxId & SFX_ID_MASK) == SFX_NONE)
        return NULL;

    DYNAMICSOUNDS *snd = Sound_Play(sfxId,&CONST_ZERO_VECTOR,volume,-1.0,-1.0,pitch,0,ref);
    if (snd != NULL) {
        snd->frameDelay = tmpFrameDelay;
    }
    
    return snd;
}

// AUTOINJECT
void Sound_StopAllWithId(Action_SFX sfx) {

    // Iterate over the playback list
    for(DYNAMICSOUNDS* ds = (DYNAMICSOUNDS*)DynamicSoundList.activeList.head; ds != NULL; ds = ds->next) {
        // If it matches, set its status to 2
        if(ds->sfxId == sfx)
            ds->playbackState = 2;
    }

}

// AUTOINJECT
void Sound_ZeroAlertness(void) {

    for(DYNAMICSOUNDS* ds = (DYNAMICSOUNDS*)DynamicSoundList.activeList.head; ds != NULL; ds = ds->next) {
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
    float pitch;          // 0x14 truncated to a short for Sound_Play's pitch
    float radiusOuter;    // 0x18 also the distance at which the sound starts (and 1.2x it, stops)
    float radiusInner;    // 0x1c
} MapSound;
#pragma pack(pop)
static_assert(sizeof(MapSound) == 0x20, "Bad size for MapSound");
static_assert(offsetof(MapSound, position) == 0x08, "Bad offset of MapSound.position");
static_assert(offsetof(MapSound, radiusOuter) == 0x18, "Bad offset of MapSound.radiusOuter");

// XBE_GLOBAL(0x00299ae8, 0x640)
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
    {(Action_TranslatedText)0x06000007, SFX_CHR_SPEECH_M3_KIK_004},
    {(Action_TranslatedText)(0x06000008 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M3_GUA_001},
    {(Action_TranslatedText)0x06000009, SFX_CHR_SPEECH_M3_DOM_001},
    {(Action_TranslatedText)0x0600000a, SFX_CHR_SPEECH_M3_DOM_002},
    {(Action_TranslatedText)0x0600000b, SFX_CHR_SPEECH_M3_DOM_002_ALT},
    {(Action_TranslatedText)0x0600000c, SFX_CHR_SPEECH_M3_BON_005},
    {(Action_TranslatedText)0x0600000d, SFX_CHR_SPEECH_M3_DOM_003},
    {(Action_TranslatedText)0x0600000e, SFX_CHR_SPEECH_M3_DOM_004},
    {(Action_TranslatedText)0x0600000f, SFX_CHR_SPEECH_M3_BON_007},
    {(Action_TranslatedText)0x06000010, SFX_CHR_SPEECH_M3_GUA_011},
    {(Action_TranslatedText)0x06000011, SFX_CHR_SPEECH_M3_GUA_012},
    {(Action_TranslatedText)0x06000012, SFX_CHR_SPEECH_M3_GUA_021},
    {(Action_TranslatedText)0x06000013, SFX_CHR_SPEECH_M3_GUA_022},
    {(Action_TranslatedText)0x06000014, SFX_CHR_SPEECH_M3_GUA_023},
    {(Action_TranslatedText)0x06000015, SFX_CHR_SPEECH_M3_BUI_001},
    {(Action_TranslatedText)0x06000016, SFX_CHR_SPEECH_M3_BUI_001A},
    {(Action_TranslatedText)0x06000017, SFX_CHR_SPEECH_M3_BUI_002},
    {(Action_TranslatedText)0x06000018, SFX_CHR_SPEECH_M3_BUI_003},
    {(Action_TranslatedText)0x06000019, SFX_CHR_SPEECH_M3_BUI_004},
    {(Action_TranslatedText)0x0600001a, SFX_CHR_SPEECH_M3_BUI_005},
    {(Action_TranslatedText)0x0600001b, SFX_CHR_SPEECH_M3_BUI_006},
    {(Action_TranslatedText)0x0600001c, SFX_CHR_SPEECH_M3_BUI_007},
    {(Action_TranslatedText)0x0600001d, SFX_CHR_SPEECH_M3_BUI_008},
    {(Action_TranslatedText)0x0600001e, SFX_CHR_SPEECH_M3_BUI_009},
    {(Action_TranslatedText)0x0600001f, SFX_CHR_SPEECH_M3_BUI_010},
    {(Action_TranslatedText)0x06000020, SFX_CHR_SPEECH_M3_BUI_011},
    {(Action_TranslatedText)0x06000021, SFX_CHR_SPEECH_M3_BUI_012},
    {(Action_TranslatedText)0x06000022, SFX_CHR_SPEECH_M3_BUI_013},
    {(Action_TranslatedText)0x06000023, SFX_CHR_SPEECH_M3_BUI_014},
    {(Action_TranslatedText)0x06000024, SFX_CHR_SPEECH_M3_BUI_015},
    {(Action_TranslatedText)0x06000025, SFX_CHR_SPEECH_M3_BUI_020},
    {(Action_TranslatedText)0x06000026, SFX_CHR_SPEECH_M3_BUI_021},
    {(Action_TranslatedText)0x06000027, SFX_CHR_SPEECH_M3_BUI_022},
    {(Action_TranslatedText)0x06000028, SFX_CHR_SPEECH_M3_BUI_023},
    {(Action_TranslatedText)0x06000029, SFX_CHR_SPEECH_M3_BUI_027},
    {(Action_TranslatedText)0x0600002a, SFX_CHR_SPEECH_M3_BUI_028},
    {(Action_TranslatedText)0x0600002b, SFX_CHR_SPEECH_M3_BUI_029},
    {(Action_TranslatedText)0x0600002c, SFX_CHR_SPEECH_M3_BUI_030},
    {(Action_TranslatedText)0x0600002d, SFX_CHR_SPEECH_M5_BO1_003},
    {(Action_TranslatedText)0x0600002e, SFX_CHR_SPEECH_M5_BO1_004},
    {(Action_TranslatedText)0x0600002f, SFX_CHR_SPEECH_M5_BO1_005},
    {(Action_TranslatedText)0x06000030, SFX_CHR_SPEECH_M5_BO1_006},
    {(Action_TranslatedText)0x06000031, SFX_CHR_SPEECH_M5_BO1_007},
    {(Action_TranslatedText)0x06000032, SFX_CHR_SPEECH_M5_BO1_008},
    {(Action_TranslatedText)0x06000033, SFX_CHR_SPEECH_M5_BO1_011},
    {(Action_TranslatedText)0x06000034, SFX_CHR_SPEECH_M5_BO1_012},
    {(Action_TranslatedText)0x0600003d, SFX_CHR_SPEECH_M5_ELE_001},
    {(Action_TranslatedText)(0x0600003e | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M5_YAK_001},
    {(Action_TranslatedText)0x0600003f, SFX_CHR_SPEECH_M5_BON_004},
    {(Action_TranslatedText)0x06000040, SFX_CHR_SPEECH_M5_BON_004_ALT4},
    {(Action_TranslatedText)0x06000047, SFX_CHR_SPEECH_M2_INTRO4_MAYHEW1},
    {(Action_TranslatedText)0x06000048, SFX_CHR_SPEECH_M2_INTRO4_BOND1},
    {(Action_TranslatedText)0x06000049, SFX_CHR_SPEECH_M2_INTRO5_KIKO1},
    {(Action_TranslatedText)0x0600004a, SFX_CHR_SPEECH_M2_INTRO5_MAYHEW1},
    {(Action_TranslatedText)0x0600004b, SFX_CHR_SPEECH_M2_BUNKER_OBJ_MAYHEW1},
    {(Action_TranslatedText)0x0600004c, SFX_CHR_SPEECH_M2_BUNKER_OBJ_BOND1},
    {(Action_TranslatedText)0x0600004d, SFX_CHR_SPEECH_M2_BUNKER_OBJ_MAYHEW2},
    {(Action_TranslatedText)0x0600004e, SFX_CHR_SPEECH_M2_BUNKER_OBJ_BOND2},
    {(Action_TranslatedText)0x0600004f, SFX_CHR_SPEECH_M2_BUNKER_OBJ_MAYHEW3},
    {(Action_TranslatedText)0x06000050, SFX_CHR_SPEECH_M2_BUNKER_OBJ_BOND3},
    {(Action_TranslatedText)0x06000051, SFX_CHR_SPEECH_M2_BUNKER_FAIL_MAYHEW13},
    {(Action_TranslatedText)0x06000052, SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW14},
    {(Action_TranslatedText)0x06000053, SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW15},
    {(Action_TranslatedText)0x06000054, SFX_CHR_SPEECH_M2_MAY_DIR_MAYHEW19},
    {(Action_TranslatedText)0x06000055, SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW3},
    {(Action_TranslatedText)0x06000056, SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW6},
    {(Action_TranslatedText)0x06000057, SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW8},
    {(Action_TranslatedText)0x06000058, SFX_CHR_SPEECH_M2_MAY_SCOLDING_MAYHEW12},
    {(Action_TranslatedText)(0x06000059 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M2_SNIPER_YAK1},
    {(Action_TranslatedText)0x0600005a, SFX_CHR_SPEECH_M2_BATH_SERV1},
    {(Action_TranslatedText)0x0600005b, SFX_CHR_SPEECH_M2_BATH_BOND1},
    {(Action_TranslatedText)(0x0600005c | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M2_AMBUSH_YAK1},
    {(Action_TranslatedText)(0x0600005d | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M2_AMBUSH_YAK21},
    {(Action_TranslatedText)0x0600005e, SFX_CHR_SPEECH_M2_BUNKER_COMPLETE_BOND1},
    {(Action_TranslatedText)0x0600005f, SFX_CHR_SPEECH_M2_BUNKER_COMPLETE_MAYHEW1},
    {(Action_TranslatedText)0x06000060, SFX_CHR_SPEECH_M2_BUNKER_COMPLETE_BOND2},
    {(Action_TranslatedText)(0x0600006c | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_DRIVER_TALK_DRIVER01},
    {(Action_TranslatedText)(0x0600006d | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_DRIVER_TALK_GUARD01A},
    {(Action_TranslatedText)(0x0600006e | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_DRIVER_TALK_DRIVER02},
    {(Action_TranslatedText)(0x0600006f | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_DRIVER_TALK_GUARD02},
    {(Action_TranslatedText)(0x06000070 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDA01},
    {(Action_TranslatedText)(0x06000071 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDB01},
    {(Action_TranslatedText)(0x06000072 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDA02},
    {(Action_TranslatedText)0x06000073, SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDA04},
    {(Action_TranslatedText)0x06000074, SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDB05},
    {(Action_TranslatedText)0x06000075, SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDA05},
    {(Action_TranslatedText)0x06000076, SFX_CHR_SPEECH_M1_GUARD_TALK_SOLDB06},
    {(Action_TranslatedText)(0x06000077 | SUBTITLE_ALWAYS), SFX_CHR_SPEECH_M1_GUARD_LIGHTS_SOLDA02},
    {(Action_TranslatedText)0x06000078, SFX_CHR_SPEECH_M1_BOND_ICY_QUIP_BOND4},
    {(Action_TranslatedText)0x06000079, SFX_CHR_SPEECH_M1_TUX_CHANGE_M1},
    {(Action_TranslatedText)0x0600007a, SFX_CHR_SPEECH_M1_RESCUE_ZOE7},
    {(Action_TranslatedText)0x0600007b, SFX_CHR_SPEECH_CAS_DRAKE1B},
    {(Action_TranslatedText)0x0600007c, SFX_CHR_SPEECH_M1_LIBRARY1_DOM1},
    {(Action_TranslatedText)0x0600007d, SFX_CHR_SPEECH_M1_LIBRARY1_BOND2},
    {(Action_TranslatedText)0x0600007e, SFX_CHR_SPEECH_M1_LIBRARY1_DOM3},
    {(Action_TranslatedText)0x0600007f, SFX_CHR_SPEECH_M1_LIBRARY1_ZOE4},
    {(Action_TranslatedText)0x06000080, SFX_CHR_SPEECH_M1_LIBRARY_DOM5},
    {(Action_TranslatedText)0x06000081, SFX_CHR_SPEECH_M1_LIBRARY1_ZOE6},
    {(Action_TranslatedText)0x06000082, SFX_CHR_SPEECH_M1_LIBRARY1_DOM7},
    {(Action_TranslatedText)0x06000083, SFX_CHR_SPEECH_M1_LIBRARY2_BOND1},
    {(Action_TranslatedText)0x06000084, SFX_CHR_SPEECH_M1_LIBRARY2_ZOE2},
    {(Action_TranslatedText)0x06000085, SFX_CHR_SPEECH_M1_LIBRARY2_BOND3},
    {(Action_TranslatedText)0x06000086, SFX_CHR_SPEECH_M1_LIBRARY2_ZOE4},
    {(Action_TranslatedText)0x06000087, SFX_CHR_SPEECH_M1_MEETING2_DRAKE2_FIX},
    {(Action_TranslatedText)0x06000088, SFX_CHR_SPEECH_M1_MEETING2_MAYHEW3},
    {(Action_TranslatedText)0x06000089, SFX_CHR_SPEECH_M1_MEETING3_ZOE1},
    {(Action_TranslatedText)0x0600008a, SFX_CHR_SPEECH_M1_MEETING3_GUARD2},
    {(Action_TranslatedText)0x0600008b, SFX_CHR_SPEECH_M1_MEETING3_ZOE3},
    {(Action_TranslatedText)0x0600008c, SFX_CHR_SPEECH_M1_MEETING3_GUARD4},
    {(Action_TranslatedText)0x0600008d, SFX_CHR_SPEECH_M1_MEETING3_ZOE5},
    {(Action_TranslatedText)0x0600008e, SFX_CHR_SPEECH_M1_MEETING4_ZOE6},
    {(Action_TranslatedText)0x0600008f, SFX_CHR_SPEECH_M1_MEETING_6_DRAKE1_FIX},
    {(Action_TranslatedText)0x06000090, SFX_CHR_SPEECH_M1_MEETING7_DRAKE2_FIX},
    {(Action_TranslatedText)0x06000098, SFX_CHR_SPEECH_AST_ROOK1_ALT},
    {(Action_TranslatedText)0x06000099, SFX_CHR_SPEECH_CAS_IG_KIKOBOND1B},
    {(Action_TranslatedText)0x0600009a, SFX_CHR_SPEECH_CAS_IG_KIKOBOND2B},
    {(Action_TranslatedText)0x0600009b, SFX_CHR_SPEECH_CAS_IG_KIKOBOND3B},
    {(Action_TranslatedText)0x0600009c, SFX_CHR_SPEECH_CAS_IG_KIKOBOND6B},
    {(Action_TranslatedText)0x0600009e, SFX_CHR_SPEECH_CAS_DRAKE1},
    {(Action_TranslatedText)0x0600009f, SFX_CHR_SPEECH_CAS_DRAKE2},
    {(Action_TranslatedText)0x060000a0, SFX_CHR_SPEECH_CAS_IG_GUEST_F1_USA},
    {(Action_TranslatedText)0x060000a1, SFX_CHR_SPEECH_CAS_IG_GUEST_F10_USA},
    {(Action_TranslatedText)0x060000a2, SFX_CHR_SPEECH_CAS_IG_GUEST_F2_USA},
    {(Action_TranslatedText)0x060000a3, SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_USA},
    {(Action_TranslatedText)0x060000a4, SFX_CHR_SPEECH_CAS_IG_GUEST_F3_USA},
    {(Action_TranslatedText)0x060000a5, SFX_CHR_SPEECH_CAS_IG_GUEST_F4_USA},
    {(Action_TranslatedText)0x060000a6, SFX_CHR_SPEECH_CAS_IG_GUEST_F5_USA},
    {(Action_TranslatedText)0x060000a7, SFX_CHR_SPEECH_CAS_IG_GUEST_F6_USA},
    {(Action_TranslatedText)0x060000a8, SFX_CHR_SPEECH_CAS_IG_GUEST_F7_USA},
    {(Action_TranslatedText)0x060000a9, SFX_CHR_SPEECH_CAS_IG_GUEST_F8_USA},
    {(Action_TranslatedText)0x060000aa, SFX_CHR_SPEECH_CAS_IG_GUEST_F9_USA},
    {(Action_TranslatedText)0x060000ab, SFX_CHR_SPEECH_CAS_IG_GUEST_M1_E},
    {(Action_TranslatedText)0x060000ac, SFX_CHR_SPEECH_CAS_IG_GUEST_M3_E},
    {(Action_TranslatedText)0x060000ad, SFX_CHR_SPEECH_CAS_IG_GUEST_M4_E},
    {(Action_TranslatedText)0x060000ae, SFX_CHR_SPEECH_CAS_IG_GUEST_M5_E},
    {(Action_TranslatedText)0x060000af, SFX_CHR_SPEECH_CAS_IG_GUEST_M6_E},
    {(Action_TranslatedText)0x060000b0, SFX_CHR_SPEECH_CAS_IG_GUEST_M7_E},
    {(Action_TranslatedText)0x060000b1, SFX_CHR_SPEECH_CAS_IG_GUEST_M8_E},
    {(Action_TranslatedText)0x060000b2, SFX_CHR_SPEECH_CAS_IG_GUEST_M9_E},
    {(Action_TranslatedText)0x060000b3, SFX_CHR_SPEECH_CAS_IG_INT_GUARD11},
    {(Action_TranslatedText)0x060000b4, SFX_CHR_SPEECH_CAS_IG_INT_GUARD12},
    {(Action_TranslatedText)0x060000b5, SFX_CHR_SPEECH_CAS_IG_INT_GUARD13},
    {(Action_TranslatedText)0x060000b6, SFX_CHR_SPEECH_CAS_IG_INT_GUARD6},
    {(Action_TranslatedText)0x060000b7, SFX_CHR_SPEECH_CAS_IG_INT_GUARD9},
    {(Action_TranslatedText)0x060000b8, SFX_CHR_SPEECH_CAS_IG_KIKO02},
    {(Action_TranslatedText)0x060000b9, SFX_CHR_SPEECH_CAS_IG_KIKO03},
    {(Action_TranslatedText)0x060000ba, SFX_CHR_SPEECH_CAS_IG_KIKOBOND1A},
    {(Action_TranslatedText)0x060000bb, SFX_CHR_SPEECH_CAS_IG_KIKOBOND2A},
    {(Action_TranslatedText)0x060000bc, SFX_CHR_SPEECH_CAS_IG_KIKOBOND3A},
    {(Action_TranslatedText)0x060000bd, SFX_CHR_SPEECH_CAS_IG_KIKOBOND6A},
    {(Action_TranslatedText)0x060000be, SFX_CHR_SPEECH_CAS_IG_ROOK1},
    {(Action_TranslatedText)0x060000bf, SFX_CHR_SPEECH_CAS_IG_ROOK2},
    {(Action_TranslatedText)0x060000c0, SFX_CHR_SPEECH_CAS_IG_ROOK3},
    {(Action_TranslatedText)0x060000c1, SFX_CHR_SPEECH_CAS_ROOK1},
    {(Action_TranslatedText)0x060000c2, SFX_CHR_SPEECH_CAS_IG_ZOE04A},
    {(Action_TranslatedText)0x060000c3, SFX_CHR_SPEECH_CAS_IG_ZOE06B},
    {(Action_TranslatedText)0x060000c4, SFX_CHR_SPEECH_CAS_IG_ZOE6B2},
    {(Action_TranslatedText)0x060000c5, SFX_CHR_SPEECH_CAS_IG_ZOE08},
    {(Action_TranslatedText)0x060000c6, SFX_CHR_SPEECH_EST_BOND1},
    {(Action_TranslatedText)0x060000c7, SFX_CHR_SPEECH_EST_KIKO1},
    {(Action_TranslatedText)0x060000c8, SFX_CHR_SPEECH_EST_MAYHEW1},
    {(Action_TranslatedText)0x060000cd, SFX_CHR_SPEECH_ISS_ALU_5},
    {(Action_TranslatedText)0x060000ce, SFX_CHR_SPEECH_ISS_ALU_6},
    {(Action_TranslatedText)0x060000cf, SFX_CHR_SPEECH_ISS_ALU_7},
    {(Action_TranslatedText)0x060000d3, SFX_CHR_SPEECH_ISS_BLOCK1B},
    {(Action_TranslatedText)0x060000d6, SFX_CHR_SPEECH_ISS_BOND_1},
    {(Action_TranslatedText)0x060000d7, SFX_CHR_SPEECH_ISS_BOND_2},
    {(Action_TranslatedText)0x060000d8, SFX_CHR_SPEECH_ISS_BOND_3},
    {(Action_TranslatedText)0x060000d9, SFX_CHR_SPEECH_ISS_BOND_5},
    {(Action_TranslatedText)0x060000da, SFX_CHR_SPEECH_ISS_BLOCK1A},
    {(Action_TranslatedText)0x060000db, SFX_CHR_SPEECH_ISS_BLOCK1C},
    {(Action_TranslatedText)0x060000dc, SFX_CHR_SPEECH_ISS_DRA_3},
    {(Action_TranslatedText)0x060000dd, SFX_CHR_SPEECH_ISS_DRA_4},
    {(Action_TranslatedText)0x060000de, SFX_CHR_SPEECH_ISS_SYS_1},
    {(Action_TranslatedText)0x060000df, SFX_CHR_SPEECH_ISS_SYS_2},
    {(Action_TranslatedText)0x060000e0, SFX_CHR_SPEECH_ISS_USAF_1},
    {(Action_TranslatedText)0x060000e1, SFX_CHR_SPEECH_ISS_USAF_2},
    {(Action_TranslatedText)0x060000e2, SFX_CHR_SPEECH_NUK_BOND1},
    {(Action_TranslatedText)0x060000ed, SFX_CHR_SPEECH_NUK_ROOK2A},
    {(Action_TranslatedText)0x060000ee, SFX_CHR_SPEECH_NUK_ROOK2B},
    {(Action_TranslatedText)0x060000ef, SFX_CHR_SPEECH_TWI_BOND1},
    {(Action_TranslatedText)0x060000f5, SFX_CHR_SPEECH_USB_ALU_5A},
    {(Action_TranslatedText)0x060000f6, SFX_CHR_SPEECH_USB_ALU_5B},
    {(Action_TranslatedText)0x06000101, SFX_CHR_SPEECH_USB_IG_BOND_6},
    {(Action_TranslatedText)0x06000102, SFX_CHR_SPEECH_USB_IG_BOND_7},
    {(Action_TranslatedText)0x06000104, SFX_CHR_SPEECH_USB_IG_DRA_1},
    {(Action_TranslatedText)0x06000105, SFX_CHR_SPEECH_USB_IG_DRA_2},
    {(Action_TranslatedText)0x06000106, SFX_CHR_SPEECH_USB_IG_DRA_3},
    {(Action_TranslatedText)0x06000107, SFX_CHR_SPEECH_USB_IG_DRA_4},
    {(Action_TranslatedText)0x06000108, SFX_CHR_SPEECH_USB_IG_DRA_5},
    {(Action_TranslatedText)0x06000109, SFX_CHR_SPEECH_USB_KIK_1},
    {(Action_TranslatedText)0x0600010a, SFX_CHR_SPEECH_USB_KIK_2},
    {(Action_TranslatedText)0x0600010b, SFX_CHR_SPEECH_USB_KIK_3},
    {(Action_TranslatedText)0x0600010c, SFX_CHR_SPEECH_USB_KIK_4},
    {(Action_TranslatedText)0x0600010d, SFX_CHR_SPEECH_USB_KIK_5},
    {(Action_TranslatedText)0x0600010e, SFX_CHR_SPEECH_USB_IG_KIKOTAUNT1},
    {(Action_TranslatedText)0x0600010f, SFX_CHR_SPEECH_USB_IG_KIKOTAUNT13},
    {(Action_TranslatedText)0x06000110, SFX_CHR_SPEECH_USB_IG_KIKOTAUNT14},
    {(Action_TranslatedText)0x06000111, SFX_CHR_SPEECH_USB_IG_KIKOTAUNT9},
    {(Action_TranslatedText)0x06000112, SFX_CHR_SPEECH_USB_PA_1A},
    {(Action_TranslatedText)0x06000113, SFX_CHR_SPEECH_USB_PA_1B},
    {(Action_TranslatedText)0x06000114, SFX_CHR_SPEECH_USB_PA_1C},
    {(Action_TranslatedText)0x06000115, SFX_CHR_SPEECH_USB_PA_1D},
    {(Action_TranslatedText)0x06000117, SFX_CHR_SPEECH_USB_SYS_1},
    {(Action_TranslatedText)0x06000118, SFX_CHR_SPEECH_CAS_IG_GUEST_F2_BRIT},
    {(Action_TranslatedText)0x06000119, SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_BRIT},
    {(Action_TranslatedText)0x0600011a, SFX_CHR_SPEECH_CAS_IG_GUEST_F3_BRIT},
    {(Action_TranslatedText)0x0600011b, SFX_CHR_SPEECH_CAS_IG_GUEST_F4_BRIT},
    {(Action_TranslatedText)0x0600011c, SFX_CHR_SPEECH_CAS_IG_GUEST_F5_BRIT},
    {(Action_TranslatedText)0x0600011d, SFX_CHR_SPEECH_CAS_IG_GUEST_F6_BRIT},
    {(Action_TranslatedText)0x0600011e, SFX_CHR_SPEECH_CAS_IG_GUEST_F7_BRIT},
    {(Action_TranslatedText)0x0600011f, SFX_CHR_SPEECH_CAS_IG_GUEST_F8_BRIT},
    {(Action_TranslatedText)0x06000120, SFX_CHR_SPEECH_CAS_IG_GUEST_F9_BRIT},
    {(Action_TranslatedText)0x06000121, SFX_CHR_SPEECH_CAS_IG_GUEST_F1_AUS},
    {(Action_TranslatedText)0x06000122, SFX_CHR_SPEECH_CAS_IG_GUEST_F10_AUS},
    {(Action_TranslatedText)0x06000123, SFX_CHR_SPEECH_CAS_IG_GUEST_F2_AUS},
    {(Action_TranslatedText)0x06000124, SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_AUS},
    {(Action_TranslatedText)0x06000125, SFX_CHR_SPEECH_CAS_IG_GUEST_F3_AUS},
    {(Action_TranslatedText)0x06000126, SFX_CHR_SPEECH_CAS_IG_GUEST_F4_AUS},
    {(Action_TranslatedText)0x06000127, SFX_CHR_SPEECH_CAS_IG_GUEST_F5_AUS},
    {(Action_TranslatedText)0x06000128, SFX_CHR_SPEECH_CAS_IG_GUEST_F6_AUS},
    {(Action_TranslatedText)0x06000129, SFX_CHR_SPEECH_CAS_IG_GUEST_F7_AUS},
    {(Action_TranslatedText)0x0600012a, SFX_CHR_SPEECH_CAS_IG_GUEST_F8_AUS},
    {(Action_TranslatedText)0x0600012b, SFX_CHR_SPEECH_CAS_IG_GUEST_F9_AUS},
    {(Action_TranslatedText)0x0600012c, SFX_CHR_SPEECH_CAS_IG_GUEST_F1_RUS},
    {(Action_TranslatedText)0x0600012d, SFX_CHR_SPEECH_CAS_IG_GUEST_F10_RUS},
    {(Action_TranslatedText)0x0600012e, SFX_CHR_SPEECH_CAS_IG_GUEST_F2_RUS},
    {(Action_TranslatedText)0x0600012f, SFX_CHR_SPEECH_CAS_IG_GUEST_F2B_RUS},
    {(Action_TranslatedText)0x06000130, SFX_CHR_SPEECH_CAS_IG_GUEST_F3_RUS},
    {(Action_TranslatedText)0x06000131, SFX_CHR_SPEECH_CAS_IG_GUEST_F4_RUS},
    {(Action_TranslatedText)0x06000132, SFX_CHR_SPEECH_CAS_IG_GUEST_F5_RUS},
    {(Action_TranslatedText)0x06000133, SFX_CHR_SPEECH_CAS_IG_GUEST_F6_RUS},
    {(Action_TranslatedText)0x06000134, SFX_CHR_SPEECH_CAS_IG_GUEST_F7_RUS},
    {(Action_TranslatedText)0x06000135, SFX_CHR_SPEECH_CAS_IG_GUEST_F8_RUS},
    {(Action_TranslatedText)0x06000136, SFX_CHR_SPEECH_CAS_IG_GUEST_F9_RUS},
    {(Action_TranslatedText)0x06000138, SFX_CHR_SPEECH_CAS_IG_INT_GUARD10_B},
    {(Action_TranslatedText)0x06000139, SFX_CHR_SPEECH_CAS_IG_INT_GUARD11_B},
    {(Action_TranslatedText)0x0600013a, SFX_CHR_SPEECH_CAS_IG_INT_GUARD12_B},
    {(Action_TranslatedText)0x0600013b, SFX_CHR_SPEECH_CAS_IG_INT_GUARD18_B},
    {(Action_TranslatedText)0x0600013c, SFX_CHR_SPEECH_CAS_IG_INT_GUARD19_B},
    {(Action_TranslatedText)0x0600013d, SFX_CHR_SPEECH_CAS_IG_INT_GUARD5_B},
    {(Action_TranslatedText)0x0600013e, SFX_CHR_SPEECH_CAS_IG_INT_GUARD8_B},
    {(Action_TranslatedText)0x0600013f, SFX_CHR_SPEECH_CAS_IG_INT_GUARD9_B},
    {(Action_TranslatedText)0x06000140, SFX_CHR_SPEECH_CAS_IG_ROOK4_ALT},
    {(Action_TranslatedText)0x06000141, SFX_CHR_SPEECH_CAS_MAYHEW1_FIX},
    {(Action_TranslatedText)0x06000142, SFX_CHR_SPEECH_ISS_IG_ALU_10},
    {(Action_TranslatedText)0x06000143, SFX_CHR_SPEECH_ISS_IG_ALU_11},
    {(Action_TranslatedText)0x06000144, SFX_CHR_SPEECH_ISS_IG_ALU_12},
    {(Action_TranslatedText)0x06000145, SFX_CHR_SPEECH_ISS_IG_ALU_13},
    {(Action_TranslatedText)0x06000146, SFX_CHR_SPEECH_ISS_IG_ALU_14},
    {(Action_TranslatedText)0x06000147, SFX_CHR_SPEECH_ISS_IG_ALU_15},
    {(Action_TranslatedText)0x06000148, SFX_CHR_SPEECH_ISS_IG_ALU_16},
    {(Action_TranslatedText)0x06000149, SFX_CHR_SPEECH_ISS_IG_ALU_17},
    {(Action_TranslatedText)0x0600014a, SFX_CHR_SPEECH_ISS_IG_ALU_18},
    {(Action_TranslatedText)0x0600014b, SFX_CHR_SPEECH_ISS_IG_ALU_19},
    {(Action_TranslatedText)0x0600014c, SFX_CHR_SPEECH_ISS_IG_ALU_20},
    {(Action_TranslatedText)0x0600014d, SFX_CHR_SPEECH_ISS_IG_ALU_21},
    {(Action_TranslatedText)0x0600014e, SFX_CHR_SPEECH_ISS_IG_ALU_22},
    {(Action_TranslatedText)0x0600014f, SFX_CHR_SPEECH_ISS_IG_ALU_23},
    {(Action_TranslatedText)0x06000150, SFX_CHR_SPEECH_ISS_IG_ALU_24},
    {(Action_TranslatedText)0x06000151, SFX_CHR_SPEECH_ISS_IG_ALU_25},
    {(Action_TranslatedText)0x06000152, SFX_CHR_SPEECH_ISS_IG_ALU_26},
    {(Action_TranslatedText)0x06000153, SFX_CHR_SPEECH_ISS_IG_ALU_27},
    {(Action_TranslatedText)0x06000154, SFX_CHR_SPEECH_ISS_IG_ALU_8},
    {(Action_TranslatedText)0x06000155, SFX_CHR_SPEECH_ISS_IG_ALU_9},
    {(Action_TranslatedText)0x06000156, SFX_CHR_SPEECH_ISS_IG_ALU_9_ALT1},
    {(Action_TranslatedText)0x06000157, SFX_CHR_SPEECH_ISS_IG_ALU_9_ALT2},
    {(Action_TranslatedText)0x06000158, SFX_CHR_SPEECH_ISS_IG_DRA_10A},
    {(Action_TranslatedText)0x06000159, SFX_CHR_SPEECH_ISS_IG_DRA_10B},
    {(Action_TranslatedText)0x0600015a, SFX_CHR_SPEECH_ISS_IG_DRA_11},
    {(Action_TranslatedText)0x0600015b, SFX_CHR_SPEECH_ISS_IG_DRA_12},
    {(Action_TranslatedText)0x0600015c, SFX_CHR_SPEECH_ISS_IG_DRA_13},
    {(Action_TranslatedText)0x0600015d, SFX_CHR_SPEECH_ISS_IG_DRA_14},
    {(Action_TranslatedText)0x0600015e, SFX_CHR_SPEECH_ISS_IG_DRA_15},
    {(Action_TranslatedText)0x0600015f, SFX_CHR_SPEECH_ISS_IG_DRA_16},
    {(Action_TranslatedText)0x06000160, SFX_CHR_SPEECH_ISS_IG_DRA_17},
    {(Action_TranslatedText)0x06000161, SFX_CHR_SPEECH_ISS_IG_DRA_17B},
    {(Action_TranslatedText)0x06000162, SFX_CHR_SPEECH_ISS_IG_DRA_18},
    {(Action_TranslatedText)0x06000163, SFX_CHR_SPEECH_ISS_IG_DRA_6},
    {(Action_TranslatedText)0x06000164, SFX_CHR_SPEECH_ISS_IG_DRA_7},
    {(Action_TranslatedText)0x06000165, SFX_CHR_SPEECH_ISS_IG_DRA_8},
    {(Action_TranslatedText)0x06000166, SFX_CHR_SPEECH_ISS_IG_DRA_9},
    {(Action_TranslatedText)0x06000168, SFX_CHR_SPEECH_TWI_M1},
    {(Action_TranslatedText)0x0600016b, SFX_CHR_SPEECH_USB_IG_ALU_10},
    {(Action_TranslatedText)0x0600016c, SFX_CHR_SPEECH_USB_IG_ALU_11},
    {(Action_TranslatedText)0x0600016d, SFX_CHR_SPEECH_USB_IG_ALU_12},
    {(Action_TranslatedText)0x0600016e, SFX_CHR_SPEECH_USB_IG_ALU_13},
    {(Action_TranslatedText)0x0600016f, SFX_CHR_SPEECH_USB_IG_ALU_14},
    {(Action_TranslatedText)0x06000170, SFX_CHR_SPEECH_USB_IG_ALU_15},
    {(Action_TranslatedText)0x06000171, SFX_CHR_SPEECH_USB_IG_ALU_16},
    {(Action_TranslatedText)0x06000172, SFX_CHR_SPEECH_USB_IG_ALU_17},
    {(Action_TranslatedText)0x06000173, SFX_CHR_SPEECH_USB_IG_ALU_18},
    {(Action_TranslatedText)0x06000174, SFX_CHR_SPEECH_USB_IG_ALU_19},
    {(Action_TranslatedText)0x06000175, SFX_CHR_SPEECH_USB_IG_ALU_20},
    {(Action_TranslatedText)0x06000176, SFX_CHR_SPEECH_USB_IG_ALU_21},
    {(Action_TranslatedText)0x06000177, SFX_CHR_SPEECH_USB_IG_ALU_22},
    {(Action_TranslatedText)0x06000178, SFX_CHR_SPEECH_USB_IG_ALU_23},
    {(Action_TranslatedText)0x06000179, SFX_CHR_SPEECH_USB_IG_ALU_24},
    {(Action_TranslatedText)0x0600017a, SFX_CHR_SPEECH_USB_IG_ALU_25},
    {(Action_TranslatedText)0x0600017b, SFX_CHR_SPEECH_USB_IG_ALU_26},
    {(Action_TranslatedText)0x0600017c, SFX_CHR_SPEECH_USB_IG_ALU_27},
    {(Action_TranslatedText)0x0600017d, SFX_CHR_SPEECH_USB_IG_ALU_28},
    {(Action_TranslatedText)0x0600017e, SFX_CHR_SPEECH_USB_IG_ALU_29},
    {(Action_TranslatedText)0x06000180, SFX_CHR_SPEECH_USB_IG_ALU_31},
    {(Action_TranslatedText)0x06000181, SFX_CHR_SPEECH_USB_IG_ALU_32},
    {(Action_TranslatedText)0x06000182, SFX_CHR_SPEECH_USB_IG_ALU_33},
    {(Action_TranslatedText)0x06000183, SFX_CHR_SPEECH_USB_IG_ALU_34},
    {(Action_TranslatedText)0x06000184, SFX_CHR_SPEECH_USB_IG_ALU_35},
    {(Action_TranslatedText)0x06000185, SFX_CHR_SPEECH_USB_IG_ALU_36},
    {(Action_TranslatedText)0x06000186, SFX_CHR_SPEECH_USB_IG_ALU_37},
    {(Action_TranslatedText)0x06000187, SFX_CHR_SPEECH_USB_IG_ALU_38},
    {(Action_TranslatedText)0x06000188, SFX_CHR_SPEECH_USB_IG_ALU_39},
    {(Action_TranslatedText)0x06000189, SFX_CHR_SPEECH_USB_IG_ALU_40},
    {(Action_TranslatedText)0x0600018a, SFX_CHR_SPEECH_USB_IG_ALU_42},
    {(Action_TranslatedText)0x0600018b, SFX_CHR_SPEECH_USB_IG_ALU_43},
    {(Action_TranslatedText)0x0600018c, SFX_CHR_SPEECH_USB_IG_ALU_44},
    {(Action_TranslatedText)0x0600018d, SFX_CHR_SPEECH_USB_IG_ALU_45},
    {(Action_TranslatedText)0x0600018e, SFX_CHR_SPEECH_USB_IG_ALU_46},
    {(Action_TranslatedText)0x0600018f, SFX_CHR_SPEECH_USB_IG_ALU_47},
    {(Action_TranslatedText)0x06000190, SFX_CHR_SPEECH_USB_IG_ALU_48},
    {(Action_TranslatedText)0x06000191, SFX_CHR_SPEECH_USB_IG_ALU_49},
    {(Action_TranslatedText)0x06000192, SFX_CHR_SPEECH_USB_IG_ALU_50},
    {(Action_TranslatedText)0x06000193, SFX_CHR_SPEECH_USB_IG_ALU_7},
    {(Action_TranslatedText)0x06000194, SFX_CHR_SPEECH_USB_IG_ALU_8},
    {(Action_TranslatedText)0x06000195, SFX_CHR_SPEECH_USB_IG_ALU_9},
    {(Action_TranslatedText)0x06000196, SFX_CHR_SPEECH_USB_GUA_1},
    {(Action_TranslatedText)0x06000197, SFX_CHR_SPEECH_USB_GUA_2},
    {(Action_TranslatedText)0x06000198, SFX_CHR_SPEECH_NUK_SNIP_1},
    {(Action_TranslatedText)0x06000199, SFX_CHR_SPEECH_NUK_SNIP_10},
    {(Action_TranslatedText)0x0600019a, SFX_CHR_SPEECH_NUK_SNIP_11},
    {(Action_TranslatedText)0x0600019b, SFX_CHR_SPEECH_NUK_SNIP_12},
    {(Action_TranslatedText)0x0600019c, SFX_CHR_SPEECH_NUK_SNIP_13},
    {(Action_TranslatedText)0x0600019d, SFX_CHR_SPEECH_NUK_SNIP_14},
    {(Action_TranslatedText)0x0600019e, SFX_CHR_SPEECH_NUK_SNIP_15},
    {(Action_TranslatedText)0x0600019f, SFX_CHR_SPEECH_NUK_SNIP_16},
    {(Action_TranslatedText)0x060001a0, SFX_CHR_SPEECH_NUK_SNIP_17},
    {(Action_TranslatedText)0x060001a1, SFX_CHR_SPEECH_NUK_SNIP_18},
    {(Action_TranslatedText)0x060001a2, SFX_CHR_SPEECH_NUK_SNIP_19},
    {(Action_TranslatedText)0x060001a3, SFX_CHR_SPEECH_NUK_SNIP_2},
    {(Action_TranslatedText)0x060001a4, SFX_CHR_SPEECH_NUK_SNIP_20},
    {(Action_TranslatedText)0x060001a5, SFX_CHR_SPEECH_NUK_SNIP_21},
    {(Action_TranslatedText)0x060001a6, SFX_CHR_SPEECH_NUK_SNIP_22},
    {(Action_TranslatedText)0x060001a7, SFX_CHR_SPEECH_NUK_SNIP_23},
    {(Action_TranslatedText)0x060001a8, SFX_CHR_SPEECH_NUK_SNIP_24},
    {(Action_TranslatedText)0x060001a9, SFX_CHR_SPEECH_NUK_SNIP_25},
    {(Action_TranslatedText)0x060001aa, SFX_CHR_SPEECH_NUK_SNIP_26},
    {(Action_TranslatedText)0x060001ab, SFX_CHR_SPEECH_NUK_SNIP_27},
    {(Action_TranslatedText)0x060001ac, SFX_CHR_SPEECH_NUK_SNIP_28},
    {(Action_TranslatedText)0x060001ad, SFX_CHR_SPEECH_NUK_SNIP_29},
    {(Action_TranslatedText)0x060001ae, SFX_CHR_SPEECH_NUK_SNIP_3},
    {(Action_TranslatedText)0x060001af, SFX_CHR_SPEECH_NUK_SNIP_30},
    {(Action_TranslatedText)0x060001b0, SFX_CHR_SPEECH_NUK_SNIP_31},
    {(Action_TranslatedText)0x060001b1, SFX_CHR_SPEECH_NUK_SNIP_32},
    {(Action_TranslatedText)0x060001b2, SFX_CHR_SPEECH_NUK_SNIP_33},
    {(Action_TranslatedText)0x060001b3, SFX_CHR_SPEECH_NUK_SNIP_34},
    {(Action_TranslatedText)0x060001b4, SFX_CHR_SPEECH_NUK_SNIP_35},
    {(Action_TranslatedText)0x060001b5, SFX_CHR_SPEECH_NUK_SNIP_36},
    {(Action_TranslatedText)0x060001b6, SFX_CHR_SPEECH_NUK_SNIP_37},
    {(Action_TranslatedText)0x060001b7, SFX_CHR_SPEECH_NUK_SNIP_38},
    {(Action_TranslatedText)0x060001b8, SFX_CHR_SPEECH_NUK_SNIP_39},
    {(Action_TranslatedText)0x060001b9, SFX_CHR_SPEECH_NUK_SNIP_4},
    {(Action_TranslatedText)0x060001ba, SFX_CHR_SPEECH_NUK_SNIP_40},
    {(Action_TranslatedText)0x060001bb, SFX_CHR_SPEECH_NUK_SNIP_41},
    {(Action_TranslatedText)0x060001bc, SFX_CHR_SPEECH_NUK_SNIP_42},
    {(Action_TranslatedText)0x060001bd, SFX_CHR_SPEECH_NUK_SNIP_43},
    {(Action_TranslatedText)0x060001be, SFX_CHR_SPEECH_NUK_SNIP_44},
    {(Action_TranslatedText)0x060001bf, SFX_CHR_SPEECH_NUK_SNIP_45},
    {(Action_TranslatedText)0x060001c0, SFX_CHR_SPEECH_NUK_SNIP_46},
    {(Action_TranslatedText)0x060001c1, SFX_CHR_SPEECH_NUK_SNIP_5},
    {(Action_TranslatedText)0x060001c2, SFX_CHR_SPEECH_NUK_SNIP_6},
    {(Action_TranslatedText)0x060001c3, SFX_CHR_SPEECH_NUK_SNIP_7},
    {(Action_TranslatedText)0x060001c4, SFX_CHR_SPEECH_NUK_SNIP_8},
    {(Action_TranslatedText)0x060001c5, SFX_CHR_SPEECH_NUK_SNIP_9},
    {(Action_TranslatedText)0x060001c6, SFX_CHR_SPEECH_M1_LIBRARY1_DOM8},
    {(Action_TranslatedText)0x060001c7, SFX_CHR_SPEECH_ISS_IG_ALU_28},
    {(Action_TranslatedText)0x060001c8, SFX_CHR_SPEECH_ISS_IG_ALU_29},
    {(Action_TranslatedText)0x060001c9, SFX_CHR_SPEECH_ISS_IG_ALU_30},
    {(Action_TranslatedText)0x060001ca, SFX_CHR_SPEECH_ISS_IG_ALU_31},
    {(Action_TranslatedText)0x060001cb, SFX_CHR_SPEECH_ISS_IG_ALU_32},
    {(Action_TranslatedText)0x060001cc, SFX_CHR_SPEECH_ISS_IG_ALU_33},
    {(Action_TranslatedText)0x060001cd, SFX_CHR_SPEECH_ISS_IG_ALU_34},
    {(Action_TranslatedText)0x060001ce, SFX_CHR_SPEECH_ISS_IG_ALU_35},
    {(Action_TranslatedText)0x060001cf, SFX_CHR_SPEECH_ISS_IG_ALU_36},
    {(Action_TranslatedText)0x060001d0, SFX_CHR_SPEECH_ISS_IG_ALU_37},
    {(Action_TranslatedText)0x060001d1, SFX_CHR_SPEECH_ISS_IG_ALU_38},
    {(Action_TranslatedText)0x060001d2, SFX_CHR_SPEECH_ISS_IG_ALU_39},
    {(Action_TranslatedText)0x060001d3, SFX_CHR_SPEECH_USB_IG_ALU_51},
    {(Action_TranslatedText)0x060001d4, SFX_CHR_SPEECH_USB_IG_ALU_52},
    {(Action_TranslatedText)0x060001d6, SFX_CHR_SPEECH_M3_BUI_031A},
    {(Action_TranslatedText)0x060001d7, SFX_CHR_SPEECH_M3_BUI_031B},
    {(Action_TranslatedText)0x060001d8, SFX_CHR_SPEECH_M3_BUI_032},
    {(Action_TranslatedText)0x060001d9, SFX_CHR_SPEECH_M3_BUI_033},
    {(Action_TranslatedText)0x060001da, SFX_CHR_SPEECH_M3_BUI_034},
    {(Action_TranslatedText)0x060001db, SFX_CHR_SPEECH_M3_BUI_035},
    {(Action_TranslatedText)0x060001dc, SFX_CHR_SPEECH_M3_BUI_036},
    {(Action_TranslatedText)0x060001dd, SFX_CHR_SPEECH_M3_BUI_037A},
    {(Action_TranslatedText)0x060001de, SFX_CHR_SPEECH_M3_BUI_037B},
    {(Action_TranslatedText)0x060001df, SFX_CHR_SPEECH_M3_BUI_038},
    {(Action_TranslatedText)0x060001e0, SFX_CHR_SPEECH_M3_BUI_039},
    {(Action_TranslatedText)0x060001e1, SFX_CHR_SPEECH_M3_BUI_040},
    {(Action_TranslatedText)0x060001e2, SFX_CHR_SPEECH_M3_BUI_041},
    {(Action_TranslatedText)0x060001e3, SFX_CHR_SPEECH_M3_BUI_042},
    {(Action_TranslatedText)0x060001e6, SFX_CHR_SPEECH_CAS_IG_M_13},
    {(Action_TranslatedText)0x060001e7, SFX_CHR_SPEECH_CAS_IG_M_17_ALT},
    {(Action_TranslatedText)0x060001e8, SFX_CHR_SPEECH_CAS_IG_M_19},
    {(Action_TranslatedText)0x060001e9, SFX_CHR_SPEECH_CAS_IG_M_22},
    {(Action_TranslatedText)0x060001ea, SFX_CHR_SPEECH_EST_IG_M_001},
    {(Action_TranslatedText)0x060001eb, SFX_CHR_SPEECH_EST_IG_M_002},
    {(Action_TranslatedText)0x060001ee, SFX_CHR_SPEECH_TWI_IG_M_002},
    {(Action_TranslatedText)0x060001ef, SFX_CHR_SPEECH_TWI_IG_M_003},
    {(Action_TranslatedText)0x060001f0, SFX_CHR_SPEECH_TWI_IG_M_004},
    {(Action_TranslatedText)0x060001f1, SFX_CHR_SPEECH_TWI_IG_M_005},
    {(Action_TranslatedText)0x060001f2, SFX_CHR_SPEECH_TWI_IG_M_006},
    {(Action_TranslatedText)0x060001f3, SFX_CHR_SPEECH_TWI_IG_M_007},
    {(Action_TranslatedText)0x060001f4, SFX_CHR_SPEECH_TWI_IG_M_008},
    {(Action_TranslatedText)0x060001f5, SFX_CHR_SPEECH_EST_IG_MAY_017},
    {(Action_TranslatedText)0x060001f6, SFX_CHR_SPEECH_USB_PHX_56},
    {(Action_TranslatedText)0x060001f7, SFX_CHR_SPEECH_USB_PHX3_56},
    {(Action_TranslatedText)0x060001f8, SFX_CHR_SPEECH_GEN_IG_Q_001},
    {(Action_TranslatedText)0x060001f9, SFX_CHR_SPEECH_GEN_IG_Q_005},
    {(Action_TranslatedText)0x060001fa, SFX_CHR_SPEECH_NUK_IG_Q_3},
    {(Action_TranslatedText)0x060001fb, SFX_CHR_SPEECH_NUK_IG_Q_6},
    {(Action_TranslatedText)0x060001fc, SFX_CHR_SPEECH_TWI_IG_Q_001},
    {(Action_TranslatedText)0x060001fd, SFX_CHR_SPEECH_TWI_IG_Q_002},
    {(Action_TranslatedText)0x060001fe, SFX_CHR_SPEECH_TWI_IG_Q_003},
    {(Action_TranslatedText)0x060001ff, SFX_CHR_SPEECH_TWI_IG_Q_004},
    {(Action_TranslatedText)0x06000200, SFX_CHR_SPEECH_TWI_IG_Q_005},
    {(Action_TranslatedText)0x06000201, SFX_CHR_SPEECH_TWI_IG_Q_006},
    {(Action_TranslatedText)0x06000202, SFX_CHR_SPEECH_TWI_IG_Q_007},
    {(Action_TranslatedText)0x06000203, SFX_CHR_SPEECH_TWI_IG_Q_008},
    {(Action_TranslatedText)0x06000204, SFX_CHR_SPEECH_TWI_IG_Q_009},
    {(Action_TranslatedText)0x06000205, SFX_CHR_SPEECH_TWI_IG_Q_010},
    {(Action_TranslatedText)0x06000206, SFX_CHR_SPEECH_TWI_IG_Q_012},
    {(Action_TranslatedText)0x06000207, SFX_CHR_SPEECH_TWI_IG_Q_013},
    {(Action_TranslatedText)0x06000208, SFX_CHR_SPEECH_TWI_IG_Q_014},
    {(Action_TranslatedText)0x06000209, SFX_CHR_SPEECH_TWI_IG_Q_015},
    {(Action_TranslatedText)0x0600020a, SFX_CHR_SPEECH_TWI_IG_Q_016},
    {(Action_TranslatedText)0x0600020b, SFX_CHR_SPEECH_TWI_IG_Q_017},
    {(Action_TranslatedText)0x0600020c, SFX_CHR_SPEECH_NUK_IG_SNIP2_10B},
    {(Action_TranslatedText)0x0600020d, SFX_CHR_SPEECH_NUK_IG_SNIP2_11B},
    {(Action_TranslatedText)0x0600020f, SFX_CHR_SPEECH_NUK_IG_SNIP2_13B},
    {(Action_TranslatedText)0x06000210, SFX_CHR_SPEECH_NUK_IG_SNIP2_14B},
    {(Action_TranslatedText)0x06000211, SFX_CHR_SPEECH_NUK_IG_SNIP2_15B},
    {(Action_TranslatedText)0x06000212, SFX_CHR_SPEECH_NUK_IG_SNIP2_16B},
    {(Action_TranslatedText)0x06000213, SFX_CHR_SPEECH_NUK_IG_SNIP2_17B},
    {(Action_TranslatedText)0x06000214, SFX_CHR_SPEECH_NUK_IG_SNIP2_18B},
    {(Action_TranslatedText)0x06000215, SFX_CHR_SPEECH_NUK_IG_SNIP2_19B},
    {(Action_TranslatedText)0x06000216, SFX_CHR_SPEECH_NUK_IG_SNIP2_1B},
    {(Action_TranslatedText)0x06000217, SFX_CHR_SPEECH_NUK_IG_SNIP2_20B},
    {(Action_TranslatedText)0x06000218, SFX_CHR_SPEECH_NUK_IG_SNIP2_21B},
    {(Action_TranslatedText)0x06000219, SFX_CHR_SPEECH_NUK_IG_SNIP2_22B},
    {(Action_TranslatedText)0x0600021a, SFX_CHR_SPEECH_NUK_IG_SNIP2_23B},
    {(Action_TranslatedText)0x0600021b, SFX_CHR_SPEECH_NUK_IG_SNIP2_24B},
    {(Action_TranslatedText)0x0600021c, SFX_CHR_SPEECH_NUK_IG_SNIP2_25B},
    {(Action_TranslatedText)0x0600021d, SFX_CHR_SPEECH_NUK_IG_SNIP2_26B},
    {(Action_TranslatedText)0x0600021e, SFX_CHR_SPEECH_NUK_IG_SNIP2_27B},
    {(Action_TranslatedText)0x0600021f, SFX_CHR_SPEECH_NUK_IG_SNIP2_28B},
    {(Action_TranslatedText)0x06000220, SFX_CHR_SPEECH_NUK_IG_SNIP2_29B},
    {(Action_TranslatedText)0x06000221, SFX_CHR_SPEECH_NUK_IG_SNIP2_2B},
    {(Action_TranslatedText)0x06000222, SFX_CHR_SPEECH_NUK_IG_SNIP2_30B},
    {(Action_TranslatedText)0x06000223, SFX_CHR_SPEECH_NUK_IG_SNIP2_31B},
    {(Action_TranslatedText)0x06000224, SFX_CHR_SPEECH_NUK_IG_SNIP2_32B},
    {(Action_TranslatedText)0x06000225, SFX_CHR_SPEECH_NUK_IG_SNIP2_33B},
    {(Action_TranslatedText)0x06000226, SFX_CHR_SPEECH_NUK_IG_SNIP2_34B},
    {(Action_TranslatedText)0x06000227, SFX_CHR_SPEECH_NUK_IG_SNIP2_35B},
    {(Action_TranslatedText)0x06000228, SFX_CHR_SPEECH_NUK_IG_SNIP2_36B},
    {(Action_TranslatedText)0x06000229, SFX_CHR_SPEECH_NUK_IG_SNIP2_37B},
    {(Action_TranslatedText)0x0600022a, SFX_CHR_SPEECH_NUK_IG_SNIP2_38B},
    {(Action_TranslatedText)0x0600022b, SFX_CHR_SPEECH_NUK_IG_SNIP2_39B},
    {(Action_TranslatedText)0x0600022c, SFX_CHR_SPEECH_NUK_IG_SNIP2_3B},
    {(Action_TranslatedText)0x0600022d, SFX_CHR_SPEECH_NUK_IG_SNIP2_40B},
    {(Action_TranslatedText)0x0600022e, SFX_CHR_SPEECH_NUK_IG_SNIP2_41B},
    {(Action_TranslatedText)0x0600022f, SFX_CHR_SPEECH_NUK_IG_SNIP2_42B},
    {(Action_TranslatedText)0x06000230, SFX_CHR_SPEECH_NUK_IG_SNIP2_43B},
    {(Action_TranslatedText)0x06000231, SFX_CHR_SPEECH_NUK_IG_SNIP2_44B},
    {(Action_TranslatedText)0x06000232, SFX_CHR_SPEECH_NUK_IG_SNIP2_45B},
    {(Action_TranslatedText)0x06000233, SFX_CHR_SPEECH_NUK_IG_SNIP2_46B},
    {(Action_TranslatedText)0x06000234, SFX_CHR_SPEECH_NUK_IG_SNIP2_4B},
    {(Action_TranslatedText)0x06000235, SFX_CHR_SPEECH_NUK_IG_SNIP2_5B},
    {(Action_TranslatedText)0x06000236, SFX_CHR_SPEECH_NUK_IG_SNIP2_6B},
    {(Action_TranslatedText)0x06000237, SFX_CHR_SPEECH_NUK_IG_SNIP2_7B},
    {(Action_TranslatedText)0x06000238, SFX_CHR_SPEECH_NUK_IG_SNIP2_8B},
    {(Action_TranslatedText)0x06000239, SFX_CHR_SPEECH_NUK_IG_SNIP2_9B},
    {(Action_TranslatedText)0x06000269, SFX_CHR_SPEECH_USB_IG_KIK_4},
};
// The subtitles' frames per second of their sound (PS2: g_fSubtitleDurationModification)
// XBE_GLOBAL(0x0018cbd8, 0x4) readonly relocate
float SubtitleDurationModification = 0.92f;
extern uint32_t SubtitlesEnabled;

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
