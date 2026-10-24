#ifndef SOUND_H
#define SOUND_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

#pragma pack(push, 1)

typedef struct DYNAMICSOUNDS {
    struct DYNAMICSOUNDS *prev;     // 0x00 - DynamicSoundList's links, an LLNODE_tag's
    struct DYNAMICSOUNDS *next;     // 0x04
    _VECTOR location;
    _VECTOR velocity;
    unsigned int limitedDistance;   // 0x20 - its radii were given (Sound_Play): SFXStart3D takes them
    unsigned int sfxId;
    undefined4 ref;                 // 0x28 - the caller's tag (Sound_Stop_Ref, Sound_Playing_Ref)
    float volume;
    float radiusInner;
    float radiusOuter;
    float alertness;
    short pitch;
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

LAYOUT_32(sizeof(DYNAMICSOUNDS) == 0x48, "Size of DYNAMICSOUNDS not correct");
static_assert(offsetof(DYNAMICSOUNDS, next) == offsetof(LLNODE_tag, next), "a sound's links are its list node's");
LAYOUT_32(offsetof(DYNAMICSOUNDS, playbackState) == 0x40, "Offset of playbackState not correct");

typedef struct SFXOutputDataEntry {
    uint index;
    float defaultRadiusInner;
    float defaultRadiusOuter;
    float alertnessRelated;
    float durationForSubtitles;
    char loopAlways;
    char tracked3d;
    char _pad_1[0x2];
} SFXOutputDataEntry;

static_assert(sizeof(SFXOutputDataEntry) == 0x18, "Size of SFXOutputDataEntry not correct");

#pragma pack(pop)

DYNAMICSOUNDS* Sound_Play3D(Action_SFX sfxId,_VECTOR *position,float volume,float radiusOuter,float radiusInner, undefined2 pitch,undefined4 ref,int isLimitedRadius);
DYNAMICSOUNDS* Sound_PlayExt(Action_SFX sfxId, float volume, undefined2 pitch, undefined4 ref);
void Sound_Stop(DYNAMICSOUNDS *handle);
extern float SubtitleDurationModification;
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
DYNAMICSOUNDS* Sound_Play(Action_SFX sfxId, _VECTOR *position, float volume, float radiusOuter, float radiusInner, short pitch, char is3d, undefined4 ref);
void Sound_ReqestPlaySfx(DYNAMICSOUNDS *snd);
void Sound_UpdateSounds(_VECTOR *listener);
bool Sound_IsFinnished(DYNAMICSOUNDS *handle);
float Sound_Alertness(_VECTOR *pos, float *level, DYNAMICSOUNDS **loudest);
void Sound_Stop_Ref(DYNAMICSOUNDS *handle, undefined4 ref);
bool Sound_Playing_Ref(DYNAMICSOUNDS *handle, undefined4 ref);
void Sound_LoadMapSounds(int *mapSoundBlock);
void __fastcall Sound_DoSubtitle(Action_SFX sound);

// Set by SFXLoadCorrectBank once the level's bank is loaded
extern uint32_t SoundInit;

extern SFXOutputDataEntry SFXOutputData[0x60d];

#pragma pack(push, 1)
// A sound with a subtitle, and its label; the label's top bit shows it even with subtitles off
typedef struct SND2LBL_tag {
    Action_TranslatedText label;
    Action_SFX sound;
} SND2LBL_tag;
#pragma pack(pop)
static_assert(sizeof(SND2LBL_tag) == 8, "SND2LBL_tag is 8 bytes");
#define SUBTITLE_ALWAYS 0x80000000u     // the label's top bit

// The sound ids' low 20 bits index SFXOutputData
#define SFX_ID_MASK 0xfffff

#define NUM_SND2LBL 0x1e4
extern SND2LBL_tag Snd2Lbl[NUM_SND2LBL];

#endif // SOUND_H
