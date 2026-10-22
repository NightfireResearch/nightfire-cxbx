// Shadow tests for the drone core (game/drone/Drone.cpp): the level set-up, Drone_InitComms, each drone's frame
// (Drone_Control, PreDroneControl, the type controls, PostDroneControl, DoAnimation, GetOpponentInfo) and
// Drone_Delete. Each original and ours run on the same made-up drones, with every callee that needs a level (or
// belongs to another package) going to a logging fake. Run at start with MenuShadowTests=on (MenuProbe.cpp), before
// any level exists.
//
// Faked, so not tested here: the state machine's sends and state changes, the opponent search, tracking, talking,
// moving, collision, firing, explosives, door opening, the anim calls and updates, the bone positions, the breath,
// the AI network's floods, the per-drone post-load set-up, the groups and spawners, the head tracking anim, and the
// heap. Drone_Control is tested with PreDroneControl and the controls faked (they are tested on their own).

#include "DroneCoreShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/AINetwork.h"
#include "../engine/Anim.h"
#include "../engine/viewer.h"
#include "../game/drone/Drone.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Opponent.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/obj/DroneSpawner.h"
#include "../game/obj/GT.h"
#include "../game/sp/SwitchChannels.h"
#include "../sound/music.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

#define switch_NO_DRONES U32_AT(XbeOurs(0x001df99c))
#define Drone_DummyPlayer (*(char *)XbeOurs(0x001dfa29))
#define Drone_bDontDraw (*(uchar *)XbeOurs(0x001dfa2b))
// MoveTest_MaxSlope .. AIPath_MaxNodes
#define MoveTestBlock (GameSpan<uint8_t>{0x00274cac})
const int kMoveTestBytes = 0x18;
// VIDEO_FRAME_RATE .. REC_FRAME_RATE
#define FrameRateBlock (GameSpan<uint8_t>{0x0017c0f0})
const int kFrameRateBytes = 0x18;

const int kDrones = 4;
const int kOthers = 7;

// Others: 0 bullet, 1 casing, 2 spawner, 3 gun turret, 4 door, 5 a player, 6 another player
struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    obj_tag others[kOthers];
    BU_tag bullet;
    uint8_t casing[0x40];
    DroneSpawner_tag spawner;
    GUNTURRET turret;
    CoverNode_tag cover[4];
    DroneAIPoint_tag aiPoints[3];
    sAnimScript_tag facial;
    cel_tag cels[2];
    ushort routeNodes[kDrones][2][2];
    DCVars_tag dcv;

    // What the fakes answer
    uint32_t result[8];
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronecore", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// A pointer as an offset into the world or NPCGlobals, the same for both runs
int Who(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    if (p == NULL)
        return -1;
    if (b >= (const uint8_t *)&world && b < (const uint8_t *)&world + sizeof(world))
        return (int)(b - (const uint8_t *)&world);
    if (b >= (const uint8_t *)&NPCGlobals && b < (const uint8_t *)&NPCGlobals + sizeof(NPCGlobals))
        return 0x1000000 + (int)(b - (const uint8_t *)&NPCGlobals);
    return 0x7fffffff;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

void __cdecl FakeSendMsgSelf(uint type, void *extra, uint delay, uint scope, DCVars_tag *dcv) {
    ShadowLog("sendself(%x,%x,%u,%x,%x)", type, (uint)(uintptr_t)extra, delay, scope, Who(dcv));
}
uint32_t __cdecl FakeSetState(StateMachineInfo_tag *sm, uint state, int param) {
    ShadowLog("setstate(%x,%x,%d)", Who(sm), state, param);
    return world.result[0];
}
uint32_t __cdecl FakeChangeToAttackMode(Drone_tag *drone) {
    ShadowLog("attackmode(%x)", Who(drone));
    return world.result[1];
}
void __cdecl FakeNinjaEyes(obj_tag *obj) { ShadowLog("ninjaeyes(%x)", Who(obj)); }
uint32_t __cdecl FakeSoundFinished(DYNAMICSOUNDS *sound) {
    ShadowLog("soundfinished(%x)", (uint)(uintptr_t)sound);
    return world.result[2];
}
void __cdecl FakeBotInfo(DCVars_tag *dcv) { ShadowLog("botinfo(%x)", Who(dcv)); }
obj_tag *__cdecl FakeFindOpponent(Drone_tag *drone) {
    ShadowLog("findopponent(%x)", Who(drone));
    return NULL;
}
void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%x,%x,%d)", Who(obj), which, datum);
    pos->x = obj->position.x + 0.25f;
    pos->y = obj->position.y + 1.5f;
    pos->z = obj->position.z - 0.125f;
    for (int k = 0; k < (int)(sizeof(_MATRIX) / sizeof(float)); k++)
        mtx->m[k] = (float)((k * 7) % 5) * 0.3f - 0.6f;
}
void __cdecl FakeBreath(_VECTOR *pos, _VECTOR *dir, cel_tag *cel, int seed) {
    LogVector("breath", pos);
    LogVector("dir", dir);
    ShadowLog("(%x,%x)", Who(cel), seed);
}
void __cdecl FakeTalkingCore(DCVars_tag *dcv) { ShadowLog("talking(%x)", Who(dcv)); }
void __cdecl FakeTracking(char on, DCVars_tag *dcv) { ShadowLog("tracking(%d,%x)", on, Who(dcv)); }
void __cdecl FakeMove(DCVars_tag *dcv) {
    LogVector("move", &dcv->drone->moveRotation);
    dcv->drone->moveRotation.y += 0.25f;
}
void __cdecl FakeBoundryFlags(DCVars_tag *dcv) { ShadowLog("boundry(%x)", Who(dcv)); }
uint32_t __cdecl FakeNoBunching(DCVars_tag *dcv) {
    ShadowLog("nobunching(%x)", Who(dcv));
    return world.result[3];
}
void __cdecl FakeCollision(obj_tag *obj) { ShadowLog("collision(%x)", Who(obj)); }
void __cdecl FakeFiring(DCVars_tag *dcv) { ShadowLog("firing(%x)", Who(dcv)); }
void __cdecl FakeExplosives(DCVars_tag *dcv) { ShadowLog("explosives(%x)", Who(dcv)); }
uint32_t __cdecl FakeDoorIsOpen(DCVars_tag *dcv) {
    ShadowLog("doorisopen(%x)", Who(dcv));
    return world.result[4];
}
void __cdecl FakeOpenDoor(DCVars_tag *dcv) { ShadowLog("opendoor(%x)", Who(dcv)); }
uint32_t __cdecl FakePlr2Ind(obj_tag *obj) {
    ShadowLog("plr2ind(%x)", Who(obj));
    return world.result[5];
}
void __cdecl FakeSnapRotate(Drone_tag *drone) { ShadowLog("snaprotate(%x)", Who(drone)); }
uint32_t __cdecl FakeAnimUpdate(obj_tag *obj) {
    ShadowLog("animupdate(%x)", Who(obj));
    return 1;
}
void __cdecl FakeCallHandler(DCVars_tag *dcv) { ShadowLog("callhandler(%x)", Who(dcv)); }
void __cdecl FakeAnimFunc(Drone_tag *drone, ushort phase) { ShadowLog("animfunc(%x,%u)", Who(drone), phase); }
void __cdecl FakeStateFunc(Drone_tag *drone, ushort phase) { ShadowLog("statefunc(%x,%u)", Who(drone), phase); }
uint32_t __cdecl FakePreDroneControl(DCVars_tag *dcv) {
    ShadowLog("pre(%x,%x,%x,%x)", Who(dcv->gameObj), Who(dcv->drone), Who(dcv->cel), Who(dcv->aiStateMachine));
    return world.result[6];
}
uint32_t __cdecl FakeStandard(DCVars_tag *dcv) {
    ShadowLog("standard(%x)", Who(dcv));
    return 1;
}
uint32_t __cdecl FakeTypeControl(DCVars_tag *dcv) {
    ShadowLog("typecontrol(%x)", Who(dcv));
    return 1;
}

// Drone_InitComms' callees
void __cdecl FakeClearLinkFlags(ushort flags) { ShadowLog("clearlinks(%x)", flags); }
void __cdecl FakeAwarePoints(void) { ShadowLog("awarepoints"); }
void __cdecl FakeUpdateTargets(void) { ShadowLog("updatetargets"); }
void __cdecl FakeCoverNodes(void) { ShadowLog("covernodes"); }
void __cdecl FakeOpponents(void) { ShadowLog("opponents"); }
void __cdecl FakeClearTargets(void) { ShadowLog("cleartargets"); }
void __cdecl FakeDroneSight(void) { ShadowLog("dronesight"); }
void __cdecl FakeNavNodeCache(void) { ShadowLog("navnodecache"); }
void __cdecl FakeSoundAlerts(DCVars_tag *dcv) {
    ShadowLog("soundalerts(%x,%x,%x,%x)", Who(dcv->gameObj), Who(dcv->drone), Who(dcv->cel), Who(dcv->aiStateMachine));
}
void __cdecl FakeZeroAlertness(void) { ShadowLog("zeroalertness"); }
void __cdecl FakeBroadcast(uint type, void *extra, uint delay, uint sender) {
    ShadowLog("broadcast(%x,%x,%u,%x)", type, (uint)(uintptr_t)extra, delay, sender);
}
void __cdecl FakeDelayedMsgs(void) { ShadowLog("delayed"); }
void __cdecl FakeAlarmRaised(void) { ShadowLog("alarmraised"); }

// The set-up's
void __cdecl FakeLListInit(LLISTINFO_tag *list, ushort size, ushort n) {
    ShadowLog("llistinit(%x,%x,%u)", (uint)(uintptr_t)list, size, n);
}
uint32_t __cdecl FakeSMInit(void) {
    ShadowLog("sminit");
    return world.result[7];
}
void __cdecl FakeAllocEmitter(AIEmitter_tag *emitter, uint size) {
    ShadowLog("allocemitter(%x,%u)", Who(emitter), size);
}
uint32_t __cdecl FakeEmitPath(AIEmitter_tag *emitter, float cost) {
    ShadowLog("emitpath(%x,%08x)", Who(emitter), Bits(cost));
    return 0;
}
void __cdecl FakeCreateBlank(void) { ShadowLog("createblank"); }
uint32_t __cdecl FakeSetupGroups(void) {
    ShadowLog("setupgroups");
    return 0;
}
void __cdecl FakeDronePostLoad(obj_tag *obj) { ShadowLog("dronepostload(%x)", Who(obj)); }
void __cdecl FakeDoorNodes(void) { ShadowLog("doornodes"); }
void __cdecl FakeKickNodes(void) { ShadowLog("kicknodes"); }
uint32_t __cdecl FakePassable(void) {
    ShadowLog("passable");
    return 0;
}

// Drone_Delete's
void __cdecl FakeBodyGlowDelete(void *glow) { ShadowLog("bodyglowdelete(%x)", Who(glow)); }
void __cdecl FakeMemFree(void **ptr) {
    ShadowLog("memfree(%x,%x)", Who(ptr), Who(*ptr));
    *ptr = NULL;
}
void __cdecl FakeSpawnerDroneDelete(obj_tag *spawner, obj_tag *drone) {
    ShadowLog("spawnerdronedelete(%x,%x)", Who(spawner), Who(drone));
}
void __cdecl FakeHeadTracking(obj_tag *obj, void *target, float range, float rate, char randomDelay, char targetIsObj,
                              uchar bone) {
    ShadowLog("headtracking(%x,%x,%08x,%08x,%d,%d,%u)", Who(obj), Who(target), Bits(range), Bits(rate), randomDelay,
              targetIsObj, bone);
}
LLNODE_tag *__cdecl FakeLListRemove(LLISTINFO_tag *list, LLNODE_tag *node) {
    ShadowLog("llistremove(%x,%x)", (uint)(uintptr_t)list, Who(node));
    return node;
}

} // namespace

// NDrone2_HandleTalking takes the DCVars in EDI. Outside the namespace so the naked fake can call it by name.
void __cdecl DroneCoreShadow_LogTalking(DCVars_tag *dcv) {
    ShadowLog("talking(%x)", Who(dcv));
}

static void __declspec(naked) DroneCoreShadow_FakeTalking(void) {
    _asm {
        push edi
        call DroneCoreShadow_LogTalking
        add esp, 4
        ret
    }
}

namespace {

void Hook(unsigned at, const void *fake) {
    ShadowHook_Install(at, fake);
}

void HookControlCallees() {
    Hook(0x0004e890, (const void *)&FakeSendMsgSelf);
    Hook(0x0004e320, (const void *)&FakeSetState);
    Hook(0x000397f0, (const void *)&FakeChangeToAttackMode);
    Hook(0x0003b6c0, (const void *)&FakeNinjaEyes);
    Hook(0x000cbac0, (const void *)&FakeSoundFinished);
    Hook(0x0001a660, (const void *)&FakeBotInfo);
    Hook(0x00037c10, (const void *)&FakeFindOpponent);
    Hook(0x00013070, (const void *)&FakeBone);
    Hook(0x00068db0, (const void *)&FakeBreath);
    Hook(0x0003ac10, (const void *)&FakeTracking);
    Hook(0x0003c430, (const void *)&DroneCoreShadow_FakeTalking);
    // Our NDrone2_ControlSTANDARD calls the adaptor's core
    Hook((unsigned)(uintptr_t)&_NDrone2_HandleTalking, (const void *)&FakeTalkingCore);
    Hook(0x00043400, (const void *)&FakeMove);
    Hook(0x000454e0, (const void *)&FakeBoundryFlags);
    Hook(0x000437f0, (const void *)&FakeNoBunching);
    Hook(0x0003f350, (const void *)&FakeCollision);
    Hook(0x00067e10, (const void *)&FakeFiring);
    Hook(0x00038a90, (const void *)&FakeExplosives);
    Hook(0x00039f10, (const void *)&FakeDoorIsOpen);
    Hook(0x00039ec0, (const void *)&FakeOpenDoor);
    Hook(0x0002d8a0, (const void *)&FakePlr2Ind);
    Hook(0x00033150, (const void *)&FakeSnapRotate);
    Hook(0x00018690, (const void *)&FakeAnimUpdate);
    Hook(0x00035cb0, (const void *)&FakeCallHandler);
}

void HookFrameCallees() {
    Hook(0x0004a530, (const void *)&FakeClearLinkFlags);
    Hook(0x00030e70, (const void *)&FakeAwarePoints);
    Hook(0x00031190, (const void *)&FakeUpdateTargets);
    Hook(0x000311e0, (const void *)&FakeCoverNodes);
    Hook(0x000313e0, (const void *)&FakeOpponents);
    Hook(0x00031110, (const void *)&FakeClearTargets);
    Hook(0x000661f0, (const void *)&FakeDroneSight);
    Hook(0x00044de0, (const void *)&FakeNavNodeCache);
    Hook(0x0003b410, (const void *)&FakeSoundAlerts);
    Hook(0x000cbc80, (const void *)&FakeZeroAlertness);
    Hook(0x0004e900, (const void *)&FakeBroadcast);
    Hook(0x00064380, (const void *)&FakeDelayedMsgs);
    Hook(0x0003a100, (const void *)&FakeAlarmRaised);
}

void HookSetupCallees() {
    Hook(0x0006ea10, (const void *)&FakeLListInit);
    Hook(0x0004e1e0, (const void *)&FakeSMInit);
    Hook(0x00049d10, (const void *)&FakeAllocEmitter);
    Hook(0x00049d70, (const void *)&FakeEmitPath);
    Hook(0x0002fb90, (const void *)&FakeCreateBlank);
    Hook(0x00036e90, (const void *)&FakeSetupGroups);
    Hook(0x00042a90, (const void *)&FakeDronePostLoad);
    Hook(0x00031af0, (const void *)&FakeDoorNodes);
    Hook(0x00031c00, (const void *)&FakeKickNodes);
    Hook(0x0004a720, (const void *)&FakePassable);
    Hook(0x000ce5a0, (const void *)&FakeBodyGlowDelete);
    Hook(0x000705f0, (const void *)&FakeMemFree);
    Hook(0x000308b0, (const void *)&FakeSpawnerDroneDelete);
    Hook(0x000119a0, (const void *)&FakeHeadTracking);
    Hook(0x0006e550, (const void *)&FakeLListRemove);
}

// The originals of Drone.cpp's ports, swapped in together for the original's run, except those a group fakes
const unsigned kPorts[] = {0x0002fb00, 0x00031780, 0x00031fd0, 0x00031260, 0x00031530, 0x0003ed20, 0x0003f5f0,
                           0x0003f8a0, 0x0003f910, 0x0003f930, 0x0003b670, 0x00037730, 0x00032130, 0x00032a00,
                           0x000314a0, 0x0003cbf0};
const int kPortCount = sizeof(kPorts) / sizeof(kPorts[0]);
unsigned faked[8];
int fakedCount;

struct Originals {
    bool swapped[kPortCount];
    Originals() {
        for (int i = 0; i < kPortCount; i++) {
            bool skip = false;
            for (int k = 0; k < fakedCount; k++)
                skip |= faked[k] == kPorts[i];
            swapped[i] = !skip && XbeOriginal_Restore(kPorts[i], true);
        }
    }
    ~Originals() {
        for (int i = kPortCount; i-- > 0;)
            if (swapped[i])
                XbeOriginal_Restore(kPorts[i], false);
    }
};

// ---- the made-up world

float Pick(const float *values, int n) {
    return values[ShadowRand(n)];
}

float SpecialOr(float lo, float hi) {
    switch (ShadowRand(20)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return NAN;
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = SpecialOr(-range, range);
    v->y = SpecialOr(-range, range);
    v->z = SpecialOr(-range, range);
}

obj_tag *RandomObj() {
    switch (ShadowRand(5)) {
    case 0:
        return NULL;
    case 1:
        return &world.others[5];
    default:
        return &world.objs[ShadowRand(kDrones)];
    }
}

Drone_tag *RandomDronePtr() {
    return ShadowRand(3) == 0 ? NULL : &world.drones[ShadowRand(kDrones)];
}

// Around GameState.NumFramesUnpaused (1000 to 1030), and the 30 and 60 frame (36, 72 at 50 Hz) waits before it
uint RandomFrame() {
    static const uint frames[] = {0,   1,   999, 1000, 1001, 1030, 1031, 0xfffffff0u, 0x80000000u, 970, 969, 964, 963,
                                  940, 939, 928, 927, 958, 957, 970, 994};
    return frames[ShadowRand(21)];
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    o->objectType = ShadowRand(6) == 0 ? OBJECTTYPE_DEAD_DRONE : (ShadowRand(10) == 0 ? OBJECTTYPE_PLAYER
                                                                                      : OBJECTTYPE_DRONE);
    o->extraObjectData = d;
    o->animState = anim;
    o->inCel = ShadowRand(5) == 0 ? NULL : &world.cels[ShadowRand(2)];
    RandomVector(&o->position, 10.0f);
    RandomVector(&o->lastPosition, 10.0f);
    RandomVector(&o->rotation, 4.0f);
    o->drawnCountdown = (char)(ShadowRand(3) == 0 ? 0 : ShadowRand(3));
    o->transformFlags = (ushort)ShadowRand(0x100);
    o->effectFlags = ShadowRand(4) == 0 ? 0x50 : ShadowRand(0x40);
    o->flags = (char)ShadowRand(4);
    o->creationTimeFrames = ShadowRand(5000);
    o->nextObject = i + 1 < kDrones ? &world.objs[i + 1] : &world.others[0];
    d->gameObj = ShadowRand(10) == 0 ? &world.others[ShadowRand(kOthers)] : o;
    d->next = i + 1 < kDrones && ShadowRand(6) ? &world.drones[i + 1] : NULL;

    static const float roots[] = {0.0f, -0.0f, 0.4f, NAN};
    anim->animObj.rootHeight = Pick(roots, 4);

    static const uint flagBits[] = {0x1, 0x100, 0x100, 0x200, 0x400, 0x80000, 0x80000, 0x100000, 0x10};
    for (int k = 0; k < 9; k++)
        if (ShadowRand(2))
            d->flags |= flagBits[k];
    static const short skins[] = {0, 5, 0xc, 0x11};
    d->skinClass = skins[ShadowRand(4)];
    d->unknown434 = ShadowRand(4) == 0 ? (void *)&world.facial : NULL;
    d->unknown46c = ShadowRand(3) == 0 ? 0 : ShadowRand(0x40);
    d->unknown47c = ShadowRand(4) == 0 ? 1000 : RandomFrame();
    static const float speeds[] = {0.0f, -0.0f, 0.05f, -0.05f, 1.0f, NAN};
    d->animMoveSpeed = Pick(speeds, 6);
    d->boundaryFlags = ShadowRand(8);
    d->door = ShadowRand(3) == 0 ? &world.others[ShadowRand(2) ? 4 : 0] : NULL;
    d->doorFrame = RandomFrame();
    d->animFunc = ShadowRand(2) ? &FakeAnimFunc : NULL;
    d->animStateFunc = ShadowRand(2) ? &FakeStateFunc : NULL;
    RandomVector(&d->unknown3a0, 3.0f);
    d->dtype = (uchar)(ShadowRand(3) == 0 ? ShadowRand(DTYPE_COUNT) : ShadowRand(31));

    static const float alerts[] = {0.0f, 0.2f, 0.5f, 0.9999f, 1.0f, 1.5f, -0.0f, NAN};
    d->alertness = Pick(alerts, 8);
    d->baseAlertness1 = Pick(alerts, 8);
    static const float healths[] = {0.0f, -0.0f, -1.0f, 50.0f, NAN};
    d->health = Pick(healths, 5);
    d->side = (uchar)ShadowRand(5);
    d->flies = (uchar)(ShadowRand(4) == 0);
    d->ninjaEyes = (uchar)(ShadowRand(4) == 0);
    d->speechChannel = ShadowRand(2) ? (DYNAMICSOUNDS *)(uintptr_t)0x1234 : NULL;
    d->facialAnimPlaying = (uchar)ShadowRand(2);
    d->facialAnimDone = (uchar)(ShadowRand(3) == 0);
    d->facialAnim = ShadowRand(4) == 0 ? NULL : (void *)&world.facial;
    RandomVector(&d->moveRotation, 4.0f);
    d->yawOffset = SpecialOr(-1.0f, 1.0f);
    d->bunchFlags = ShadowRand(100);
    d->nearestDrone = RandomDronePtr();
    d->nearestDroneDist = 3.0f;

    d->inPlayersSights = (uchar)ShadowRand(2);
    d->inPlayersSightsFrame = RandomFrame();
    d->inPlayersSightsFrames = ShadowRand(50);
    d->modeChangeSwitchChannel = (uchar)(ShadowRand(3) == 0 ? 0 : 1 + ShadowRand(3));
    static const short modes[] = {0, 0, DMODE_DeleteMe, 5, 1};
    d->secondaryMode = modes[ShadowRand(5)];
    d->stateTimeoutFrame = ShadowRand(3) == 0 ? 0 : RandomFrame();
    d->sm.curState = ShadowRand(3) == 0 ? (ShadowRand(2) ? DSTATE_Dead : DSTATE_Fade) : ShadowRand(250);
    static const uint states[] = {DSTATE_HostageDead, DSTATE_Dead, DSTATE_Fade, DSTATE_FadeFast, DSTATE_AstronautDeath,
                                  4};
    if (ShadowRand(2))
        d->sm.curState = states[ShadowRand(6)];
    d->sm.id = 1 + i;
    d->sm.stateEnteredFrame = RandomFrame();
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = RandomFrame();
    d->timerA.unknown8 = ShadowRand(10);
    d->timerB.on = (uchar)ShadowRand(2);
    d->timerB.dueFrame = RandomFrame();
    d->timerB.unknown8 = ShadowRand(10);
    d->prevAlertness = 7.0f;
    d->lastHit = (HITDATA_tag *)(uintptr_t)0x4444;
    d->unknown38 = 1;

    d->opponent = ShadowRand(4) == 0 ? NULL : RandomObj();
    d->targetSlot = (uchar)(ShadowRand(3) == 0 ? NO_TARGET_SLOT : ShadowRand(MAX_DRONE_TARGETS));
    d->opponentSeenFrame = RandomFrame();
    RandomVector(&d->opponentSeenPos, 10.0f);
    RandomVector(&d->opponentLastPos, 10.0f);
    RandomVector(&d->opponentLastRot, 4.0f);
    static const float distances[] = {0.0f, 29.9f, 30.0f, 30.5f, 40.0f, NAN};
    d->distanceToTarget = Pick(distances, 6);

    // Drone_Delete's links
    d->bodyGlow = ShadowRand(2) ? (obj_tag *)&world.casing : NULL;
    d->route1.nodes = ShadowRand(2) ? world.routeNodes[i][0] : NULL;
    d->route2.nodes = ShadowRand(2) ? world.routeNodes[i][1] : NULL;
    d->group = ShadowRand(3);
    d->opponentCursor = RandomDronePtr();
    d->unknown28c = RandomDronePtr();
    d->unknown230 = RandomObj();
    d->lastAttacker = RandomObj();
    d->unknown23c = RandomObj();
    d->headTarget = RandomObj();
    d->headTracking = (uchar)ShadowRand(2);
}

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);

    for (int i = 0; i < kOthers; i++) {
        memset(&world.others[i], 0, sizeof(obj_tag));
        RandomVector(&world.others[i].position, 10.0f);
        RandomVector(&world.others[i].rotation, 4.0f);
        world.others[i].nextObject = i + 1 < kOthers ? &world.others[i + 1] : NULL;
    }
    world.others[0].objectType = OBJECTTYPE_BULLET;
    world.others[0].extraObjectData = &world.bullet;
    world.others[1].objectType = OBJECTTYPE_CASING;
    world.others[1].extraObjectData = &world.casing;
    world.others[2].objectType = OBJECTTYPE_DRONE_SPAWNER;
    world.others[2].extraObjectData = &world.spawner;
    world.others[3].objectType = OBJECTTYPE_GUNTURRET;
    world.others[3].extraObjectData = ShadowRand(5) == 0 ? NULL : &world.turret;
    world.others[4].objectType = ShadowRand(5) == 0 ? OBJECTTYPE_BULLET : OBJECTTYPE_DOOR;
    world.others[4].extraObjectData = &world.bullet;
    world.others[5].objectType = OBJECTTYPE_PLAYER;
    world.others[6].objectType = OBJECTTYPE_PLAYER;
    // The door near the drones now and then
    if (ShadowRand(2)) {
        world.others[4].position = world.objs[0].position;
        world.others[4].position.x += ShadowRand(2) ? 1.0f : 3.0f;
    }
    memset(&world.bullet, 0, sizeof(world.bullet));
    world.bullet.firedByObj = RandomObj();
    memset(world.casing, 0, sizeof(world.casing));
    obj_tag *casingObj = RandomObj();
    memcpy(world.casing + 0x20, &casingObj, sizeof(casingObj));
    memset(&world.spawner, 0, sizeof(world.spawner));
    world.spawner.group = ShadowRand(3);
    memset(&world.turret, 0, sizeof(world.turret));
    world.turret.owner = RandomObj();

    for (int i = 0; i < 4; i++) {
        memset(&world.cover[i], 0, sizeof(CoverNode_tag));
        world.cover[i].occupant = RandomObj();
        world.cover[i].flags = ShadowRand(0x200);
    }
    for (int i = 0; i < 3; i++)
        memset(&world.aiPoints[i], 0, sizeof(DroneAIPoint_tag));
    memset(&world.facial, 0, sizeof(world.facial));
    world.facial.flags = ShadowRand(2) ? 0x10000000 : 0x1;
    memset(world.cels, 0, sizeof(world.cels));
    for (int i = 0; i < 8; i++)
        world.result[i] = ShadowRand(2) | (ShadowRand(2) ? 0x100 : 0);
    world.result[1] = ShadowRand(250);
    world.result[5] = ShadowRand(3) == 0 ? 0xffff : ShadowRand(4);   // a short: -1 for no player

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = world.objs[0].inCel;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    for (int k = 0; k < 4; k++)
        glb_players[k] = ShadowRand(3) == 0 ? NULL : &world.others[5 + (k & 1)];
    if (ShadowRand(2)) {
        glb_players[0] = &world.others[5];
        world.others[5].position = world.objs[0].position;
        world.others[5].position.z += ShadowRand(2) ? 5.0f : 15.0f;
    }

    GameState.NumFramesUnpaused = 1000 + ShadowRand(3) * 15;
    static const HASHCODE levels[] = {HT_Level_EvilBase, HT_Level_EvilBaseC, HT_Level_CastleIndoors2,
                                      HT_Level_HendersonA};
    GameState.CurrentLevelHashcode = levels[ShadowRand(4)];
    MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;

    memset(&NPCGlobals, 0x11, sizeof(NPCGlobals));
    NPCGlobals.dummyPlayer = NULL;
    NPCGlobals.enabled = (uchar)(ShadowRand(8) != 0);
    NPCGlobals.NDrone2List = ShadowRand(8) == 0 ? NULL : &world.drones[0];
    NPCGlobals.deadFrames = 600;
    NPCGlobals.numCoverNodes = ShadowRand(5);
    NPCGlobals.coverNodes = world.cover;
    NPCGlobals.numAIPoints = ShadowRand(4);
    NPCGlobals.aiPoints = world.aiPoints;
    NPCGlobals.alarmFrame = RandomFrame();
    NPCGlobals.alarmSaid = (uchar)ShadowRand(2);
    for (int k = 0; k < MAX_DRONE_TARGETS; k++) {
        NPCGlobals.targets[k].flags = ShadowRand(4);
        NPCGlobals.targets[k].seenFrame = RandomFrame();
    }
    NPCGlobals.enemies = 3;
    NPCGlobals.allies = 4;
    NPCGlobals.neutrals = 5;
    NPCGlobals.unknown188 = 6;
    NPCGlobals.seenAttacking = 7;
    NPCGlobals.seenOpponentCount = 8;
    NPCGlobals.lostOpponentCount = 9;
    Drone_bDisableSystem = (uchar)(ShadowRand(8) == 0);
    Drone_bDontDraw = (uchar)(ShadowRand(4) == 0);
    switch_NO_DRONES = ShadowRand(6) == 0;
    Drone_DummyPlayer = (char)ShadowRand(2);
    for (int k = 0; k < 256; k++) {
        switch_channels[k] = (char)(ShadowRand(3) == 0);
        switch_channels_time[k] = 0;
    }
    MusicVars.calmThreshold = (ushort)ShadowRand(4);
    MusicVars.attackers = (ushort)ShadowRand(4);
    MusicVars.combat = 1;
    AIPath_MaxNodes = ShadowRand(2) ? 0 : 40;
    ShadowFakes.firstObject = &world.objs[0];

    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        _FRAME_RATE = 60.0f;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        _FRAME_RATE = 50.0f;
        FRAME_RATE_DIV = 1.2f;
    }
}

void SetFaked(std::initializer_list<unsigned> list) {
    fakedCount = 0;
    for (unsigned at : list)
        faked[fakedCount++] = at;
}

// ---- the cases

DCVars_tag *DcvArg() {
    return &world.dcv;
}

// The world for one drone's frame: the drone under test is its object's
void RandomControlWorld() {
    RandomWorld();
    world.drones[0].gameObj = &world.objs[0];
}

void TestSetup() {
    ShadowHook_RemoveAll();
    ShadowFakes_Install();
    HookSetupCallees();
    SetFaked({});
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        Compare("Drone_PreLoad_Init", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0002fb00, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_PreLoad_Init();
                    return 0u;
                });
        RandomWorld();
        Compare("Drone_LevelReset", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00031780, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_LevelReset();
                    return 0u;
                });
        RandomWorld();
        Compare("Drone_PostLoad_Init", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00031fd0, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_PostLoad_Init();
                    return 0u;
                });
        RandomWorld();
        Compare("Drone_Delete", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00032130, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    Drone_Delete(&world.objs[0]);
                    return 0u;
                });
    }
}

void TestFrame() {
    ShadowHook_RemoveAll();
    ShadowFakes_Install();
    HookFrameCallees();
    SetFaked({});
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        // The first drone's DCVars are uninitialised stack in the original until a drone fills them
        world.drones[0].gameObj = &world.objs[0];
        if (world.objs[0].objectType == OBJECTTYPE_PLAYER)
            world.objs[0].objectType = OBJECTTYPE_DRONE;
        Compare("Drone_InitComms", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00032a00, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_InitComms();
                    return 0u;
                });
        RandomWorld();
        Compare("Drone_UpdateCombatChannels", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x000314a0, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_UpdateCombatChannels();
                    return 0u;
                });
    }
}

void TestControl() {
    ShadowHook_RemoveAll();
    ShadowFakes_Install();
    HookControlCallees();
    SetFaked({});
    for (int trial = 0; trial < 8000; trial++) {
        RandomControlWorld();
        Compare("Drone_GetOpponentInfo", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00031260, void(__cdecl *)(Drone_tag *, obj_tag *))(&world.drones[0], &world.objs[0]);
                    return 0u;
                },
                [] {
                    Drone_GetOpponentInfo(&world.drones[0], &world.objs[0]);
                    return 0u;
                });
        RandomControlWorld();
        Compare("NDrone2_PreDroneControl", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003ed20, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_PreDroneControl(DcvArg()); });
        RandomControlWorld();
        Compare("NDrone2_ControlSTANDARD", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003f5f0, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_ControlSTANDARD(DcvArg()); });
        RandomControlWorld();
        Compare("NDrone2_ControlDTYPE_Zoe", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003f8a0, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_ControlDTYPE_Zoe(DcvArg()); });
        RandomControlWorld();
        Compare("NDrone2_ControlDTYPE_Ninja", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003f910, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_ControlDTYPE_Ninja(DcvArg()); });
        Compare("NDrone2_ControlDTYPE_Astronaut", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003f930, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_ControlDTYPE_Astronaut(DcvArg()); });
        RandomControlWorld();
        Compare("NDrone2_HandleDoor", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003cbf0, void(__cdecl *)(DCVars_tag *))(DcvArg());
                    return 0u;
                },
                [] {
                    NDrone2_HandleDoor(DcvArg());
                    return 0u;
                });
        Compare("NDrone2_PostDroneControl", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003b670, uint32_t(__cdecl *)(DCVars_tag *))(DcvArg()) & 0xff;
                },
                [] { return (uint32_t)NDrone2_PostDroneControl(DcvArg()); });
        Compare("NDrone2_DoAnimation", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00037730, uint32_t(__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff;
                },
                [] { return (uint32_t)NDrone2_DoAnimation(&world.drones[0]); });
    }

    // Drone_Control with PreDroneControl and the controls faked
    Hook(0x0003ed20, (const void *)&FakePreDroneControl);
    Hook(0x0003f5f0, (const void *)&FakeStandard);
    Hook(0x0003f8a0, (const void *)&FakeTypeControl);
    Hook(0x0003f910, (const void *)&FakeTypeControl);
    Hook(0x0003f930, (const void *)&FakeTypeControl);
    SetFaked({0x0003ed20, 0x0003f5f0, 0x0003f8a0, 0x0003f910, 0x0003f930});
    for (int trial = 0; trial < 8000; trial++) {
        RandomControlWorld();
        Compare("Drone_Control", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00031530, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    Drone_Control(&world.objs[0]);
                    return 0u;
                });
    }
    SetFaked({});
}

} // namespace

void DroneCoreShadow_Run(void) {
    struct Saved {
        void *at;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved;
    regions.Clear();
    auto add = [&](void *at, size_t size) {
        regions.Add(at, size);
        saved.push_back({at, std::vector<uint8_t>((uint8_t *)at, (uint8_t *)at + size)});
    };
    add(&world, sizeof(world));
    add(&GameState, sizeof(GameState_t));
    add(&MPSettings, sizeof(MPSettings_t));
    add(&NPCGlobals, sizeof(NPCGlobals_t));
    add(glb_players, sizeof(obj_tag *) * 4);
    add(&switch_channels, sizeof(switch_channels));
    add(&switch_channels_time, sizeof(switch_channels_time));
    add(&MusicVars, sizeof(MusicVars_t));
    add(&AIBoxList, sizeof(LLISTINFO_tag));
    add(MoveTestBlock.game(), kMoveTestBytes);
    add(FrameRateBlock.game(), kFrameRateBytes);
    add(&Drone_bDisableSystem, 1);
    add(&Drone_bDontDraw, 1);
    add(&Drone_DummyPlayer, 1);
    add(&switch_NO_DRONES, 4);
    ShadowFakes_AddRegions(regions);

    NfWarnMuted++;
    cases = differ = 0;
    TestSetup();
    int setupCases = cases, setupDiffer = differ;
    TestFrame();
    int frameCases = cases, frameDiffer = differ;
    TestControl();
    NfWarnMuted--;
    ShadowHook_RemoveAll();
    for (Saved &s : saved)
        memcpy(s.at, s.bytes.data(), s.bytes.size());

    printf("[dronecore] set-up and deletion: %d cases, %d differ\n", setupCases, setupDiffer);
    printf("[dronecore] Drone_InitComms and the combat channels: %d cases, %d differ\n", frameCases - setupCases,
           frameDiffer - setupDiffer);
    printf("[dronecore] each drone's frame: %d cases, %d differ\n", cases - frameCases, differ - frameDiffer);
}
