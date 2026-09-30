#include "Sound.h"

#include "SFX.h"
#include "../util/DList.h"
#include "../math/math.h"

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

// AUTOGEN
void Sound_UpdateListeners(void);

// AUTOGEN
void __stdcall SFXSuspendFileAccess(void);

// AUTOGEN
void __stdcall SFXUnSuspendFileAccess(void);

// UNINJECTABLE - custom calling convention
DYNAMICSOUNDS* Sound_Play(Action_SFX sfxId, float volume, float radiusOuter, float radiusInner, undefined2 maybePitchBend, char is3d, undefined4 param_7, _VECTOR *position) {

    DYNAMICSOUNDS* snd = (DYNAMICSOUNDS *)DList_MoveFromFree2InUse(DynamicSoundList);

    if (snd == NULL)
        return NULL;

    bool limitedDistance = false;

    if (radiusInner >= 0.0f)  {
        // If valid (>= 0), use the provided radius
        limitedDistance = true;
    } else if (sfxId < ARRAY_SIZE(SFXOutputData)) {
        // Otherwise, if the SFX has default radius data, use that
        radiusInner = SFXOutputData[sfxId].defaultRadiusInner;
    } else {
        // Otherwise, use a sane default
        radiusInner = 10.0f;
    }

    if (radiusOuter >= 0.0f)  {
        // If valid (>= 0), use the provided radius
        limitedDistance = true;
    } else if (sfxId < ARRAY_SIZE(SFXOutputData)) {
        // Otherwise, if the SFX has default radius data, use that
        radiusOuter = SFXOutputData[sfxId].defaultRadiusOuter;
    } else {
        // Otherwise, use a sane default
        radiusOuter = 20.0f;
    }

    snd->limitedDistance = limitedDistance;
    snd->radiusOuter = radiusOuter;
    snd->radiusInner = radiusInner;
    snd->volume = volume;
    snd->sfxId = sfxId;
    snd->playbackState = 0;
    snd->is3d = is3d;
    snd->maybePitchBend = maybePitchBend;
    snd->needsUpdate = true;

    snd->unknown0 = 0;

    // Look up the alertness value for this SFX
    if (sfxId < ARRAY_SIZE(SFXOutputData)) {
        snd->alertness = SFXOutputData[sfxId].alertnessRelated;
    } else {
        snd->alertness = 0.0f;
    }
                                
    snd->unknown1 = 0;  
    snd->isLimitedRadius = 0;
    snd->unknown2 = 0;
    snd->unknownParam = param_7;  
    
    Vec_Copy(position, &snd->location);
    Vec_Copy(&CONST_ZERO_VECTOR, &snd->velocity);
    
    return snd;
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
    DYNAMICSOUNDS *snd = Sound_Play(param_1,volume,radiusOuter,radiusInner,maybePitchBend,'\x01',param_7,position);
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

    DYNAMICSOUNDS *snd = Sound_Play(param_1,volume,-1.0,-1.0,maybePitchBend,'\0',param_4,&CONST_ZERO_VECTOR);
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

// Still the game's: all three are written by Sound_LoadMapSounds (not reimplemented).
#define GMapSounds (*(MapSound(*)[MAX_MAP_SOUNDS])0x00299ae8)
#define GMapSoundActive (*(char(*)[MAX_MAP_SOUNDS])0x0029a14c)
#define GNumMapSounds (*(int*)0x0029a284)

// Written by Sound_UpdateListeners (not reimplemented), once per frame.
#define GlobalListenerPos (*(_VECTOR*)0x0029a140)

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
        float distSq = Vec_SqDist3D(&GlobalListenerPos, &pos);

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
