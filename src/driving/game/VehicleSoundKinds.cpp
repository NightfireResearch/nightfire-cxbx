#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif
#include "VehicleSoundKinds.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <bit>

#include "BondCarState.h"               // CarSurface
#include "VehicleSound.h"
#include "WeaponSound.h"                // AGun, ATurret
#include "MissionManager.h"             // MissionCinematicMode
#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Mix.h"               // AMix
#include "../audio/Sound.h"             // ABaseSound, AOneShotSound, ABasic, AMenuSound
#include "../audio/SoundManager.h"      // ASoundManager, FramesSinceAudioUpdate
#include "../audio/Voice.h"             // AVoice
#include "../data/StdStreams.h"         // GameStd::String
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"     // Abs
#include "../platform/RealMath.h"       // v3dotprod, VU0_sqrt
#include "../platform/X87.h"            // RoundToInt
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"

#pragma fp_contract(off)

// ---- the game's globals

// SMissionManager's: set by SetCinematicMode, cleared by ClearCinematicMode, set by Win and Lose (name ours)

// Counted down by ATrafficVehicle's destructor for a sound with countedB (or countedA) set; nothing else in the
// game reads or writes them (names ours)
// XBE_GLOBAL(0x00243b5c, 4)
int32_t TrafficCountA;
// XBE_GLOBAL(0x00243b60, 4)
int32_t TrafficCountB;

// ---- calls to originals not ported

// AOneShotSound's constructors: a bank's patch at a place (and velocity), at `volume`; the second leaves the views
// looping when `loop` is set
#define AOneShotSound_Construct ((AOneShotSound *(__fastcall *)(void *, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity, float volume, const char *mixName))0x000476f0)
#define AOneShotSound_ConstructAt ((AOneShotSound *(__fastcall *)(void *, int, int bank, int patch, const Coord3 *position, float volume, const char *mixName, bool loop))0x000477d0)
// Whether the voice is done with (Ghidra: FUN_000d36c0; the name is ours, as audio/Sound.cpp's)
#define AVoice_IsFinished ((bool (__fastcall *)(AVoice *, int))0x000d36c0)
// Every view's loop flag cleared: the voice plays once (Ghidra: FUN_0003e6b0; the name is ours)
#define AVoice_PlayOnce ((void (__fastcall *)(AVoice *, int))0x0003e6b0)

namespace {

const uint32_t kAPlayerVehicleVtable = 0x001a2abc;
const uint32_t kAPlayerHeliVtable = 0x001a2b14;
const uint32_t kAUltraLiteVtable = 0x001a2bc4;
const uint32_t kASnowMobileVtable = 0x001a2c40;
const uint32_t kASubmersibleVtable = 0x001a2d44;
const uint32_t kAPlayerTankVtable = 0x001a2e28;
const uint32_t kAHelicopterVtable = 0x001a2e58;
const uint32_t kATrafficVehicleVtable = 0x001a2ed8;

// The azimuths the three close voices of AUltraLite and APlayerHeli are heard at (CloseVoice)
const float kCloseAzimuths[kCloseVoices] = { 0.25f, 0.75f, 0.0f };

// new AVoice(mix, bank's handle, bank's patch): the patch looked up once the block is there
AVoice *NewVoice(AMix *mix, ABank *bank, const char *patchName) {
    void *block = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    if (block == NULL)
        return NULL;
    return static_cast<AVoice *>(block)->Construct(mix, bank->handle, bank->index.Lookup(patchName));
}

// The same in the mix `mixName`, got (and made if new) after the patch is looked up
AVoice *NewVoice(const char *mixName, ABank *bank, const char *patchName) {
    void *block = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    if (block == NULL)
        return NULL;
    int patch = bank->index.Lookup(patchName);
    return static_cast<AVoice *>(block)->Construct(AMix::Get(mixName), bank->handle, patch);
}

// delete voice: its views destroyed last to first, the block freed
void DeleteVoice(AVoice *voice) {
    if (voice == NULL)
        return;
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
    UMemory::FastFree(voice, sizeof(AVoice));
}

// An AVoice member's constructor and destructor: the mix, and the views made first to last (destroyed last to first)
void ConstructVoice(AVoice *voice, AMix *mix) {
    voice->mix = mix;
    for (int i = 0; i < 3; i++)
        voice->views[i].Construct();
}

void DestructVoice(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
}

// The gun's fire patch, from a std::string made of `patch` (inline, as the game's string constructor is)
void SetFirePatch(AGun *gun, const char *patch) {
    GameStd::String text;
    text.capacity = 15;
    text.size = 0;
    text.text.buffer[0] = '\0';
    text.AssignText(patch, static_cast<uint32_t>(strlen(patch)));
    gun->SetFirePatch(text);
}

// A one-shot sound of bank slot 9's patch at the sound's position and velocity, in `mixName`, left to play once
void PlayOneShot(AVehicle *sound, const char *patchName, const char *mixName) {
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    if (block == NULL)
        return;
    ABank *bank = fgBanks[9];
    int patch = bank->index.Lookup(patchName);
    AOneShotSound_Construct(block, 0, bank->handle, patch, &sound->position, &sound->velocity,
                            float(sound->GetMixedVolume()), mixName);
}

// A one-shot sound of the bank's patch constructed on the stack and destroyed straight away, as the snowmobiles'
// landings make theirs: it is never in the sound list, so never heard
void MakeLandingSound(AVehicle *sound, ABank *bank, const char *patchName, const char *mixName) {
    int patch = bank->index.Lookup(patchName);
    AOneShotSound landing;
    AOneShotSound_Construct(&landing, 0, bank->handle, patch, &sound->position, &sound->velocity,
                            float(sound->GetMixedVolume()), mixName);
    landing.Destruct();
}

// The submersible's one-shots: bank slot 9's patch at the sound's position, in "One Shots"
AOneShotSound *NewCavitationShot(ASubmersible *sound, const char *patchName, float volume) {
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    if (block == NULL)
        return NULL;
    ABank *bank = fgBanks[9];
    int patch = bank->index.Lookup(patchName);
    return AOneShotSound_ConstructAt(block, 0, bank->handle, patch, &sound->position, volume, "One Shots", false);
}

AOneShotSound *NewServoShot(ASubmersible *sound, const char *patchName) {
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    if (block == NULL)
        return NULL;
    ABank *bank = fgBanks[9];
    int patch = bank->index.Lookup(patchName);
    return AOneShotSound_ConstructAt(block, 0, bank->handle, patch, &sound->position, float(sound->GetMixedVolume()),
                                     "One Shots", false);
}

// The snowmobiles' carving surfaces besides paved ground
bool IsSnowy(int surface) {
    return surface == kSNOW || surface == kICE || surface == kGRASS;
}

// The sign flipped by its bit, as the x87's FCHS does (PhysicsMath.h's Abs for a double)
double AbsDouble(double x) {
    return x < 0.0 ? std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull) : x;
}

// ABasic's vtable slot 4: Stop
void CallStop(ABaseSound *sound, ASoundPlayParams *params) {
    typedef void (ABaseSound::*StopMethod)(ASoundPlayParams *params);
    (sound->*XbeVirtual<StopMethod>(sound, 4))(params);
}

}  // namespace

// =============================================================================================================
// APlayerVehicle

// FUNC_AT(0x001284b0)
APlayerVehicle* APlayerVehicle::Construct(const char *name) {
    AVehicle::Construct("Player Car", kVehicleSkids);
    vtable = kAPlayerVehicleVtable;
    engine = AEngine::Get(name, this, 4500.0f);
    strcpy(engineName, name);
    ActivateGun("Machine Gun");
    ActivateWind();
    roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_PavedRN");
    for (int wheel = 0; wheel < 4; wheel++)
        roadSurfaces[wheel] = surfaces[wheel];
    if (_stricmp(name, "suv") == 0)
        vehicleType = kVehicleSoundPlayerCar;
    return this;
}

// FUNC_AT(0x001285f0)
void APlayerVehicle::Destruct() {
    vtable = kAPlayerVehicleVtable;
    DeleteVoice(roadVoice);
    AEngine::RemoveAll();
    AVehicle::Destruct();
}

// FUNC_AT(0x00128670)
APlayerVehicle* APlayerVehicle::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(APlayerVehicle));
    return this;
}

// The road voices by surface are a jump table (byte index at 0x00128a24, bodies at 0x00128a08).
// FUNC_AT(0x001286a0)
void APlayerVehicle::Play(ASoundPlayParams *params) {
    if (double(rpmWobble) + rpm < 500.0)
        rpm = 500.0f;
    AVehicle::Play(params);
    engine->CallPlay(params);
    if (roadVoice != NULL) {
        float level;
        if (speed < 15.0f)
            level = 0.0f;
        else if (speed < 50.0f)
            level = float((double(speed) - 15.0f) * 0.028571429f);
        else
            level = 1.0f;
        roadVoice->Play(params->listener->view, float(asphaltMix->GetVolume() * level * params->volume),
                        params->pitch, params->azimuth, 0.0f, 0.0f);
    }
    for (int wheel = 0; wheel < 4; wheel++) {
        if (surfaces[wheel] == roadSurfaces[wheel])
            continue;
        roadSurfaces[wheel] = surfaces[wheel];
        if (roadVoice != NULL) {
            DeleteVoice(roadVoice);
            roadVoice = NULL;
        }
        switch (roadSurfaces[wheel]) {
        case kPAVED:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_PavedRN");
            break;
        case kGRAVEL:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_GravelRN");
            break;
        case kCOBBLE:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_CobbleRN");
            break;
        case kICE:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_IceRN");
            break;
        case kSNOW:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_SnowRN");
            break;
        case kPAVED_ROUGH:
        case kSurface15:
            roadVoice = NewVoice(asphaltMix, fgBanks[0], "SFX_OffRN");
            break;
        }
    }
}

// =============================================================================================================
// APlayerHeli

// FUNC_AT(0x00128a40)
APlayerHeli* APlayerHeli::Construct() {
    AVehicle::Construct("Player Car", kVehicleSqueals);
    vtable = kAPlayerHeliVtable;
    for (int i = 0; i < kCloseVoices; i++)
        changeVoices[i] = NULL;
    for (int i = 0; i < kCloseVoices; i++)
        rotorVoices[i] = NULL;
    moving = 0.0f;
    ABank *bank = fgBanks[0];
    for (int i = 0; i < kCloseVoices; i++)
        changeVoices[i] = NewVoice("Player Car", bank, "SFX_helichange");
    for (int i = 0; i < kCloseVoices; i++)
        rotorVoices[i] = NewVoice("Player Car", bank, "SFX_heli");
    maxDistance = 0.0f;
    maxDistanceSq = 0.0f;
    ActivateGun("Machine Gun");
    ActivateZoom();
    ActivateWind();
    vehicleType = kVehicleSoundHeli;
    return this;
}

// FUNC_AT(0x001290e0)
void APlayerHeli::Destruct() {
    vtable = kAPlayerHeliVtable;
    for (int i = 0; i < kCloseVoices; i++)
        DeleteVoice(changeVoices[i]);
    for (int i = 0; i < kCloseVoices; i++)
        DeleteVoice(rotorVoices[i]);
    AVehicle::Destruct();
}

// FUNC_AT(0x00129220)
APlayerHeli* APlayerHeli::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(APlayerHeli));
    return this;
}

// FUNC_AT(0x00128d10)
void APlayerHeli::Play(ASoundPlayParams *params) {
    AVehicle::Play(params);
    Coord3 v = velocity;
    float velocityLength = VU0_sqrt(float(double(v.z) * v.z + double(v.y) * v.y + double(v.x) * v.x));
    moving = velocityLength < 1.0f ? float(double(moving) * 0.9f) : float(double(moving) * 0.9f + 0.1f);
    int view = params->listener->view;
    if (params->distance < 2.0f) {
        for (int i = 0; i < kCloseVoices; i++) {
            double level = GetMixedVolume();
            changeVoices[i]->Play(view, float((1.0 - moving) * level), pitch * params->pitch, kCloseAzimuths[i],
                                  0.0f, 0.0f);
        }
        for (int i = 0; i < kCloseVoices; i++) {
            double level = GetMixedVolume();
            rotorVoices[i]->Play(view, float(level * moving), pitch * params->pitch, kCloseAzimuths[i], 0.0f, 0.0f);
        }
    } else {
        double level = GetMixedVolume();
        changeVoices[0]->Play(view, float((1.0 - moving) * params->volume * level), pitch * params->pitch,
                              params->azimuth, 0.0f, 0.0f);
        changeVoices[1]->Play(view, 0.0f, params->pitch * pitch, params->azimuth, 0.0f, 0.0f);
        changeVoices[2]->Play(view, 0.0f, params->pitch * pitch, params->azimuth, 0.0f, 0.0f);
        level = GetMixedVolume();
        rotorVoices[0]->Play(view, float(double(moving) * params->volume * level), pitch * params->pitch, params->azimuth,
                             0.0f, 0.0f);
        rotorVoices[1]->Play(view, 0.0f, params->pitch * pitch, params->azimuth, 0.0f, 0.0f);
        rotorVoices[2]->Play(view, 0.0f, params->pitch * pitch, params->azimuth, 0.0f, 0.0f);
    }
}

// =============================================================================================================
// AUltraLite

// FUNC_AT(0x0012a440)
AUltraLite* AUltraLite::Construct() {
    static const char *const kPatches[kUltraLiteVoices] = {
        "SFX_bultralightDive", "SFX_BultraliteLdLW", "SFX_BultraliteLdHI"
    };
    AVehicle::Construct("Player Car", kVehicleSqueals);
    vtable = kAUltraLiteVtable;
    for (int at = 0; at < kCloseVoices; at++)
        for (int kind = 0; kind < kUltraLiteVoices; kind++)
            voices[at][kind] = NULL;
    motorPitch = 0.0f;
    levelGain = 1.0f;
    diveGain = 0.0f;
    climbGain = 0.0f;
    ABank *bank = fgBanks[0];
    for (int kind = 0; kind < kUltraLiteVoices; kind++)
        for (int at = 0; at < kCloseVoices; at++)
            voices[at][kind] = NewVoice(mix, bank, kPatches[kind]);
    vehicleType = kVehicleSoundUltraLite;
    ActivateWind();
    ActivateGun("POV Weapons");
    gunLoops = 0;
    SetFirePatch(gun, "SFX_umntmachfire");
    return this;
}

// FUNC_AT(0x00129b20)
void AUltraLite::Destruct() {
    vtable = kAUltraLiteVtable;
    for (int at = 0; at < kCloseVoices; at++)
        for (int kind = 0; kind < kUltraLiteVoices; kind++)
            DeleteVoice(voices[at][kind]);
    AVehicle::Destruct();
}

// FUNC_AT(0x0012a410)
AUltraLite* AUltraLite::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(AUltraLite));
    return this;
}

// FUNC_AT(0x00129b00)
void AUltraLite::Play(ASoundPlayParams *params) {
    CallPlayMotor(params);
    AVehicle::Play(params);
}

// FUNC_AT(0x00129cd0)
void AUltraLite::PlayMotor(ASoundPlayParams *params) {
    float climb = velocity.y;
    double target = motorPitch;
    if (climb <= 5.0f && climb >= -5.0f) {
        target = double(Abs(climb)) * 0.3f * 0.2f + 0.7f;
        climbGain = climbGain * 0.95f;
        diveGain = diveGain * 0.95f;
        levelGain = float(double(levelGain) * 0.95f + 0.05f);
    } else if (climb > 5.0f) {
        target = 1.2f;
        climbGain = float(double(climbGain) * 0.95f + 0.05f);
        diveGain = diveGain * 0.95f;
        levelGain = levelGain * 0.95f;
    } else if (climb < 5.0f) {
        target = AbsDouble(double(climb) * 0.0833333358f);
        climbGain = climbGain * 0.95f;
        diveGain = float(double(diveGain) * 0.95f + 0.05f);
    }
    double eased = double(motorPitch) * 0.95f + target * 0.05f;
    motorPitch = float(eased);
    if (climb > 5.0f && eased > 1.2f)
        motorPitch = 1.2f;
    if (climb < -5.0f) {
        if (motorPitch < 1.0f)
            motorPitch = 1.0f;
        if (motorPitch > 1.7f)
            motorPitch = 1.7f;
    }

    int view = params->listener->view;
    if (params->distance > 1.0f) {
        // the first three at the listener's azimuth, the others silent
        double level = GetMixedVolume();
        voices[kCloseLeft][kUltraLiteLevel]->Play(view, float(double(levelGain) * params->volume * level),
                                                  float(double(motorPitch) * params->pitch * pitch),
                                                  params->azimuth, 0.0f, 0.0f);
        level = GetMixedVolume();
        voices[kCloseLeft][kUltraLiteClimb]->Play(view, float(double(climbGain) * params->volume * level),
                                                  float(double(motorPitch) * params->pitch * pitch),
                                                  params->azimuth, 0.0f, 0.0f);
        level = GetMixedVolume();
        voices[kCloseLeft][kUltraLiteDive]->Play(view, float(double(diveGain) * params->volume * level),
                                                 float(double(motorPitch) * params->pitch * pitch),
                                                 params->azimuth, 0.0f, 0.0f);
        float silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseRight][kUltraLiteLevel]->Play(view, params->volume * 0.0f, silentPitch, 0.75f, 0.0f, 0.0f);
        silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseRight][kUltraLiteClimb]->Play(view, params->volume * 0.0f, silentPitch, 0.75f, 0.0f, 0.0f);
        silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseRight][kUltraLiteDive]->Play(view, params->volume * 0.0f, silentPitch, 0.75f, 0.0f, 0.0f);
        silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseCentre][kUltraLiteLevel]->Play(view, params->volume * 0.0f, silentPitch, 0.0f, 0.0f, 0.0f);
        silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseCentre][kUltraLiteClimb]->Play(view, params->volume * 0.0f, silentPitch, 0.0f, 0.0f, 0.0f);
        silentPitch = float(double(pitch) * motorPitch * params->pitch);
        voices[kCloseCentre][kUltraLiteDive]->Play(view, params->volume * 0.0f, silentPitch, 0.0f, 0.0f, 0.0f);
    } else {
        // all nine around the listener
        for (int at = 0; at < kCloseVoices; at++) {
            double level = GetMixedVolume();
            voices[at][kUltraLiteLevel]->Play(view, float(level * levelGain * params->volume),
                                              float(double(pitch) * motorPitch * params->pitch), kCloseAzimuths[at],
                                              0.0f, 0.0f);
            level = GetMixedVolume();
            voices[at][kUltraLiteClimb]->Play(view, float(level * climbGain * params->volume),
                                              float(double(pitch) * motorPitch * params->pitch), kCloseAzimuths[at],
                                              0.0f, 0.0f);
            level = GetMixedVolume();
            voices[at][kUltraLiteDive]->Play(view, float(level * params->volume * diveGain),
                                             float(double(pitch) * motorPitch * params->pitch), kCloseAzimuths[at],
                                             0.0f, 0.0f);
        }
    }
}

// =============================================================================================================
// ASnowMobile

// FUNC_AT(0x0012b5f0)
ASnowMobile* ASnowMobile::Construct(const char *name) {
    AVehicle::Construct("Player Car", kVehicleSqueals);
    vtable = kASnowMobileVtable;
    snowWind = NULL;
    revVoice = NULL;
    carvingMix = AMix::Get("OffRoad");
    motorMix = mix;
    airborneWheels = 0;
    jumped = false;
    rearLifted = false;
    revving = false;
    cameraAlong = 0.0f;
    cameraAcross = 0.0f;
    unknown23c = 0.078f;
    rearCompression = 0.0f;
    frontCompression = 0.0f;
    cruiseGain = 0.0f;
    cruiseLevel = 0.0f;
    unknown250 = 0.09f;
    smoothedRpm = 0.0f;
    AVehicleWind *block = static_cast<AVehicleWind *>(::OperatorNew(sizeof(AVehicleWind)));
    snowWind = block != NULL ? block->Construct(this) : NULL;
    restCompression[0] = 0.119527f;
    restCompression[1] = 0.119531f;
    restCompression[2] = 0.075912f;
    restCompression[3] = 0.075912f;
    ActivateGun("POV Weapons");
    SetFirePatch(gun, "SFX_snomachfire");
    gun->shells = true;
    gunLoops = 0;
    snowVoice = NewVoice(carvingMix, fgBanks[0], "SFX_snowRN");
    roadVoice = NewVoice(carvingMix, fgBanks[0], "SFX_roadRN");
    roadCarveVoice = NewVoice(carvingMix, fgBanks[0], "SFX_roadcarve");
    carveVoice = NewVoice(carvingMix, fgBanks[0], "SFX_carve");
    idleVoice = NewVoice(motorMix, fgBanks[9], "SFX_idle");
    cruiseVoice = NewVoice(motorMix, fgBanks[9], "SFX_cruz");
    vehicleType = kVehicleSoundSnowMobile;
    return this;
}

// FUNC_AT(0x0012a890)
void ASnowMobile::Destruct() {
    vtable = kASnowMobileVtable;
    DeleteVoice(snowVoice);
    DeleteVoice(roadVoice);
    DeleteVoice(roadCarveVoice);
    DeleteVoice(carveVoice);
    DeleteVoice(idleVoice);
    DeleteVoice(cruiseVoice);
    if (snowWind != NULL) {
        snowWind->Destruct();
        ::OperatorDelete(snowWind);
    }
    DeleteVoice(revVoice);
    AVehicle::Destruct();
}

// FUNC_AT(0x0012b510)
ASnowMobile* ASnowMobile::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASnowMobile));
    return this;
}

// FUNC_AT(0x0012b540)
void ASnowMobile::Play(ASoundPlayParams *params) {
    cameraAlong = v3dotprod(&cameraAim, &forward);
    cameraAcross = v3dotprod(&cameraAim, &right);
    float frames = float(FramesSinceAudioUpdate);
    if (cameraAlong > 0.0f) {
        snowWind->volume = Abs(cameraAlong);
        snowWind->Play(params, frames);
    } else {
        snowWind->Stop(params);
    }
    AVehicle::Play(params);
    PlayCarving(params);
    PlayMotor(params);
}

// FUNC_AT(0x0012aa10)
void ASnowMobile::PlayMotor(ASoundPlayParams *params) {
    double rpmNow = (double(rpmWobble) + rpm) * 0.1f + double(smoothedRpm) * 0.9f;
    smoothedRpm = float(rpmNow);
    motorPitch = float((rpmNow + 2000.0f) * 0.00013333333f);
    if (rpmNow <= 1000.0f)
        cruiseLevel = float(rpmNow * 0.001f);
    else
        cruiseLevel = 1.0f;
    if (revving)
        cruiseGain = cruiseGain * 0.75f;
    else
        cruiseGain = 1.0f;

    double rear = (double(suspensionCompression[3]) + suspensionCompression[2]) * 0.5f;
    rearCompression = float(rear);
    frontCompression = float((double(suspensionCompression[0]) + suspensionCompression[1]) * 0.5f);
    if (rearLifted && rear > 0.0 && throttle > 0.0f)
        PlayOneShot(this, "SFX_freewheel", "Offroad");
    rearLifted = rearCompression == 0.0f;

    int view = params->listener->view;
    int16_t wheels = airborneWheels;
    if (wheels != GetAirborneWheels()) {
        if (wheels == 0 && GetAirborneWheels() > 0)
            jumped = true;
        else if (airborneWheels != 0)
            jumped = false;
        airborneWheels = static_cast<int16_t>(GetAirborneWheels());
        if (jumped && (revVoice == NULL || AVoice_IsFinished(revVoice, 0)) && throttle > 0.0f) {
            const char *patch = NULL;
            if (suspensionCompression[0] == 0.0f && suspensionCompression[1] == 0.0f)
                patch = "SFX_rev2";
            else if (suspensionCompression[0] == 0.0f || suspensionCompression[1] == 0.0f)
                patch = "SFX_rev1";
            if (patch != NULL) {
                revVoice = NewVoice(motorMix, fgBanks[9], patch);
                AVoice_PlayOnce(revVoice, 0);
                revVoice->Play(view, float(GetMixedVolume() * params->volume), params->pitch * motorPitch,
                               params->azimuth, 0.0f, 0.0f);
                revving = true;
            }
        }
    }

    if (revVoice != NULL && !AVoice_IsFinished(revVoice, 0)) {
        revVoice->Play(view, float(GetMixedVolume() * params->volume), motorPitch * params->pitch, params->azimuth,
                       0.0f, 0.0f);
        idleVoice->Play(view, float((1.0 - cruiseLevel) * GetMixedVolume() * params->volume * 0.0f), params->pitch,
                        params->azimuth, 0.0f, 0.0f);
        cruiseVoice->Play(view, float(GetMixedVolume() * cruiseLevel * cruiseGain * params->volume * 0.0f),
                          motorPitch * params->pitch, params->azimuth, 0.0f, 0.0f);
    } else {
        idleVoice->Play(view, float((1.0 - cruiseLevel) * GetMixedVolume() * params->volume), params->pitch,
                        params->azimuth, 0.0f, 0.0f);
        cruiseVoice->Play(view, float(GetMixedVolume() * cruiseLevel * cruiseGain * params->volume),
                          motorPitch * params->pitch, params->azimuth, 0.0f, 0.0f);
        if (revVoice != NULL && AVoice_IsFinished(revVoice, 0)) {
            DeleteVoice(revVoice);
            revVoice = NULL;
            revving = false;
            DeleteVoice(cruiseVoice);
            cruiseVoice = NewVoice(motorMix, fgBanks[9], "SFX_cruz");
        }
    }
}

// FUNC_AT(0x0012b060)
void ASnowMobile::PlayCarving(ASoundPlayParams *params) {
    float carving = float(double(speed) * 0.05f * carvingMix->GetVolume());
    bool frontCarving = false;
    bool rearCarving = false;
    bool close = params->distance < 2.0f;
    float rearAzimuth, frontAzimuth;
    if (close) {
        if (cameraAlong <= 0.0f)
            rearAzimuth = float(double(cameraAcross) * 0.25f + 0.5f);
        else if (cameraAcross >= 0.0f)
            rearAzimuth = float(1.0f - double(cameraAcross) * 0.25f);
        else
            rearAzimuth = Abs(cameraAcross) * 0.25f;
        frontAzimuth = 1.0f - rearAzimuth;
    } else {
        rearAzimuth = params->azimuth;
        frontAzimuth = params->azimuth;
    }

    if (airborneWheels != 0 && GetAirborneWheels() == 0) {
        ABank *bank = fgBanks[1];
        double rest = (double(restCompression[1]) + restCompression[0]) * 0.5f;
        float restAverage = float(rest);
        if (rest * 2.5f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_heavy1", "Landings");
        if (double(restAverage) * 1.67f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_med2", "Landings");
        if (double(restAverage) * 1.25f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_tiressnow", "Landings");
    }

    int view = params->listener->view;
    if (close && MissionCinematicMode == 0) {
        if (surfaces[0] == kPAVED || surfaces[1] == kPAVED) {
            roadCarveVoice->Play(view, carving, params->pitch, frontAzimuth, 0.0f, 0.0f);
            carveVoice->Play(view, 0.0f, params->pitch, frontAzimuth, 0.0f, 0.0f);
            frontCarving = true;
        } else if (IsSnowy(surfaces[0]) || IsSnowy(surfaces[1])) {
            carveVoice->Play(view, carving, params->pitch, frontAzimuth, 0.0f, 0.0f);
            roadCarveVoice->Play(view, 0.0f, params->pitch, frontAzimuth, 0.0f, 0.0f);
            frontCarving = true;
        }
        // The rear: surfaces[3], and the word after it (previousSurfaces[0])
        if (surfaces[3] == kPAVED || previousSurfaces[0] == kPAVED) {
            roadVoice->Play(view, carving, params->pitch, rearAzimuth, 0.0f, 0.0f);
            snowVoice->Play(view, 0.0f, params->pitch, rearAzimuth, 0.0f, 0.0f);
            rearCarving = true;
        } else if (IsSnowy(surfaces[3]) || IsSnowy(previousSurfaces[0])) {
            snowVoice->Play(view, carving, params->pitch, rearAzimuth, 0.0f, 0.0f);
            roadVoice->Play(view, 0.0f, params->pitch, rearAzimuth, 0.0f, 0.0f);
            rearCarving = true;
        }
    }
    if (!frontCarving) {
        roadCarveVoice->Play(view, 0.0f, params->pitch, frontAzimuth, 0.0f, 0.0f);
        carveVoice->Play(view, 0.0f, params->pitch, frontAzimuth, 0.0f, 0.0f);
    }
    if (!rearCarving) {
        snowVoice->Play(view, 0.0f, params->pitch, rearAzimuth, 0.0f, 0.0f);
        roadVoice->Play(view, 0.0f, params->pitch, rearAzimuth, 0.0f, 0.0f);
    }
}

// =============================================================================================================
// ASubmersible

// FUNC_AT(0x0012b960)
ASubmersible* ASubmersible::Construct(const char *name) {
    AVehicle::Construct("Player Car", kVehicleSkids);
    vtable = kASubmersibleVtable;
    jetVoice = NULL;
    rudderVoice = NULL;
    cavitationShot = NULL;
    servoShot = NULL;
    servoOnCount = 0;
    servoOffCount = 0;
    cavitationCount = 0;
    unknown21a = 10;
    unknown21c = false;
    fansOn = false;
    unknown21e = true;
    rudderOn = false;
    jetOn = false;
    jetLevel = 0.0f;
    rudderLevel = 0.0f;
    levelStep = 0.06f;
    creakLevel = 0;
    creakPeriod = 0;
    creakDelay = 0;
    creakTime = 0;
    strcpy(soundName, name);
    fanVoice = NewVoice("Player Car", fgBanks[9], "SFX_load");
    jetVoice = NewVoice("Player Car", fgBanks[9], "SFX_jetcav1");
    rudderVoice = NewVoice("Player Car", fgBanks[9], "SFX_rudcav1");
    maxDistance = 0.0f;
    maxDistanceSq = 0.0f;
    vehicleType = kVehicleSoundSubmersible;
    return this;
}

// FUNC_AT(0x0012be30)
void ASubmersible::Destruct() {
    vtable = kASubmersibleVtable;
    DeleteVoice(jetVoice);
    DeleteVoice(rudderVoice);
    DeleteVoice(fanVoice);
    AVehicle::Destruct();
}

// FUNC_AT(0x0012c4e0)
ASubmersible* ASubmersible::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ASubmersible));
    return this;
}

// FUNC_AT(0x0012c510)
void ASubmersible::Play(ASoundPlayParams *params) {
    AVehicle::Play(params);
    PlayFanMotors(params);
    PlayServos(params);
    PlayCavitation(params);
    PlayCreaks(params);
}

// FUNC_AT(0x0012bb60)
void ASubmersible::SetCreakLevel(int16_t level, int32_t period) {
    creakLevel = level;
    creakPeriod = period;
}

// FUNC_AT(0x0012bb80)
void ASubmersible::PlayCreaks(ASoundPlayParams *params) {
    if (creakLevel < 1)
        return;
    if (creakTime < creakDelay) {
        creakTime++;
        return;
    }
    float variation = float(ASoundManager::NormalizedRandomNumber() * creakPeriod * 0.1f);
    creakDelay = creakPeriod + RoundToInt(variation);
    creakTime = 0;
    const char *patches[4] = { "SFX_creak1", "SFX_creak2", "SFX_creak3", "SFX_creak4" };
    int patch = fgBanks[0]->index.Lookup(patches[creakLevel - 1]);
    AMenuSound::Trigger(fgBanks[0]->handle, patch, "Ambience", kSoundViewShared);
}

// FUNC_AT(0x0012bc60)
void ASubmersible::PlayFanMotors(ASoundPlayParams *params) {
    float fanPitch = speed * 0.05f;
    if (brakeButton)
        fanPitch = fanPitch * 0.85f;
    int view = params->listener->view;
    if (gasButton || brakeButton) {
        if (!fansOn)
            StartFade(10);
        fansOn = true;
        float fade = GetFade();
        double level = GetMixedVolume();
        fanVoice->Play(view, float((double(rpmWobble) + rpm) * 0.0003030303f * params->volume * level * fade),
                       fanPitch * params->pitch, params->azimuth, 0.0f, 0.0f);
    }
    if (!gasButton && !brakeButton && fansOn) {
        if (!fading)
            StartFade(10);
        float fade = GetFade();
        double level = GetMixedVolume();
        fanVoice->Play(view,
                       float((double(rpmWobble) + rpm) * 0.0003030303f * (1.0 - fade) * params->volume * level),
                       fanPitch * params->pitch, params->azimuth, 0.0f, 0.0f);
    }
    if (!gasButton && !brakeButton && !fading) {
        fansOn = false;
        fanVoice->Play(view, params->volume * 0.0f, params->pitch, params->azimuth, 0.0f, 0.0f);
    }
}

// FUNC_AT(0x0012bf00)
void ASubmersible::PlayCavitation(ASoundPlayParams *params) {
    float level = float(double(speed) * 0.05f * mix->GetVolume() * volume);
    int view = params->listener->view;
    if ((carSpeed > 0.0f && brakeButton) || (carSpeed < 0.0f && gasButton)) {
        jetOn = true;
        jetVoice->Play(view, level * params->volume,
                       float(((double(rpmWobble) + rpm) * 0.0005556 + 0.2f) * params->pitch), params->azimuth,
                       0.0f, 0.0f);
        jetLevel = level;
    } else if (jetOn) {
        double fading = double(jetLevel) - levelStep;
        jetLevel = float(fading);
        if (fading < 0.0) {
            jetLevel = 0.0f;
            jetOn = false;
            jetVoice->Play(view, params->volume * 0.0f, params->pitch, params->azimuth, 0.0f, 0.0f);
        } else {
            jetVoice->Play(view, float(fading * params->volume),
                           float(((double(rpmWobble) + rpm) * 0.0005556 + 0.2) * params->pitch), params->azimuth,
                           0.0f, 0.0f);
        }
    }

    if ((carSpeed > 0.0f && steering != 0.0f) || steeringVertical != 0.0f) {
        rudderOn = true;
        rudderVoice->Play(view, level * params->volume, params->pitch, params->azimuth, 0.0f, 0.0f);
        rudderLevel = level;
    } else if (rudderOn) {
        double fading = double(rudderLevel) - levelStep;
        float fadedLevel = float(fading);
        rudderLevel = fadedLevel;
        if (fading < 0.0) {
            rudderLevel = 0.0f;
            rudderOn = false;
            rudderVoice->Play(view, params->volume * 0.0f, params->pitch, params->azimuth, 0.0f, 0.0f);
            if (++cavitationCount > 1)
                cavitationCount = 0;
            switch (cavitationCount) {
            case 0:
                cavitationShot = NewCavitationShot(this, "SFX_rudcav2", level);
                break;
            case 1:
                cavitationShot = NewCavitationShot(this, "SFX_rudcav3", level);
                break;
            }
        } else {
            rudderVoice->Play(view, fadedLevel, params->pitch, params->azimuth, 0.0f, 0.0f);
        }
    }
}

// FUNC_AT(0x0012c260)
void ASubmersible::PlayServos(ASoundPlayParams *params) {
    if (xyTrigger) {
        if (++servoOnCount > 1)
            servoOnCount = 0;
        switch (servoOnCount) {
        case 0:
            servoShot = NewServoShot(this, "SFX_servon1");
            break;
        case 1:
            servoShot = NewServoShot(this, "SFX_servon2");
            break;
        }
        xyTrigger = 0;
    }
    if (xyReleaseTrigger) {
        if (++servoOffCount > 1)
            servoOffCount = 0;
        switch (servoOffCount) {
        case 0:
            servoShot = NewServoShot(this, "SFX_servoff1");
            break;
        case 1:
            servoShot = NewServoShot(this, "SFX_servoff2");
            break;
        }
        xyReleaseTrigger = 0;
    }
}

// =============================================================================================================
// APlayerTank

// FUNC_AT(0x0012c550)
APlayerTank* APlayerTank::Construct(const char *engineName) {
    AVehicle::Construct("Player Car", kVehicleSqueals);
    vtable = kAPlayerTankVtable;
    tankTurret.Construct();
    engine = AEngine::Get(engineName, this, 4500.0f);
    treadMix = AMix::Get("Tank Treads");
    treadVoice = NewVoice(treadMix, fgBanks[0], "SFX_TankTread");
    ActivateGun("Gatling Gun");
    vehicleType = kVehicleSoundTank;
    return this;
}

// FUNC_AT(0x0012c730)
void APlayerTank::Destruct() {
    vtable = kAPlayerTankVtable;
    DeleteVoice(treadVoice);
    tankTurret.Destruct();
    AVehicle::Destruct();
}

// FUNC_AT(0x0012c7c0)
APlayerTank* APlayerTank::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(APlayerTank));
    return this;
}

// FUNC_AT(0x0012c640)
void APlayerTank::Play(ASoundPlayParams *params) {
    AVehicle::Play(params);
    engine->CallPlay(params);
    float tread;
    if (surfaces[2] == 0 || surfaces[3] == 0)
        tread = 0.0f;
    else if (speed < 8.0f)
        tread = speed * 0.125f;
    else
        tread = 1.0f;
    treadVoice->Play(params->listener->view, float(treadMix->GetVolume() * tread * params->volume),
                     float((double(tread) + 1.0f) * params->pitch * 0.5f), params->azimuth, 0.0f, 0.0f);
    tankTurret.Play(turningSideways, turningUpDown, servo, params);
}

// =============================================================================================================
// AHelicopter

// FUNC_AT(0x0012c7f0)
AHelicopter* AHelicopter::Construct(const char *mixName) {
    AVehicle::Construct(mixName, kVehicleSqueals);
    vtable = kAHelicopterVtable;
    ConstructVoice(&rotorVoice, mix);
    ConstructVoice(&gunVoice, mix);
    ConstructVoice(&unknownVoice2A0, mix);
    rotorLevel = 0.0f;
    landingTime = 0.0f;
    pitchOffset = 0.0f;
    ABank *bank = fgBanks[0];
    rotorVoice.Set(bank->handle, bank->index.Lookup("SFX_Heli"));
    bank = fgBanks[4];
    gunVoice.Set(bank->handle, bank->index.Lookup("SFX_HeliMachGun"));
    minDistance = 40.0f;
    maxDistance = 600.0f;
    maxDistanceSq = 360000.0f;
    return this;
}

// FUNC_AT(0x0012cac0)
void AHelicopter::Destruct() {
    vtable = kAHelicopterVtable;
    DestructVoice(&unknownVoice2A0);
    DestructVoice(&gunVoice);
    DestructVoice(&rotorVoice);
    AVehicle::Destruct();
}

// FUNC_AT(0x0012cb60)
AHelicopter* AHelicopter::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(AHelicopter));
    return this;
}

// FUNC_AT(0x0012c930)
void AHelicopter::PlayLanding(float volume, const Coord3 *position, const Coord3 *velocity) {
    if (landingTime == 0.0f)
        landingTime = 0.3f;
}

// FUNC_AT(0x0012c950)
void AHelicopter::Play(ASoundPlayParams *params) {
    double frames = FramesSinceAudioUpdate;
    if (rotorLevel < 0.8f) {
        double raised = frames * 0.0166666675f + rotorLevel;
        rotorLevel = float(raised);
        if (raised > 0.8f)
            rotorLevel = 0.8f;
    }
    if (landingTime > 0.0f) {
        double left = landingTime - frames * 0.0166666675f;
        landingTime = float(left);
        if (left <= 0.0) {
            ABank *bank = fgBanks[0];
            rotorVoice.Set(bank->handle, bank->index.Lookup("SFX_Helidie"));
            landingTime = -1.0f;
            rotorLevel = 1.0f;
        }
    }
    int view = params->listener->view;
    double level = GetMixedVolume();
    rotorVoice.Play(view, float(double(rotorLevel) * params->volume * level),
                    float((double(pitch) + pitchOffset) * params->pitch), params->azimuth, 0.0f, 0.0f);
    float gunLevel = firing ? float(GetMixedVolume() * params->volume) : 0.0f;
    gunVoice.Play(view, gunLevel, pitch * params->pitch, params->azimuth, 0.0f, 0.0f);
}

// =============================================================================================================
// ATrafficVehicle

// FUNC_AT(0x0012cb90)
ATrafficVehicle* ATrafficVehicle::Construct(const char *engine, const char *mixName) {
    AVehicle::Construct(mixName, kVehicleSkids);
    vtable = kATrafficVehicleVtable;
    siren = NULL;
    engineRpm = 0.0f;
    engineGain = 0.0f;
    pitchOffset = 0.0f;
    sirenOn = false;
    airborneWheels = 0;
    jumped = false;
    rearLifted = false;
    strcpy(engineName, engine);
    ABank *bank = fgBanks[0];
    char patch[32];
    sprintf(patch, "SFX_%sLdEn", engine);
    engineVoice = NewVoice(mix, bank, patch);
    sprintf(patch, "SFX_%sLdEx", engine);
    exhaustVoice = NewVoice(mix, bank, patch);
    if (_stricmp(engine, "tank") == 0)
        roadVoice = NewVoice("Tank Treads", bank, "SFX_tanktreads");
    else
        roadVoice = NewVoice(mix, bank, "SFX_TrafficRN");
    if (_stricmp(engine, "supsled") == 0 || _stricmp(engine, "oppsled") == 0) {
        restCompression[0] = 0.119527f;
        restCompression[1] = 0.119531f;
        restCompression[2] = 0.075912f;
        restCompression[3] = 0.075912f;
        vehicleType = kVehicleSoundSnowMobile;
    }
    return this;
}

// FUNC_AT(0x0012ce10)
bool ATrafficVehicle::IsTracked() {
    return vehicleType == kVehicleSoundSnowMobile;
}

// FUNC_AT(0x0012ce20)
char* ATrafficVehicle::GetName() {
    return engineName;
}

// FUNC_AT(0x0012ce30)
void ATrafficVehicle::AddSiren(const char *patch) {
    if (siren != NULL)
        return;
    void *block = OperatorNew(sizeof(ABasic), "ABasic");
    siren = block != NULL ? static_cast<ABasic *>(block)->Construct(patch, "Opponents") : NULL;
}

// FUNC_AT(0x0012ceb0)
void ATrafficVehicle::PlaySnowmobileSpecifics(float pitch) {
    if (airborneWheels != 0 && GetAirborneWheels() == 0) {
        ABank *bank = fgBanks[1];
        double rest = (double(restCompression[1]) + restCompression[0]) * 0.5f;
        float restAverage = float(rest);
        if (rest * 2.5f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_heavy1", "Opponents");
        if (double(restAverage) * 1.67f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_med2", "Opponents");
        if (double(restAverage) * 1.25f <= frontCompression)
            MakeLandingSound(this, bank, "SFX_tiressnow", "Opponents");
    }

    double rear = (double(suspensionCompression[3]) + suspensionCompression[2]) * 0.5f;
    rearCompression = float(rear);
    frontCompression = float((double(suspensionCompression[0]) + suspensionCompression[1]) * 0.5f);
    if (rearLifted && rear > 0.0 && throttle > 0.0f)
        PlayOneShot(this, "SFX_freewheel", "Opponents");
    rearLifted = rearCompression == 0.0f;

    int16_t wheels = airborneWheels;
    if (wheels != GetAirborneWheels()) {
        if (wheels == 0 && GetAirborneWheels() > 0)
            jumped = true;
        else if (airborneWheels != 0)
            jumped = false;
        airborneWheels = static_cast<int16_t>(GetAirborneWheels());
        if (jumped && throttle > 0.0f) {
            if (suspensionCompression[0] == 0.0f && suspensionCompression[1] == 0.0f)
                PlayOneShot(this, "SFX_rev1", "Opponents");
            else if (suspensionCompression[0] == 0.0f || suspensionCompression[1] == 0.0f)
                PlayOneShot(this, "SFX_rev2", "Opponents");
        }
    }
}

// FUNC_AT(0x0012d300)
void ATrafficVehicle::Destruct() {
    vtable = kATrafficVehicleVtable;
    if (countedB)
        TrafficCountB--;
    else if (countedA)
        TrafficCountA--;
    DeleteVoice(roadVoice);
    DeleteVoice(exhaustVoice);
    DeleteVoice(engineVoice);
    if (siren != NULL)
        siren->CallDelete(1);
    AVehicle::Destruct();
}

// FUNC_AT(0x0012d400)
ATrafficVehicle* ATrafficVehicle::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ATrafficVehicle));
    return this;
}

// FUNC_AT(0x0012d430)
void ATrafficVehicle::Play(ASoundPlayParams *params) {
    float frames = float(FramesSinceAudioUpdate);
    AVehicle::Play(params);
    double targetRpm = CalculateRpm();
    float gainTarget = 1.0f;
    if (surfaces[2] == 0 || surfaces[3] == 0)
        targetRpm = throttle > 0.0f ? 10000.0f : 0.0f;

    // The rpm moves by at most 200 a frame, falling at half that and dropping the gain's target to 1/8
    float rpmStep = frames * 200.0f;
    double rpmChange = targetRpm - engineRpm;
    if (rpmChange <= -rpmStep) {
        gainTarget = 0.125f;
        engineRpm = float(engineRpm - double(rpmStep) * 0.5f);
    } else if (rpmChange >= rpmStep) {
        engineRpm = float(double(rpmStep) + engineRpm);
    } else {
        engineRpm = float(targetRpm);
    }
    double gainStep = double(frames) * 0.12598425f;
    double gainChange = double(gainTarget) - engineGain;
    if (gainChange <= -gainStep)
        engineGain = float(engineGain - gainStep);
    else if (gainChange >= gainStep)
        engineGain = float(gainStep + engineGain);
    else
        engineGain = gainTarget;

    float enginePitch = float((double(engineRpm) + 4000.0f) * 0.00013888889f + pitchOffset);
    float level = engineOn ? float(GetMixedVolume() * engineGain) : 0.0f;
    int view = params->listener->view;
    engineVoice->Play(view, float((1.0f - double(params->facing) * 0.5f) * params->volume * level * 0.66f),
                      enginePitch * params->pitch, params->azimuth, 0.0f, 0.0f);
    exhaustVoice->Play(view, float((double(params->facing) * 0.5f + 1.0f) * params->volume * level * 0.66f),
                       enginePitch * params->pitch, params->azimuth, 0.0f, 0.0f);
    double road = 0.0;
    if (speed > 2.0f) {
        road = (double(speed) - 2.0f) * mix->GetVolume() * volume * 0.033333335f;
        if (road > 0.5f)
            road = 0.5f;
    }
    roadVoice->Play(view, float(road * params->volume), params->pitch, params->azimuth, 0.0f, 0.0f);
    if (vehicleType == kVehicleSoundSnowMobile)
        PlaySnowmobileSpecifics(enginePitch);
    if (siren != NULL) {
        if (sirenOn)
            siren->CallPlay(params);
        else
            CallStop(siren, params);
    }
}
