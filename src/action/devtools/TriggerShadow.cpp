// Shadow tests for the triggers (game/obj/Trigger.cpp) and hints (game/obj/Hint.cpp): each function runs, the
// original and ours, on the same made-up trigger, the objects touching it, the switch channels and GameState, with
// the calls that need a game in play (the heap, rooms, paths, text, sounds, game flow) going to the fakes of
// ShadowHarness.cpp. Run at start with MenuShadowTests=on (MenuProbe.cpp).
//
// Covered: every create function (the two the time demos never reach, Trigger_MultiplexOut and
// Trigger_MoviePlayer, among them), Trigger_Type, Trigger_Locked, Trigger_Hit, Trigger_Update (every mode),
// Trigger_Activate, Trigger_Calc, InverterTrigger_Update and Hint_Update. Where an original calls another of these
// it reaches ours, so each is compared on its own.

#include "TriggerShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/obj/Hint.h"
#include "../game/obj/Trigger.h"
#include "../game/obj/bullet.h"
#include "../game/sp/SwitchChannels.h"

#include <stdio.h>
#include <string.h>

namespace {

const unsigned kTriggerCreate = 0x000d26d0;
const unsigned kMultiplexIn = 0x000d33f0;
const unsigned kMultiplexOrIn = 0x000d3450;
const unsigned kMultiplexSIn = 0x000d34b0;
const unsigned kMultiplexOut = 0x000d3500;
const unsigned kTouch = 0x000d3310;
const unsigned kTouchOnce = 0x000d3380;
const unsigned kLoadLevelCreate = 0x000d3260;
const unsigned kMoviePlayer = 0x000d32c0;
const unsigned kInverterCreate = 0x000d2d50;
const unsigned kInverterUpdate = 0x000d2d90;
const unsigned kTriggerType = 0x000d2ce0;
const unsigned kTriggerLocked = 0x000d2d00;
const unsigned kTriggerHit = 0x000d2840;
const unsigned kTriggerUpdate = 0x000d3560;
const unsigned kTriggerActivate = 0x000d2ab0;
const unsigned kTriggerCalc = 0x000d3950;
const unsigned kHintUpdate = 0x000d3190;
const unsigned kDoneBondMoment = 0x000b0500;

#define ProcessTouchByte U8_AT(0x0029b2cc)

typedef obj_tag *(__cdecl *CreateFn)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *);

#define OTHERS 6
#define TRIGGERS 4

// Everything the functions under test are given, in one block
struct World {
    obj_tag triggers[TRIGGERS];
    TRIGGER triggerData[TRIGGERS];
    obj_tag others[OTHERS];             // what touches the trigger
    uint8_t otherData[OTHERS][0x100];   // a drone's skin at +0xc4, a bullet's BU_tag
    HITDATA_tag hits[OTHERS];
    weapon_definition_tag weapons[2];
    obj_tag player;
    uint8_t bloke[0x900];
    celglist_tag celgl;
    uint params[44];                    // a placement: the header, then 33 parameters from +0x2c
    _VECTOR pos, rot;
    uint hint[4];
};
World world;

ShadowRegions regions;
int cases, differ;

// The case under test
obj_tag *obj;
obj_tag *user;
unsigned function;
CreateFn create;

void SetChannels() {
    for (int ch = 0; ch < 256; ch++) {
        switch_channels[ch] = 0;
        switch_channels_time[ch] = 100 + ShadowRand(12);   // close together, for the sequence's four frames
    }
    for (int ch = 0; ch < 24; ch++) {
        uint r = ShadowRand(10);
        switch_channels[ch] = r < 5 ? 0 : (r < 9 ? 1 : 2);
    }
    for (int ch = 0x60; ch <= 0x6f; ch++)
        switch_channels[ch] = ShadowRand(5) == 0;
    switch_channels[0x7d] = ShadowRand(2);
    switch_channels[0xa4] = ShadowRand(2);
    switch_channels[0x34] = ShadowRand(2);
    GameState.NumFramesUnpaused = 300 + ShadowRand(100);
    static const HASHCODE levels[] = {(HASHCODE)0x07001500, HT_Level_TowerC, HT_Level_CastleExterior,
                                      (HASHCODE)0x07000001};
    GameState.CurrentLevelHashcode = levels[ShadowRand(4)];
    ProcessTouchByte = ShadowRand(2);
}

ushort RandomChannel() {
    static const ushort special[] = {0x7d, 0x65, 0x6a, 0x6e, 0x60, 0x63};
    if (ShadowRand(6) == 0)
        return special[ShadowRand(6)];
    return ShadowRand(24);
}

void RandomTrigger(obj_tag *o, TRIGGER *t) {
    memset(o, 0, sizeof(*o));
    memset(t, 0, sizeof(*t));
    o->extraObjectData = t;
    o->objectType = OBJECTTYPE_TRIGGER;
    o->effectFlags = ShadowRand(2) ? FLAG_HIDDEN : 0;
    o->effectFlags |= ShadowRand(2) ? 0x30008000 : 0;
    o->objGraphics = ShadowRand(3) ? &world.celgl : NULL;
    static const ushort modes[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 10, 10, 12, 12, 13, 13, 7, 99};
    t->mode = modes[ShadowRand(sizeof(modes) / sizeof(modes[0]))];
    t->channel = ShadowRand(4) ? RandomChannel() : 0;
    int inputs = ShadowRand(TRIGGER_INPUTS + 1);
    for (int i = 0; i < TRIGGER_INPUTS; i++) {
        if (i < inputs)
            t->inputs[i] = ShadowRand(5) == 0 ? (ShadowRand(2) ? 0xb : 0xe) : RandomChannel();
        else
            t->inputs[i] = ShadowRand(4) == 0 ? RandomChannel() : 0;   // past the end of the list
    }
    if (ShadowRand(3) == 0)
        t->inputs[ShadowRand(TRIGGER_INPUTS)] = 0x100 + ShadowRand(3);   // a drone skin
    t->touch = ShadowRand(0x20);
    t->lock = ShadowRand(3) == 0 ? RandomChannel() : 0;
    t->hideOnLock = ShadowRand(2);
    t->cooldown = ShadowRand(3);
    t->lastKey = ShadowRand(3) == 0 ? 1 + ShadowRand(4) : 0;
    t->speed = ShadowRandFloat(0.0f, 0.2f);
    t->pathPos = t->lastKey ? ShadowRandFloat(0.0f, t->lastKey + 0.1f) : 0.0f;
    t->path = (void *)0x1234;
}

void RandomHits(obj_tag *o) {
    int n = ShadowRand(OTHERS + 1);
    o->hitList = NULL;
    for (int i = n - 1; i >= 0; i--) {
        obj_tag *other = &world.others[i];
        memset(other, 0, sizeof(*other));
        memset(world.otherData[i], 0, sizeof(world.otherData[i]));
        other->extraObjectData = world.otherData[i];
        static const uchar types[] = {OBJECTTYPE_DRONE, OBJECTTYPE_DRONE, OBJECTTYPE_PLAYER, OBJECTTYPE_BULLET,
                                      OBJECTTYPE_BULLET, OBJECTTYPE_SUB, OBJECTTYPE_ROTOR};
        other->objectType = types[ShadowRand(sizeof(types))];
        *(uint *)(world.otherData[i] + 0xc4) = 0x05000100 + ShadowRand(3);   // a drone's skin hashcode
        BU_tag *bullet = (BU_tag *)world.otherData[i];
        if (other->objectType == OBJECTTYPE_BULLET)
            bullet->wpnDef = &world.weapons[ShadowRand(2)];
        HITDATA_tag *hit = &world.hits[i];
        memset(hit, 0, sizeof(*hit));
        hit->hitObj = ShadowRand(8) == 0 ? NULL : other;
        hit->next = o->hitList;
        o->hitList = hit;
    }
}

void RandomWorld() {
    memset(&world, 0, sizeof(world));
    world.celgl.applyFlagsToObject = ShadowRand(2) ? 0x20 : 0x11;
    world.weapons[0].weaponBaseNum = 0x101;
    world.weapons[1].weaponBaseNum = 0x0e;
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.extraObjectData = world.bloke;
    world.bloke[0x8de] = ShadowRand(2);
    glb_players[0] = &world.player;
    glb_blokes[0] = ShadowRand(4) ? (BLData *)world.bloke : NULL;
    SetChannels();
    for (int i = 0; i < TRIGGERS; i++)
        RandomTrigger(&world.triggers[i], &world.triggerData[i]);
    RandomHits(&world.triggers[0]);
}

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "trigger", what, trial, theirs, ours))
        differ++;
}

// ---- the create functions

void RandomPlacement() {
    memset(world.params, 0, sizeof(world.params));
    uint16_t *header = (uint16_t *)world.params;
    header[1] = ShadowRand(3) ? ShadowRand(6) : 0;    // path points
    header[2] = ShadowRand(5) ? 1 + ShadowRand(300) : 0;   // path duration
    world.params[2] = 0x4321;                        // the path
    for (int i = 0; i < 33; i++)
        world.params[11 + i] = ShadowRand(3) ? ShadowRand(16) : (ShadowRand(2) ? 0 : 0x70000 + ShadowRand(16));
    world.pos = {ShadowRandFloat(-50, 50), ShadowRandFloat(-5, 5), ShadowRandFloat(-50, 50)};
    world.rot = {ShadowRandFloat(-3.2f, 3.2f), ShadowRandFloat(-3.2f, 3.2f), ShadowRandFloat(-3.2f, 3.2f)};
}

void TestCreates() {
    struct {
        const char *name;
        unsigned at;
        CreateFn ours;
    } creates[] = {
        {"Trigger_Create", kTriggerCreate, Trigger_Create},
        {"Trigger_MultiplexIn", kMultiplexIn, Trigger_MultiplexIn},
        {"Trigger_MultiplexOrIn", kMultiplexOrIn, Trigger_MultiplexOrIn},
        {"Trigger_MultiplexSIn", kMultiplexSIn, Trigger_MultiplexSIn},
        {"Trigger_MultiplexOut", kMultiplexOut, Trigger_MultiplexOut},
        {"Trigger_Touch", kTouch, Trigger_Touch},
        {"Trigger_TouchOnce", kTouchOnce, Trigger_TouchOnce},
        {"Trigger_LoadLevelCreate", kLoadLevelCreate, Trigger_LoadLevelCreate},
        {"Trigger_MoviePlayer", kMoviePlayer, Trigger_MoviePlayer},
    };
    for (auto &c : creates) {
        for (int trial = 0; trial < 60; trial++) {
            RandomWorld();
            RandomPlacement();
            ShadowFakes.createFails = ShadowRand(10) == 0;
            function = c.at;
            create = c.ours;
            Compare(c.name, trial,
                    [] {
                        XbeOriginalScope original(function);
                        return (uint32_t)(uintptr_t)((CreateFn)(uintptr_t)function)(&world.pos, &world.rot,
                                                                                  (level_tag *)world.params,
                                                                                  &world.celgl);
                    },
                    [] {
                        return (uint32_t)(uintptr_t)create(&world.pos, &world.rot, (level_tag *)world.params,
                                                           &world.celgl);
                    });
        }
    }
    ShadowFakes.createFails = false;

    for (int trial = 0; trial < 40; trial++) {
        RandomWorld();
        RandomPlacement();
        ShadowFakes.createFails = ShadowRand(5) == 0;
        Compare("InverterTrigger_Create", trial,
                [] {
                    XbeOriginalScope original(kInverterCreate);
                    return (uint32_t)((bool (__cdecl *)(level_tag *))(uintptr_t)kInverterCreate)(
                               (level_tag *)world.params) & 0xff;
                },
                [] { return (uint32_t)InverterTrigger_Create((level_tag *)world.params) & 0xff; });
    }
    ShadowFakes.createFails = false;
}

// ---- the rest

void TestTriggers() {
    for (int trial = 0; trial < 400; trial++) {
        RandomWorld();
        obj = ShadowRand(20) == 0 ? NULL : &world.triggers[0];
        Compare("Trigger_Type", trial,
                [] {
                    XbeOriginalScope original(kTriggerType);
                    return (uint32_t)((ushort (__cdecl *)(obj_tag *))(uintptr_t)kTriggerType)(obj);
                },
                [] { return (uint32_t)Trigger_Type(obj); });
        Compare("Trigger_Locked", trial,
                [] {
                    XbeOriginalScope original(kTriggerLocked);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *))(uintptr_t)kTriggerLocked)(obj);
                },
                [] { return (uint32_t)(uint8_t)Trigger_Locked(obj); });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = &world.triggers[0];
        Compare("Trigger_Hit", trial,
                [] {
                    XbeOriginalScope original(kTriggerHit);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *, TRIGGER *))(uintptr_t)kTriggerHit)(
                        obj, (TRIGGER *)obj->extraObjectData);
                },
                [] { return (uint32_t)Trigger_Hit(obj, (TRIGGER *)obj->extraObjectData); });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = ShadowRand(50) == 0 ? NULL : &world.triggers[0];
        Compare("Trigger_Update", trial,
                [] {
                    XbeOriginalScope original(kTriggerUpdate);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kTriggerUpdate)(obj);
                    return 0u;
                },
                [] {
                    Trigger_Update(obj);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 1500; trial++) {
        RandomWorld();
        obj = ShadowRand(30) == 0 ? NULL : &world.triggers[0];
        static obj_tag *const users[] = {NULL, &world.player, &world.player, &world.player, &world.others[0]};
        user = users[ShadowRand(5)];
        world.others[0].objectType = OBJECTTYPE_DRONE;
        // The level-load trigger's own lock channels clear, most of the time
        if (ShadowRand(3))
            switch_channels[0x60] = switch_channels[0x61] = switch_channels[0x62] = switch_channels[0x63] = 0;
        Compare("Trigger_Activate", trial,
                [] {
                    XbeOriginalScope original(kTriggerActivate);
                    ((void (__cdecl *)(obj_tag *, obj_tag *))(uintptr_t)kTriggerActivate)(obj, user);
                    return 0u;
                },
                [] {
                    Trigger_Activate(obj, user);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        // A list of the triggers, mostly touch triggers, and an object of another kind
        for (int i = 0; i < TRIGGERS; i++) {
            if (ShadowRand(3))
                world.triggerData[i].mode = TriggerMode_Touch;
            world.triggers[i].nextObject = i + 1 < TRIGGERS ? &world.triggers[i + 1] : &world.others[5];
            RandomHits(&world.triggers[i]);
        }
        world.others[5].objectType = OBJECTTYPE_DOOR;
        world.others[5].nextObject = NULL;
        ShadowFakes.firstObject = ShadowRand(20) ? &world.triggers[0] : NULL;
        Compare("Trigger_Calc", trial,
                [] {
                    XbeOriginalScope original(kTriggerCalc);
                    ((void (__cdecl *)(void))(uintptr_t)kTriggerCalc)();
                    return 0u;
                },
                [] {
                    Trigger_Calc();
                    return 0u;
                });
    }
    ShadowFakes.firstObject = NULL;

    for (int trial = 0; trial < 400; trial++) {
        RandomWorld();
        obj = &world.triggers[0];
        obj->curState = ShadowRand(30);
        obj->subState = ShadowRand(3) ? ShadowRand(30) : 0;
        Compare("InverterTrigger_Update", trial,
                [] {
                    XbeOriginalScope original(kInverterUpdate);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kInverterUpdate)(obj);
                    return 0u;
                },
                [] {
                    InverterTrigger_Update(obj);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        obj = &world.triggers[0];
        obj->objectType = OBJECTTYPE_HINT;
        obj->extraObjectData = world.hint;
        world.hint[0] = ShadowRand(2) ? 0x123 : 0;          // sound
        world.hint[1] = 0x02000100 + ShadowRand(4);         // label
        world.hint[2] = ShadowRand(2) ? RandomChannel() : 0;
        world.hint[3] = ShadowRand(2) ? RandomChannel() : 0;
        for (int i = 0; i < OTHERS; i++) {
            if (world.others[i].objectType == OBJECTTYPE_PLAYER)
                world.others[i].extraObjectData = world.bloke;
        }
        Compare("Hint_Update", trial,
                [] {
                    XbeOriginalScope original(kHintUpdate);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kHintUpdate)(obj);
                    return 0u;
                },
                [] {
                    Hint_Update(obj);
                    return 0u;
                });
    }
}

void __cdecl FakeDoneBondMoment(uint player, ushort moment) {
    ShadowLog("bondmoment(%u,%u)", player, moment);
}

} // namespace

void TriggerShadow_Run(void) {
    // What the tests change, put back at the end
    static char channels[256], channelsPrev[256], channelsHold[256];
    static uint32_t times[256];
    static uint8_t gameState[sizeof(GameState_t)];
    static obj_tag *players[4];
    static BLData *blokes[4];
    static uint8_t processTouch;
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(channelsPrev, switch_channels_prev, sizeof(channelsPrev));
    memcpy(channelsHold, switch_channels_hold, sizeof(channelsHold));
    memcpy(times, switch_channels_time, sizeof(times));
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(players, glb_players, sizeof(players));
    memcpy(blokes, glb_blokes, sizeof(blokes));
    processTouch = ProcessTouchByte;

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(switch_channels, 256);
    regions.Add(switch_channels_hold, 256);
    regions.Add(switch_channels_time, 1024);
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&ProcessTouchByte, 1);
    ShadowFakes_AddRegions(regions);

    ShadowFakes_Install();
    ShadowHook_Install(kDoneBondMoment, (const void *)&FakeDoneBondMoment);
    memset(&ShadowFakes, 0, sizeof(ShadowFakes));

    cases = differ = 0;
    TestCreates();
    TestTriggers();

    ShadowHook_RemoveAll();
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_prev, channelsPrev, sizeof(channelsPrev));
    memcpy(switch_channels_hold, channelsHold, sizeof(channelsHold));
    memcpy(switch_channels_time, times, sizeof(times));
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(glb_players, players, sizeof(players));
    memcpy(glb_blokes, blokes, sizeof(blokes));
    ProcessTouchByte = processTouch;

    printf("[trigger] Trigger_*, Hint_Update: %d cases, %d differ\n", cases, differ);
}
