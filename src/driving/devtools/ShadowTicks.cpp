#include "ShadowTicks.h"
#include "OwnedGlobals.h"
#include "StandaloneTraps.h"
#include "../EventManager.hpp"
#include "../sound/snd/SndGlobals.h"
#include "CarpShadow.h"
#include "DebugVarShadow.h"
#include "AttribShadow.h"
#include "ColQueryShadow.h"
#include "ColListShadow.h"
#include "ColGridShadow.h"
#include "WorldShadow.h"
#include "TriggerShadow.h"
#include "RoadNetShadow.h"
#include "RoadNavShadow.h"
#include "TargetShadow.h"
#include "PhysObjShadow.h"
#include "RigidBasicsShadow.h"
#include "RigidCoreShadow.h"
#include "RigidResolveShadow.h"
#include "RigidCollideShadow.h"
#include "RenderShadow.h"
#include "GeoPrimShadow.h"
#include "ViewCamShadow.h"
#include "CamDataShadow.h"
#include "PlayerCamShadowA.h"
#include "PlayerCamShadowB.h"
#include "PlayerCamShadowC.h"
#include "GeomShadow.h"
#include "AudioMgrShadow.h"
#include "AudioMixShadow.h"
#include "AudioStreamShadow.h"
#include "AudioVoiceShadow.h"
#include "AudioIndexShadow.h"
#include "ActorShadow.h"
#include "CharacterShadow.h"
#include "IKShadow.h"
#include "ActModelShadow.h"
#include "PoserShadow.h"
#include "AnimEngineShadow.h"
#include "RendererShadow.h"
#include "LightShadow.h"
#include "SceneObjShadow.h"
#include "TexContextShadow.h"
#include "ReflectionShadow.h"
#include "RenderFxShadow.h"
#include "ParticleShadow.h"
#include "InputShadow.h"
#include "BondCarShadowA.h"
#include "BondCarShadowB.h"
#include "BondCarShadowC.h"
#include "BondCarShadowD.h"
#include "GameAudioShadowA.h"
#include "GameAudioShadowB.h"
#include "GameAudioShadowC.h"
#include "GameAudioShadowD.h"
#include "GameEffectsShadowA.h"
#include "GameEffectsShadowB.h"
#include "GameEffectsShadowC.h"
#include "GameEffectsShadowD.h"
#include "GameWeaponsShadowA.h"
#include "GameWeaponsShadowB.h"
#include "GameWeaponsShadowC.h"
#include "GameWeaponsShadowD.h"
#include "GameMissionsShadowA.h"
#include "GameMissionsShadowB.h"
#include "GameMissionsShadowC.h"
#include "GameMissionsShadowD.h"
#include "GameEventsShadowA.h"
#include "GameEventsShadowB.h"
#include "GameEventsShadowC.h"
#include "GameEventsShadowD.h"
#include "GameAIShadowA.h"
#include "GameAIShadowB.h"
#include "GameAIShadowC.h"
#include "GameAIShadowD.h"
#include "GameAIShadowE.h"
#include "GameAIShadowF.h"
#include "GameFrontEndShadowA.h"
#include "GameFrontEndShadowB.h"
#include "GameFrontEndShadowC.h"
#include "GameFrontEndShadowD.h"
#include "CRuntimeShadowA.h"
#include "CRuntimeShadowB.h"
#include "VehicleShadow.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---- the vehicle shadows' footprint on the game's queues
// What a shadow leaves behind that the next ticks act on: events still queued (they run right after this, in
// Scheduler::Run), sounds in the sound list, and the sound driver's free DirectSound buffers (152 ADPCM and 28
// PCM16 when idle; FUN_0013d550 faults once both run out). Printed around each vehicle shadow and, while anything
// changes, for the ticks after them. Events a vehicle shadow raised are test artefacts: they are dropped.

#define SOUND_LIST              OwnedGlobals_Ours(0x00243a58)   // -> fgSoundList (MSVC std::list: allocator, head, size)

struct QueueState {
    uint32_t events;        // bytes queued
    uint32_t sounds;
    int32_t freeAdpcm, freePcm16;
};

static QueueState ReadQueues(void) {
    QueueState q;
    q.events = gCreationPoint != NULL && gDeletionPoint != NULL ? uint32_t(gCreationPoint - gDeletionPoint) : 0;
    uint32_t *list = *(uint32_t **)SOUND_LIST;
    q.sounds = list != NULL ? list[2] : 0;
    q.freeAdpcm = FreeBufferLists[SND::kPoolAdpcm].count;
    q.freePcm16 = FreeBufferLists[SND::kPoolPcm16].count;
    return q;
}

static void PrintQueues(const char *when, const QueueState &q) {
    printf("[vshadowq] %s: %u event bytes queued, %u sounds, free buffers %d ADPCM + %d PCM16\n", when, q.events,
           q.sounds, q.freeAdpcm, q.freePcm16);
    fflush(stdout);
}

static bool AnyVehicleShadow(void) {
    static const char *const kNames[] = { "NIGHTFIRE_BONDCARSHADOWA", "NIGHTFIRE_BONDCARSHADOWB",
                                          "NIGHTFIRE_BONDCARSHADOWC", "NIGHTFIRE_BONDCARSHADOWD",
                                          "NIGHTFIRE_VEHICLESHADOW" };
    for (const char *name : kNames) {
        char value[16] = "";
        DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
        if (length != 0 && length < sizeof(value) && atoi(value) != 0)
            return true;
    }
    return false;
}

// One vehicle shadow, its leftover events dropped and its footprint printed
static void RunVehicleShadow(const char *name, void (*run)(void)) {
    char *creation = gCreationPoint;
    run();
    QueueState after = ReadQueues();
    char when[96];
    snprintf(when, sizeof(when), "after %s (%d event bytes it left dropped)", name, int(gCreationPoint - creation));
    gCreationPoint = creation;
    after.events = ReadQueues().events;
    PrintQueues(when, after);
}

// The ticks after the vehicle shadows, while the footprint changes
static void WatchQueues(void) {
    static int ticks = -1;
    static QueueState last;
    if (ticks < 0) {
        if (!AnyVehicleShadow()) {
            ticks = 0;
            return;
        }
        ticks = 200;
        last = ReadQueues();
        return;
    }
    if (ticks == 0)
        return;
    ticks--;
    QueueState now = ReadQueues();
    if (memcmp(&now, &last, sizeof(now)) == 0)
        return;
    last = now;
    char when[32];
    snprintf(when, sizeof(when), "tick +%d", 200 - ticks);
    PrintQueues(when, now);
}

void ShadowTicks_FirstTick(void) {
    // The data-layer shadows need the game started (UMemory, the static initialisers, a mounted archive):
    // the first simulation tick is the first point after GameLoop_StartUp that devtools code sees.
    CarpShadow_Run();   // NIGHTFIRE_CARPSHADOW=1 only
    DebugVarShadow_Run();   // NIGHTFIRE_DBVARSHADOW=1 only
    AttribShadow_Run();   // NIGHTFIRE_ATTRIBSHADOW=1 only
    ColQueryShadow_Run();   // NIGHTFIRE_COLQUERYSHADOW=1 only
    ColListShadow_Run();   // NIGHTFIRE_COLLISTSHADOW=1 only
    ColGridShadow_Run();   // NIGHTFIRE_COLGRIDSHADOW=1 only
    GeomShadow_RunWorld();   // NIGHTFIRE_GEOMSHADOW=1 only
    WorldShadow_Run();   // NIGHTFIRE_WORLDSHADOW=1 only
    TriggerShadow_Run();   // NIGHTFIRE_TRIGGERSHADOW=1 only
    RoadNetShadow_Run();   // NIGHTFIRE_ROADNETSHADOW=1 only
    RoadNavShadow_Run();   // NIGHTFIRE_ROADNAVSHADOW=1 only
    TargetShadow_Run();   // NIGHTFIRE_TARGETSHADOW=1 only
    PhysObjShadow_Run();   // NIGHTFIRE_PHYSOBJSHADOW=1 only
    RigidBasicsShadow_Run();   // NIGHTFIRE_RIGIDBASICSSHADOW=1 only
    RigidCoreShadow_Run();   // NIGHTFIRE_RIGIDCORESHADOW=1 only
    RigidResolveShadow_Run();   // NIGHTFIRE_RIGIDRESOLVESHADOW=1 only
    RigidCollideShadow_Run();   // NIGHTFIRE_RIGIDCOLLIDESHADOW=1 only
    RenderShadow_Run();   // NIGHTFIRE_RENDERSHADOW=1 only
    GeoPrimShadow_Run();   // NIGHTFIRE_GEOPRIMSHADOW=1 only
    ViewCamShadow_Run();   // NIGHTFIRE_VIEWCAMSHADOW=1 only
    CamDataShadow_Run();   // NIGHTFIRE_CAMDATASHADOW=1 only
    PlayerCamShadowA_Run();   // NIGHTFIRE_PLAYERCAMSHADOWA=1 only
    PlayerCamShadowB_Run();   // NIGHTFIRE_PLAYERCAMSHADOWB=1 only
    PlayerCamShadowC_Run();   // NIGHTFIRE_PLAYERCAMSHADOWC=1 only
    AudioMgrShadow_Run();   // NIGHTFIRE_AUDIOMGRSHADOW=1 only
    AudioMixShadow_Run();   // NIGHTFIRE_AUDIOMIXSHADOW=1 only
    AudioStreamShadow_Run();   // NIGHTFIRE_AUDIOSTREAMSHADOW=1 only
    AudioVoiceShadow_Run();   // NIGHTFIRE_AUDIOVOICESHADOW=1 only
    AudioIndexShadow_Run();   // NIGHTFIRE_AUDIOINDEXSHADOW=1 only
    ActorShadow_Run();   // NIGHTFIRE_ACTORSHADOW=1 only
    CharacterShadow_Run();   // NIGHTFIRE_CHARACTERSHADOW=1 only
    IKShadow_Run();   // NIGHTFIRE_IKSHADOW=1 only
    ActModelShadow_Run();   // NIGHTFIRE_ACTMODELSHADOW=1 only
    PoserShadow_Run();   // NIGHTFIRE_POSERSHADOW=1 only
    AnimEngineShadow_Run();   // NIGHTFIRE_ANIMENGINESHADOW=1 only
    RendererShadow_Run();   // NIGHTFIRE_RENDERERSHADOW=1 only
    LightShadow_Run();   // NIGHTFIRE_LIGHTSHADOW=1 only
    SceneObjShadow_Run();   // NIGHTFIRE_SCENEOBJSHADOW=1 only
    TexContextShadow_Run();   // NIGHTFIRE_TEXCONTEXTSHADOW=1 only
    ReflectionShadow_Run();   // NIGHTFIRE_REFLECTIONSHADOW=1 only
    RenderFxShadow_Run();   // NIGHTFIRE_RENDERFXSHADOW=1 only
    ParticleShadow_Run();   // NIGHTFIRE_PARTICLESHADOW=1 only
    InputShadow_Run();   // NIGHTFIRE_INPUTSHADOW=1 only
    GameAudioShadowA_Run();   // NIGHTFIRE_GAMEAUDIOSHADOWA=1 only
    GameAudioShadowB_Run();   // NIGHTFIRE_GAMEAUDIOSHADOWB=1 only
    GameAudioShadowC_Run();   // NIGHTFIRE_GAMEAUDIOSHADOWC=1 only
    GameAudioShadowD_Run();   // NIGHTFIRE_GAMEAUDIOSHADOWD=1 only
    GameEffectsShadowA_Run();   // NIGHTFIRE_GAMEEFFECTSSHADOWA=1 only
    GameEffectsShadowB_Run();   // NIGHTFIRE_GAMEEFFECTSSHADOWB=1 only
    GameEffectsShadowC_Run();   // NIGHTFIRE_GAMEEFFECTSSHADOWC=1 only
    GameEffectsShadowD_Run();   // NIGHTFIRE_GAMEEFFECTSSHADOWD=1 only
    GameWeaponsShadowA_Run();   // NIGHTFIRE_GAMEWEAPONSSHADOWA=1 only
    GameWeaponsShadowB_Run();   // NIGHTFIRE_GAMEWEAPONSSHADOWB=1 only
    GameWeaponsShadowC_Run();   // NIGHTFIRE_GAMEWEAPONSSHADOWC=1 only
    GameWeaponsShadowD_Run();   // NIGHTFIRE_GAMEWEAPONSSHADOWD=1 only
    GameMissionsShadowA_Run();  // NIGHTFIRE_GAMEMISSIONSSHADOWA=1 only
    GameMissionsShadowB_Run();  // NIGHTFIRE_GAMEMISSIONSSHADOWB=1 only
    GameMissionsShadowC_Run();  // NIGHTFIRE_GAMEMISSIONSSHADOWC=1 only
    GameMissionsShadowD_Run();  // NIGHTFIRE_GAMEMISSIONSSHADOWD=1 only
    GameEventsShadowA_Run();    // NIGHTFIRE_GAMEEVENTSSHADOWA=1 only
    GameEventsShadowB_Run();    // NIGHTFIRE_GAMEEVENTSSHADOWB=1 only
    GameEventsShadowC_Run();    // NIGHTFIRE_GAMEEVENTSSHADOWC=1 only
    GameEventsShadowD_Run();    // NIGHTFIRE_GAMEEVENTSSHADOWD=1 only
    GameAIShadowA_Run();        // NIGHTFIRE_GAMEAISHADOWA=1 only
    GameAIShadowB_Run();        // NIGHTFIRE_GAMEAISHADOWB=1 only
    GameAIShadowC_Run();        // NIGHTFIRE_GAMEAISHADOWC=1 only
    GameAIShadowD_Run();        // NIGHTFIRE_GAMEAISHADOWD=1 only
    GameAIShadowE_Run();        // NIGHTFIRE_GAMEAISHADOWE=1 only
    GameAIShadowF_Run();        // NIGHTFIRE_GAMEAISHADOWF=1 only
    GameFrontEndShadowA_Run();  // NIGHTFIRE_GAMEFRONTENDSHADOWA=1 only
    GameFrontEndShadowB_Run();  // NIGHTFIRE_GAMEFRONTENDSHADOWB=1 only
    GameFrontEndShadowC_Run();  // NIGHTFIRE_GAMEFRONTENDSHADOWC=1 only
    GameFrontEndShadowD_Run();  // NIGHTFIRE_GAMEFRONTENDSHADOWD=1 only
    CRuntimeShadowA_Run();  // NIGHTFIRE_CRUNTIMESHADOWA=1 only
    CRuntimeShadowB_Run();  // NIGHTFIRE_CRUNTIMESHADOWB=1 only
    StandaloneTraps_FirstTick();   // NIGHTFIRE_TRAP_DATA=1 only: after the shadows
    if (AnyVehicleShadow()) {
        PrintQueues("before the vehicle shadows", ReadQueues());
        RunVehicleShadow("bondcarA", BondCarShadowA_Run);   // NIGHTFIRE_BONDCARSHADOWA=1 only
        RunVehicleShadow("bondcarB", BondCarShadowB_Run);   // NIGHTFIRE_BONDCARSHADOWB=1 only
        RunVehicleShadow("bondcarC", BondCarShadowC_Run);   // NIGHTFIRE_BONDCARSHADOWC=1 only
        RunVehicleShadow("bondcarD", BondCarShadowD_Run);   // NIGHTFIRE_BONDCARSHADOWD=1 only
        RunVehicleShadow("vehicle", VehicleShadow_Run);     // NIGHTFIRE_VEHICLESHADOW=1 only
    }
}

void ShadowTicks_Tick(void) {
    // NIGHTFIRE_VEHICLESHADOW=1 only: the helicopter tests again once one is live (events it raises dropped)
    char *creation = gCreationPoint;
    VehicleShadow_Tick();
    BondCarShadowB_Tick();   // NIGHTFIRE_BONDCARSHADOWB=3 only: the live submarine/spline comparison
    BondCarShadowA_Tick();   // NIGHTFIRE_BONDCARSHADOWA=3 only: the live snowmobile physics comparison
    if (gCreationPoint != creation) {
        printf("[vshadowq] the live helicopter tests left %d event bytes: dropped\n", int(gCreationPoint - creation));
        gCreationPoint = creation;
    }
    WatchQueues();   // any vehicle shadow on: the event queue, sound list and sound buffers as they change
}
