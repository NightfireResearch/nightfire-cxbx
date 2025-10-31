#include "Sound.h"

#include "SFX.h"
#include "../util/DList.h"
#include "../math/math.h"

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

// DLISTINFO_tag object stored at 0x0029a128
#define DynamicSoundList (*(DLISTINFO_tag*)(0x0029a128))

// UNINJECTABLE - custom calling convention
DYNAMICSOUNDS* Sound_Play(Action_SFX sfxId, float volume, float radiusOuter, float radiusInner, undefined2 maybePitchBend, char is3d, undefined4 param_7, _VECTOR *position) {

    DYNAMICSOUNDS* snd = (DYNAMICSOUNDS *)DList_MoveFromFree2InUse(&DynamicSoundList);

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

#define Sound_FrameDelay U32_AT(0x0029a288)

// AUTOINJECT
DYNAMICSOUNDS* Sound_Play3D(Action_SFX param_1,_VECTOR *position,float volume,float radiusOuter,float radiusInner, undefined2 maybePitchBend,undefined4 param_7,int isLimitedRadius) {
    
    undefined1 tmpFrameDelay;
    DYNAMICSOUNDS *snd;
  
    tmpFrameDelay = (undefined1)Sound_FrameDelay;
    Sound_FrameDelay = 0;
    
    if ((param_1 & 0xfffff) == 0xffff)
        return NULL;

    if (isLimitedRadius == 0) {
        radiusInner = -1.0;
        radiusOuter = -1.0;
    }
    snd = Sound_Play(param_1,volume,radiusOuter,radiusInner,maybePitchBend,'\x01',param_7,position);
    if (snd != NULL) {
        snd->isLimitedRadius = (char)isLimitedRadius;
        snd->frameDelay = tmpFrameDelay;
    }
    
    return snd;
}