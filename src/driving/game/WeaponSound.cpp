#pragma fp_contract(off)

#include "WeaponSound.h"

#include <string.h>

#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../audio/SoundManager.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// ASceneObj, AGun, ATurret, ASentry, ASmackable, ATargeting and AWorldSound's Play: see WeaponSound.h.
// ---------------------------------------------------------------------------------------------------------------

// AOneShotSound's constructors: a patch of a bank at a place (with a velocity; or without one, looped or not)
#define AOneShotSound_Construct ((AOneShotSound *(__fastcall *)(void *, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity, float volume, const char *mix))0x000476f0)
// AVoice's destructor where it is not inlined (Ghidra: FUN_0003f400)
#define AVoice_Destruct ((void (__fastcall *)(AVoice *, int))0x0003f400)
// Whether the voice is done with (Ghidra: FUN_000d36c0)
#define AVoice_IsFinished ((bool (__fastcall *)(AVoice *, int))0x000d36c0)

// The audio frame's clock (name ours)
#define AudioClock FLOAT_AT(0x00243a6c)

// XBE_GLOBAL(0x00243b68, 4)
int32_t NextHardCollision;

namespace {

const uint32_t kASceneObjVtable = 0x001a2f80;
const uint32_t kASentryVtable = 0x001a2f08;
const uint32_t kASmackableVtable = 0x001a2f38;
const uint32_t kAWorldSoundVtable = 0x00193ae8;

// The banks these sounds take their patches from (names ours)
enum {
    kTurretBankSlot = 0,
    kWeaponBankSlot = 4,
};

const uint32_t kWholeString = 0xffffffff;      // npos

// AVoice's destructor as the compiler inlines it: its views', last first (the C runtime's vector destructor
// iterator in the original).
void DestroyVoiceViews(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
}

// A voice from the pools, destroyed inline.
void DeleteVoice(AVoice *voice) {
    DestroyVoiceViews(voice);
    UMemory::FastFree(voice, sizeof(AVoice));
}

// A voice from the pools on the bank's patch `name` (NULL if the pools have none).
AVoice* NewVoice(AMix *mix, ABank *bank, const char *name) {
    void *memory = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    if (memory == NULL)
        return NULL;
    return static_cast<AVoice *>(memory)->Construct(mix, bank->handle, bank->index.Lookup(name));
}

// A voice of the patch id (bank << 7 | patch).
AVoice* NewVoice(AMix *mix, int id) {
    void *memory = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    if (memory == NULL)
        return NULL;
    return static_cast<AVoice *>(memory)->Construct(mix, id >> 7, id & 0x7f);
}

// Each view plays once.
void ClearLoops(AVoice *voice) {
    for (int i = 0; i < 3; i++)
        voice->views[i].loop = 0;
}

// A turret motor's voice played at its mix's volume times `scale`, and freed once it is done.
void PlayMotor(AVoice *&voice, AMix *mix, double scale, ASoundPlayParams *params) {
    if (voice == NULL)
        return;
    voice->Play(params->listener->view, float(mix->GetVolume() * scale * params->volume), params->pitch,
                params->azimuth, 0.0f, 0.0f);
    if (AVoice_IsFinished(voice, 0)) {
        DeleteVoice(voice);
        voice = NULL;
    }
}

}  // namespace

// =============================================================================================================
// ASceneObj
// =============================================================================================================

// FUNC_AT(0x0012e190)
ASceneObj* ASceneObj::Construct(const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    servo = 0;
    firing = 0;
    turningSideways = 0;
    turningUpDown = 0;
    aiming = 0;
    vtable = kASceneObjVtable;
    gunLoops = 1;
    return this;
}

// FUNC_AT(0x0012e1e0)
void ASceneObj::Destruct() {
    vtable = kASceneObjVtable;
    ABaseSound::Destruct();
}

// FUNC_AT(0x0012e1f0)
ASceneObj* ASceneObj::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASceneObj));
    return this;
}

// FUNC_AT(0x00037a10)
void ASceneObj::PlayCollision(float impact, const Coord3 *position, const Coord3 *velocity, int surface) {
}

// =============================================================================================================
// AGun
// =============================================================================================================

// FUNC_AT(0x00129550)
AGun* AGun::Construct(ASceneObj *owner, const char *mixName) {
    this->owner = owner;
    bank = fgBanks[kWeaponBankSlot];
    mix = AMix::Get(mixName);
    fireTimer = 5.0f;
    sustain = 0.0f;
    fireInterval = 6.0f;
    unknown20 = 0.0f;
    firePatch.ConstructText("SFX_HeliMachGun");
    shellPatch.ConstructText("SFX_ShellCasing");
    this->mixName.ConstructText(mixName);
    shells = false;
    voice = NewVoice(mix, bank, firePatch.CStr());
    return this;
}

// FUNC_AT(0x00129470)
void AGun::Destruct() {
    if (voice != NULL)
        DeleteVoice(voice);
    mixName.Destruct();
    shellPatch.Destruct();
    firePatch.Destruct();
}

// FUNC_AT(0x001296b0)
void AGun::SetFirePatch(GameStd::String patch) {
    firePatch.AssignSub(&patch, 0, kWholeString);
    if (voice != NULL)
        DeleteVoice(voice);
    voice = NewVoice(mix, bank, firePatch.CStr());
    patch.Destruct();
}

// FUNC_AT(0x00129250)
void AGun::Play(uint8_t firing, ASoundPlayParams *params, float frames) {
    if (firing == 1) {
        // the sum is compared as the x87 has it, before its store rounds it
        double timer = double(frames) + fireTimer;
        fireTimer = float(timer);
        if (timer >= fireInterval) {
            if (owner->CallIsTracked() || !owner->gunLoops) {
                void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                AOneShotSound *shot = NULL;
                if (memory != NULL)
                    shot = AOneShotSound_Construct(memory, 0, bank->handle, bank->index.Lookup(firePatch.CStr()),
                                                   &owner->position, &owner->velocity, 1.0f, mixName.CStr());
                shot->minDistance = 30.0f;
                shot->maxDistance = 500.0f;
                shot->maxDistanceSq = 250000.0f;
            }
            if (shells && rrandom() < 0.1) {
                ASceneObj *source = owner;
                void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
                if (memory != NULL)
                    AOneShotSound_Construct(memory, 0, bank->handle, bank->index.Lookup(shellPatch.CStr()),
                                            &source->position, &source->velocity, 1.0f, "Shell Casings");
            }
            fireTimer -= fireInterval;
        }
        sustain = 10.0f;
    } else {
        fireTimer = fireInterval;
    }

    if (sustain > 0.0f && owner->gunLoops) {
        sustain -= frames;
        voice->Play(params->listener->view, float(mix->GetVolume() * params->volume), params->pitch,
                    params->azimuth, 0.0f, 1.0f);
    } else {
        voice->Play(params->listener->view, 0.0f, params->pitch, params->azimuth, 0.0f, 0.0f);
    }
}

// =============================================================================================================
// ATurret
// =============================================================================================================

// FUNC_AT(0x0012e220)
ATurret* ATurret::Construct() {
    mix = AMix::Get("Weapons Init");
    servoVoice = NULL;
    sidewaysVoice = NULL;
    upDownVoice = NULL;
    servo = false;
    turningSideways = false;
    turningUpDown = false;
    return this;
}

// FUNC_AT(0x0012e250)
void ATurret::Destruct() {
    if (sidewaysVoice != NULL) {
        AVoice_Destruct(sidewaysVoice, 0);
        UMemory::FastFree(sidewaysVoice, sizeof(AVoice));
    }
    if (upDownVoice != NULL) {
        AVoice_Destruct(upDownVoice, 0);
        UMemory::FastFree(upDownVoice, sizeof(AVoice));
    }
    if (servoVoice != NULL) {
        AVoice_Destruct(servoVoice, 0);
        UMemory::FastFree(servoVoice, sizeof(AVoice));
    }
}

// FUNC_AT(0x0012e2b0)
void ATurret::Play(bool turningSideways, bool turningUpDown, bool servo, ASoundPlayParams *params) {
    ABank *bank = fgBanks[kTurretBankSlot];
    if (turningSideways) {
        if (!this->turningSideways) {
            if (sidewaysVoice != NULL)
                DeleteVoice(sidewaysVoice);
            sidewaysVoice = NewVoice(mix, bank, "SFX_TurretLR");
            this->turningSideways = true;
        }
    } else if (this->turningSideways) {
        if (sidewaysVoice != NULL)
            DeleteVoice(sidewaysVoice);
        sidewaysVoice = NewVoice(mix, bank, "SFX_TurretLRStop");
        ClearLoops(sidewaysVoice);
        this->turningSideways = false;
    }
    PlayMotor(sidewaysVoice, mix, 0.5, params);

    if (turningUpDown) {
        if (!this->turningUpDown) {
            if (upDownVoice != NULL)
                DeleteVoice(upDownVoice);
            upDownVoice = NewVoice(mix, bank, "SFX_TurretUD");
            this->turningUpDown = true;
        }
    } else if (this->turningUpDown) {
        if (upDownVoice != NULL)
            DeleteVoice(upDownVoice);
        upDownVoice = NewVoice(mix, bank, "SFX_TurretUDStop");
        ClearLoops(upDownVoice);
        this->turningUpDown = false;
    }
    PlayMotor(upDownVoice, mix, 0.5, params);

    bank = fgBanks[kWeaponBankSlot];
    if (servo) {
        if (!this->servo) {
            AMenuSound::Trigger(bank->handle, bank->index.Lookup("SFX_Kachung"), "Weapons Init", kSoundViewShared);
            if (servoVoice != NULL)
                DeleteVoice(servoVoice);
            servoVoice = NewVoice(mix, bank, "SFX_servo");
            this->servo = true;
        }
    } else if (this->servo) {
        if (servoVoice != NULL)
            DeleteVoice(servoVoice);
        servoVoice = NewVoice(mix, bank, "SFX_servodown");
        ClearLoops(servoVoice);
        this->servo = false;
    }
    PlayMotor(servoVoice, mix, 1.0, params);
}

// =============================================================================================================
// ASentry
// =============================================================================================================

// FUNC_AT(0x0012d7f0)
ASentry* ASentry::Construct() {
    ASceneObj::Construct("World Sounds");
    vtable = kASentryVtable;
    turret.Construct();
    gun.Construct(this, "World Sounds");
    GameStd::String patch;
    patch.ConstructText("SFX_SALFire");
    gun.SetFirePatch(patch);
    gunLoops = 0;
    return this;
}

// FUNC_AT(0x0012d750)
void ASentry::Destruct() {
    vtable = kASentryVtable;
    gun.Destruct();
    turret.Destruct();
    ASceneObj::Destruct();
}

// FUNC_AT(0x0012d7c0)
ASentry* ASentry::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASentry));
    return this;
}

// FUNC_AT(0x0012d6f0)
void ASentry::Play(ASoundPlayParams *params) {
    turret.Play(turningSideways, turningUpDown, servo, params);
    gun.Play(firing, params, float(FramesSinceAudioUpdate));
}

// =============================================================================================================
// ASmackable
// =============================================================================================================

// FUNC_AT(0x0012d8b0)
ASmackable* ASmackable::Construct(int collisionId0, int collisionId1, int collisionId2, int rollId,
                                  const char *mixName) {
    ASceneObj::Construct(mixName);
    vtable = kASmackableVtable;
    collisionVoice = NULL;
    rollVoice = NULL;
    rollFrames = 0;
    rollVolume = 0.0f;
    detached = false;
    collisionIds[0] = collisionId0;
    collisionIds[1] = collisionId1;
    collisionIds[2] = collisionId2;
    if (rollId > -1) {
        rollVoice = NewVoice(mix, rollId);
        minDistance = 5.0f;
    }
    maxDistance = 75.0f;
    maxDistanceSq = 5625.0f;
    return this;
}

// FUNC_AT(0x0012d9a0)
void ASmackable::PlayLanding(float impact, const Coord3 *position, const Coord3 *velocity) {
    if (rollFrames < 10)
        rollFrames += FramesSinceAudioUpdate;
}

// FUNC_AT(0x0012d9c0)
void ASmackable::PlayCollision(float impact, const Coord3 *position, const Coord3 *velocity, int surface) {
    int which;
    if (impact <= 0.75f) {
        which = 0;
    } else {
        which = NextHardCollision + 1;
        NextHardCollision = NextHardCollision == 0;
    }
    int id = collisionIds[which];
    if (id > -1 && collisionVoice == NULL) {
        collisionVoice = NewVoice(mix, id);
        ClearLoops(collisionVoice);
    }
}

// FUNC_AT(0x0012da80)
bool ASmackable::IsStillActive() {
    return collisionVoice != NULL;
}

// FUNC_AT(0x0012da90)
void ASmackable::Play(ASoundPlayParams *params) {
    if (rollVoice != NULL) {
        int frames = FramesSinceAudioUpdate;
        float step = frames * 0.04f;
        // the target stays on the x87 stack, unrounded
        double target = 0.0;
        if (rollFrames > 0) {
            rollFrames -= frames;
            if (rollFrames < 0)
                rollFrames = 0;
            if (rollFrames > 2) {
                float speed = VU0_v3length(&velocity);
                target = speed < 10.0f ? speed * double(0.1f) : 1.0;
            }
        }
        double difference = target - rollVolume;
        double eased;
        if (difference <= -step)
            eased = rollVolume - step;
        else if (!(difference >= step))
            eased = target;
        else
            eased = double(step) + step + rollVolume;
        rollVolume = float(eased);
        rollVoice->Play(params->listener->view,
                        float(mix->GetVolume() * volume * rollVolume * params->volume), params->pitch,
                        params->azimuth, 0.0f, 0.0f);
    }
    if (collisionVoice != NULL) {
        collisionVoice->Play(params->listener->view, float(mix->GetVolume() * volume * params->volume),
                             params->pitch, params->azimuth, 0.0f, 0.0f);
        if (AVoice_IsFinished(collisionVoice, 0)) {
            AVoice_Destruct(collisionVoice, 0);
            UMemory::FastFree(collisionVoice, sizeof(AVoice));
            collisionVoice = NULL;
        }
    } else if (detached) {
        CallDelete(1);
    }
}

// FUNC_AT(0x0012dc50)
void ASmackable::Detatch() {
    if (collisionVoice != NULL && !AVoice_IsFinished(collisionVoice, 0))
        detached = true;
    else
        CallDelete(1);
}

// FUNC_AT(0x0012dc80)
const char* ASmackable::GetName() {
    if (collisionVoice != NULL)
        return ABank::GetPatchName(collisionVoice->views[0].bank, collisionVoice->views[0].patch);
    return "Smackable";
}

// FUNC_AT(0x0012dcb0)
void ASmackable::Destruct() {
    vtable = kASmackableVtable;
    if (collisionVoice != NULL)
        DeleteVoice(collisionVoice);
    if (rollVoice != NULL)
        DeleteVoice(rollVoice);
    ASceneObj::Destruct();
}

// FUNC_AT(0x0012dd50)
ASmackable* ASmackable::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASmackable));
    return this;
}

// =============================================================================================================
// ATargeting
// =============================================================================================================

// FUNC_AT(0x0012e0f0)
ATargeting* ATargeting::Construct() {
    state = kLockNone;
    unknown04 = 0;
    mix = AMix::Get("Tracking System");
    return this;
}

// FUNC_AT(0x0012e120)
void ATargeting::SetState(TargetLockState state) {
    if (ASoundManager_fgIsPaused || state == this->state)
        return;
    switch (state) {
    case kLockNone:
        if (this->state == kLockLocked || this->state == kLockTracking)
            AMenuSound::Trigger(0, "SFX_targetunlock", "Tracking System", 0);
        break;
    case kLockLocked:
        if (this->state == kLockNone || this->state == kLockTracking)
            AMenuSound::Trigger(0, "SFX_targetlock", "Tracking System", 0);
        break;
    default:
        break;
    }
    this->state = state;
}

// =============================================================================================================
// AWorldSound
// =============================================================================================================

// FUNC_AT(0x0012dd80)
void AVoice::Rewind() {
    for (int i = 0; i < 3; i++) {
        views[i].finished = 0;
        views[i].handle = -1;
    }
}

// FUNC_AT(0x0012e090)
void AWorldSound::Destruct() {
    vtable = kAWorldSoundVtable;
    DestroyVoiceViews(&voice);
    ABaseSound::Destruct();
}

// FUNC_AT(0x0012dda0)
void AWorldSound::Play(ASoundPlayParams *params) {
    if (waiting) {
        float now = AudioClock;
        if (repeats > 0.5) {
            repeats = float(repeats - 1.0);
            waiting = 0;
            voice.Rewind();
        } else if (nextTime < now) {
            if (double(maxDelay) + minDelay != 0.0) {
                if (nextTime == 0.0f)
                    nextTime = float(rrandom() * maxDelay + now);
                else
                    nextTime = float(rrandom() * (double(maxDelay) - minDelay) + now + minDelay);
                repeats = float(rrandom() * (double(maxRepeats) - minRepeats) + minRepeats);
            }
            waiting = 0;
            voice.Rewind();
        }
    }
    if (AVoice_IsFinished(&voice, 0))
        waiting = 1;

    float level = waiting ? 0.0f : float(mix->GetVolume() * volume * params->volume);
    float voicePitch = mode == 1 ? pitch : pitch * params->pitch;
    float azimuth = mode == 1 ? 0.0f : params->azimuth;
    voice.Play(params->listener->view, level, voicePitch, azimuth, 0.0f, 0.0f);

    if (passVoice != NULL) {
        // where the sound will be over the next five steps of its velocity (the copy takes position's padding
        // word along, as the w the VU0 add keeps)
        Coord4 ahead;
        memcpy(&ahead, &position, sizeof(ahead));
        for (int step = 5; step > 0; step--) {
            VU0_v3add(&velocity, &ahead, &ahead);
            if (vec3distance(&ahead, params->listener->unknown60) < 6.0f)
                passFrames = 180;
        }
        float passLevel = 0.0f;
        if (passFrames > 0) {
            double loud = mix->GetVolume() * volume * params->volume * 2;
            passLevel = float(loud);
            if (loud > 1.0)
                passLevel = 1.0f;
        }
        float passPitch = 0.0f;
        if (params->pitch > 0.0f)
            passPitch = float((params->pitch - 1.0) * 0.25 + 1.0);
        passVoice->Play(params->listener->view, passLevel, passPitch, params->azimuth, 0.0f, 0.0f);
        if (passFrames > 0) {
            passFrames -= FramesSinceAudioUpdate;
            if (passFrames < 0)
                passFrames = 0;
        }
    }
}
