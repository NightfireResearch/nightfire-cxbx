#include "VehicleSound.h"
#include "../Vtables.h"   // our copies of the game's vtables

#pragma fp_contract(off)

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <bit>

#include "BondCarState.h"               // CarSurface
#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Index.h"             // AIndex
#include "../audio/SoundManager.h"      // FramesSinceAudioUpdate
#include "../engine/InputConfig.h"      // BuildFileName
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"       // REAL_random, VU0_v3length
#include "../platform/RealSystem.h"     // TIMER_gettick
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AVehicle and AHorn (0x0011e0f0..0x00120d80), ported from the listings. See VehicleSound.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address


// ---- globals

// TIMER_gettick before which each of PlayCollision's impact sounds may not play again. Kept at the game's address:
// the searches step past a busy entry without a bound, so a run of busy entries can read past the twelve into the
// globals after them, as the original does.
// XBE_GLOBAL(0x00243988, 0x30)
int32_t CollisionTicks[12];
enum { kCollisionSounds = 12 };

// XBE_GLOBAL(0x00243984, 0x4)
uint32_t MetalGateCount;

namespace {

const uint32_t kAVehicleVtable = uint32_t(reinterpret_cast<uintptr_t>(Vtable_AVehicle));

enum { kVoiceBytes = 0x58 };

// The bank slots
enum { kCommonBank = 0, kVehicleBank = 1, kSmackablesBank = 2 };

// An impact sound for a strength above `threshold` (PlayCollision's tables, built on its stack in the original)
struct ImpactSound {
    float threshold;
    const char *patch;
};

const ImpactSound kImpacts[kCollisionSounds] = {
    { 0.05f, "SFX_Cobble1" }, { 0.1f, "SFX_Cobble2" }, { 0.15f, "SFX_Light1" }, { 0.2f, "SFX_Light2" },
    { 0.25f, "SFX_Light3" },  { 0.3f, "SFX_Light4" },  { 0.35f, "SFX_Med1" },   { 0.4f, "SFX_Med2" },
    { 0.45f, "SFX_Med3" },    { 0.5f, "SFX_Med4" },    { 0.55f, "SFX_Heavy1" }, { 0.6f, "SFX_Heavy2" },
};
// A submersible's
const ImpactSound kSubmersibleImpacts[10] = {
    { 0.05f, "SFX_Light1" }, { 0.1f, "SFX_Light2" }, { 0.2f, "SFX_Light3" }, { 0.25f, "SFX_Light4" },
    { 0.3f, "SFX_Med1" },    { 0.35f, "SFX_Med2" },  { 0.4f, "SFX_Med3" },   { 0.45f, "SFX_Med4" },
    { 0.5f, "SFX_Heavy1" },  { 0.55f, "SFX_Heavy2" },
};
// A submersible's on dirt
const ImpactSound kSubmersibleDirtImpacts[3] = {
    { 0.2f, "SFX_sandbump1" }, { 0.35f, "SFX_sandbump2" }, { 0.5f, "SFX_sandbump3" },
};

// AVoice's destructor as the compiler inlines it: its views', last first (the C runtime's vector destructor
// iterator in the original)
void DestroyVoiceViews(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
}

// A voice from the pools playing the bank's patch `name` in `mix`
AVoice *NewVoice(AMix *mix, ABank *bank, const char *name) {
    AVoice *voice = static_cast<AVoice *>(UMemory::FastAlloc(kVoiceBytes, "AVoice"));
    if (voice == NULL)
        return NULL;
    return voice->Construct(mix, bank->handle, bank->index.Lookup(name));
}

void DeleteVoice(AVoice *voice) {
    if (voice != NULL) {
        DestroyVoiceViews(voice);
        UMemory::FastFree(voice, kVoiceBytes);
    }
}

// A one-shot sound of the bank's patch, left to play once
void PlayOneShot(ABank *bank, const char *patch, const Coord3 *position, const Coord3 *velocity, float volume,
                 const char *mixName) {
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    if (block != NULL)
        static_cast<AOneShotSound *>(block)->Construct(bank->handle, bank->index.Lookup(patch), position, velocity, volume,
                                                       mixName);
}

// A level moved towards `target` by at most `step`, landing on the target (as computed) when within it
float Approach(float level, double target, double step) {
    double difference = target - level;
    if (difference <= -step)
        return (float)(level - step);
    if (difference >= step)
        return (float)(step + level);
    return (float)target;
}

// The view of the listener the parameters are for
int ListenerViewOf(const ASoundPlayParams *params) {
    return params->listener->view;
}

// A voice played for the listener at the parameters' pitch and azimuth
void PlayVoice(AVoice *voice, const ASoundPlayParams *params, float volume) {
    voice->Play(ListenerViewOf(params), volume, params->pitch, params->azimuth, 0.0f, 0.0f);
}

// The scrape's level target: while its timer is above 3 frames, by speed up to 15
double ScrapeTarget(int32_t &timer, int frames, float speed) {
    double target = 0.0;
    if (timer > 0) {
        timer -= frames;
        if (timer < 0)
            timer = 0;
        if (timer > 3)
            target = speed < 15.0f ? double(speed) * (1.0f / 15.0f) : 1.0;
    }
    return target;
}
static_assert(std::bit_cast<uint32_t>(1.0f / 15.0f) == 0x3d888889, "the scrapes' scale is the float 1/15");

// The rumbles' and bumps' scale by speed, up to 5
float RumbleSpeedFactor(float speed) {
    return speed < 5.0f ? (float)(speed * 0.2f) : 1.0f;
}

}  // namespace

// =============================================================================================================
// AHorn
// =============================================================================================================

// FUNC_AT(0x0011e0f0)
AHorn* AHorn::Construct(const char *patch, float volume, float pitch) {
    this->volume = volume;
    this->pitch = pitch;
    mix = AMix::Add("Horns");
    ABank *bank = fgBanks[kCommonBank];
    voice.Construct(mix, bank->handle, bank->index.Lookup(patch));
    honks = 0;
    honkTime = 0;
    pause = 0;
    return this;
}

// FUNC_AT(0x0011e150)
void AHorn::Play(bool honk, ASoundPlayParams *params) {
    if (honkTime != 0) {
        if (--honkTime == 0) {
            if (honks == 0)
                pause = REAL_random() % 240 + 120;
            else
                pause = REAL_random() % 30 + 5;
        }
    } else if (honk) {
        if (honks != 0)
            honks--;
        else
            honks = REAL_random() % 3 + 3;
        if (pause != 0)
            pause--;
        else
            honkTime = REAL_random() % 60 + 5;
    }
    double level = honkTime != 0 ? mix->GetVolume() * volume : 0.0;
    voice.Play(ListenerViewOf(params), (float)(level * params->volume), params->pitch * pitch, params->azimuth,
               0.0f, 0.0f);
}

// =============================================================================================================
// AVehicle
// =============================================================================================================

// FUNC_AT(0x0011e230)
void AVehicle::Reset() {
    throttle = 0.0f;
    rpm = 0.0f;
    speed = 0.0f;
    rpmWobble = 0.0f;
    rpmWobbleDirection = 1.0f;
    frontSlip = 0.0f;
    rearSlip = 0.0f;
    frontSlipTarget = 0.0f;
    rearSlipTarget = 0.0f;
    for (int i = 0; i < kVehicleRumbleCount; i++)
        rumbleLevels[i] = 0.0f;
    for (int i = 0; i < kVehicleBumpCount; i++)
        bumpDistances[i] = 0.0f;
    bumpToggles[kBumpWood] = false;
    bumpToggles[kBumpRail] = false;
    hornHonk = false;
    boost = 0;
    engineOn = 0;
    for (int i = 0; i < kVehicleScrapeCount; i++) {
        scrapeTimers[i] = 0;
        scrapeLevels[i] = 0.0f;
    }
    terrain = 0;
}

// FUNC_AT(0x0011e2e0)
AVehicle* AVehicle::Construct(const char *mixName, int skidVoices) {
    ASceneObj::Construct(mixName);
    vtable = kAVehicleVtable;
    skidMultiple = 1.0f;
    terrain = 0;
    gun = NULL;
    turret = NULL;
    zoom = NULL;
    bank = fgBanks[kVehicleBank];
    unknown1d0 = NULL;
    carAlarm = NULL;
    wind = NULL;
    horn = NULL;
    asphaltMix = AMix::Get("Asphalt");
    offRoadMix = AMix::Get("OffRoad");
    skidsMix = AMix::Get("Skids");
    scrapesMix = AMix::Get("Scrapes");
    Reset();

    ABank *vehicleBank = bank;
    ABank *common = fgBanks[kCommonBank];
    voices[kVoiceOffRoad] = NewVoice(offRoadMix, common, "SFX_OffRN");
    voices[kVoiceGravel] = NewVoice(offRoadMix, common, "SFX_GravelRN");
    voices[kVoiceCobble] = NewVoice(offRoadMix, common, "SFX_CobbleRN");
    voices[kVoiceGrassSkidFront] = NewVoice(skidsMix, common, "SFX_GrassSkid");
    voices[kVoiceGrassSkidBack] = NewVoice(skidsMix, common, "SFX_GrassSkid");
    voices[kVoiceOffRoadSkidFront] = NewVoice(skidsMix, common, "SFX_OffRoadSkid");
    voices[kVoiceOffRoadSkidBack] = NewVoice(skidsMix, common, "SFX_OffRoadSkid");
    voices[kVoiceIceSkidFront] = NewVoice(skidsMix, common, "SFX_iceskidfront");
    voices[kVoiceIceSkidBack] = NewVoice(skidsMix, common, "SFX_iceskidback");
    voices[kVoiceScrapeMetal] = NewVoice(scrapesMix, vehicleBank, "SFX_ScrapeMetal");
    voices[kVoiceScrapeRock] = NewVoice(scrapesMix, vehicleBank, "SFX_ScrapeRock");
    voices[kVoiceScrapeSand] = NewVoice(scrapesMix, vehicleBank, "SFX_scrapesand");
    bumpPatches[kBumpWood][0] = bank->index.Lookup("SFX_Cobble1");
    bumpPatches[kBumpWood][1] = bank->index.Lookup("SFX_Cobble2");
    bumpPatches[kBumpRail][0] = bank->index.Lookup("SFX_Railbump1");
    bumpPatches[kBumpRail][1] = bank->index.Lookup("SFX_Railbump2");
    if (skidVoices == kVehicleSkids) {
        squeals = false;
        voices[kVoiceSkidFront] = NewVoice(skidsMix, common, "SFX_SkidFront1");
        voices[kVoiceSkidBack] = NewVoice(skidsMix, common, "SFX_SkidBack1");
    } else {
        squeals = true;
        voices[kVoiceSkidFront] = NewVoice(skidsMix, vehicleBank, "SFX_SquealFront1");
        voices[kVoiceSkidBack] = NewVoice(skidsMix, vehicleBank, "SFX_SquealBack1");
    }
    for (int wheel = 0; wheel < 4; wheel++) {
        surfaces[wheel] = 1;
        previousSurfaces[wheel] = 1;
    }
    return this;
}

// FUNC_AT(0x0011e9a0)
void AVehicle::ActivateGun(const char *mixName) {
    if (gun == NULL) {
        AGun *block = static_cast<AGun *>(::OperatorNew(sizeof(AGun)));
        gun = block != NULL ? block->Construct(this, mixName) : NULL;
    }
}

// FUNC_AT(0x0011ea10)
void AVehicle::ActivateTurret() {
    if (turret == NULL) {
        ATurret *block = static_cast<ATurret *>(UMemory::FastAlloc(sizeof(ATurret), "ATurret"));
        turret = block != NULL ? block->Construct() : NULL;
    }
}

// FUNC_AT(0x0011ea80)
void AVehicle::ActivateZoom() {
    if (zoom == NULL) {
        AZoomObj *block = static_cast<AZoomObj *>(UMemory::FastAlloc(sizeof(AZoomObj), "AZoomObj"));
        zoom = block != NULL ? block->Construct(this) : NULL;
    }
}

// FUNC_AT(0x0011eaf0)
void AVehicle::ActivateWind() {
    if (wind == NULL) {
        AVehicleWind *block = static_cast<AVehicleWind *>(::OperatorNew(sizeof(AVehicleWind)));
        wind = block != NULL ? block->Construct(this) : NULL;
    }
}

// FUNC_AT(0x0011eb50)
void AVehicle::CalculateSpeed() {
    float length = VU0_v3length(&velocity);
    if (horn != NULL) {
        // Between 5 and 14, 14 (a NaN goes through)
        if (length > 5.0f && length < 14.0f)
            length = 14.0f;
        float step = (float)(double(FramesSinceAudioUpdate) * 0.1f);
        length = Approach(speed, length, step);
    }
    speed = length;
}

// FUNC_AT(0x0011ebf0)
int AVehicle::GetAirborneWheels() {
    int airborne = 0;
    for (int wheel = 0; wheel < 4; wheel++) {
        if (previousSurfaces[wheel] == 0)
            airborne++;
    }
    return airborne;
}

// FUNC_AT(0x0011ec10)
void AVehicleVoicePair::Destruct() {
    for (int i = 2; i-- > 0;)          // the C runtime's vector destructor iterator, a voice at a time
        DestroyVoiceViews(&voices[i]);
}

namespace {

#if NIGHTFIRE_X87
// FLDLN2, FYL2X: the natural logarithm exactly as the x87 makes it (the C runtime's log need not agree in the last
// bit).
__declspec(naked) double VehicleLogX87(double) {
    __asm {
        fldln2
        fld qword ptr [esp + 4]
        fyl2x
        ret
    }
}
#else
double VehicleLogX87(double x) {
    return log(x);
}
#endif

}  // namespace

// FUNC_AT(0x0011ec80)
double AVehicle::CalculateRpm() {
    if (CallIsTracked()) {
        if (speed > 1.0f)
            return VehicleLogX87(speed) * 1000.0f;
        return 0.0;
    }
    if (horn != NULL) {
        // Up a gear every 15 m/s, to the ninth
        double gearTop;
        int gear = 1;
        for (;;) {
            gearTop = double(gear) * 15.0f;
            if (gearTop > speed)
                break;
            if (++gear >= 10)
                break;
        }
        return double(speed) * 5000.0f / gearTop + rpmWobble;
    }
    return double(rpmWobble) + rpm;
}

// FUNC_AT(0x0011ed30)
void AVehicle::PlayLanding(float, const Coord3 *position, const Coord3 *) {
    ABank *vehicleBank = fgBanks[kVehicleBank];
    bool paved = false, wood = false, snow = false;
    for (int wheel = 0; wheel < 4; wheel++) {
        int surface = surfaces[wheel];
        if (surface == kWOOD)
            wood = true;
        else if (surface == kPAVED)
            paved = true;
        else if (surface == kICE || surface == kSNOW)
            snow = true;
    }
    double force = double(landingForce) + 0.1f;
    float volume = (float)force;
    if (force > 1.0)
        volume = 1.0f;

    if (int16_t(GetAirborneWheels()) <= 0)
        return;
    if (landingForce >= 0.5f) {
        switch (GetAirborneWheels()) {      // jump table 0x0011f184: 0, 1 / 2, 3 / 4
        case 0:
        case 1:
            PlayOneShot(vehicleBank, "SFX_TiresHeavy", position, &velocity, volume, "Landings");
            break;
        case 2:
        case 3:
            PlayOneShot(vehicleBank, "SFX_TiresHeavy", position, &velocity, volume, "Landings");
            PlayOneShot(vehicleBank, "SFX_Light4", position, &velocity, volume, "Landings");
            break;
        case 4:
            PlayOneShot(vehicleBank, "SFX_TiresHeavy", position, &velocity, volume, "Landings");
            PlayOneShot(vehicleBank, "SFX_Med3", position, &velocity, volume, "Landings");
            break;
        }
    } else if (paved) {
        PlayOneShot(vehicleBank, "SFX_TiresHeavy", position, &velocity, volume, "Landings");
    } else if (wood) {
        PlayOneShot(vehicleBank, "SFX_TiresWood", position, &velocity, volume, "Landings");
    } else if (snow) {
        PlayOneShot(vehicleBank, "SFX_TiresSnow", position, &velocity, volume, "Landings");
    } else {
        PlayOneShot(vehicleBank, "SFX_TiresHeavy", position, &velocity, volume, "Landings");
    }
}

// FUNC_AT(0x0011f1a0)
void AVehicle::PlayCollision(float strength, const Coord3 *position, const Coord3 *velocity, int surface) {
    if (surface == kDIRT) {
        if (scrapeTimers[kScrapeSand] < 10)
            scrapeTimers[kScrapeSand] += 3;
    } else if (surface == kMETAL) {
        if (scrapeTimers[kScrapeMetal] < 10)
            scrapeTimers[kScrapeMetal] += 3;
    } else {
        if (scrapeTimers[kScrapeRock] < 10)
            scrapeTimers[kScrapeRock] += 3;
    }
    // Nothing more while a scrape goes on
    if (scrapeTimers[kScrapeMetal] > 3 || scrapeTimers[kScrapeRock] > 3 || scrapeTimers[kScrapeSand] > 3)
        return;

    if (surface == kPAVED) {
        if (surfaces[0] != 0 || (surfaces[1] != 0 && surfaces[2] != 0) || surfaces[3] != 0) {
            PlayOneShot(bank, "SFX_TiresHeavy", position, &this->velocity, 1.0f, "Landings");
            return;
        }
    } else if (surface == kMETAL && vehicleType != kVehicleSoundSubmersible) {
        strength = strength + 0.35f;
    }
    if (strength < 0.05f && CollisionTicks[0] > 0)
        return;

    if (squeals) {
        ABank *smackables = fgBanks[kSmackablesBank];
        void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
        if (block != NULL) {
            float volume = speed < 5.0f ? (float)(speed * 0.2f) : 1.0f;
            const char *gate = (MetalGateCount++ & 1) != 0 ? "SFX_MetalGate3" : "SFX_MetalGate2";
            static_cast<AOneShotSound *>(block)->Construct(smackables->handle, smackables->index.Lookup(gate), position, velocity,
                                                           volume, "Smackables");
        }
    }

    const ImpactSound *table;
    int count;
    const char *mixName = "Collisions";
    if (vehicleType == kVehicleSoundSubmersible) {
        if (surface == kDIRT) {
            table = kSubmersibleDirtImpacts;
            count = 3;
        } else {
            table = kSubmersibleImpacts;
            count = 10;
        }
    } else if (vehicleType == kVehicleSoundSnowMobile) {
        return;
    } else {
        table = kImpacts;
        count = kCollisionSounds;
        if (squeals)
            mixName = "Smackables";
    }
    // The hardest impact the strength passes (up to the last), then the next one free
    int i = 0;
    while (i < count - 1 && strength > table[i].threshold)
        i++;
    while (CollisionTicks[i] > TIMER_gettick())
        i++;
    if (i >= count || !(double(strength) + 0.3f > table[i].threshold))
        return;
    CollisionTicks[i] = TIMER_gettick() + 5;
    void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
    if (block != NULL) {
        float volume = speed < 2.0f ? (float)(speed * 0.5f) : 1.0f;
        static_cast<AOneShotSound *>(block)->Construct(bank->handle, bank->index.Lookup(table[i].patch), position, velocity,
                                                       volume, mixName);
    }
}

// FUNC_AT(0x0011f9b0)
void AVehicle::Play(ASoundPlayParams *params) {
    // (the base's Play first: an empty function, 0x00017550)
    CalculateSpeed();
    int frameCount = FramesSinceAudioUpdate;
    float frames = (float)frameCount;
    float step = (float)(double(frameCount) * 0.04f);

    double wobble = double(frames) * rpmWobbleDirection * 5.0f + rpmWobble;
    rpmWobble = (float)wobble;
    if (wobble > 150.0f)
        rpmWobbleDirection = -1.0f;
    else if (wobble < -150.0f)
        rpmWobbleDirection = 1.0f;

    // The scrapes
    static const VehicleVoice kScrapeVoices[kVehicleScrapeCount] = { kVoiceScrapeRock, kVoiceScrapeMetal,
                                                                      kVoiceScrapeSand };
    for (int scrape = 0; scrape < kVehicleScrapeCount; scrape++) {
        double target = ScrapeTarget(scrapeTimers[scrape], frameCount, speed);
        scrapeLevels[scrape] = Approach(scrapeLevels[scrape], target, step);
        PlayVoice(voices[kScrapeVoices[scrape]], params,
                  (float)(scrapesMix->GetVolume() * scrapeLevels[scrape] * params->volume));
    }

    // The skids
    frontSlip = Approach(frontSlip, frontSlipTarget, step);
    float frontSkid = (float)(skidsMix->GetVolume() * frontSlip * skidMultiple);
    rearSlip = Approach(rearSlip, rearSlipTarget, step);
    float rearSkid = (float)(skidsMix->GetVolume() * skidMultiple * rearSlip);
    int front0 = surfaces[0], front1 = surfaces[1], rear0 = surfaces[2], rear1 = surfaces[3];

    bool on = front0 == kPAVED || front1 == kPAVED || front0 == kCOBBLE || front1 == kCOBBLE;
    PlayVoice(voices[kVoiceSkidFront], params, on ? frontSkid * params->volume : 0.0f);
    on = rear0 == kPAVED || rear1 == kPAVED || rear0 == kCOBBLE || rear1 == kCOBBLE;
    PlayVoice(voices[kVoiceSkidBack], params, on ? rearSkid * params->volume : 0.0f);

    on = front0 == kGRASS || front1 == kGRASS || front0 == kPAVED_ROUGH || front1 == kPAVED_ROUGH;
    PlayVoice(voices[kVoiceGrassSkidFront], params, on ? frontSkid * params->volume : 0.0f);
    on = rear0 == kGRASS || rear1 == kGRASS || rear0 == kPAVED_ROUGH || rear1 == kPAVED_ROUGH;
    if (on)
        voices[kVoiceGrassSkidBack]->Play(ListenerViewOf(params), rearSkid * params->volume, params->pitch * 0.75f,
                                          params->azimuth, 0.0f, 0.0f);
    else
        PlayVoice(voices[kVoiceGrassSkidBack], params, 0.0f);

    on = front0 == kDIRT || front0 == kGRAVEL || front0 == kRAILROAD || front1 == kDIRT || front1 == kGRAVEL ||
         front1 == kRAILROAD;
    PlayVoice(voices[kVoiceOffRoadSkidFront], params, on ? frontSkid * params->volume : 0.0f);
    on = rear0 == kDIRT || rear0 == kGRAVEL || rear0 == kRAILROAD || rear1 == kDIRT || rear1 == kGRAVEL ||
         rear1 == kRAILROAD;
    if (on)
        voices[kVoiceOffRoadSkidBack]->Play(ListenerViewOf(params), rearSkid * params->volume,
                                            params->pitch * 0.75f, params->azimuth, 0.0f, 0.0f);
    else
        PlayVoice(voices[kVoiceOffRoadSkidBack], params, 0.0f);

    on = front0 == kICE || front1 == kICE || front0 == kSNOW || front1 == kSNOW;
    PlayVoice(voices[kVoiceIceSkidFront], params, on ? frontSkid * params->volume : 0.0f);
    on = rear0 == kICE || rear1 == kICE || rear0 == kSNOW || rear1 == kSNOW;
    PlayVoice(voices[kVoiceIceSkidBack], params, on ? rearSkid * params->volume : 0.0f);

    // The rumbles, and which bumps play. The off-road target stays unrounded, the others are rounded at each sum.
    bool onWood = false, onRails = false;
    double offRoad = 0.0;
    float gravel = 0.0f, cobble = 0.0f;
    for (int wheel = 0; wheel < 4; wheel++) {
        switch (surfaces[wheel]) {
        case kWOOD:
            onWood = true;
            break;
        case kRAILROAD:
            onRails = true;
            gravel = (float)(gravel + 0.15f);
            break;
        case kDIRT:
            gravel = (float)(gravel + 0.15f);
            break;
        case kGRAVEL:
            gravel = (float)(gravel + 0.25f);
            break;
        case kCOBBLE:
            cobble = (float)(cobble + 0.25f);
            break;
        case kGRASS:
            offRoad = offRoad + 0.15f;
            break;
        case kPAVED_ROUGH:
            offRoad = offRoad + 0.25f;
            break;
        }
    }
    rumbleLevels[kRumbleOffRoad] = Approach(rumbleLevels[kRumbleOffRoad], offRoad, step);
    float factor = RumbleSpeedFactor(speed);
    PlayVoice(voices[kVoiceOffRoad], params,
              (float)(offRoadMix->GetVolume() * factor * rumbleLevels[kRumbleOffRoad] * params->volume));
    rumbleLevels[kRumbleGravel] = Approach(rumbleLevels[kRumbleGravel], gravel, step);
    factor = RumbleSpeedFactor(speed);
    double mixVolume = offRoadMix->GetVolume();
    PlayVoice(voices[kVoiceGravel], params,
              (float)(mixVolume * (double(factor) * rumbleLevels[kRumbleGravel]) * params->volume));
    rumbleLevels[kRumbleCobble] = Approach(rumbleLevels[kRumbleCobble], cobble, step);
    factor = RumbleSpeedFactor(speed);
    PlayVoice(voices[kVoiceCobble], params,
              (float)(offRoadMix->GetVolume() * factor * rumbleLevels[kRumbleCobble] * params->volume));

    // The bumps: every 150 (wood) or 250 (rails) covered above 5 m/s, the two patches by turns
    float excess = (float)(speed - 5.0f);
    if (onWood && excess > 0.0f) {
        double covered = double(speed) * frames + bumpDistances[kBumpWood];
        bumpDistances[kBumpWood] = (float)covered;
        if (covered > 150.0f) {
            void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
            if (block != NULL) {
                float volume;
                int patch;
                if (bumpToggles[kBumpWood]) {
                    volume = excess < 20.0f ? (float)(excess * 0.05f) : 1.0f;
                    patch = bumpPatches[kBumpWood][1];
                } else {
                    volume = excess < 5.0f ? (float)(excess * 0.2f) : 1.0f;
                    patch = bumpPatches[kBumpWood][0];
                }
                static_cast<AOneShotSound *>(block)->Construct(bank->handle, patch, &position, &this->velocity, volume, "Wood");
            }
            bumpDistances[kBumpWood] = (float)(bumpDistances[kBumpWood] - 150.0f);
            bumpToggles[kBumpWood] = !bumpToggles[kBumpWood];
        }
    }
    if (onRails && excess > 0.0f) {
        double covered = double(speed) * frames + bumpDistances[kBumpRail];
        bumpDistances[kBumpRail] = (float)covered;
        if (covered > 250.0f) {
            void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
            if (block != NULL) {
                float volume;
                int patch;
                if (bumpToggles[kBumpRail]) {
                    volume = excess < 20.0f ? (float)(excess * 0.05f) : 1.0f;
                    patch = bumpPatches[kBumpRail][1];
                } else {
                    volume = excess < 5.0f ? (float)(excess * 0.2f) : 1.0f;
                    patch = bumpPatches[kBumpRail][0];
                }
                static_cast<AOneShotSound *>(block)->Construct(bank->handle, patch, &position, &this->velocity, volume, "Rail");
            }
            bumpDistances[kBumpRail] = (float)(bumpDistances[kBumpRail] - 250.0f);
            bumpToggles[kBumpRail] = !bumpToggles[kBumpRail];
        }
    }

    // The wind while airborne, at one to two from the listener, or on a snowmobile
    if (wind != NULL) {
        float distance = params->distance;
        bool airborne = surfaces[0] == 0 && surfaces[1] == 0 && surfaces[2] == 0 && surfaces[3] == 0;
        if (airborne || (distance >= 1.0f && distance <= 2.0f) || vehicleType == kVehicleSoundSnowMobile)
            wind->Play(params, frames);
        else
            wind->Stop(params);
    }
    if (horn != NULL) {
        horn->Play(hornHonk, params);
        hornHonk = false;
    }
    if (gun != NULL)
        gun->Play(firing, params, frames);
    if (turret != NULL)
        turret->Play(turningSideways, turningUpDown, servo, params);
    if (zoom != NULL)
        zoom->Play(params);
    if (carAlarm != NULL)
        carAlarm->Play(ListenerViewOf(params), params->volume, params->pitch, params->azimuth, 0.0f, 0.0f);
}

// FUNC_AT(0x00120960)
void AVehicle::ChooseHorn(const char *name, float volume, float pitch) {
    if (horn != NULL) {
        DestroyVoiceViews(&horn->voice);
        ::OperatorDelete(horn);
    }
    AHorn *block = static_cast<AHorn *>(::OperatorNew(sizeof(AHorn)));
    if (block != NULL) {
        char patch[0x40];
        horn = block->Construct(BuildFileName(patch, 0, "SFX_", name, ""), volume, pitch);
    } else {
        horn = NULL;
    }
}

// FUNC_AT(0x00120a10)
void AVehicle::Destruct() {
    vtable = kAVehicleVtable;
    if (horn != NULL) {
        DestroyVoiceViews(&horn->voice);
        ::OperatorDelete(horn);
    }
    if (gun != NULL) {
        gun->Destruct();
        ::OperatorDelete(gun);
    }
    if (carAlarm != NULL) {
        DestroyVoiceViews(carAlarm);
        ::OperatorDelete(carAlarm);
    }
    if (wind != NULL) {
        wind->Destruct();
        ::OperatorDelete(wind);
    }
    if (unknown1d0 != NULL) {
        unknown1d0->Destruct();
        ::OperatorDelete(unknown1d0);
    }
    if (turret != NULL) {
        turret->Destruct();
        UMemory::FastFree(turret, sizeof(ATurret));
    }
    if (zoom != NULL) {
        zoom->Destruct();
        UMemory::FastFree(zoom, sizeof(AZoomObj));
    }
    static const VehicleVoice kOrder[kVehicleVoiceCount] = {
        kVoiceSkidBack, kVoiceSkidFront, kVoiceGrassSkidBack, kVoiceGrassSkidFront, kVoiceOffRoadSkidBack,
        kVoiceOffRoadSkidFront, kVoiceOffRoad, kVoiceGravel, kVoiceCobble, kVoiceScrapeMetal, kVoiceScrapeRock,
        kVoiceScrapeSand, kVoiceIceSkidFront, kVoiceIceSkidBack,
    };
    for (int i = 0; i < kVehicleVoiceCount; i++)
        DeleteVoice(voices[kOrder[i]]);
    ASceneObj::Destruct();
}

// FUNC_AT(0x00120d50)
AVehicle* AVehicle::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        ABaseSound::OperatorDelete(this, sizeof(AVehicle));
    return this;
}
