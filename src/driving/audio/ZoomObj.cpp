#pragma fp_contract(off)

#include "ZoomObj.h"
#include "SoundManager.h"               // fgBanks, FramesSinceAudioUpdate

#include "../engine/UMemory.hpp"
#include "../../helpers.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// ASound, AOneShotSound's and ALimitedSound's Play, and AZoomObj (0x00127a70..0x00128350), ported from the
// listings. See ZoomObj.h.
// ---------------------------------------------------------------------------------------------------------------

// AVoice's destructor, out of line (Ghidra: FUN_0003f400).
#define AVoice_Destruct ((void (__fastcall *)(AVoice *, int))0x0003f400)
// Whether a voice is done: set, its views that the active views include have finished (Ghidra: FUN_000d36c0;
// the name is ours).
#define AVoice_IsFinished ((bool (__fastcall *)(AVoice *, int))0x000d36c0)

// The C runtime's `eh vector constructor iterator` and `eh vector destructor iterator`, given AVoice::View's
// constructor and destructor by address.
#define EhVectorConstructor ((void (__stdcall *)(void *array, uint32_t size, int count, uint32_t construct, uint32_t destruct))0x0013326e)
#define EhVectorDestructor ((void (__stdcall *)(void *array, uint32_t size, int count, uint32_t destruct))0x0013332e)

namespace {

constexpr uint32_t kASoundVtable = 0x001a2a7c;
constexpr uint32_t kViewConstruct = 0x00123820;     // AVoice::View::Construct
constexpr uint32_t kViewDestruct = 0x00123c50;      // AVoice::View::Destruct

constexpr float kFrameSeconds = 1.0f / 60.0f;
static_assert(std::bit_cast<uint32_t>(kFrameSeconds) == 0x3c888889, "1/60 as the original's .rdata has it");

constexpr int kZoneSteps = 10;
constexpr int kBreathBankSlot = 6;

// The mixes InTheZone moves, and where to
struct ZoneMix {
    const char *name;
    float volume;
};

const ZoneMix kZoneMixes[] = {
    { "Ambience", 0.0f },
    { "Asphalt", 0.0f },
    { "Blowout", 0.0f },
    { "Booster", 0.0f },
    { "Bullet Impact", 0.0f },
    { "Bullet Ricochet", 0.0f },
    { "Characters", 0.0f },
    { "Collisions", 0.15f },
    { "Explosions", 0.2f },
    { "ExplosionBass", 0.1f },
    { "Friendly", 0.1f },
    { "Gadgets", 0.0f },
    { "Gatling Gun", 0.0f },
    { "Giotto", 0.15f },
    { "Helicopter", 0.0f },
    { "Henchmen Fire", 0.15f },
    { "Horns", 0.1f },
    { "Landings", 0.25f },
    { "Machine Gun", 0.0f },
    { "Object Sounds", 0.0f },
    { "OffRoad", 0.0f },
    { "Opponents", 0.1f },
    { "Player Car", 0.15f },
    { "Player Grunts", 0.2f },
    { "Player Wind", 1.0f },
    { "POV Weapons", 1.0f },
    { "Power Ups", 0.0f },
    { "Rail", 0.0f },
    { "Scrapes", 0.0f },
    { "Shell Casings", 0.0f },
    { "Skids", 0.0f },
    { "Smackables", 0.0f },
    { "Tank Treads", 0.0f },
    { "Tracking System", 0.0f },
    { "Traffic Cars", 0.0f },
    { "Weapons Fire", 0.0f },
    { "Weapons Init", 1.0f },
    { "Weapons Reload", 0.0f },
    { "Weapons Transit", 0.0f },
    { "Wood", 0.0f },
    { "World Sounds", 0.15f },
    { "Music", 0.05f },
};
static_assert(std::bit_cast<uint32_t>(0.15f) == 0x3e19999a && std::bit_cast<uint32_t>(0.05f) == 0x3d4ccccd,
              "the zone volumes as the original pushes them");

// The sound's voice played for the listener: the sound's volume times its mix's, times the listener's.
void PlayVoice(ABaseSound *sound, AVoice *voice, ASoundPlayParams *params, float fxLevel) {
    float volume = float(sound->mix->GetVolume() * sound->volume * params->volume);
    voice->Play(params->listener->unknown5c, volume, sound->pitch * params->pitch, params->azimuth, params->unknown10,
                fxLevel);
}

}  // namespace

// =============================================================================================================
// ASound
// =============================================================================================================

// FUNC_AT(0x00127a70)
ASound* ASound::Construct(int bank, int patch, const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kASoundVtable;
    voice.Construct(mix, bank, patch);
    volume = 1.0f;
    pitch = 1.0f;
    return this;
}

// FUNC_AT(0x00127bf0)
ASound* ASound::Construct(const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kASoundVtable;
    voice.mix = mix;
    EhVectorConstructor(voice.views, sizeof(AVoice::View), 3, kViewConstruct, kViewDestruct);
    volume = 0.0f;
    pitch = 1.0f;
    return this;
}

// FUNC_AT(0x00127b70)
void ASound::Destruct() {
    vtable = kASoundVtable;
    EhVectorDestructor(voice.views, sizeof(AVoice::View), 3, kViewDestruct);
    ABaseSound::Destruct();
}

// FUNC_AT(0x00127c80)
ASound* ASound::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASound));
    return this;
}

// FUNC_AT(0x00127bd0)
const char* ASound::GetName() {
    return ABank::GetPatchName(voice.views[0].bank, voice.views[0].patch);
}

// FUNC_AT(0x00127b00)
void ASound::Play(ASoundPlayParams *params) {
    PlayVoice(this, &voice, params, 0.0f);
}

// =============================================================================================================
// AOneShotSound and ALimitedSound
// =============================================================================================================

// FUNC_AT(0x00127cb0)
void AOneShotSound::Play(ASoundPlayParams *params) {
    PlayVoice(this, &voice, params, fxLevel);
    if (!(timeLeft >= 0.0f))
        return;
    if (AVoice_IsFinished(&voice, 0)) {
        CallDelete(1);
        return;
    }
    if (timeLeft > 0.0f) {
        // compared before the store rounds it
        double left = timeLeft - FramesSinceAudioUpdate * double(kFrameSeconds);
        timeLeft = float(left);
        if (left <= 0.0)
            CallDelete(1);
    }
}

// FUNC_AT(0x00127da0)
void ALimitedSound::Play(ASoundPlayParams *params) {
    volume = float(1.0 / LimitedSoundCount);
    AOneShotSound::Play(params);
}

// =============================================================================================================
// AZoomObj
// =============================================================================================================

// FUNC_AT(0x00127dc0)
AZoomObj* AZoomObj::Construct(AVehicle *owner) {
    mix = AMix::Get("Weapons Init");
    voice = NULL;
    vehicle = owner;
    inZone = false;
    AVoice *breath = static_cast<AVoice *>(UMemory::FastAlloc(sizeof(AVoice), "AVoice"));
    if (breath != NULL) {
        ABank *bank = fgBanks[kBreathBankSlot];
        breath = breath->Construct(mix, bank->handle, bank->index.Lookup("SFX_breath"));
    }
    voice = breath;
    return this;
}

// FUNC_AT(0x00128330)
void AZoomObj::Destruct() {
    if (voice != NULL) {
        AVoice_Destruct(voice, 0);
        UMemory::FastFree(voice, sizeof(AVoice));
    }
}

// FUNC_AT(0x00127e50)
void AZoomObj::Play(ASoundPlayParams *params) {
    if (inZone) {
        float volume = float(mix->GetVolume() * params->volume);
        voice->Play(params->listener->unknown5c, volume, params->pitch, params->azimuth, 0.0f, 0.0f);
    } else {
        voice->Play(params->listener->unknown5c, 0.0f, params->pitch, params->azimuth, 0.0f, 0.0f);
    }
}

// FUNC_AT(0x00127ed0)
void AZoomObj::InTheZone(bool entering) {
    if (entering) {
        if (!inZone) {
            for (const ZoneMix &zone : kZoneMixes)
                AMix::Get(zone.name)->SetTransition(zone.volume, kZoneSteps);
        }
    } else if (inZone) {
        AMix::Reset(kZoneSteps);
    }
    inZone = entering;
}
