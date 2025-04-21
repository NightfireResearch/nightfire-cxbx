#ifndef SOUND_H
#define SOUND_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct DYNAMICSOUNDS {
    char _pad_1[0x4];
    DYNAMICSOUNDS *next;
    _VECTOR location;
    _VECTOR velocity;
    unsigned int limitedDistance;
    unsigned int sfxId;
    char _pad_2[0x4];
    float volume;
    float radiusInner;
    float radiusOuter;
    float alertness;
    char _pad_3[0x2];
    char is3d;
    char needsUpdate;
    char playbackState;
    char _pad_4[0x7];
} DYNAMICSOUNDS;

static_assert(sizeof(DYNAMICSOUNDS) == 0x48, "Size of DYNAMICSOUNDS not correct");
static_assert(offsetof(DYNAMICSOUNDS, playbackState) == 0x40, "Offset of playbackState not correct");

typedef struct SFXOutputDataEntry {
    uint index;
    float defaultRadiusInner;
    float defaultRadiusOuter;
    float alertnessRelated;
    float durationForSubtitles;
    char loopAlways;
    char maybeTracked3d;
    char _pad_1[0x2];
} SFXOutputDataEntry;

static_assert(sizeof(SFXOutputDataEntry) == 0x18, "Size of SFXOutputDataEntry not correct");

#pragma pack(pop)

uint Sound_Play3D(Action_SFX param_1,_VECTOR *position,float param_3,float param_4,float param_5, undefined2 param_6,undefined4 param_7,int param_8);
void Sound_Stop(DYNAMICSOUNDS *handle);
bool Sound_SetPosition(DYNAMICSOUNDS *handle, _VECTOR *position);
bool Sound_SetVolume(DYNAMICSOUNDS *handle, float volume);
void Sound_SetAlertness(DYNAMICSOUNDS *handle, float alert);
void Sound_ModAlertness(DYNAMICSOUNDS *handle, float multiplier);

#define SFXOutputData (*(SFXOutputDataEntry(*)[0x60d])0x00182b80)

#endif // SOUND_H