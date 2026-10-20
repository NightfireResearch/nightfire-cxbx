#ifndef SOUND_H
#define SOUND_H

#include "../actionhelpers.h"

#pragma pack(push, 1)

typedef struct DYNAMICSOUNDS {
    char _pad_1[0x4];
    DYNAMICSOUNDS *next;
    _VECTOR location;
    _VECTOR velocity;
    unsigned int limitedDistance;   // 0x20 - its radii were given (Sound_Play): SFXStart3D takes them
    unsigned int sfxId;
    undefined4 ref;                 // 0x28 - the caller's tag (Sound_Stop_Ref, Sound_Playing_Ref)
    float volume;
    float radiusInner;
    float radiusOuter;
    float alertness;
    short maybePitchBend;
    char is3d;
    char needsUpdate;
    char playbackState;             // 0x40 - 0 to start, 1 playing, 2 to stop, 3 free
    char age;                       // 0x41 - frames since Sound_Play
    char audible;                   // 0x42 - gives its subtitle when it starts
    char isLimitedRadius;
    char unknown44;
    undefined1 frameDelay;
    char _pad_6[0x2];
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

DYNAMICSOUNDS* Sound_Play3D(Action_SFX param_1,_VECTOR *position,float volume,float radiusOuter,float radiusInner, undefined2 maybePitchBend,undefined4 param_7,int isLimitedRadius);
DYNAMICSOUNDS* Sound_PlayExt(Action_SFX param_1, float volume, undefined2 maybePitchBend, undefined4 param_4);
void Sound_Stop(DYNAMICSOUNDS *handle);
bool Sound_SetPosition(DYNAMICSOUNDS *handle, _VECTOR *position);
bool Sound_SetVolume(DYNAMICSOUNDS *handle, float volume);
void Sound_SetAlertness(DYNAMICSOUNDS *handle, float alert);
void Sound_ModAlertness(DYNAMICSOUNDS *handle, float multiplier);
bool Sound_IsLooping(DYNAMICSOUNDS *handle);
void Sound_UpdateListeners(void);
void Sound_StopAllWithId(Action_SFX sfx);
void Sound_ZeroAlertness(void);
void HandleMapSoundAllocation(void);
void Sound_Init(void);
void Sound_Kill(void);
DYNAMICSOUNDS* _Sound_Play(Action_SFX sfxId, _VECTOR *position, float volume, float radiusOuter, float radiusInner, short pitch, char is3d, undefined4 ref);
void _Sound_ReqestPlaySfx(DYNAMICSOUNDS *snd);
// The register adaptors (Sound.cpp)
DYNAMICSOUNDS* Sound_Play(void);
void Sound_ReqestPlaySfx(void);
void Sound_UpdateSounds(_VECTOR *listener);
bool Sound_IsFinnished(DYNAMICSOUNDS *handle);
float Sound_Alertness(_VECTOR *pos, float *level, DYNAMICSOUNDS **loudest);
void Sound_Stop_Ref(DYNAMICSOUNDS *handle, undefined4 ref);
bool Sound_Playing_Ref(DYNAMICSOUNDS *handle, undefined4 ref);
void Sound_LoadMapSounds(int *mapSoundBlock);
void __fastcall Sound_DoSubtitle(Action_SFX sound);

// Set by SFXLoadCorrectBank once the level's bank is loaded
#define SoundInit U32_AT(0x0029a280)

#define SFXOutputData (*(SFXOutputDataEntry(*)[0x60d])0x00182b80)

#endif // SOUND_H