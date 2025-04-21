#include "Sound.h"

#include "SFX.h"
#include "../math/math.h"

// AUTOGEN
uint Sound_Play3D(Action_SFX param_1,_VECTOR *position,float param_3,float param_4,float param_5, undefined2 param_6,undefined4 param_7,int param_8);


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