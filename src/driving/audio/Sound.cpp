#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif
#pragma fp_contract(off)

#include "Sound.h"
#include "../Vtables.h"   // our copies of the game's vtables
#include "Bank.h"
#include "SoundManager.h"
#include "../engine/CoreFoundation.h"   // ThrowLengthError
#include "../engine/UMemory.hpp"
#include "../../helpers.h"
#include "../platform/CRuntime.h"

#include <string.h>

// XBE_GLOBAL(0x00243b30, 0x4)
int32_t LimitedSoundCount;

// ---------------------------------------------------------------------------------------------------------------
// ABaseSound and the sound kinds whose code sits beside it: see Sound.h. Each class's vtable is the game's,
// written by its constructor and destructor as the original writes it.
// ---------------------------------------------------------------------------------------------------------------

// The list code every std::list of pointers shares: _Buynode(next, prev, value)

namespace {

const uint32_t kABaseSoundVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_ABaseSound));
const uint32_t kAOneShotSoundVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_0018c840));
const uint32_t kALimitedSoundVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_0018c8c8));
const uint32_t kAMenuSoundPrivVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AMenuSoundPriv));
const uint32_t kABasicVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_ABasic));

constexpr float kDefaultMinDistance = 3.0f;
constexpr float kDefaultMaxDistance = 350.0f;

const uint32_t kListMaxSize = 0x3fffffff;

// AVoice's destructor as the compiler inlines it: its views', last first (the C runtime's vector destructor
// iterator in the original).
void DestroyVoiceViews(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
}

}  // namespace

// =============================================================================================================
// ABaseSound
// =============================================================================================================

// FUNC_AT(0x0001bfa0)
ABaseSound* ABaseSound::Construct(const char *mixName, int view) {
    vtable = kABaseSoundVtable;
    mix = AMix::Add(mixName);
    views = 0;
    position.x = position.y = position.z = 0.0f;
    velocity.x = velocity.y = velocity.z = 0.0f;
    forward.x = forward.y = 0.0f;
    forward.z = 1.0f;
    right.x = right.y = 0.0f;
    right.z = 1.0f;
    up.x = up.y = 0.0f;
    up.z = 1.0f;
    minDistance = kDefaultMinDistance;
    maxDistance = kDefaultMaxDistance;
    maxDistanceSq = kDefaultMaxDistance * kDefaultMaxDistance;
    falloff = 0.0f;
    volume = 1.0f;
    pitch = 1.0f;
    for (int i = 0; i < kSoundViewCount; i++)
        lastVolumes[i] = 1.0f;
    paused = false;
    fade = 0.0f;
    fadeStep = 0;
    fadeSteps = 0;
    fading = false;
    if (view == kSoundViewsActive)
        views |= fgActiveViews;
    else
        views |= 1u << view;
    return this;
}

// FUNC_AT(0x0001c080)
bool Generic_FuncReturnsFalse() {
    return false;
}

// FUNC_AT(0x0001c090)
ABaseSound* ABaseSound::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ABaseSound));
    return this;
}

// FUNC_AT(0x0011c6f0)
void ABaseSound::Destruct() {
    vtable = kABaseSoundVtable;
    mix->Remove();
}

// FUNC_AT(0x0011c710)
char* ABaseSound::GetName() {
    return name;
}

// FUNC_AT(0x0011c720)
void ABaseSound::StartFade(int steps) {
    fadeStep = 0;
    fadeSteps = steps;
    fade = 0.0f;
    fading = true;
}

// FUNC_AT(0x0011c750)
float ABaseSound::GetFade() {
    if (fading) {
        if (fadeStep < fadeSteps) {
            fadeStep++;
            fade = float(double(fadeStep) / fadeSteps);
            return fade;
        }
        fadeStep = 0;
        fading = false;
    }
    fade = 1.0f;
    return fade;
}

// FUNC_AT(0x000d3690)
void ABaseSound::SetMix(const char *mixName) {
    mix->Remove();
    mix = AMix::Add(mixName);
}

// FUNC_AT(0x0011c830)
void ABaseSound::OperatorDelete(void *block, unsigned int size) {
    ABaseSound *sound = static_cast<ABaseSound *>(block);
    fgSoundList->Remove(sound);
    UMemory::FastFree(block, size);
}

// FUNC_AT(0x0011c910)
void* ABaseSound::OperatorNew(unsigned int size, const char *name) {
    ABaseSound *sound = static_cast<ABaseSound *>(UMemory::FastAlloc(size, name));
    ASoundList *list = fgSoundList;
    PointerListNode *end = list->head;
    void *value = sound;
    PointerListNode *node = list->BuyNode(end, end->prev, &value);
    list->IncreaseSize(1);
    end->prev = node;
    node->prev->next = node;
    strcpy(sound->name, name + 1);     // the label without its first character
    return sound;
}

// FUNC_AT(0x0011dab0)
double ABaseSound::GetMixedVolume() {
    return mix->GetVolume() * volume;
}

// =============================================================================================================
// The sound list
// =============================================================================================================

// FUNC_AT(0x0011c860)
void ASoundList::IncreaseSize(uint32_t count) {
    if (kListMaxSize - size < count) {
        AUDIO_SOUND_UNTESTED("list<T>::_Incsize (too long)");
        ThrowLengthError("list<T> too long");
    }
    size += count;
}

// =============================================================================================================
// AOneShotSound, ALimitedSound
// =============================================================================================================

// FUNC_AT(0x000476f0)
AOneShotSound* AOneShotSound::Construct(int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                                        float volume, const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kAOneShotSoundVtable;
    voice.Construct(mix, bank, patch);
    this->volume = volume;
    fxLevel = 0.0f;
    timeLeft = 0.0f;
    voice.PlayOnce();
    this->position = *position;
    this->velocity = *velocity;
    return this;
}

// FUNC_AT(0x000477d0)
AOneShotSound* AOneShotSound::ConstructAt(int bank, int patch, const Coord3 *position, float volume,
                                          const char *mixName, bool loop) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kAOneShotSoundVtable;
    voice.Construct(mix, bank, patch);
    fxLevel = 0.0f;
    timeLeft = 0.0f;
    this->volume = volume;
    if (!loop)
        voice.PlayOnce();
    this->position = *position;
    return this;
}

// FUNC_AT(0x000478a0)
void AOneShotSound::Destruct() {
    vtable = kAOneShotSoundVtable;
    DestroyVoiceViews(&voice);
    ABaseSound::Destruct();
}

// FUNC_AT(0x00047900)
AOneShotSound* AOneShotSound::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(AOneShotSound));
    return this;
}

// FUNC_AT(0x0004de20)
ALimitedSound* ALimitedSound::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ALimitedSound));
    return this;
}

// FUNC_AT(0x0004de50)
void ALimitedSound::Destruct() {
    vtable = kALimitedSoundVtable;
    LimitedSoundCount--;
    AOneShotSound::Destruct();
}

// =============================================================================================================
// AMenuSoundPriv, AMenuSound
// =============================================================================================================

// FUNC_AT(0x0011de40)
AMenuSoundPriv* AMenuSoundPriv::Construct(int bank, int patch, const char *mixName, int view) {
    ABaseSound::Construct(mixName, view);
    vtable = kAMenuSoundPrivVtable;
    voice.Construct(mix, bank, patch);
    maxDistance = 0.0f;
    maxDistanceSq = 0.0f;
    volume = 1.0f;
    for (int i = 0; i < 3; i++)
        voice.views[i].loop = 0;
    return this;
}

// FUNC_AT(0x0011dee0)
void AMenuSoundPriv::Play(ASoundPlayParams *params) {
    float level = float(mix->GetVolume() * volume * params->volume);
    voice.Play(params->listener->view, level, params->pitch, params->azimuth, 0.0f, 0.0f);
    if (voice.IsFinished())
        CallDelete(1);
}

// FUNC_AT(0x0011df50)
void AMenuSound::Trigger(int bank, int patch, const char *mixName, int view) {
    AMenuSoundPriv *sound =
        static_cast<AMenuSoundPriv *>(ABaseSound::OperatorNew(sizeof(AMenuSoundPriv), "AMenuSoundPriv"));
    if (sound != NULL) {
        bool shared = (fgActiveViews & (1u << kSoundViewShared)) != 0;
        sound->Construct(bank, patch, mixName, shared ? kSoundViewShared : view);
    }
}

// FUNC_AT(0x0011dfd0)
void AMenuSound::Trigger(const char *bankName, const char *patchName, const char *mixName, int view) {
    ABank *bank = ABank::Get(bankName);
    int patch = bank->index.Lookup(patchName);
    if (patch > -1)
        Trigger(bank->handle, patch, mixName, view);
}

// The patch is looked up twice, as the original does.
// FUNC_AT(0x0011e010)
void AMenuSound::Trigger(int bankNumber, const char *patchName, const char *mixName, int view) {
    ABank *bank = fgBanks[bankNumber];
    if (bank->index.Lookup(patchName) > -1) {
        int handle = bank->handle;
        Trigger(handle, bank->index.Lookup(patchName), mixName, view);
    }
}

// FUNC_AT(0x0011e060)
AMenuSoundPriv* AMenuSoundPriv::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(AMenuSoundPriv));
    return this;
}

// FUNC_AT(0x0011e090)
void AMenuSoundPriv::Destruct() {
    vtable = kAMenuSoundPrivVtable;
    DestroyVoiceViews(&voice);
    ABaseSound::Destruct();
}

// =============================================================================================================
// ABasic
// =============================================================================================================

// FUNC_AT(0x0012fa40)
ABasic* ABasic::Construct(const char *name, const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kABasicVtable;
    voice = NULL;
    Crt::sprintf(patchName, "%s", name);
    return this;
}

// FUNC_AT(0x0012fa90)
void ABasic::Play(ASoundPlayParams *params) {
    if (voice == NULL) {
        ABank *bank = fgBanks[0];
        int patch = bank->index.Lookup(patchName);
        AVoice *made = static_cast<AVoice *>(UMemory::FastAlloc(sizeof(AVoice), "AVoice"));
        voice = made != NULL ? made->Construct(mix, bank->handle, patch) : NULL;
    }
    double level = mix->GetVolume() * volume;
    voice->Play(params->listener->view, float(level * params->volume), pitch * params->pitch, params->azimuth,
                0.0f, 0.0f);
}

// FUNC_AT(0x0012fb80)
void ABasic::Destruct() {
    vtable = kABasicVtable;
    if (voice != NULL) {
        DestroyVoiceViews(voice);
        UMemory::FastFree(voice, sizeof(AVoice));
        voice = NULL;
    }
    ABaseSound::Destruct();
}

// FUNC_AT(0x0012fc00)
ABasic* ABasic::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ABasic));
    return this;
}

// FUNC_AT(0x0012fc30)
void ABasic::Stop(ASoundPlayParams *params) {
    if (voice != NULL) {
        voice->Destruct();
        UMemory::FastFree(voice, sizeof(AVoice));
    }
    voice = NULL;
}
