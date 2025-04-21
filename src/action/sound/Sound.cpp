#include "Sound.h"

#include "../math/math.h"

// AUTOGEN
uint Sound_Play3D(Action_SFX param_1,_VECTOR *position,float param_3,float param_4,float param_5, undefined2 param_6,undefined4 param_7,int param_8);
// AUTOGEN
bool SFXIsSFXPlaying(DYNAMICSOUNDS *param_1,int param_2);


// AUTOINJECT
bool Sound_SetPosition(DYNAMICSOUNDS *handle, _VECTOR *position) {

    if(handle == NULL || position == NULL)
        return false;

    Vec_Copy(position, &handle->location);
    handle->needsUpdate = true;

    if((handle->sfxId < 0x60d) && SFXOutputData[handle->sfxId].loopAlways)
        return handle->playbackState == 1;

    return SFXIsSFXPlaying(handle, -1);
}

// AUTOINJECT
void Sound_Stop(DYNAMICSOUNDS *handle) {
    if(handle == NULL)
        return;

    handle->playbackState = 2;
}
