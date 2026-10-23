// Shadow tests for the security system (game/sp/SSys.cpp) and the monitors (game/obj/Monitor.cpp): each function
// runs, the original and ours, on the same made-up countdowns, cameras, monitors and switch channels, with the calls
// that need a game in play (sounds, the camera, the HUD, input, explosions) going to the fakes of ShadowHarness.cpp.
// Run at start with MenuShadowTests=on (MenuProbe.cpp).
//
// SSys_Monitor runs a few hundred frames on each case, so the countdowns go through every message, the cut-short
// ones and the item 2 delay the time demos never reach. Monitor_Update runs on monitors in every state, with cameras
// disabled, the button pressed and shots landing.

#include "SecurityShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../engine/Script.h"
#include "../engine/viewer.h"
#include "../game.h"
#include "../game/obj/Monitor.h"
#include "../game/obj/Sensor.h"
#include "../game/sp/SSys.h"
#include "../game/sp/SwitchChannels.h"

#include <stdio.h>
#include <string.h>

namespace {

const unsigned kSSysInit = 0x000cf7b0;
const unsigned kSSysMsg = 0x000cf7f0;
const unsigned kSSysMonitor = 0x000cf8d0;
const unsigned kSSysRamLoad = 0x000cfb40;
const unsigned kMonitorCreate = 0x000d0f10;
const unsigned kMonitorActivate = 0x000d12c0;
const unsigned kMonitorUpdate = 0x000d0f90;

#define Items (GameSpan<uint8_t>{0x0029aac8})   // the delay timer, then the five items
#define ITEMS_SIZE (4 + SSYS_ITEMS * 8)
#define WarmReset U8_AT(XbeOurs(0x00279250))

// The original SSys_Msg takes the item on the stack and the message in SI
__declspec(naked) void __cdecl OriginalMsg(unsigned item, unsigned msg) {
    _asm {
        push esi
        mov esi, [esp + 12]
        push dword ptr [esp + 8]
        mov eax, 0x000cf7f0
        call eax
        add esp, 4
        pop esi
        ret
    }
}

#define CAMERAS 5

struct World {
    obj_tag monitor;
    MONITOR monitorData;
    obj_tag cameras[CAMERAS];
    SENSOR sensors[CAMERAS];
    obj_tag player;
    uint8_t bloke[0x900];
    uint8_t viewer[sizeof(viewer_tag)];
    celglist_tag celgl;
    uint params[13];
    _VECTOR pos, rot;
    SSYSITEM saved[SSYS_ITEMS];
};
World world;

ShadowRegions regions;
int cases, differ;

obj_tag *obj;
unsigned item, msg, frames;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "security", what, trial, theirs, ours))
        differ++;
}

void RandomItems() {
    float *delay = (float *)XbeOurs(Items.at);
    *delay = ShadowRand(3) ? 0.0f : ShadowRandFloat(-0.5f, 9.0f);
    SSYSITEM *items = (SSYSITEM *)XbeOurs(Items.at + 4);
    for (int i = 0; i < SSYS_ITEMS; i++) {
        items[i].channel = ShadowRand(4) == 0 ? SSYS_NO_CHANNEL : (ShadowRand(6) == 0 ? 0 : 1 + ShadowRand(8));
        items[i].state = ShadowRand(3) ? 0 : ShadowRand(8);
        static const float timers[] = {-1.0f, -1.0f, 240.0f, 180.01f, 120.0f, 60.02f, 6.01f, 0.01f, -2.0f};
        items[i].timer = ShadowRand(2) ? timers[ShadowRand(9)] : ShadowRandFloat(-1.5f, 241.0f);
        items[i].unknown06[0] = ShadowRand(256);
        items[i].unknown06[1] = ShadowRand(256);
    }
    for (int ch = 0; ch < 16; ch++)
        switch_channels[ch] = ShadowRand(4) != 0;
    switch_channels[0x76] = ShadowRand(2);
    static const HASHCODE levels[] = {HT_Level_TowerB, HT_Level_TowerC, HT_Level_CastleExterior};
    GameState.CurrentLevelHashcode = levels[ShadowRand(3)];
    ScriptCam = ShadowRand(10) == 0 ? (HASHCODE)0x1234 : (HASHCODE)0;
    WarmReset = ShadowRand(3) == 0;
}

void TestSSys() {
    for (int trial = 0; trial < 400; trial++) {
        RandomItems();
        item = ShadowRand(6);
        msg = ShadowRand(7);
        Compare("SSys_Msg", trial,
                [] {
                    XbeOriginalScope original(kSSysMsg);
                    OriginalMsg(item, msg);
                    return 0u;
                },
                [] {
                    SSys_Msg(item, msg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 100; trial++) {
        RandomItems();
        Compare("SSys_Init", trial,
                [] {
                    XbeOriginalScope original(kSSysInit);
                    ((void (__cdecl *)(void))(uintptr_t)kSSysInit)();
                    return 0u;
                },
                [] {
                    SSys_Init();
                    return 0u;
                });
    }

    for (int trial = 0; trial < 200; trial++) {
        RandomItems();
        for (int i = 0; i < SSYS_ITEMS; i++) {
            world.saved[i].timer = ShadowRandFloat(-1, 240);
            world.saved[i].channel = ShadowRand(256);
            world.saved[i].state = ShadowRand(7);
            world.saved[i].unknown06[0] = ShadowRand(256);
        }
        Compare("SSys_RamLoad", trial,
                [] {
                    XbeOriginalScope original(kSSysRamLoad);
                    ((void (__cdecl *)(SSYSITEM *))(uintptr_t)kSSysRamLoad)(world.saved);
                    return 0u;
                },
                [] {
                    SSys_RamLoad(world.saved);
                    return 0u;
                });
    }

    // Long runs: the countdowns go through their messages, with channels switched on and off along the way
    for (int trial = 0; trial < 300; trial++) {
        RandomItems();
        frames = 1 + ShadowRand(trial % 3 == 0 ? 400 : 20);
        Compare("SSys_Monitor", trial,
                [] {
                    XbeOriginalScope original(kSSysMonitor);
                    uint32_t seed = frames;
                    for (unsigned f = 0; f < frames; f++) {
                        seed = seed * 69069u + 1;
                        if ((seed >> 24) == 0)
                            switch_channels[(seed >> 16) & 7] ^= 1;
                        ((void (__cdecl *)(void))(uintptr_t)kSSysMonitor)();
                    }
                    return 0u;
                },
                [] {
                    uint32_t seed = frames;
                    for (unsigned f = 0; f < frames; f++) {
                        seed = seed * 69069u + 1;
                        if ((seed >> 24) == 0)
                            switch_channels[(seed >> 16) & 7] ^= 1;
                        SSys_Monitor();
                    }
                    return 0u;
                });
    }
}

// ---- monitors

// Every camera has its sensor data (both read it unchecked once a camera is shown); some are left out of the list
void RandomCameras() {
    CameraList.head = NULL;
    CameraList.tail = NULL;
    for (int i = 0; i < CAMERAS; i++) {
        obj_tag *camera = &world.cameras[i];
        SENSOR *sensor = &world.sensors[i];
        camera->extraObjectData = sensor;
        camera->objectType = OBJECTTYPE_SENSOR;
        camera->curState = ShadowRand(4) == 0 ? SENSOR_STATE_DISABLED : ShadowRand(3);
        camera->displayMask = 0x1f;
        for (int k = 0; k < 15; k++)
            camera->transformMatrix.m[k] = ShadowRandFloat(-1, 1);
        sensor->owner = camera;
        sensor->monitorGroup = ShadowRand(3);
        sensor->monitoredChannel = ShadowRand(2) ? 1 + ShadowRand(12) : 0;
        sensor->childObjectVisibilityState = ShadowRand(3);
    }
    int count = ShadowRand(CAMERAS + 1);
    for (int i = count - 1; i >= 0; i--) {
        SENSOR *sensor = &world.sensors[i];
        sensor->cameraListNode.next = CameraList.head;
        if (CameraList.head != NULL)
            CameraList.head->prev = &sensor->cameraListNode;
        else
            CameraList.tail = &sensor->cameraListNode;
        CameraList.head = &sensor->cameraListNode;
    }
}

void RandomMonitor() {
    memset(&world, 0, sizeof(world));
    obj = &world.monitor;
    obj->extraObjectData = &world.monitorData;
    obj->objectType = OBJECTTYPE_MONITOR;
    static const ushort states[] = {0, 1, 2, 2, 2, 2, 3, 4, 5, 9, 0xffff};
    obj->curState = states[ShadowRand(sizeof(states) / sizeof(states[0]))];
    obj->subState = ShadowRand(3);
    obj->centrePoint = {ShadowRandFloat(-5, 5), 1.0f, ShadowRandFloat(-5, 5)};
    obj->radius = ShadowRandFloat(0.2f, 1.0f);
    RandomCameras();

    world.player.objectType = ShadowRand(10) ? OBJECTTYPE_PLAYER : OBJECTTYPE_DRONE;
    world.player.extraObjectData = world.bloke;
    world.bloke[0x8de] = ShadowRand(2);
    *(void **)(world.bloke + 0x770) = (void *)0x7770;   // the HUD, handed to the faked HUD_Enable

    MONITOR *monitor = &world.monitorData;
    monitor->viewer = (viewer_tag *)world.viewer;
    monitor->camera = ShadowRand(5) ? &world.cameras[ShadowRand(CAMERAS)] : NULL;
    // A showing monitor always has its user: with no camera left, both hand the view back to them unchecked
    monitor->user = ShadowRand(4) || obj->curState == MonitorState_Showing ? &world.player : NULL;
    monitor->health = ShadowRand(3) == 0 ? 0.0f : ShadowRandFloat(0.0f, 1.0f);
    monitor->viewerIndex = world.bloke[0x8de];
    glb_viewer[0] = (viewer_tag *)world.viewer;
    glb_viewer[1] = (viewer_tag *)world.viewer;

    ShadowFakes.damage = ShadowRand(2) ? 0.0f : ShadowRandFloat(0.0f, 2.0f);
    ShadowFakes.inputAction = ShadowRand(3) == 0;
    for (int ch = 0; ch < 16; ch++)
        switch_channels[ch] = 0;
}

void TestMonitors() {
    for (int trial = 0; trial < 300; trial++) {
        RandomMonitor();
        world.params[11] = ShadowRand(4);
        world.params[12] = ShadowRand(2);
        ShadowFakes.createFails = ShadowRand(8) == 0;
        Compare("Monitor_Create", trial,
                [] {
                    XbeOriginalScope original(kMonitorCreate);
                    typedef obj_tag *(__cdecl *Fn)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *);
                    return (uint32_t)(uintptr_t)((Fn)(uintptr_t)kMonitorCreate)(&world.pos, &world.rot,
                                                                               (level_tag *)world.params,
                                                                               &world.celgl);
                },
                [] {
                    return (uint32_t)(uintptr_t)Monitor_Create(&world.pos, &world.rot, (level_tag *)world.params,
                                                               &world.celgl);
                });
    }
    ShadowFakes.createFails = false;

    for (int trial = 0; trial < 1500; trial++) {
        RandomMonitor();
        if (ShadowRand(2))
            obj->curState = MonitorState_Off;
        Compare("Monitor_Activate", trial,
                [] {
                    XbeOriginalScope original(kMonitorActivate);
                    ((void (__cdecl *)(obj_tag *, obj_tag *))(uintptr_t)kMonitorActivate)(obj, &world.player);
                    return 0u;
                },
                [] {
                    Monitor_Activate(obj, &world.player);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomMonitor();
        Compare("Monitor_Update", trial,
                [] {
                    XbeOriginalScope original(kMonitorUpdate);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kMonitorUpdate)(obj);
                    return 0u;
                },
                [] {
                    Monitor_Update(obj);
                    return 0u;
                });
    }
}

} // namespace

void SecurityShadow_Run(void) {
    static char channels[256];
    static uint8_t gameState[sizeof(GameState_t)];
    static uint8_t items[ITEMS_SIZE];
    static uint8_t cameraList[sizeof(LLISTINFO_tag)];
    static viewer_tag *viewers[2];
    static HASHCODE scriptCam;
    static uint8_t warmReset;
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(gameState, &GameState, sizeof(gameState));
    XbeOursRead(items, Items.at, sizeof(items));
    memcpy(cameraList, &CameraList, sizeof(cameraList));
    viewers[0] = glb_viewer[0];
    viewers[1] = glb_viewer[1];
    scriptCam = ScriptCam;
    warmReset = WarmReset;

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(switch_channels, 256);
    regions.Add(Items.game(), ITEMS_SIZE);
    regions.Add(&CameraList, sizeof(LLISTINFO_tag));
    ShadowFakes_AddRegions(regions);

    memset(&ShadowFakes, 0, sizeof(ShadowFakes));
    ShadowFakes_Install();

    cases = differ = 0;
    TestSSys();
    TestMonitors();

    ShadowHook_RemoveAll();
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(&GameState, gameState, sizeof(gameState));
    XbeOursWrite(Items.at, items, sizeof(items));
    memcpy(&CameraList, cameraList, sizeof(cameraList));
    glb_viewer[0] = viewers[0];
    glb_viewer[1] = viewers[1];
    ScriptCam = scriptCam;
    WarmReset = warmReset;

    printf("[security] SSys_*, Monitor_*: %d cases, %d differ\n", cases, differ);
}
