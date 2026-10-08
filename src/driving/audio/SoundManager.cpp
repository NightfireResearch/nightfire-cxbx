#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif
#pragma fp_contract(off)

#include "SoundManager.h"
#include "Fader.h"
#include "Stream.h"
#include "../data/Dafi.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"       // VU0_v3distancesquare, VU0_sqrt
#include "../platform/RealMemory.h"     // MEM_size, MEM_free
#include "../sound/snd/System.h"
#include "../../helpers.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// ASoundManager and ASystem: see SoundManager.h. The sound list holds every sound; the views a sound plays to are
// the listeners, and each sound keeps the volume each view last played it at, so that BuildPaths, Stop and Pause
// silence only what was heard.
// ---------------------------------------------------------------------------------------------------------------

// Not ported
#define AEngine_RemoveAll ((void (*)(void))0x0012f9e0)
// The play parameters for the sound as params->listener hears it (Ghidra: FUN_0012eab0; the name is ours)
#define SoundPlayParams_Compute ((ASoundPlayParams *(__fastcall *)(ASoundPlayParams *, int, ABaseSound *))0x0012eab0)
// "<directory><name>.<extension>" into the buffer (no '.' for an empty extension), answered
#define BuildFileName ((char *(__fastcall *)(char *, int, const char *, const char *, const char *))0x00051e90)

// The list code every std::list of pointers shares: the head node, _Buynode(next, prev, value), the destructor
#define SoundList_BuyHead ((PointerListNode *(__fastcall *)(ASoundList *, int))0x000b8490)
#define StreamNameList_BuyHead ((PointerListNode *(__fastcall *)(StreamNameList *, int))0x000b8490)

// The C runtime: its rand state, its output and formatting are the game's
#define CRT_rand ((int (*)(void))0x00133ee0)
#define CRT_printf ((int (*)(const char *, ...))0x00132192)
#define CRT_sprintf ((int (*)(char *, const char *, ...))0x00132767)

// NormalizedRandomNumber's second number, kept for the next call
struct NormalSpare {
    float value;                // +0x00
    uint8_t available;          // +0x04
};

#define fgFailedStreams (*(StreamNameList **)0x00243a5c)  // (this name and the next two ours)
#define fgInitialised BOOL8_AT(0x00243a76)
#define RandomSpare (*(NormalSpare *)0x00243a78)
#define SoundManagerUnknown243a6c U32_AT(0x00243a6c)    // zeroed by Stop
#define MusicStreamFile ((char *)0x00243a18)            // 64 bytes (name ours)

namespace {

constexpr float kRandToSigned = 0x1.0002p-14f;     // rand() * this - 1 is in [-1, 1]: 2 / 32767, as a float
static_assert(kRandToSigned == 2.0f / 32767.0f, "NormalizedRandomNumber's scale");

const unsigned int kAudioHeapSize = 0x40000;
const unsigned int kStreamBufferSize = 0x80000;
const uint16_t kRenderModeHardware = 0x420;      // bank voices: DirectSound buffers (docs/driving/sound.md 1.3)
const uint16_t kRenderModeMixer = 0x24;          // streams: the software mixer

// The banks banks.ini names, by slot (slot 8 is g_common.bnk's)
const char *const kBankKeys[kBankSlotCount] = {
    "Generic", "Collisions", "Smackables", "CarWeapons", "Weapons", "Ambience", "Misc", "Actors", NULL, "Engine",
};
const int kCommonBankSlot = 8;

// Each mix group and the mix it is scaled by, as Init sets them up
struct MixMaster {
    const char *mix;
    const char *master;
};
const MixMaster kMixMasters[] = {
    { "0:Fader2", "0:Fader1" },
    { "0:Fader1", "0:Fader0" },
    { "0:Fader0", "1:Effects" },
    { "Speech", "1:Effects" },
    { "Ambience", "0:Fader2" },
    { "Asphalt", "0:Fader2" },
    { "Blowout", "0:Fader2" },
    { "Booster", "0:Fader2" },
    { "Bullet Impact", "0:Fader2" },
    { "Bullet Ricochet", "0:Fader2" },
    { "Characters", "0:Fader2" },
    { "Collisions", "0:Fader2" },
    { "Explosions", "0:Fader2" },
    { "ExplosionBass", "0:Fader2" },
    { "Friendly", "0:Fader2" },
    { "Gadgets", "0:Fader2" },
    { "Gatling Gun", "0:Fader2" },
    { "Giotto", "0:Fader2" },
    { "Helicopter", "0:Fader2" },
    { "Henchmen Fire", "0:Fader2" },
    { "Horns", "0:Fader2" },
    { "Landings", "0:Fader1" },
    { "Machine Gun", "0:Fader2" },
    { "Menu Sounds", "1:Effects" },
    { "Music", "1:Music" },
    { "NIS", "0:Fader2" },
    { "Object Sounds", "0:Fader2" },
    { "OffRoad", "0:Fader2" },
    { "Opponents", "0:Fader2" },
    { "Player Car", "0:Fader2" },
    { "Player Grunts", "0:Fader1" },
    { "Player Wind", "0:Fader1" },
    { "POV Weapons", "0:Fader2" },
    { "Power Ups", "0:Fader2" },
    { "Rail", "0:Fader2" },
    { "Scrapes", "0:Fader2" },
    { "Shell Casings", "0:Fader2" },
    { "Skids", "0:Fader1" },
    { "Smackables", "0:Fader2" },
    { "Tank Treads", "0:Fader2" },
    { "Tracking System", "0:Fader2" },
    { "Traffic Cars", "0:Fader2" },
    { "Weapons Fire", "0:Fader2" },
    { "Weapons Init", "0:Fader2" },
    { "Weapons Reload", "0:Fader2" },
    { "Weapons Transit", "0:Fader2" },
    { "Wood", "0:Fader2" },
    { "World Sounds", "0:Fader2" },
};

// FLDLN2, FYL2X: the natural logarithm exactly as the x87 makes it (the C runtime's log need not agree in the last
// bit).
__declspec(naked) double LogX87(double) {
    __asm {
        fldln2
        fld qword ptr [esp + 4]
        fyl2x
        ret
    }
}

// The music and effects mixes back at the volumes the options set.
void RestoreOptionVolumes() {
    float music = MusicVolumeScale;
    AMix::Get("1:Music")->SetVolume(music);
    float effects = EffectsVolumeScale;
    AMix::Get("1:Effects")->SetVolume(effects);
}

// A new empty std::list from the pools
ASoundList* NewSoundList(const char *label) {
    ASoundList *list = static_cast<ASoundList *>(UMemory::FastAlloc(sizeof(ASoundList), label));
    if (list != NULL) {
        list->head = SoundList_BuyHead(list, 0);
        list->size = 0;
    }
    return list;
}

StreamNameList* NewStreamNameList(const char *label) {
    StreamNameList *list = static_cast<StreamNameList *>(UMemory::FastAlloc(sizeof(StreamNameList), label));
    if (list != NULL) {
        list->head = StreamNameList_BuyHead(list, 0);
        list->size = 0;
    }
    return list;
}

}  // namespace

// =============================================================================================================
// ASoundManager
// =============================================================================================================

// FUNC_AT(0x00120d80)
void ASoundManager::SetMissionOver() {
    if (ASystem_fgSystem != NULL)
        AStream::Get("Music")->FadeOut();
    ASoundManager_fgMissionOver = 1;
}

// FUNC_AT(0x00120db0)
double ASoundManager::NormalizedRandomNumber() {
    if (RandomSpare.available) {
        RandomSpare.available = 0;
        return RandomSpare.value;
    }
    float u, v;
    double radiusSquared;
    do {
        u = float(double(CRT_rand()) * kRandToSigned - 1.0f);
        double vUnrounded = double(CRT_rand()) * kRandToSigned - 1.0f;
        v = float(vUnrounded);
        radiusSquared = vUnrounded * v + double(u) * u;     // v's square from the unrounded and the stored v
    } while (radiusSquared >= 1.0f);    // a NaN ends the loop, as the original's test does
    float factor = VU0_sqrt(float(LogX87(radiusSquared) / radiusSquared * -2.0f));
    RandomSpare.available = 1;
    RandomSpare.value = float(double(v) * factor);
    return double(factor) * u;
}

// The parameters live across the sounds: a sound silenced gets the last computed +0x10 and +0x14, as in the
// original (whose first ones are its stack's; ours start at zero).
// FUNC_AT(0x00120e60)
void ASoundManager::BuildPaths(AListener *listener) {
    AFX::Update();
    ASoundPlayParams params = {};
    PointerListNode *node = fgSoundList->Begin();
    while (node != fgSoundList->head) {
        ABaseSound *sound = static_cast<ABaseSound *>(node->value);
        node = node->next;
        if (sound->paused)
            continue;
        float distanceSquared = VU0_v3distancesquare(&sound->position, &listener->position);
        params.listener = listener;
        float heard = 0.0f;
        bool audible = false;
        if ((sound->views & (1u << listener->unknown5c)) &&
            (sound->maxDistanceSq == 0.0f || distanceSquared < sound->maxDistanceSq)) {
            SoundPlayParams_Compute(&params, 0, sound);
            heard = params.volume;
            audible = params.volume > 0.0f;
        }
        if (audible) {
            sound->CallPlay(&params);
        } else if (sound->lastVolumes[listener->unknown5c] > 0.0f) {
            params.volume = 0.0f;
            params.pitch = 0.0f;
            params.azimuth = 0.0f;
            params.unknown0c = 0.0f;
            sound->CallPlay(&params);
        }
        sound->lastVolumes[listener->unknown5c] = heard;
    }
}

// The parameters' +0x10 and +0x14 are left as the original's stack had them; ours are zero.
// FUNC_AT(0x00120f90)
void ASoundManager::Stop() {
    ASoundPlayParams params = {};
    alignas(16) AListener listener;
    PointerListNode *node = fgSoundList->Begin();
    while (node != fgSoundList->head) {
        ABaseSound *sound = static_cast<ABaseSound *>(node->value);
        node = node->next;
        for (int view = 0; view < kSoundViewCount; view++) {
            if ((sound->views & (1u << view)) && sound->lastVolumes[view] > 0.0f) {
                params.listener = listener.Construct(view);
                params.volume = 0.0f;
                params.pitch = 0.0f;
                params.azimuth = 0.0f;
                params.unknown0c = 0.0f;
                sound->CallPlay(&params);
            }
        }
    }
    AVoice::PlayVoices();
    SoundManagerUnknown243a6c = 0;
}

// FUNC_AT(0x00121070)
void ASoundManager::Pause() {
    ASoundPlayParams params = {};
    alignas(16) AListener listener;
    PointerListNode *node = fgSoundList->Begin();
    while (node != fgSoundList->head) {
        ABaseSound *sound = static_cast<ABaseSound *>(node->value);
        node = node->next;
        for (int view = 0; view < kSoundViewCount; view++) {
            if ((sound->views & (1u << view)) && sound->lastVolumes[view] > 0.0f) {
                params.listener = listener.Construct(view);
                params.volume = sound->lastVolumes[view];
                params.pitch = 0.0f;
                params.azimuth = 0.0f;
                params.unknown0c = 0.0f;
                sound->CallPlay(&params);
            }
        }
        sound->paused = true;
    }
    AVoice::PlayVoices();
    AFX::Pause();
    ASoundManager_fgIsPaused = 1;
}

// FUNC_AT(0x00121150)
void ASoundManager::Resume() {
    for (PointerListNode *node = fgSoundList->Begin(); node != fgSoundList->head; node = node->next)
        static_cast<ABaseSound *>(node->value)->paused = false;
    AFX::Resume();
    ASoundManager_fgIsPaused = 0;
    RestoreOptionVolumes();
}

// FUNC_AT(0x001211d0)
void ASoundManager::StopSoundPos(const Coord3 *position) {
    CRT_printf("Looking for sound at (%.2f, %.2f, %.2f)\n", position->x, position->y, position->z);
    PointerListNode *node = fgSoundList->Begin();
    while (node != fgSoundList->head) {
        ABaseSound *sound = static_cast<ABaseSound *>(node->value);
        node = node->next;
        if (sound->position.x == position->x && sound->position.y == position->y &&
            sound->position.z == position->z) {
            CRT_printf("Deleting sound at (%.2f, %.2f, %.2f)\n", position->x, position->y, position->z);
            sound->CallDelete(1);
        }
    }
}

// FUNC_AT(0x00121280)
void ASoundManager::ClearMission() {
    AStream::Get("music")->Stop();
    AStream::Get("speech")->Stop();
    Stop();
    AEngine_RemoveAll();
    for (int slot = 0; slot < kBankSlotCount; slot++)
        fgBanks[slot]->Remove();
}

// FUNC_AT(0x00121320)
void ASoundManager::Restart() {
    for (int pass = 0; pass < 2; pass++) {
        PointerListNode *node = fgSoundList->Begin();
        while (node != fgSoundList->head) {
            ABaseSound *sound = static_cast<ABaseSound *>(node->value);
            node = node->next;
            if (sound->CallIsTransient())
                sound->CallDelete(1);
            else
                sound->paused = false;
        }
    }
    AFX::Resume();
    AMix::Reset(1);
    ASoundManager_fgIsPaused = 0;
    RestoreOptionVolumes();
    AMix::Get("0:Fader2")->SetVolume(1.0f);
    AMix::Get("0:Fader1")->SetVolume(1.0f);
    AMix::Get("0:Fader0")->SetVolume(1.0f);
    StreamNameList *failed = fgFailedStreams;
    PointerListNode *erased;
    failed->Erase(&erased, failed->head != NULL ? failed->head->next : NULL, failed->head);
    AStream::Get("Music")->Stop();
    AStream::Get("Speech")->Stop();
    AFader::Get("SpeechVsAmbience")->SetSecondary(true);
    ASoundManager_fgMissionOver = 0;
}

// FUNC_AT(0x00121470)
void ASoundManager::Init(const char *directory, bool loadBanks, const char *streamFile, const char *section,
                         int outputMode) {
    fgSoundList = NewSoundList("ASoundList");
    fgFailedStreams = NewStreamNameList("FailedStreams");
    fgActiveViews = 1u << kSoundViewShared;

    char mixFile[64];
    AMix::Load(BuildFileName(mixFile, 0, "data/audio/", section, "ini"));
    AMix::Add("1:Effects");
    AMix::Add("1:Music");
    AMix::Add("0:Fader2");
    AMix::Add("0:Fader1");
    AMix::Add("0:Fader0");
    AMix::Get("0:Fader2")->SetVolume(1.0f);
    AMix::Get("0:Fader1")->SetVolume(1.0f);
    AMix::Get("0:Fader0")->SetVolume(1.0f);
    AMix::Get("Speech")->SetVolume(1.0f);
    for (const MixMaster &entry : kMixMasters)
        AMix::Get(entry.mix)->SetMaster(entry.master);

    AFX::Init();
    ASystem::SetOpts(outputMode);
    ASoundManager_fgIsPaused = 0;
    if (loadBanks)
        ASystem::Init();
    if (directory != NULL)
        strcpy(AudioDirectory, directory);

    AStream::SetPath("driving/");
    CRT_sprintf(MusicStreamFile, "%s", streamFile);
    strncpy(MusicStreamFile + strlen(MusicStreamFile) - 2, "", 1);
    AStream::Create("Music", MusicStreamFile, kStreamBufferSize);
    AMix *fader = AMix::Get("0:Fader2");
    AStream *speech = AStream::Create("Speech", streamFile, kStreamBufferSize);
    AFader::Create("SpeechVsAmbience", speech, fader);

    if (loadBanks) {
        ABank::Load("g_common.bnk", kCommonBankSlot);
        char banksFile[64];
        char *text = static_cast<char *>(
            UFileLoader::FileLoad(BuildFileName(banksFile, 0, AudioDirectory, "banks.ini", ""), 0x100));
        DAFI *dafi = DAFI_open(text, MEM_size(text));
        if (dafi != NULL) {
            if (DAFI_setsection(dafi, section) < 0)
                DAFI_setsection(dafi, "default");
            for (int slot = 0; slot < kBankSlotCount; slot++) {
                if (kBankKeys[slot] == NULL)
                    continue;
                char *bank = DAFI_getvalue(dafi, kBankKeys[slot]);
                if (bank != NULL)
                    ABank::Load(bank, slot);
            }
        }
        if (dafi != NULL)
            DAFI_close(dafi);
        MEM_free(text);
    }

    RestoreOptionVolumes();
    FramesSinceAudioUpdate = 0;
    fgInitialised = 1;
}

// FUNC_AT(0x00121d00)
void ASoundManager::Shutdown() {
    fgInitialised = 0;
    while (fgSoundList->size != 0) {
        ABaseSound *last = static_cast<ABaseSound *>(fgSoundList->head->prev->value);
        if (last != NULL)
            last->CallDelete(1);
    }
    AFX::Shutdown();
    ASystem::Shutdown();
    AMix::Clear();
    ASoundList *sounds = fgSoundList;
    if (sounds != NULL) {
        sounds->Destruct();
        UMemory::FastFree(sounds, sizeof(ASoundList));
    }
    fgSoundList = NULL;
    StreamNameList *failed = fgFailedStreams;
    if (failed != NULL) {
        failed->Destruct();
        UMemory::FastFree(failed, sizeof(StreamNameList));
    }
    fgFailedStreams = NULL;
}

// FUNC_AT(0x00121d90)
void StreamNameList::IncreaseSize(uint32_t count) {
    PointerList::IncreaseSize(count);
}

// FUNC_AT(0x00121e40)
void ASoundManager::ReportFailure(const char *name) {
    StreamNameList *list = fgFailedStreams;
    PointerListNode *end = list->head;
    void *value = const_cast<char *>(name);     // the list holds untyped pointers
    PointerListNode *node = list->BuyNode(end, end->prev, &value);
    list->IncreaseSize(1);
    end->prev = node;
    node->prev->next = node;
}

// =============================================================================================================
// ASystem
// =============================================================================================================

// SNDSYSI_init is given a third argument, 0x72a00, which it does not read.
// FUNC_AT(0x00128350)
ASystem* ASystem::Construct() {
    heap = NULL;
    ASystem_fgSystem = this;
    SNDSYS_vectortoreal();
    heap = UMemory::Alloc(kAudioHeapSize, 0, "Audio Heap");
    SNDSYSI_init(heap, kAudioHeapSize);
    return this;
}

// FUNC_AT(0x00128390)
void ASystem::Init() {
    if (ASystem_fgSystem == NULL) {
        ASystem *system = static_cast<ASystem *>(UMemory::FastAlloc(sizeof(ASystem), "ASystem"));
        if (system != NULL)
            system->Construct();
    }
}

// FUNC_AT(0x001283f0)
void ASystem::SetOpts(int outputMode) {
    SND::SysOpts opts;
    SNDSYS_getopts(&opts);
    opts.set.numRenderModes = 2;
    opts.set.renderModes[0] = kRenderModeHardware;
    opts.set.renderModes[1] = kRenderModeMixer;
    switch (outputMode) {
    case 0:
        opts.set.outputMode = 1;
        break;
    case 1:
        opts.set.outputMode = 2;
        break;
    }
    SNDSYS_setops(&opts);
}

// FUNC_AT(0x00128470)
void ASystem::Shutdown() {
    ASystem *system = ASystem_fgSystem;
    if (system == NULL)
        return;
    if (system->heap != NULL) {
        SNDSYS_restore();
        UMemory::Free(system->heap);
        system->heap = NULL;
    }
    UMemory::FastFree(system, sizeof(ASystem));
    ASystem_fgSystem = NULL;
}
