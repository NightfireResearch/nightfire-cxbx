// Shadow tests for the doors (game/obj/Door.cpp): each function runs, the original and ours, on the same made-up
// doors, the objects hitting or using them, the door list and group states, the switch channels and GameState, with
// the calls that need a game in play (the heap, rooms, paths, sounds, text) going to the fakes of ShadowHarness.cpp.
// The paths are a made-up matrix turning with the frame, so the swing doors' sides and the pushes come out both ways.
// Run at start with MenuShadowTests=on (MenuProbe.cpp).
//
// Covered: Door_Init, Door_Create, Door_SetupSwing, Door_SetupSlide, Door_SetState, Door_Interp, Door_ReactToHit
// (the swing door's push-back the time demos never reach among it), Door_Activate, Door_IsLocked, Door_IsClosed,
// Door_IsOpen, Door_Link2Portal, Door_DoMsg, Door_MoveGroup, Door_CloseIfClear, Doors_Calc and Door_Update. Where an
// original calls another of these it reaches ours, so each is compared on its own.

#include "DoorShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Door.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const unsigned kDoorInit = 0x0002f610;
const unsigned kDoorCreate = 0x0002f640;
const unsigned kSetupSwing = 0x0002ec30;
const unsigned kSetupSlide = 0x0002eb80;
const unsigned kSetState = 0x0002edb0;
const unsigned kInterp = 0x0002ee70;
const unsigned kReactToHit = 0x0002f070;
const unsigned kActivate = 0x0002f1f0;
const unsigned kIsLocked = 0x0002f570;
const unsigned kIsClosed = 0x0002f520;
const unsigned kIsOpen = 0x0002f4e0;
const unsigned kLink2Portal = 0x0002f4c0;
const unsigned kDoMsg = 0x0002f5b0;
const unsigned kMoveGroup = 0x0002f7e0;
const unsigned kCloseIfClear = 0x0002f850;
const unsigned kDoorsCalc = 0x0002f8d0;
const unsigned kDoorUpdate = 0x0002f930;

#define TheDoorsList (*(LLISTINFO_tag *)0x001df98c)
#define DoorGroups ((uint8_t *)0x001df8c0)
#define DOOR_GROUPS_SIZE 0xca

// The originals that take registers: these put them there
__declspec(naked) void __cdecl OriginalSetup(unsigned at, obj_tag *obj) {
    _asm {
        mov eax, [esp + 8]
        push 0
        call dword ptr [esp + 8]
        add esp, 4
        ret
    }
}

__declspec(naked) void __cdecl OriginalSetState(obj_tag *obj, unsigned state) {
    _asm {
        push ebx
        push esi
        mov esi, [esp + 12]
        movzx ebx, word ptr [esp + 16]
        mov eax, 0x0002edb0
        call eax
        pop esi
        pop ebx
        ret
    }
}

__declspec(naked) void __cdecl OriginalInterp(obj_tag *obj, DOORINFO *door, int advance) {
    _asm {
        push edi
        mov eax, [esp + 8]
        mov edi, [esp + 12]
        push dword ptr [esp + 16]
        mov ecx, 0x0002ee70
        call ecx
        add esp, 4
        pop edi
        ret
    }
}

__declspec(naked) void __cdecl OriginalMoveGroup(obj_tag *leadObj, DOORINFO *lead, int active) {
    _asm {
        push ebx
        push esi
        mov esi, [esp + 16]
        mov bl, [esp + 20]
        push dword ptr [esp + 12]
        mov eax, 0x0002f7e0
        call eax
        add esp, 4
        pop esi
        pop ebx
        ret
    }
}

__declspec(naked) void __cdecl OriginalCloseIfClear(obj_tag *obj) {
    _asm {
        push edi
        mov edi, [esp + 8]
        mov eax, 0x0002f850
        call eax
        pop edi
        ret
    }
}

#define DOORS 4
#define OTHERS 6

struct World {
    obj_tag doors[DOORS];
    DOORINFO doorData[DOORS];
    obj_tag others[OTHERS];
    uint8_t otherData[OTHERS][0x40];
    HITDATA_tag hits[OTHERS];
    obj_tag player;
    celglist_tag celgl;
    char name[8];
    uint params[23];
    _VECTOR pos, rot;
};
World world;

ShadowRegions regions;
int cases, differ;

obj_tag *obj;
obj_tag *user;
unsigned state, flag;

// A rotation about y and an offset
void RandomMatrix(_MATRIX *mtx) {
    float angle = ShadowRandFloat(-3.2f, 3.2f);
    float c = cosf(angle), s = sinf(angle);
    float m[15] = {c, 0.0f, -s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, s, 0.0f, c, 0.0f,
                   ShadowRandFloat(-3, 3), ShadowRandFloat(-1, 1), ShadowRandFloat(-3, 3)};
    memcpy(mtx->m, m, sizeof(m));
}

_VECTOR RandomPoint() {
    return {ShadowRandFloat(-3, 3), ShadowRandFloat(-1, 1), ShadowRandFloat(-3, 3)};
}

void RandomDoor(obj_tag *o, DOORINFO *d) {
    memset(o, 0, sizeof(*o));
    memset(d, 0, sizeof(*d));
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DOOR;
    o->objGraphics = &world.celgl;
    static const ushort kinds[] = {0, 0, 0, 1, 2, 2, 3, 0xffff};
    o->subState = kinds[ShadowRand(sizeof(kinds) / sizeof(kinds[0]))];
    static const ushort states[] = {2, 4, 8, 0x10, 8, 0x10, 0x18, 0, 6};
    o->curState = states[ShadowRand(sizeof(states) / sizeof(states[0]))];
    o->unknown_0xd9 = ShadowRand(0x10);
    RandomMatrix(&o->transformMatrix);
    o->centrePoint = RandomPoint();
    o->radius = ShadowRandFloat(0.2f, 2.0f);
    o->position = RandomPoint();
    o->lastPosition = RandomPoint();

    d->obj = o;
    d->path = (void *)0x5678;
    RandomMatrix(&d->hinge);
    static const float positions[] = {0.0f, 0.5f, 1.0f};
    d->pathPos = ShadowRand(4) == 0 ? positions[ShadowRand(3)] : ShadowRandFloat(-0.2f, 1.2f);
    d->speed = ShadowRandFloat(0.004f, 0.1f);
    d->direction = ShadowRand(2) ? 1.0f : -1.0f;
    d->side = ShadowRand(2) ? 1.0f : -1.0f;
    d->openSound = 0x40 + ShadowRand(3);
    d->closeSound = 0x50 + ShadowRand(3);
    d->lockedSound = ShadowRand(4) ? 0x60 : 0xffff;
    d->channel = ShadowRand(3) ? 1 + ShadowRand(12) : 0;
    d->unlockSwitchChannel = ShadowRand(3) == 0 ? 1 + ShadowRand(12) : 0;
    d->group = ShadowRand(2) ? 1 + ShadowRand(3) : 0;
    d->cooldown = ShadowRand(3);
    d->lastKey = 1 + ShadowRand(5);
    d->flags = ShadowRand(2) | (ShadowRand(3) == 0 ? DoorFlag_StartOpen : 0) |
               (ShadowRand(4) == 0 ? DoorFlag_Moved : 0) | (ShadowRand(4) == 0 ? DoorFlag_StayOpen : 0);
    d->openTime = ShadowRand(2) ? ShadowRand(10) : FRAME_RATE_INT * 6 - 2 + ShadowRand(4);
    d->msgShown = ShadowRand(3) == 0;
    d->oneWay = ShadowRand(3) == 0 ? 1 + ShadowRand(2) : 0;
    d->noTouch = ShadowRand(4) == 0;
}

// The doors in the list, in order
void LinkDoors(int count) {
    TheDoorsList.head = count ? &world.doorData[0].node : NULL;
    TheDoorsList.tail = count ? &world.doorData[count - 1].node : NULL;
    TheDoorsList.count = count;
    for (int i = 0; i < count; i++) {
        world.doorData[i].node.prev = i > 0 ? &world.doorData[i - 1].node : NULL;
        world.doorData[i].node.next = i + 1 < count ? &world.doorData[i + 1].node : NULL;
    }
}

void RandomWorld() {
    memset(&world, 0, sizeof(world));
    strcpy(world.name, "door");
    world.celgl.name = world.name;
    world.celgl.applyFlagsToObject = ShadowRand(2) ? 0x40000 : 0x20;
    world.celgl.extentMin = {-ShadowRandFloat(0, 1), -1.0f, -ShadowRandFloat(0, 1)};
    world.celgl.extentMax = {ShadowRandFloat(0, 1), 1.0f, ShadowRandFloat(0, 1)};
    for (int i = 0; i < DOORS; i++)
        RandomDoor(&world.doors[i], &world.doorData[i]);
    LinkDoors(DOORS);

    obj_tag *list = NULL;
    for (int i = OTHERS - 1; i >= 0; i--) {
        obj_tag *other = &world.others[i];
        other->extraObjectData = world.otherData[i];
        static const uchar types[] = {OBJECTTYPE_DRONE, OBJECTTYPE_DRONE, OBJECTTYPE_PLAYER, OBJECTTYPE_DEAD_PLAYER,
                                      OBJECTTYPE_BULLET};
        other->objectType = types[ShadowRand(sizeof(types))];
        other->tweakA = ShadowRand(2);   // the faked MPDrone_MaybeIsDyingOrDead
        other->effectFlags = ShadowRand(5) == 0 ? FLAG_HIDDEN : 0;
        other->centrePoint = RandomPoint();
        other->radius = ShadowRandFloat(0.1f, 1.5f);
        RandomMatrix(&other->transformMatrix);
        other->nextObject = list;
        list = other;
        HITDATA_tag *hit = &world.hits[i];
        hit->hitObj = ShadowRand(8) == 0 ? NULL : other;
        hit->next = i + 1 < OTHERS && ShadowRand(4) ? &world.hits[i + 1] : NULL;
    }
    world.doors[0].hitList = ShadowRand(5) ? &world.hits[ShadowRand(OTHERS)] : NULL;
    ShadowFakes.firstObject = ShadowRand(4) ? list : NULL;

    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.centrePoint = RandomPoint();
    RandomMatrix(&world.player.transformMatrix);
    glb_players[0] = &world.player;

    for (int ch = 0; ch < 16; ch++) {
        switch_channels[ch] = ShadowRand(2);
        switch_channels_prev[ch] = ShadowRand(2);
        switch_channels_time[ch] = ShadowRand(100);
    }
    for (int i = 0; i < DOOR_GROUPS_SIZE / 2; i++)
        ((ushort *)DoorGroups)[i] = ShadowRand(3) ? (ushort)(2 << ShadowRand(4)) : (ushort)ShadowRand(0x20);
    static const HASHCODE levels[] = {HT_Level_EvilBase, HT_Level_Tower2Elevator, HT_Level_TowerC};
    GameState.CurrentLevelHashcode = levels[ShadowRand(3)];
    GameState.NumFramesUnpaused = 500 + ShadowRand(100);
    MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;
}

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "door", what, trial, theirs, ours))
        differ++;
}

void TestDoors() {
    for (int trial = 0; trial < 20; trial++) {
        RandomWorld();
        Compare("Door_Init", trial,
                [] {
                    XbeOriginalScope original(kDoorInit);
                    ((void (__cdecl *)(void))(uintptr_t)kDoorInit)();
                    return 0u;
                },
                [] {
                    Door_Init();
                    return 0u;
                });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        memset(world.params, 0, sizeof(world.params));
        uint16_t *header = (uint16_t *)world.params;
        header[1] = 2 + ShadowRand(4);
        header[2] = ShadowRand(4) ? 1 + ShadowRand(200) : 0;
        world.params[2] = 0x4321;
        for (int i = 11; i < 23; i++)
            world.params[i] = ShadowRand(3) ? ShadowRand(6) : 0;
        world.params[11] = ShadowRand(3);   // the kind
        world.pos = RandomPoint();
        world.rot = RandomPoint();
        Compare("Door_Create", trial,
                [] {
                    XbeOriginalScope original(kDoorCreate);
                    typedef obj_tag *(__cdecl *Fn)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *);
                    return (uint32_t)(uintptr_t)((Fn)(uintptr_t)kDoorCreate)(&world.pos, &world.rot,
                                                                            (level_tag *)world.params,
                                                                            &world.celgl);
                },
                [] {
                    return (uint32_t)(uintptr_t)Door_Create(&world.pos, &world.rot, (level_tag *)world.params,
                                                            &world.celgl);
                });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        Compare("Door_SetupSwing", trial,
                [] {
                    XbeOriginalScope original(kSetupSwing);
                    OriginalSetup(kSetupSwing, obj);
                    return 0u;
                },
                [] {
                    _Door_SetupSwing(obj);
                    return 0u;
                });
        Compare("Door_SetupSlide", trial,
                [] {
                    XbeOriginalScope original(kSetupSlide);
                    OriginalSetup(kSetupSlide, obj);
                    return 0u;
                },
                [] {
                    _Door_SetupSlide(obj);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        static const ushort states[] = {2, 4, 8, 0x10, 0x18};
        state = states[ShadowRand(5)];
        Compare("Door_SetState", trial,
                [] {
                    XbeOriginalScope original(kSetState);
                    OriginalSetState(obj, state);
                    return 0u;
                },
                [] {
                    _Door_SetState(obj, state);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        flag = ShadowRand(5) != 0;
        Compare("Door_Interp", trial,
                [] {
                    XbeOriginalScope original(kInterp);
                    OriginalInterp(obj, (DOORINFO *)obj->extraObjectData, flag);
                    return 0u;
                },
                [] {
                    _Door_Interp(obj, (DOORINFO *)obj->extraObjectData, flag);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        // Mostly moving, and ready to be pushed
        if (ShadowRand(3))
            obj->curState = ShadowRand(2) ? DoorState_Opening : DoorState_Closing;
        if (ShadowRand(3))
            ((DOORINFO *)obj->extraObjectData)->cooldown = 0;
        Compare("Door_ReactToHit", trial,
                [] {
                    XbeOriginalScope original(kReactToHit);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kReactToHit)(obj);
                    return 0u;
                },
                [] {
                    Door_ReactToHit(obj);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        user = ShadowRand(3) ? &world.player : &world.others[0];
        flag = ShadowRand(3) == 0;
        if (ShadowRand(2))
            ((DOORINFO *)obj->extraObjectData)->flags &= ~DoorFlag_Moved;
        Compare("Door_Activate", trial,
                [] {
                    XbeOriginalScope original(kActivate);
                    typedef uint8_t (__cdecl *Fn)(obj_tag *, obj_tag *, char);
                    return (uint32_t)((Fn)(uintptr_t)kActivate)(obj, user, flag);
                },
                [] { return (uint32_t)(uint8_t)Door_Activate(obj, user, flag); });
    }

    for (int trial = 0; trial < 600; trial++) {
        RandomWorld();
        obj = ShadowRand(15) ? &world.doors[0] : NULL;
        Compare("Door_IsLocked", trial,
                [] {
                    XbeOriginalScope original(kIsLocked);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *))(uintptr_t)kIsLocked)(obj);
                },
                [] { return (uint32_t)(uint8_t)Door_IsLocked(obj); });
        Compare("Door_IsClosed", trial,
                [] {
                    XbeOriginalScope original(kIsClosed);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *))(uintptr_t)kIsClosed)(obj);
                },
                [] { return (uint32_t)(uint8_t)Door_IsClosed(obj); });
        Compare("Door_IsOpen", trial,
                [] {
                    XbeOriginalScope original(kIsOpen);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *))(uintptr_t)kIsOpen)(obj);
                },
                [] { return (uint32_t)(uint8_t)Door_IsOpen(obj); });
        Compare("Door_Link2Portal", trial,
                [] {
                    XbeOriginalScope original(kLink2Portal);
                    return (uint32_t)((uint8_t (__cdecl *)(obj_tag *))(uintptr_t)kLink2Portal)(obj);
                },
                [] { return (uint32_t)Door_Link2Portal(obj); });
        Compare("Door_DoMsg", trial,
                [] {
                    XbeOriginalScope original(kDoMsg);
                    return (uint32_t)(uintptr_t)((char *(__cdecl *)(obj_tag *))(uintptr_t)kDoMsg)(obj);
                },
                [] { return (uint32_t)(uintptr_t)Door_DoMsg(obj); });
    }

    for (int trial = 0; trial < 1500; trial++) {
        RandomWorld();
        LinkDoors(ShadowRand(DOORS + 1));
        obj = &world.doors[ShadowRand(DOORS)];
        flag = ShadowRand(5) != 0;
        Compare("Door_MoveGroup", trial,
                [] {
                    XbeOriginalScope original(kMoveGroup);
                    OriginalMoveGroup(obj, (DOORINFO *)obj->extraObjectData, flag);
                    return 0u;
                },
                [] {
                    _Door_MoveGroup(obj, (DOORINFO *)obj->extraObjectData, flag);
                    return 0u;
                });
        Compare("Doors_Calc", trial,
                [] {
                    XbeOriginalScope original(kDoorsCalc);
                    ((void (__cdecl *)(void))(uintptr_t)kDoorsCalc)();
                    return 0u;
                },
                [] {
                    Doors_Calc();
                    return 0u;
                });
    }

    for (int trial = 0; trial < 1500; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        Compare("Door_CloseIfClear", trial,
                [] {
                    XbeOriginalScope original(kCloseIfClear);
                    OriginalCloseIfClear(obj);
                    return 0u;
                },
                [] {
                    _Door_CloseIfClear(obj);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        obj = &world.doors[0];
        if (ShadowRand(2))
            ((DOORINFO *)obj->extraObjectData)->flags &= ~DoorFlag_Moved;
        Compare("Door_Update", trial,
                [] {
                    XbeOriginalScope original(kDoorUpdate);
                    ((void (__cdecl *)(obj_tag *))(uintptr_t)kDoorUpdate)(obj);
                    return 0u;
                },
                [] {
                    Door_Update(obj);
                    return 0u;
                });
    }
}

} // namespace

void DoorShadow_Run(void) {
    static char channels[256], channelsPrev[256], channelsHold[256];
    static uint32_t times[256];
    static uint8_t gameState[sizeof(GameState_t)];
    static obj_tag *players[4];
    static uint8_t doors[sizeof(LLISTINFO_tag)], groups[DOOR_GROUPS_SIZE];
    static undefined4 droneAI;
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(channelsPrev, switch_channels_prev, sizeof(channelsPrev));
    memcpy(channelsHold, switch_channels_hold, sizeof(channelsHold));
    memcpy(times, switch_channels_time, sizeof(times));
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(players, glb_players, sizeof(players));
    memcpy(doors, &TheDoorsList, sizeof(doors));
    memcpy(groups, DoorGroups, sizeof(groups));
    droneAI = MPSettings.maybeDroneAIEnabled;

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(switch_channels, 256);
    regions.Add(switch_channels_hold, 256);
    regions.Add(switch_channels_prev, 256);
    regions.Add(switch_channels_time, 1024);
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&TheDoorsList, sizeof(LLISTINFO_tag));
    regions.Add(DoorGroups, DOOR_GROUPS_SIZE);
    ShadowFakes_AddRegions(regions);

    memset(&ShadowFakes, 0, sizeof(ShadowFakes));
    ShadowFakes_Install();
    NfWarnMuted++;

    cases = differ = 0;
    TestDoors();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    ShadowFakes.firstObject = NULL;
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_prev, channelsPrev, sizeof(channelsPrev));
    memcpy(switch_channels_hold, channelsHold, sizeof(channelsHold));
    memcpy(switch_channels_time, times, sizeof(times));
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(glb_players, players, sizeof(players));
    memcpy(&TheDoorsList, doors, sizeof(doors));
    memcpy(DoorGroups, groups, sizeof(groups));
    MPSettings.maybeDroneAIEnabled = droneAI;

    printf("[door] Door_*, Doors_Calc: %d cases, %d differ\n", cases, differ);
}
