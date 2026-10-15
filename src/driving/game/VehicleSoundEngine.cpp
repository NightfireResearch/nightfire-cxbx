#include "VehicleSoundEngine.h"
#include "../Vtables.h"   // our copies of the game's vtables

#pragma fp_contract(off)

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <bit>

#include "../audio/Bank.h"              // ABank, fgBanks
#include "../audio/Index.h"             // AIndex
#include "../audio/SoundManager.h"      // FramesSinceAudioUpdate
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"       // rrandom, atan_turns, SineTurns, the VU0 vector functions
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// AVehicleWind, the play parameters' computation, AEngine, URefCounter<AEngine>'s tree and ARaceEngine
// (0x0012e830..0x001306d0), ported from the listings. See VehicleSound.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

// AOneShotSound's constructor
// AVoice's destructor, out of line (Ghidra: FUN_0003f400)

namespace {

void **const kAEngineVtable = Vtable_AEngine;
void **const kARaceEngineVtable = Vtable_ARaceEngine;

constexpr uint32_t kVoiceBytes = sizeof(AVoice);

// The bank slots
enum { kWindBank = 4, kEngineBank = 9 };

// The listeners' views (AListener::view)
enum { kViewCentred = 2 };

int ListenerViewOf(const ASoundPlayParams *params) {
    return params->listener->view;
}

// The azimuth a view hears a sound at
float ViewAzimuth(int view, float azimuth) {
    if (view == kViewCentred)
        return azimuth;
    return (view & 1) != 0 ? 0.25f : -0.25f;
}

// AVoice's destructor as the compiler inlines it: its views', last first (the C runtime's vector destructor
// iterator in the original)
void DestroyVoiceViews(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
}

AVoice *NewVoice(AMix *mix, ABank *bank, const char *name) {
    AVoice *voice = static_cast<AVoice *>(UMemory::FastAlloc(kVoiceBytes, "AVoice"));
    if (voice == NULL)
        return NULL;
    return voice->Construct(mix, bank->handle, bank->index.Lookup(name));
}

}  // namespace

// =============================================================================================================
// AVehicleWind
// =============================================================================================================

// FUNC_AT(0x0012e830)
AVehicleWind* AVehicleWind::Construct(AVehicle *vehicle) {
    this->vehicle = vehicle;
    mix = AMix::Get("Player Wind");
    voice = NULL;
    volume = 1.0f;
    azimuth = 0.5f;
    voice = NewVoice(mix, fgBanks[kWindBank], "SFX_windy");
    levelRate = -0.1f;
    level = 0.4f;
    pitchRate = -0.1f;
    pitch = 1.0f;
    return this;
}

// FUNC_AT(0x0012e8f0)
void AVehicleWind::Play(ASoundPlayParams *params, float frames) {
    double newLevel = double(frames) * levelRate + level;
    level = (float)newLevel;
    pitch = (float)(double(frames) * pitchRate + pitch);
    if (newLevel > 1.0f)
        levelRate = (float)(rrandom() * -0.02 - 0.002);
    if (level < 0.4f)
        levelRate = (float)(rrandom() * 0.02 + 0.002);
    if (pitch > 2.8f)
        pitchRate = (float)(rrandom() * -0.01 - 0.001);
    if (pitch < 1.0f)
        pitchRate = (float)(rrandom() * 0.03 + 0.001);

    // Louder from 5 m/s, fully at 10
    float speedFactor = 0.0f;
    if (vehicle->speed > 5.0f) {
        double factor = (double(vehicle->speed) - 5.0f) * 0.2f;
        speedFactor = (float)factor;
        if (factor > 1.0f)
            speedFactor = 1.0f;
    }
    double loudness = mix->GetVolume() * volume * speedFactor * level;
    float voicePitch = params->pitch * pitch;
    voice->Play(ListenerViewOf(params), (float)(loudness * params->volume), voicePitch, azimuth, 0.0f, 0.0f);
}

// FUNC_AT(0x0012ea30)
void AVehicleWind::Stop(ASoundPlayParams *params) {
    voice->Play(ListenerViewOf(params), params->volume * 0.0f, params->pitch, azimuth, 0.0f, 0.0f);
}

// FUNC_AT(0x0012ea60)
void AVehicleWind::Destruct() {
    if (voice != NULL) {
        voice->Destruct();
        UMemory::FastFree(voice, kVoiceBytes);
    }
}

// =============================================================================================================
// The play parameters
// =============================================================================================================

// FUNC_AT(0x0012ea80)
float ListenerView::Azimuth(float azimuth) {
    return ViewAzimuth(view, azimuth);
}

// FUNC_AT(0x0012eab0)
ASoundPlayParams* ASoundPlayParams::Compute(ABaseSound *sound) {
    float maxDistance = sound->maxDistance;
    if (maxDistance == 0.0f) {
        // Heard at any distance: at full volume, unshifted
        volume = 1.0f;
        pitch = 1.0f;
        azimuth = ViewAzimuth(listener->view, 0.0f);
        facing = 0.0f;
        unknown10 = 0.0f;
        return this;
    }

    // A listener between the level and the sound (or the other way round) hears it scaled down across the band
    // (AListener::level and bandWidth)
    AListener *hearer = listener;
    float level = hearer->level;
    float band = hearer->bandWidth;
    float scale = 1.0f;
    if (level < sound->position.y && level > hearer->position.y)
        scale = (float)((double(band) + hearer->position.y) * (1.0f / double(band)));
    double belowLevel = double(level) - band;
    if (belowLevel > sound->position.y && belowLevel < hearer->position.y)
        scale = (float)((1.0f / double(band)) * -hearer->position.y);
    if (scale < 0.001f) {
        volume = 0.0f;
        return this;
    }

    Coord4 at = { sound->position.x, sound->position.y, sound->position.z, 1.0f };
    Coord4 relative;
    VU0_MATRIX4_vect4mult(&at, &hearer->matrix, &relative);
    float distance = VU0_v3length(&relative);
    this->distance = distance;
    float minDistance = sound->minDistance;
    if (distance > maxDistance) {
        volume = 0.0f;
        return this;
    }
    if (distance >= minDistance)
        volume = (float)((double(maxDistance) - distance) /
                         ((double(distance) - minDistance) * 8.0f + (double(maxDistance) - minDistance)) * scale);
    else
        volume = scale;

    if (distance > 0.0f) {
        if (double(minDistance) * 0.5f > distance) {
            azimuth = 0.0f;
        } else {
            float side = ViewAzimuth(hearer->view, atan_turns(relative.x, relative.z));
            azimuth = side;
            // Closer than the minimum distance: towards the middle
            if (distance < minDistance)
                azimuth = (float)((double(distance) + distance - minDistance) / minDistance * side);
        }
        // The Doppler shift, from the sound's and the listener's speeds along the line between them
        Coord4 toSound;
        VU0_v4sub4(&at, &hearer->position, &toSound);
        VU0_v4unitxyz(&toSound, &toSound);
        facing = v3dotprod(&toSound, &sound->forward);
        float approach = (float)(double(v3dotprod(&toSound, &sound->velocity)) + 400.0f);
        pitch = (float)(400.0f / (double(approach) - v3dotprod(&toSound, &hearer->velocity)));
        if (distance < 25.0f)
            unknown10 = 0.0f;
        else
            unknown10 = (float)((double(distance) - 25.0f) * 0.0025f);
    } else {
        pitch = 1.0f;
        azimuth = ViewAzimuth(hearer->view, 0.0f);
        facing = 0.0f;
        unknown10 = 0.0f;
    }
    return this;
}

// =============================================================================================================
// AEngine
// =============================================================================================================

// FUNC_AT(0x0012edd0)
AEngine* AEngine::Construct() {
    vtable = kAEngineVtable;
    return this;
}

// FUNC_AT(0x0012ede0)
void AEngine::Play(ASoundPlayParams *) {
    AVehicle *car = vehicle;
    float gas = car->throttle;
    double frames = FramesSinceAudioUpdate;
    float revs = (float)(double(car->rpmWobble) + car->rpm);
    float rpmStep = (float)(150.0f * frames);
    double throttleStep = frames * 0.05f;

    // The rpm follows the vehicle's (less the rear wheels' slip), or races or idles with a driving wheel off the
    // ground; falling fast it drops at half the step and the throttle eases off
    double target;
    if (car->surfaces[2] == 0 || car->surfaces[3] == 0)
        target = gas > 0.0f ? 10000.0f : 0.0f;
    else
        target = revs - double(car->rearSlip) * 1000.0f;
    double difference = target - engineRpm;
    if (difference <= -rpmStep) {
        engineRpm = (float)(engineRpm - double(rpmStep) * 0.5f);
        throttleStep = throttleStep * 3.0f;
        gas = gas * 0.125f;
    } else {
        engineRpm = (float)(difference * 0.1f + engineRpm);
    }

    double throttleDifference = double(gas) - engineThrottle;
    if (throttleDifference <= -throttleStep)
        engineThrottle = (float)(engineThrottle - throttleStep);
    else if (throttleDifference >= throttleStep)
        engineThrottle = (float)(throttleStep + engineThrottle);
    else
        engineThrottle = gas;

    pitch = (float)((double(engineRpm) + 2000.0f) * rpmScale);
    loadVolume = VU0_sqrt(engineThrottle);
    idleVolume = VU0_sqrt(1.0f - engineThrottle);
}

// FUNC_AT(0x0012ef20)
void AEngine::Destruct() {
    vtable = kAEngineVtable;
}

// FUNC_AT(0x0012ef30)
AEngine* AEngine::Delete(unsigned int flags) {
    vtable = kAEngineVtable;
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x0012f940)
AEngine* AEngine::Get(const char *name, AVehicle *vehicle, float rate) {
    AEngine *engine = EngineRefCounter::Get()->GetReference(name);
    if (engine == NULL) {
        ARaceEngine *block = static_cast<ARaceEngine *>(OperatorNew(sizeof(ARaceEngine)));
        engine = block != NULL ? block->Construct() : NULL;
        EngineRefCounter::Get()->AddReference(name, engine);
    }
    engine->vehicle = vehicle;
    engine->engineRpm = 0.0f;
    engine->engineThrottle = 0.0f;
    engine->revTime = 0.0f;
    engine->rpmScale = 1.0f / rate;
    return engine;
}

// FUNC_AT(0x0012f9e0)
void AEngine::RemoveAll() {
    while (EngineRefCounter::Get()->head->left != EngineRefCounter::Get()->head) {
        AEngine *engine = static_cast<AEngine *>(EngineRefCounter::Get()->head->left->value.entry.object);
        if (EngineRefCounter::Get()->RemoveReference(engine) && engine != NULL)
            engine->CallDelete(1);
    }
}

// ---- URefCounter<AEngine>'s tree: its compiled copies of RefCounterTree's code (URefCounter.cpp)

// FUNC_AT(0x0012ef50)
RefCounterPair* RefCounterPair::Construct(const char *key, const RefCounterEntry *entry) {
    memcpy(name, key, sizeof(name));
    this->entry = *entry;
    return this;
}

// FUNC_AT(0x0012ef80)
void EngineRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x0012efd0)
RefCounterNode** EngineRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x0012f340)
RefCounterNode** EngineRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0012f530)
RefCounterNode** EngineRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x0012f620)
RefCounterInsertResult* EngineRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x0012f7e0)
void EngineRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x0012f820)
void EngineRefTree::Destruct() {
    Destroy();
}

// =============================================================================================================
// ARaceEngine
// =============================================================================================================

namespace {

const char *const kEngineVoiceNames[kRaceEngineVoices][2] = {
    { "SFX_IN-idle", "SFX_OUT-idle" },
    { "SFX_IN-lowload", "SFX_OUT-lowload" },
    { "SFX_IN-midload", "SFX_OUT-midload" },
    { "SFX_IN-hiload", "SFX_OUT-hiload" },
    { "SFX_IN-cruz", "SFX_OUT-cruz" },
};

constexpr float kOneOver3000 = 1.0f / 3000.0f;
static_assert(std::bit_cast<uint32_t>(kOneOver3000) == 0x39aec33e, "the rpm's scale is the float 1/3000");

}  // namespace

// FUNC_AT(0x0012fc60)
ARaceEngine* ARaceEngine::Construct() {
    AEngine::Construct();
    vtable = kARaceEngineVtable;
    AMix *mix = AMix::Get("Player Car");
    ABank *bank = fgBanks[kEngineBank];
    for (int side = 0; side < 2; side++) {
        for (int voice = 0; voice < kRaceEngineVoices; voice++)
            voices[voice][side] = NewVoice(mix, bank, kEngineVoiceNames[voice][side]);
    }
    return this;
}

// FUNC_AT(0x0012ffb0)
void ARaceEngine::Play(ASoundPlayParams *params) {
    AEngine::Play(params);
    ABank *bank = fgBanks[kEngineBank];
    float frames = (float)FramesSinceAudioUpdate;

    // Revving from 0.2: a vacuum sound at the start, and over half throttle a waver of the pitch that dies away,
    // for 60 frames
    if (engineThrottle < 0.2f) {
        revTime = 0.0f;
    } else if (revTime < 60.0f) {
        if (revTime == 0.0f) {
            void *block = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
            if (block != NULL)
                static_cast<AOneShotSound *>(block)->Construct(bank->handle, bank->index.Lookup("SFX_vaccuum"),
                                                               &vehicle->position, &vehicle->velocity, 0.1f,
                                                               "Player Car");
        }
        double time = double(frames) + revTime;
        revTime = (float)time;
        if (engineThrottle > 0.5f) {
            double phase = time * 0.1f;
            while (phase > 1.0f)
                phase -= 1.0f;
            pitch = (float)(SineTurns((float)phase) * (2.0f / (double(revTime) + 2.0f)) * 0.3f + pitch);
        }
    }

    if (engineRpm < 2000.0f) {
        double scaled = engineRpm * double(kOneOver3000);
        float fraction = (float)scaled;
        if (scaled < 0.0)
            fraction = 0.0f;
        idleBlend = VU0_sqrt(1.0f - fraction);
        loadBlend = VU0_sqrt(fraction);
    } else {
        idleBlend = 0.0f;
        loadBlend = 1.0f;
    }

    // The inside and outside sounds, crossfaded between one and two from the listener
    float distance = params->distance;
    AVehicle *car = vehicle;
    float volumes[2];
    bool both = false;
    if (distance > 2.0f) {
        volumes[0] = 0.0f;
        double outside = car->mix->GetVolume() * car->volume;
        volumes[1] = (float)outside;
        both = outside != 0.0;
    } else if (distance > 1.0f) {
        float blend = distance - 1.0f;
        float inside = (float)(car->mix->GetVolume() * car->volume);
        volumes[0] = (float)(double(VU0_sqrt(1.0f - blend)) * inside);
        car = vehicle;
        float out = (float)(car->mix->GetVolume() * car->volume);
        double outside = double(VU0_sqrt(blend)) * out;
        volumes[1] = (float)outside;
        both = outside != 0.0;
    } else {
        volumes[0] = (float)(car->mix->GetVolume() * car->volume);
    }

    int view = ListenerViewOf(params);
    if (both) {
        // Both sets, each at its volume, the idle a little higher, panned as the listener hears the car
        for (int side = 0; side < 2; side++) {
            float sideVolume = volumes[side];
            voices[kEngineIdle][side]->Play(view, (float)(double(idleVolume) * params->volume * sideVolume * idleBlend),
                                            (float)(double(params->pitch) * pitch * 1.1f), params->azimuth, 0.0f,
                                            0.0f);
            float voicePitch = params->pitch * pitch;
            voices[kEngineLowLoad][side]->Play(view, (float)(double(loadVolume) * params->volume * sideVolume),
                                               voicePitch, params->azimuth, 0.0f, 0.0f);
            voices[kEngineMidLoad][side]->Play(
                view, (float)(double(loadVolume) * loadBlend * params->volume * sideVolume), voicePitch,
                params->azimuth, 0.0f, 0.0f);
            voices[kEngineHighLoad][side]->Play(
                view, (float)(double(loadVolume) * loadBlend * params->volume * sideVolume), voicePitch,
                params->azimuth, 0.0f, 0.0f);
            voices[kEngineCruise][side]->Play(
                view, (float)(double(idleVolume) * loadBlend * params->volume * sideVolume), voicePitch,
                params->azimuth, 0.0f, 0.0f);
        }
        return;
    }

    // The inside volume only: each IN patch centred, its OUT patch to one side three quarters as loud
    float v = volumes[0];
    float voicePitch = params->pitch * pitch;
    double volume = double(idleVolume) * params->volume * v * idleBlend;
    voices[kEngineIdle][0]->Play(view, (float)volume, voicePitch, 0.0f, 0.0f, 0.0f);
    voices[kEngineIdle][1]->Play(view, (float)(volume * 0.75f), voicePitch, 0.5f, 0.0f, 0.0f);
    volume = double(loadVolume) * params->volume * v;
    voices[kEngineLowLoad][0]->Play(view, (float)volume, voicePitch, 0.0f, 0.0f, 0.0f);
    voices[kEngineLowLoad][1]->Play(view, (float)(volume * 0.75f), voicePitch, 0.5f, 0.0f, 0.0f);
    volume = double(loadVolume) * loadBlend * params->volume * v;
    voices[kEngineMidLoad][0]->Play(view, (float)volume, voicePitch, 0.0f, 0.0f, 0.0f);
    voices[kEngineMidLoad][1]->Play(view, (float)(volume * 0.75f), voicePitch, 0.5f, 0.0f, 0.0f);
    voices[kEngineHighLoad][0]->Play(view, (float)volume, voicePitch, 0.0f, 0.0f, 0.0f);
    voices[kEngineHighLoad][1]->Play(view, (float)(volume * 0.75f), voicePitch, 0.5f, 0.0f, 0.0f);
    volume = double(idleVolume) * loadBlend * params->volume * v;
    voices[kEngineCruise][0]->Play(view, (float)volume, voicePitch, 0.0f, 0.0f, 0.0f);
    voices[kEngineCruise][1]->Play(view, (float)(volume * 0.75f), voicePitch, 0.5f, 0.0f, 0.0f);
}

// FUNC_AT(0x00130590)
void ARaceEngine::Destruct() {
    vtable = kARaceEngineVtable;
    for (int side = 0; side < 2; side++) {
        for (int voice = 0; voice < kRaceEngineVoices; voice++) {
            AVoice *each = voices[voice][side];
            if (each != NULL) {
                DestroyVoiceViews(each);
                UMemory::FastFree(each, kVoiceBytes);
            }
        }
    }
    AEngine::Destruct();
}

// FUNC_AT(0x001306b0)
ARaceEngine* ARaceEngine::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}
